/* NOTE: sending and receiving of protocol messages */

// TODO: validate frame length in pr_recv_XXX functions

/* server -> client (pr_send_XXX) */
static void
_send_constant_message(struct bc_queue *queue, struct bc_connection *connection,
                       struct bc_io *req, int payload_length)
{
    req->send.socket = connection->socket;
    req->send.start = 0;
    req->send.total = payload_length + 2;
    req->send.buf = (char *) req->buf;
    
    queue_send(queue, req, 0);
}

static int
_write_header(struct bc_io *req, int payload_length)
{
    req->buf[0] = FRAME_BINARY | 0x80;
    
    // printf("LEN %d\n", payload_length);
    
    int next = 2;
    if (payload_length <= 125) {
        req->buf[1] = payload_length & 0x7F;
    } else if (payload_length <= (u16) -1) {
        req->buf[1] = 126;
        p_writebe16(payload_length, req->buf + 2);
        next = 4;
    } else {
        req->buf[1] = 127;
        p_writebe64(payload_length, req->buf + 2);
        next = 10;
    }
    
    return(next);
}

/***********************/
/******** SEND *********/
/***********************/
static void
pr_send_auth(struct bc_queue *queue, struct bc_connection *connection, struct bc_persist_session *session)
{
    int payload_length = session ? 13 : 1; /* just the opcode, or also the 8 byte session id and 4 byte user id */
    
    struct bc_io *req = queue_push(queue, IOU_SEND);
    
    req->buf[0] = FRAME_BINARY | 0x80;
    req->buf[1] = payload_length & 0x7F;
    req->buf[2] = session ? WS_SERVER_AUTH_SUCCESS : WS_SERVER_AUTH_FAIL;
    
    if (session) {
        memcpy(req->buf + 3, &session->sid, 8);
        memcpy(req->buf + 11, &session->user_id, 4);
    }
    
    _send_constant_message(queue, connection, req, payload_length);
}

static void
pr_send_init(struct bc_queue *queue, struct bc_server *server, struct bc_connection *connection, int user_id)
{
    u64 sid = connection->session_id;
    
    struct bc_v2i *cpu = server->views.channels_per_user;
    struct bc_io *req = queue_push(queue, IOU_SENDV);
    struct bc_v2i *cu = view_find_sorted_2(cpu, user_id);
    
    if (!req) {
        return;
    }
    
    int user_count = buffer_size(server->memory.users);
    u16 cu_count = 0;
    int slot_offset = 0;
    
    int slot_id = slot_reserve(&server->slots);
    if (slot_id == -1) {
        // too many requests @load
        log_ferror(__func__, "Too many requests\n");
        return;
    }
    
    struct bc_str slot = slot_buffer(&server->slots, slot_id);
    
    req->slot_id = slot_id;
    
    if (cu) {
        int cu_index = POINTER_DIFF(cu, cpu) / sizeof(*cpu);
        int cu_total_count = buffer_size(cpu);
        
        while (cu_index < cu_total_count && cu->user_id == user_id) {
            struct bc_persist_channel_session *cs = mem_channelsession_find(&server->memory, sid, cu->channel_id);
            struct bc_persist_channel_user *full_cu = mem_channeluser_find(&server->memory, cu->user_id, cu->channel_id);
            struct bc_v2i channel_connection = { 0 };
            
            channel_connection.channel_id = cu->channel_id;
            channel_connection.socket = connection->socket;
            
            view_insert_sorted_1(server->views.connections_per_channel, channel_connection);
            
            if (!full_cu || !cs) {
                if (!full_cu) log_warning("Full channel user not found for user = %d, channel = %d\n", cu->user_id, cu->channel_id);
                if (!cs) log_warning("Channel session not found for session = %lu, channel = %d\n", sid, cu->channel_id);
            } else {
                memcpy(slot.data + slot_offset + 0,     &full_cu->channel_id,     sizeof(cu->channel_id));
                memcpy(slot.data + slot_offset + 4,     &cs->first_unrecved,      sizeof(cs->first_unrecved));
                memcpy(slot.data + slot_offset + 4 + 4, &full_cu->first_unseen,   sizeof(full_cu->first_unseen));
                ++cu_count;
                slot_offset += 3 * 4;
            }
            
            ++cu_index;
            ++cu;
        }
    } else {
        log_info("User %d not in any channel\n", user_id);
    }
    
    /* Pack usernames into the same slot */
    struct bc_persist_user *users = server->memory.users;
    struct bc_user_info empty_info = { 0 };
    
    for (int i = 0; i < user_count; ++i) {
        struct bc_persist_user *user = users + i;
        u32 curr_user_id = user->id;
        struct bc_user_info *user_info = view_find_user_info(&server->views, curr_user_id);
        int user_info_size = sizeof(struct bc_user_info);
        if (!user_info) {
            user_info = &empty_info;
        }
        memcpy(slot.data + slot_offset, user_info, user_info_size);
        slot_offset += user_info_size;
    }
    
    for (int i = 0; i < user_count; ++i) {
        struct bc_persist_user *user = users + i;
        struct bc_str name = mem_block_find(&server->memory.block_storage, user->name_block);
        struct bc_str login = mem_block_find(&server->memory.block_storage, user->login_block);
        
        memcpy(slot.data + slot_offset, &user->name_block, sizeof(user->name_block));
        slot_offset += sizeof(user->name_block);
        memcpy(slot.data + slot_offset, &name.length, sizeof(name.length));
        slot_offset += sizeof(name.length);
        memcpy(slot.data + slot_offset, name.data, name.length);
        slot_offset += name.length;
        
        memcpy(slot.data + slot_offset, &user->login_block, sizeof(user->login_block));
        slot_offset += sizeof(user->login_block);
        memcpy(slot.data + slot_offset, &login.length, sizeof(login.length));
        slot_offset += sizeof(login.length);
        memcpy(slot.data + slot_offset, login.data, login.length);
        slot_offset += login.length;
    }
    
    int users_length = user_count * sizeof(struct bc_persist_user);
    int constant_payload = 1 + 4 + 2 + 4; /* opcode + user_id + cu_count + user_count */
    int payload_length = constant_payload + slot_offset + users_length;
    int header_size = _write_header(req, payload_length);
    
    req->buf[header_size] = WS_SERVER_INIT;
    memcpy(req->buf + header_size + 1,         &user_id,    sizeof(user_id));
    memcpy(req->buf + header_size + 1 + 4,     &cu_count,   sizeof(cu_count));
    memcpy(req->buf + header_size + 1 + 4 + 2, &user_count, sizeof(user_count));
    
    req->sendv.socket = connection->socket;
    req->sendv.start = 0;
    req->sendv.total = header_size + payload_length;
    
    req->sendv.vectors[0].iov_base = req->buf;
    req->sendv.vectors[0].iov_len = header_size + constant_payload;
    
    // NOTE(aolo2): vector of size 0 should be ok
    req->sendv.vectors[1].iov_base = slot.data;
    req->sendv.vectors[1].iov_len = slot_offset;
    
    req->sendv.vectors[2].iov_base = server->memory.users;
    req->sendv.vectors[2].iov_len = users_length;
    
    req->sendv.nvecs = 3;
    
    queue_sendv(queue, req, 0, 0);
}

static void
pr_send_ack(struct bc_queue *queue, struct bc_connection *connection, 
            u32 channel_id, u32 sn)
{
    int payload_length = 1 + 4 + 4;
    struct bc_io *req = queue_push(queue, IOU_SEND);
    
    req->buf[0] = FRAME_BINARY | 0x80;
    req->buf[1] = payload_length;
    req->buf[2] = WS_SERVER_ACK;
    
    memcpy(req->buf + 3, &channel_id, 4);
    memcpy(req->buf + 3 + 4, &sn, 4);
    
    log_debug("[SN] Sending ack for channel %d for SN = %d\n", channel_id, sn);
    
    _send_constant_message(queue, connection, req, payload_length);
}

static void
pr_send_syn(struct bc_queue *queue, struct bc_server *server, struct bc_connection *connection, 
            struct bc_persist_channel_session *cs, u8 initial)
{
    struct bc_channel_info *channel_info = view_find_channel_info(&server->views, cs->channel_id);
    
    if (!channel_info) {
        log_ferror(__func__, "Channel info not found for channel %d\n", cs->channel_id);
        return;
    }
    
    char *messages = channel_info->messages;
    u32 nmessages = channel_info->message_count;
    
    if (cs->first_unsent > nmessages) {
        log_warning("Session %lu says they already have %d messages in channel %d, but really there are only %d\n", 
                    cs->session_id, cs->first_unsent, cs->channel_id, nmessages);
        return;
    }
    
    if (cs->first_unsent == nmessages) {
        /* We've sent everything for this channel user already */
        return;
    }
    
    int constant_payload = 1 + 4 + 4 + 4 + 1; // opcode + channel_id + SN + message_count + is_initial
    int payload_length = constant_payload;
    
    struct bc_persist_record *first_record = mem_message_find(server, cs->channel_id, cs->first_unsent);
    
    if (!first_record) {
        log_ferror(__func__, "First unsent message not found for session %lu in channel %d?!\n", 
                   cs->session_id, cs->channel_id);
        return;
    }
    
    int nmessages_send = nmessages - cs->first_unsent;
    payload_length += POINTER_DIFF((messages + buffer_size(messages)), first_record);
    
    struct bc_io *req = queue_push(queue, IOU_SENDV);
    int header_size = _write_header(req, payload_length);
    
    req->buf[header_size] = WS_SERVER_SYNC;
    
    memcpy(req->buf + header_size + 1,             &cs->channel_id, 4);
    memcpy(req->buf + header_size + 1 + 4,         &nmessages, 4);
    memcpy(req->buf + header_size + 1 + 4 + 4,     &nmessages_send, 4);
    memcpy(req->buf + header_size + 1 + 4 + 4 + 4, &initial, 1);
    
    req->sendv.socket = connection->socket;
    req->sendv.start = 0;
    req->sendv.total = header_size + payload_length;
    
    req->sendv.vectors[0].iov_base = req->buf;
    req->sendv.vectors[0].iov_len = header_size + constant_payload;
    
    req->sendv.vectors[1].iov_base = first_record;
    req->sendv.vectors[1].iov_len = payload_length - constant_payload;
    
    req->sendv.nvecs = 2;
    
    queue_sendv(queue, req, 0, 0);
}

static void
pr_send_syn_all_channels(struct bc_queue *queue, struct bc_server *server, struct bc_connection *connection, int user_id)
{
    u64 sid = connection->session_id;
    
    struct bc_v2i *cpu = server->views.channels_per_user;
    struct bc_v2i *cu = view_find_sorted_2(cpu, user_id);
    
    if (cu) {
        int cu_index = POINTER_DIFF(cu, cpu) / sizeof(*cpu);
        int cu_total_count = buffer_size(cpu);
        
        while (cu_index < cu_total_count && cu->user_id == user_id) {
            struct bc_persist_channel_session *cs = mem_channelsession_find(&server->memory, sid, cu->channel_id);
            
            if (cs) {
                pr_send_syn(queue, server, connection, cs, true);
            }
            
            ++cu;
            ++cu_index;
        }
    }
}

static void
pr_send_syn_all_connections(struct bc_server *server, struct bc_persist_channel_user *cu)
{
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *connection = server->connections + i;
        struct bc_persist_session *session = mem_session_find(&server->memory, connection->session_id);
        if (session && session->user_id == (s32) cu->user_id) {
            struct bc_persist_channel_session *cs = mem_channelsession_find(&server->memory, connection->session_id, cu->channel_id);
            if (cs) {
                pr_send_syn(&server->queue, server, connection, cs, true);
            }
        }
    }
}

static void
pr_send_seen(struct bc_server *server, struct bc_connection *connection,
             u32 user_id, u32 channel_id, u32 first_unseen)
{
    int payload_length = 1 + 4 + 4 + 4;
    struct bc_io *req = queue_push(&server->queue, IOU_SEND);
    
    req->buf[0] = FRAME_BINARY | 0x80;
    req->buf[1] = payload_length;
    req->buf[2] = WS_SERVER_USER_SEEN;
    
    memcpy(req->buf + 3, &user_id, 4);
    memcpy(req->buf + 3 + 4, &channel_id, 4);
    memcpy(req->buf + 3 + 4 + 4, &first_unseen, 4);
    
    _send_constant_message(&server->queue, connection, req, payload_length);
}

static void
pr_send_invited_to_channel(struct bc_queue *queue, struct bc_connection *connection, struct bc_persist_channel *channel) 
{
    int constant_payload = 1 + 4;
    struct bc_io *req = queue_push(queue, IOU_SENDV);
    int header_size = _write_header(req, constant_payload); // websocket frame header
    
    req->sendv.socket = connection->socket;
    req->sendv.start = 0;
    req->sendv.total = header_size + constant_payload;
    
    req->buf[header_size] = WS_SERVER_INVITED_TO_CHANNEL; // opcode 
    memcpy(req->buf + header_size + 1, &channel->id, 4);
    
    req->sendv.vectors[0].iov_base = req->buf;
    req->sendv.vectors[0].iov_len = header_size + constant_payload;
    
    req->sendv.nvecs = 1;
    
    queue_sendv(queue, req, 0, 0);
}

static void
pr_send_channel_info(struct bc_queue *queue, struct bc_server *server,
                     struct bc_connection *connection, struct bc_persist_channel *channel)
{
    struct bc_v2i *upc = server->views.users_per_channel;
    struct bc_v2i *cu = view_find_sorted_1(upc, channel->id);
    struct bc_io *req = queue_push(queue, IOU_SENDV);
    
    int slot_offset = 0;
    
    int slot_id = slot_reserve(&server->slots);
    if (slot_id == -1) {
        // too many requests @load
        log_ferror(__func__, "Too many requests\n");
        return;
    }
    
    struct bc_str slot = slot_buffer(&server->slots, slot_id);
    
    req->slot_id = slot_id;
    
    u16 cu_count = 0;
    
    if (cu) {
        int cu_index = POINTER_DIFF(cu, upc) / sizeof(*upc);
        int cu_total_count = buffer_size(upc);
        
        while (cu_index < cu_total_count && cu->channel_id == (int) channel->id) {
            struct bc_persist_channel_user *full_cu = mem_channeluser_find(&server->memory, cu->user_id, cu->channel_id);
            
            memcpy(slot.data + slot_offset + 0, &full_cu->user_id,      sizeof(full_cu->user_id));
            memcpy(slot.data + slot_offset + 4, &full_cu->first_unseen, sizeof(full_cu->first_unseen));
            
            slot_offset += 4 + 4;
            
            ++cu_count;
            ++cu;
            ++cu_index;
        }
    }
    
    struct bc_str channel_title = mem_block_find(&server->memory.block_storage, channel->title_block);
    
    int constant_payload = 1 + 1 + 8 + 4 + 4 + 2; // opcode + title length + avatar id + channel id + flags + cu count
    int payload_length = constant_payload + channel_title.length + slot_offset;
    int header_size = _write_header(req, payload_length);
    
    req->sendv.socket = connection->socket;
    req->sendv.start = 0;
    req->sendv.total = header_size + payload_length;
    
    req->buf[header_size] = WS_SERVER_CHANNEL_INFO;
    req->buf[header_size + 1] = channel_title.length;
    
    memcpy(req->buf + header_size + 1 + 1, &channel->avatar_id, 8);
    memcpy(req->buf + header_size + 1 + 1 + 8, &channel->id, 4);
    memcpy(req->buf + header_size + 1 + 1 + 8 + 4, &channel->flags, 4);
    memcpy(req->buf + header_size + 1 + 1 + 8 + 4 + 4, &cu_count, 2);
    
    req->sendv.vectors[0].iov_base = req->buf;
    req->sendv.vectors[0].iov_len = header_size + constant_payload;
    
    // NOTE: write of size 0 is ok AFAIK
    req->sendv.vectors[1].iov_base = channel_title.data;
    req->sendv.vectors[1].iov_len = channel_title.length;
    
    req->sendv.vectors[2].iov_base = slot.data;
    req->sendv.vectors[2].iov_len = slot_offset;
    
    req->sendv.nvecs = 3;
    
    queue_sendv(queue, req, 0, 0);
}

static void
pr_send_channel_invite_all_connections(struct bc_queue *queue, struct bc_server *server, 
                                       u32 user_id, struct bc_persist_channel *channel)
{
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *other = server->connections + i;
        struct bc_persist_session *other_session = connection_session(server, other);
        if (other_session) {
            u32 other_user_id = other_session->user_id;
            if (other_user_id == user_id) {
                pr_send_invited_to_channel(queue, other, channel);
                break;
            }
        }
    }
}

static void 
pr_send_pushpoll(struct bc_server *server) 
{
    // opcode + user count
    int constant_payload = 1 + 4;
    int user_info_size = buffer_size(server->views.user_info) * sizeof(struct bc_user_info);
    
    // Push status info (users statusi, channel seen)
    int payload_length = constant_payload + user_info_size;
    int user_count = buffer_size(server->memory.users);
    
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *connection = server->connections + i;
        struct bc_persist_session *session = connection_session(server, connection);
        if (session) {
            struct bc_io *req = queue_push(&server->queue, IOU_SENDV);
            int header_size = _write_header(req, payload_length);
            
            req->sendv.nvecs = 2;
            req->sendv.start = 0;
            req->sendv.total = header_size + payload_length;
            req->sendv.socket = connection->socket;
            
            req->buf[header_size] = WS_SERVER_PUSHPOLL;
            
            memcpy(req->buf + header_size + 1, &user_count, 4);
            //memcpy(req->buf + header_size + 1 + 4, &server->storage.memory.channel_count, 4);
            
            req->sendv.vectors[0].iov_base = req->buf;
            req->sendv.vectors[0].iov_len = header_size + constant_payload;
            
            // NOTE(aolo2): this view might change at the same time it's being sent, but it
            // doesn't seem critical to me atm
            req->sendv.vectors[1].iov_base = server->views.user_info;
            req->sendv.vectors[1].iov_len = user_info_size;
            
            queue_sendv(&server->queue, req, 0, 0);
        }
    }
}

static void
pr_send_utf8_id(struct bc_server *server, struct bc_connection *connection, 
                u32 nonce, int block_id)
{
    int payload_length = 1 + 4 + 4; /* opcode + nonce + block_id */
    
    struct bc_io *req = queue_push(&server->queue, IOU_SEND);
    
    req->buf[0] = FRAME_BINARY | 0x80;
    req->buf[1] = payload_length & 0x7F;
    req->buf[2] = WS_SERVER_UTF8_SAVED;
    
    memcpy(req->buf + 3, &nonce, sizeof(nonce));
    memcpy(req->buf + 7, &block_id, sizeof(block_id));
    
    _send_constant_message(&server->queue, connection, req, payload_length);
}

static void
pr_send_utf8_data(struct bc_server *server, struct bc_connection *connection, 
                  u32 nonce, struct bc_str blob)
{
    int constant_payload = 1 + 4 + 4; // opcode + nonce + length
    struct bc_io *req = queue_push(&server->queue, IOU_SENDV);
    int header_size = _write_header(req, constant_payload + blob.length); // websocket frame header
    
    req->sendv.socket = connection->socket;
    req->sendv.start = 0;
    req->sendv.total = header_size + constant_payload + blob.length;
    
    req->buf[header_size] = WS_SERVER_UTF8_DATA; // opcode 
    memcpy(req->buf + header_size + 1, &nonce, sizeof(nonce));
    memcpy(req->buf + header_size + 1 + 4, &blob.length, sizeof(blob.length));
    
    req->sendv.vectors[0].iov_base = req->buf;
    req->sendv.vectors[0].iov_len = header_size + constant_payload;
    
    req->sendv.vectors[1].iov_base = blob.data;
    req->sendv.vectors[1].iov_len = blob.length;
    
    req->sendv.nvecs = 2;
    
    queue_sendv(&server->queue, req, 0, 0);
}

static void
pr_send_user_is_typing(struct bc_server *server, struct bc_connection *connection, 
                       u32 channel_id, u32 user_id)
{
    int payload_length = 1 + 4 + 4;
    struct bc_io *req = queue_push(&server->queue, IOU_SEND);
    
    req->buf[0] = FRAME_BINARY | 0x80;
    req->buf[1] = payload_length;
    req->buf[2] = WS_SERVER_USER_IS_TYPING;
    
    memcpy(req->buf + 3, &channel_id, 4);
    memcpy(req->buf + 3 + 4, &user_id, 4);
    
    _send_constant_message(&server->queue, connection, req, payload_length);
    
    log_debug("[US] Sending user with id = %d is typing on channel id = %d\n", user_id, channel_id);
    
}

static void
pr_send_all_user_is_typing(struct bc_server *server, struct bc_websocket_frame *frame, u32 user_id)
{
    u32 channel_id = readu32(frame->payload.data + 1);
    
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *other = server->connections + i;
        struct bc_persist_session *other_session = connection_session(server, other);
        if (other_session) {
            u32 other_user_id = other_session->user_id;
            if (other_user_id != user_id) {
                pr_send_user_is_typing(server, other, channel_id, user_id);
            }
        }
    }
}

/***********************/
/******** RECV *********/
/***********************/
static void
pr_recv_change_password(struct bc_server *server, struct bc_websocket_frame *frame, u32 user_id)
{
    u8 password_length = frame->payload.data[1];
    struct bc_str password = { 0 };
    
    password.data = frame->payload.data + 2;
    password.length = password_length;
    
    struct bc_persist_user *user = mem_user_find(&server->memory, user_id);
    
    if (!user) {
        return;
    }
    
    if (!auth_write_hash(user, password)) {
        return;
    }
    
    if (!st2_user_update(&server->disk, &server->memory, user, 0, 0)) {
        return;
    }
}

static struct bc_persist_session *
pr_recv_auth(struct bc_server *server, struct bc_connection *connection, 
             struct bc_websocket_frame *frame)
{
    struct bc_str login = { 0 };
    struct bc_str password = { 0 };
    
    login.length = frame->payload.udata[1];
    password.length = frame->payload.length - 2 - login.length;
    
    login.data = frame->payload.data + 2;
    password.data = frame->payload.data + 2 + login.length;
    
    if (login.length <= 0 || password.length <= 0 || login.length >= frame->payload.length) {
        return(0);
    }
    
    struct bc_persist_user *user = mem_user_find_login(&server->memory, login);
    if (!user) {
        return(0);
    }
    
    if (!auth_check_password(user, password)) {
        return(0);
    }
    
    struct bc_persist_session session = { 0 };
    
    session.id = random_u31();
    session.sid = auth_generate_session();
    session.user_id = user->id;
    
    if (!st2_session_add(server, session)) {
        return(0);
    }
    
    struct bc_persist_session *saved = mem_session_find(&server->memory, session.sid);
    
    connection->session_id = session.sid;
    
    log_info("Successfully logged in user %.*s\n", login.length, login.data);
    log_debug("Session %lu\n", saved->sid);
    
    return(saved);
}

static void
pr_recv_seen(struct bc_server *server, struct bc_websocket_frame *frame, u32 user_id)
{
    u32 channel_id = readu32(frame->payload.data + 1);
    u32 first_unseen = readu32(frame->payload.data + 1 + 4);
    
    struct bc_persist_channel_user *cu_memory = mem_channeluser_find(&server->memory, user_id, channel_id);
    
    if (cu_memory) {
        if (cu_memory->first_unseen != first_unseen) {
            // Something actually changed
            cu_memory->first_unseen = first_unseen;
            st2_channeluser_update(&server->disk, cu_memory);
        }
        
        struct bc_v2i *cpc = server->views.connections_per_channel;
        struct bc_v2i *cc = view_find_sorted_1(cpc, channel_id);
        
        if (cc) {
            int cc_index = POINTER_DIFF(cc, cpc) / sizeof(*cpc);
            int cc_total_count = buffer_size(cpc);
            
            while (cc_index < cc_total_count && cc->channel_id == (int) channel_id) {
                struct bc_connection *connection = connection_get(server, cc->socket);
                
                if (connection) {
                    // TODO: @speed don't create the same buffer for every connection, use just 1 for everyone (1 per channel?)
                    pr_send_seen(server, connection, user_id, channel_id, first_unseen);
                }
                
                ++cc_index;
                ++cc;
            }
        }
    }
}

static struct bc_persist_session *
pr_recv_init(struct bc_server *server, struct bc_connection *connection, 
             struct bc_websocket_frame *frame)
{
    u64 sid = readu64(frame->payload.data + 1);
    int nchannels = readu32(frame->payload.data + 8 + 1);
    
    struct bc_persist_session *session = mem_session_find(&server->memory, sid);
    if (!session) {
        log_fwarning(__func__, "Session %lu not found\n", sid);
        return(0);
    }
    
    connection->session_id = sid;
    
    log_info("Successfully relogged session %lu\n", sid);
    
    int offset = 1 + 8 + 4;
    
    for (int i = 0; i < nchannels; ++i) {
        int channel_id = readu32(frame->payload.data + offset + 0 + i * 8);
        int sn = readu32(frame->payload.data + offset + 4 + i * 8);
        
        struct bc_persist_channel_session *cs = mem_channelsession_find(&server->memory, sid, channel_id);
        if (cs) {
            cs->first_unsent = sn;
            st2_channelsession_update(&server->disk, cs);
        } else {
            log_warning("Used %d no longer has session %lu in channel %d, ignoring SN\n", 
                        session->user_id, sid, channel_id);
        }
    }
    
    return(session);
}

static void
pr_recv_set_channel_title(struct bc_server *server, struct bc_persist_record *record, u32 channel_id)
{
    u32 title_length = record->title_changed.length;
    struct bc_str title = { 0 };
    
    title.data = record_data(record);
    title.length = title_length;
    
    struct bc_persist_channel *channel = mem_channel_find(&server->memory, channel_id);
    if (!channel) {
        log_fwarning(__func__, "Channel %d not found\n", channel_id);
        return;
    }
    
    if (!st2_channel_update(&server->disk, &server->memory, channel, &title)) {
        log_ferror(__func__, "Failed to update channel\n");
        return;
    }
}

/* IMPORTANT(aolo2): this is an EXPENSIVE function. Use with care! */
static void
_syn_channel(struct bc_server *server, u32 channel_id)
{
    /* "Broadcast" logic: send to all connections in this channel */
    struct bc_v2i *cpc = server->views.connections_per_channel;
    struct bc_v2i *cc = view_find_sorted_1(cpc, channel_id);
    
    if (cc) {
        int cc_index = POINTER_DIFF(cc, cpc) / sizeof(*cpc);
        int cc_total_count = buffer_size(cpc);
        
        while (cc_index < cc_total_count && cc->channel_id == (int) channel_id) {
            struct bc_connection *other_connection = connection_get(server, cc->socket);
            struct bc_persist_session *other_session = connection_session(server, other_connection);
            
            if (other_session) {
                struct bc_persist_channel_session *other_cs = mem_channelsession_find(&server->memory, other_connection->session_id, channel_id);
                if (other_cs) {
                    pr_send_syn(&server->queue, server, other_connection, other_cs, false);
                }
            }
            
            ++cc;
            ++cc_index;
        }
    }
}

static struct bc_persist_channel_user *
pr_recv_add_channel_user(struct bc_server *server, struct bc_persist_record *record, u32 channel_id)
{
    // TODO: check rights (not everyone can join every channel)
    
    u32 user_id = record->join.user_id;
    
    struct bc_persist_channel *channel = mem_channel_find(&server->memory, channel_id);
    
    if (!channel) {
        log_fwarning(__func__, "Attempt to invite user to a non-existant channel %d\n", channel_id);
        return(0);
    }
    
    struct bc_persist_channel_user cu = { 0 };
    
    cu.id = random_u31();
    cu.user_id = user_id;
    cu.channel_id = channel_id;
    
    if (!st2_channeluser_add(server, cu)) {
        log_fwarning(__func__, "User not added to channel!\n");
        return(0);
    }
    
    struct bc_persist_channel_user *saved = mem_channeluser_find(&server->memory, user_id, channel_id);
    
    pr_send_channel_invite_all_connections(&server->queue, server, user_id, channel);
    
#if 0
    // TODO: implement channel update on disk (in case this DM became a channel)
    struct bc_channel_info *channel_info = memory_find_channel_info(&server->storage.memory, channel_id);
    
    if (channel && channel_info) {
        if (channel->flags & CHANNEL_DM) {
            channel->flags &= ~((u32) CHANNEL_DM);
        }
    }
#endif
    
    // add connections of user to connections_per_channel view
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *other_connection = server->connections + i;
        struct bc_persist_session *other_session = connection_session(server, other_connection);
        if (other_session && other_session->user_id == (int) user_id) {
            struct bc_v2i channel_connection = { 0 };
            
            channel_connection.channel_id = channel_id;
            channel_connection.socket = other_connection->socket;
            
            view_insert_sorted_1(server->views.connections_per_channel, channel_connection);
        }
    }
    
    log_info("Successfully added user %d to channel %d\n", user_id, channel_id);
    
    return(saved);
}

static void
pr_recv_leave_channel(struct bc_server *server, struct bc_persist_record *record, u32 channel_id)
{
    u32 user_id = record->leave.user_id;
    
    struct bc_persist_channel_user *cu = mem_channeluser_find(&server->memory, user_id, channel_id);
    
    if (!cu) {
        log_warning("User %d attempted to leave channel %d they are not in. Not doing anything\n",
                    user_id, channel_id);
        return;
    }
    
    /* NOTE(aolo2): "Remove" - inmemory things, "Delete" - persistent things */
    
    /* TODO(aolo2): this deletion might happen WHILE an asyncronous send() is
going on. Implement LOCKS FINALLY! */
    
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *other = server->connections + i;
        struct bc_persist_session *session = connection_session(server, other);
        if (session && session->user_id == (int) user_id) {
            /* Remove channel-connections for all connections of this user */
            view_remove_all_pairs_2(server->views.connections_per_channel, other->socket);
            
            /* Delete channel-sessions for all sessions of this user */
            st2_channelsession_remove(server, session->sid, channel_id);
        }
    }
    
    /* Remove from user-per-channel, remove from channels-per-user */
    view_channeluser_remove(&server->views, cu);
    
    /* Delete channel-user record */
    st2_channeluser_remove(server, user_id, channel_id);
    
}

/* Probably the most important function - receiving messages from the user */
static void
pr_recv_syn(struct bc_queue *queue, struct bc_server *server, struct bc_connection *connection, struct bc_websocket_frame *frame, u32 user_id)
{
    u32 channel_id = readu32(frame->payload.data + 1);
    u32 sn = readu32(frame->payload.data + 1 + 4);
    u32 total_client_messages = readu32(frame->payload.data + 1 + 4 + 4);
    
    struct bc_channel_info *channel_info = view_find_channel_info(&server->views, channel_id);
    
    if (!channel_info) {
        log_ferror(__func__, "Channel info not found for channel %d\n", channel_id);
        return;
    }
    
    struct bc_persist_channel_session *cs = mem_channelsession_find(&server->memory, connection->session_id, channel_id);
    if (!cs) {
        log_warning("Session %lu not in channel %d\n", connection->session_id, channel_id);
        return;
    }
    
    struct bc_persist_channel_user *cu = mem_channeluser_find(&server->memory, user_id, channel_id);
    if (!cu) {
        log_warning("User %d not in channel %d\n", user_id, channel_id);
        return;
    }
    
    log_debug("[SN] Client sent SYN for channel %d with SN = %d (and %d messages)\n", channel_id, sn, total_client_messages);
    log_debug("[SN] Of those %d are new to us\n", sn - cs->first_unrecved);
    
    // Double-sends can happen, skip 'em
    if ((s32) sn > (int) cs->first_unrecved) {
        int we_expect_messages = (sn - cs->first_unrecved);
        int starting_message = total_client_messages - we_expect_messages;
        int offset = 1 + 4 + 4 + 4; // msg type + channel_id + client_sn + nmessages
        int starting_message_count = channel_info->message_count;
        
        for (int i = 0; i < (s32) total_client_messages; ++i) {
            // We could have received overlapping SYNs:
            // SYN1: SN = 10, MESSAGES = [ MESSAGE10 ]
            // SYN2: SN = 11, MESSAGES = [ MESSAGE10, MESSAGE11 ]
            //
            // Because we process them in order, our "client_sn" would get incremented
            // after processing the first SYN, meaning that we would only read 1 message from
            // SYN2 (which is correct). However, we would start reading from the start and thus
            // would process MESSAGE10 twice.
            //
            // Instead, we should read only the last (sn - client_sn) messages.
            u16 size = readu16(frame->payload.data + offset + 1); //skip byte for type
            
            // TODO: one writev for all messages (if that's possible). As it stands now,
            // this is a real way we can run out of bc_io's
            if (i >= starting_message) {
                struct bc_persist_record *record = (struct bc_persist_record *) (frame->payload.data + offset);
                
                if (record->type == WS_MESSAGE || record ->type == WS_TITLE_CHANGED || record ->type == WS_USER_LEFT || record ->type == WS_USER_JOINED) {
                    record->message.timestamp = unix_utcnow();
                }
                
                if (record->type == WS_REPLY && record->message_id == (u32) -1) {
                    record->message_id = starting_message_count; // NOTE(aolo2): message_id = -1 means this record refers to the last message
                }
                
                if (record->type == WS_ATTACH && record->message_id == (u32) - 1) {
                    record->message_id = starting_message_count;
                }
                
                if (!st2_message_add(server, channel_id, record)) {
                    log_ferror(__func__, "Failed to save message! Not reading any more\n");
                    return;
                }
                
                if (SYSTEM_RECORD(record->type)) {
                    switch (record->type) {
                        case WS_TITLE_CHANGED: {
                            pr_recv_set_channel_title(server, record, channel_id);
                            break;
                        }
                        case WS_USER_JOINED: {
                            pr_recv_add_channel_user(server, record, channel_id);
                            break;
                        }
                        case WS_USER_LEFT: {
                            pr_recv_leave_channel(server, record, channel_id);
                            break;
                        }
                    }
                }
            }
            
            offset += size;
        }
        
        cs->first_unrecved = sn;
        st2_channelsession_update(&server->disk, cs);
        
        cu->first_unseen = channel_info->message_count;
        st2_channeluser_update(&server->disk, cu);
    }
    
    pr_send_ack(queue, connection, channel_id, sn);
    
    _syn_channel(server, channel_id);
}

static void
pr_recv_ack(struct bc_server *server, struct bc_websocket_frame *frame, u64 sid)
{
    u32 channel_id = readu32(frame->payload.data + 1);
    u32 sn = readu32(frame->payload.data + 1 + 4);
    
    struct bc_persist_channel_session *cs_memory = mem_channelsession_find(&server->memory, sid, channel_id);
    
    if (cs_memory) {
        cs_memory->first_unsent = sn;
        st2_channelsession_update(&server->disk, cs_memory);
    } else {
        log_fwarning(__func__, "Session %lu not found in channel %d\n", sid, channel_id);
    }
}

static void
pr_recv_logout(struct bc_server *server, struct bc_persist_session *session)
{
    st2_session_remove(&server->disk, &server->memory, session->id);
}

static void
pr_recv_add_user(struct bc_server *server, struct bc_websocket_frame *frame)
{
    // TODO: check privilege level (only admin can add new users)
    
    u8 login_length = frame->payload.udata[1];
    u8 password_length = frame->payload.udata[2];
    u8 name_length = frame->payload.udata[3];
    
    char *login_data = frame->payload.data + 1 + 1 + 1 + 1;
    char *password_data = login_data + login_length;
    char *name_data = password_data + password_length;
    
    struct bc_str pass = { 0 };
    pass.data = password_data;
    pass.length = password_length;
    
    struct bc_str login = { 0 };
    login.data = login_data;
    login.length = login_length;
    
    struct bc_str name = { 0 };
    name.data = name_data;
    name.length = name_length;
    
    struct bc_persist_user user = { 0 };
    struct bc_persist_user *existing_user = mem_user_find_login(&server->memory, login);
    
    if (existing_user) {
        log_info("User with login %.*s already exists\n", login.length, login.data);
        return;
    }
    
    if (auth_write_hash(&user, pass)) {
        user.id = random_u31();
        if (st2_user_add(&server->disk, &server->memory, user, login, name)) {
            log_info("New user %.*s successfully added (id = %d)\n", login.length, login.data, user.id);
            return;
        }
    }
    
    log_warning("User not saved!\n");
}

static struct bc_persist_channel *
pr_recv_add_channel(struct bc_queue *queue, struct bc_server *server, struct bc_connection *connection,
                    struct bc_websocket_frame *frame, u32 user_id)
{
    // TODO: check privilege level?
    
    u8 title_length = frame->payload.udata[1];
    char *title_text = frame->payload.data + 2;
    
    struct bc_persist_channel channel = { 0 };
    struct bc_str title = { 0 };
    
    title.data = title_text;
    title.length = title_length;
    
    channel.id = random_u31();
    
    if (!st2_channel_add(server, channel, title)) {
        log_error("Channel not created!\n");
        return(0);
    }
    
    struct bc_persist_channel *channel_saved = mem_channel_find(&server->memory, channel.id); // TODO: where ID from?
    
    log_info("Successfully created channel %d ('%.*s')\n", channel_saved->id, title.length, title.data);
    
    struct bc_persist_channel_user cu = { 0 };
    
    cu.id = random_u31();
    cu.user_id = user_id;
    cu.channel_id = channel_saved->id;
    
    if (!st2_channeluser_add(server, cu)) {
        log_warning("User not added to channel!\n");
        return(0);
    }
    
    struct bc_v2i channel_connection = { 0 };
    
    channel_connection.channel_id = cu.channel_id;
    channel_connection.socket = connection->socket;
    
    view_insert_sorted_1(server->views.connections_per_channel, channel_connection);
    
    pr_send_channel_invite_all_connections(queue, server, user_id, channel_saved);
    
    log_info("Successfully invited user %d to channel %d\n", user_id, channel_saved->id);
    
    return(channel_saved);
}

static struct bc_persist_channel *
pr_recv_add_direct(struct bc_queue *queue, struct bc_server *server, 
                   struct bc_websocket_frame *frame, u32 user_id)
{
    u32 other_user_id = readu32(frame->payload.udata + 1);
    struct bc_persist_channel direct = { 0 };
    
    struct bc_str direct_title = { 0 };
    direct_title.data = (char *) ".";
    direct_title.length = 1;
    
    direct.flags |= CHANNEL_DM;
    direct.id = random_u31();
    
    if (!st2_channel_add(server, direct, direct_title)) {
        log_warning("Channel not created!\n");
        return(0);
    }
    
    struct bc_persist_channel *channel_saved = mem_channel_find(&server->memory, direct.id);
    struct bc_persist_channel_user cu = { 0 };
    
    cu.id = random_u31();
    cu.user_id = user_id;
    cu.channel_id = channel_saved->id;
    
    if (!st2_channeluser_add(server, cu)) {
        log_warning("First user not added to direct!\n");
        return(0);
    }
    
    if (other_user_id != user_id) {
        cu.id = random_u31();
        cu.user_id = other_user_id;
        
        if (!st2_channeluser_add(server, cu)) {
            log_warning("Second user not added to direct!\n");
            return(0);
        }
    }
    
    // add connections of these two users to connections_per_channel view
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *other_connection = server->connections + i;
        struct bc_persist_session *other_session = connection_session(server, other_connection);
        if (other_session && (other_session->user_id == (int) user_id || other_session->user_id == (int) other_user_id)) {
            struct bc_v2i channel_connection = { 0 };
            
            channel_connection.channel_id = channel_saved->id;
            channel_connection.socket = other_connection->socket;
            
            view_insert_sorted_1(server->views.connections_per_channel, channel_connection);
        }
    }
    
    pr_send_channel_invite_all_connections(queue, server, user_id, channel_saved);
    
    if (other_user_id != user_id) {
        pr_send_channel_invite_all_connections(queue, server, other_user_id, channel_saved);
    }
    
    return(channel_saved);
}

static void
pr_recv_request_channel_info(struct bc_queue *queue, struct bc_server *server,
                             struct bc_connection *connection, struct bc_websocket_frame *frame,
                             u32 user_id)
{
    u32 channel_id = readu32(frame->payload.udata + 1);
    
    struct bc_persist_channel_user *cu = mem_channeluser_find(&server->memory, user_id, channel_id);
    if (!cu) {
        return;
    }
    
    struct bc_persist_channel *channel = mem_channel_find(&server->memory, channel_id);
    if (channel) {
        pr_send_channel_info(queue, server, connection, channel);
    }
}

static void
pr_recv_set_user_avatar(struct bc_server *server, struct bc_websocket_frame *frame,
                        u32 user_id)
{
    u64 avatar_id = readu64(frame->payload.udata + 1);
    
    struct bc_persist_user *user = mem_user_find(&server->memory, user_id);
    if (!user) {
        log_fwarning(__func__, "User %d not found\n", user_id);
        return;
    }
    
    user->avatar_id = avatar_id;
    
    if (!st2_user_update(&server->disk, &server->memory, user, 0, 0)) {
        log_ferror(__func__, "Failed to update user\n");
        return;
    }
}

static void
pr_recv_set_channel_avatar(struct bc_server *server, struct bc_websocket_frame *frame)
{
    u32 channel_id = readu32(frame->payload.udata + 1);
    u64 avatar_id = readu64(frame->payload.udata + 1 + 4);
    
    struct bc_persist_channel *channel = mem_channel_find(&server->memory, channel_id);
    if (!channel) {
        log_fwarning(__func__, "Channel %d not found\n", channel_id);
        return;
    }
    
    channel->avatar_id = avatar_id;
    
    if (!st2_channel_update(&server->disk, &server->memory, channel, 0)) {
        log_ferror(__func__, "Failed to update channel\n");
        return;
    }
}

static void
pr_recv_save_user_blob(struct bc_server *server, struct bc_connection *connection,
                       struct bc_websocket_frame *frame, u32 user_id) 
{
    struct bc_persist_user *user = mem_user_find(&server->memory, user_id);
    
    if (!user) {
        log_ferror(__func__, "User %d not found, blob not saved\n", user_id);
        return;
    }
    
    u32 nonce = readu32(frame->payload.udata + 1);
    u32 blob_length = readu32(frame->payload.udata + 1 + 4);
    int old_user_blob_block = user->blob_block;
    
    struct bc_str blob = { 0 };
    
    blob.data = frame->payload.data + 1 + 4 + 4;
    blob.length = blob_length;
    
    if (!st2_string_put(server, blob, &user->blob_block)) {
        log_ferror(__func__, "Failed to save blob\n");
        return;
    }
    
    if (old_user_blob_block != user->blob_block) {
        if (!st2_user_update(&server->disk, &server->memory, user, 0, 0)) {
            return;
        }
    }
    
    pr_send_utf8_id(server, connection, nonce, user->blob_block);
}

static void
pr_recv_get_user_blob(struct bc_server *server, struct bc_connection *connection,
                      struct bc_websocket_frame *frame, u32 user_id) 
{
    struct bc_persist_user *user = mem_user_find(&server->memory, user_id);
    
    if (!user) {
        log_ferror(__func__, "User %d not found, blob not returned\n", user_id);
        return;
    }
    
    u32 nonce = readu32(frame->payload.udata + 1);
    struct bc_str blob = mem_block_find(&server->memory.block_storage, user->blob_block);
    
    if (blob.data) {
        pr_send_utf8_data(server, connection, nonce, blob);
    }
}

static void
pr_send_user_status_info(struct bc_server *server, u32 user_id)
{
    struct bc_user_info *user_info = view_find_user_info(&server->views, user_id);
    
    int user_info_size = sizeof(struct bc_user_info);
    int payload_length = 1 + user_info_size;
    if (user_info) {
        for (int i = 0; i < buffer_size(server->connections); ++i) {
            struct bc_connection *connection = server->connections + i;
            struct bc_persist_session *session = connection_session(server, connection);
            if (session) {
                struct bc_io *req = queue_push(&server->queue, IOU_SEND);
                
                req->buf[0] = FRAME_BINARY | 0x80;
                req->buf[1] = payload_length;
                req->buf[2] = WS_SERVER_USER_STATUS;
                
                memcpy(req->buf + 3, user_info, user_info_size);
                
                _send_constant_message(&server->queue, connection, req, payload_length);
            }
        }
    }
}