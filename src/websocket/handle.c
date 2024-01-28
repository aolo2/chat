static char response_template_101[] = "HTTP/1.1 101 Switching Protocols\r\n"
"Server: Bullet.Chat\r\n"
"Upgrade: websocket\r\n"
"Connection: Upgrade\r\n"
"Sec-WebSocket-Accept: %.*s\r\n"
"\r\n";

static char *
get_header_value(struct bc_http_request *request, const char *key)
{
    //int length = strlen(key);
    
    for (int i = 0; i < request->headers.nheaders; ++i) {
        if (strncasecmp(key, request->headers.keys[i].data, request->headers.keys[i].length) == 0) {
            return(request->headers.values[i].data);
        }
    }
    
    return(0);
}

static bool
is_websocket_upgrade(struct bc_http_request *request)
{
    char *host = get_header_value(request, "Host");
    char *upgrade = get_header_value(request, "Upgrade");
    char *connection = get_header_value(request, "Connection");
    char *sec_websocket_key = get_header_value(request, "Sec-WebSocket-Key");
    char *sec_websocket_version = get_header_value(request, "Sec-WebSocket-Version");
    
    if (!host) {
        return(false);
    }
    
    if (!upgrade || strncmp(upgrade, "websocket", 9) != 0) {
        return(false);
    }
    
    if (!connection || (strncmp(connection, "Upgrade", 7) != 0 && strncmp(connection, "keep-alive, Upgrade", 19) != 0)) {
        return(false);
    }
    
    if (!sec_websocket_key) {
        return(false);
    }
    
    if (!sec_websocket_version || strncmp(sec_websocket_version, "13", 2) != 0) {
        return(false);
    }
    
    return(true);
}

static struct bc_sha1
sha1_hash(char *value, int length)
{
    struct bc_sha1 result = { 0 };
    
    SHA1Context sha;
    SHA1Reset(&sha);
    SHA1Input(&sha, (const unsigned char *) value, length);
    SHA1Result(&sha, result.data);
    
    return(result);
}

static char base64_table[64] = 
{
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

static int
base64_encode(char *result, u8 *input, int length)
{
    int at = 0;
    
    for (int i = 0; i < length; i += 3) {
        if (i == length - 1) {
            u8 b1 = input[i + 0];
            
            int c1 = (b1 & 0xFC) >> 2;
            int c2 = (b1 & 0x3) << 4;
            
            result[at++] = base64_table[c1];
            result[at++] = base64_table[c2];
        } else if (i == length - 2) {
            u8 b1 = input[i + 0];
            u8 b2 = input[i + 1];
            
            int c1 = (b1 & 0xFC) >> 2;
            int c2 = ((b1 & 0x3) << 4) | ((b2 & 0xF0) >> 4);
            int c3 = (b2 & 0xF) << 2;
            
            result[at++] = base64_table[c1];
            result[at++] = base64_table[c2];
            result[at++] = base64_table[c3];
        } else {
            u8 b1 = input[i + 0];
            u8 b2 = input[i + 1];
            u8 b3 = input[i + 2];
            
            int c1 = (b1 & 0xFC) >> 2;
            int c2 = ((b1 & 0x3) << 4) | ((b2 & 0xF0) >> 4);
            int c3 = ((b2 & 0xF) << 2) | ((b3 & 0xC0) >> 6);
            int c4 = b3 & 0x3F;
            
            result[at++] = base64_table[c1];
            result[at++] = base64_table[c2];
            result[at++] = base64_table[c3];
            result[at++] = base64_table[c4];
        }
    }
    
    int remainder = 4 - at % 4;
    if (remainder) {
        for (int i = 0; i < remainder; ++i) {
            result[at++] = '=';
        }
    }
    
    return(at);
}

static void
update_user_online(struct bc_server *server, u32 user_id)
{
    struct bc_user_info *user_info = view_find_user_info(&server->views, user_id);
    if (user_info) {
        user_info->last_online = unix_utcnow();
        user_info->status = STATUS_ONLINE;
        pr_send_user_status_info(server, user_id);
    }
}

void
made_user_offline(struct bc_server *server, struct bc_persist_session *session)
{
    /*
If there are no connections left for this user,
then set his last online to now and status to offline
*/
    bool still_online = false;
    
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *other_connection = server->connections + i;
        struct bc_persist_session *other_session = connection_session(server, other_connection);
        if (other_session && other_session->user_id == session->user_id) {
            still_online = true;
            break;
        }
    }
    
    if (!still_online) {
        struct bc_user_info *user_info = view_find_user_info(&server->views, session->user_id);
        if (user_info) {
            user_info->last_online = unix_utcnow();
            user_info->status = STATUS_OFFLINE;
            pr_send_user_status_info(server, session->user_id);
        }
    }
}

static void
handle_connection_drop(struct bc_server *server, struct bc_connection *connection)
{
    connection_drop(server, connection);
    struct bc_persist_session *session = mem_session_find(&server->memory, connection->session_id);
    if (session) {
        made_user_offline(server, session);
    }
    
    queue_cancel_all_writes_to_socket(&server->queue, connection->socket);
}

// TODO: return bool (success)
static void
handle_websocket_handshake(struct bc_queue *queue, struct bc_connection *connection, struct bc_http_request *request)
{
    char key[256] = { 0 };
    char *sec_websocket_key = get_header_value(request, "Sec-WebSocket-Key");
    const char *suffix = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    int at = 0;
    
    for (at = 0; at < 256; ++at) {
        if (sec_websocket_key[at] != '\r') {
            key[at] = sec_websocket_key[at];
        } else {
            break;
        }
    }
    
    if (at + 36 >= 256) {
        log_ferror(__func__, "key too long\n");
        return;
    }
    
    for (int i = 0; i < 36; ++i) {
        key[at] = suffix[i];
        ++at;
    }
    
    struct bc_sha1 digest = sha1_hash(key, at);
    char base64[128] = { 0 };
    int base64_len = base64_encode(base64, digest.data, 20);
    
    // TODO: use some other buffer, remove send_buf from connection?
    int response_length = snprintf(connection->send_buf, SEND_BUFFER_SIZE, response_template_101, base64_len, base64);
    if (response_length < 0) {
        log_error("snprintf failed\n");
        return;
    }
    
    struct bc_io *req = queue_push(queue, IOU_SEND);
    
    req->send.buf = connection->send_buf;
    req->send.total = response_length;
    req->send.start = 0;
    req->send.socket = connection->socket;
    
    queue_send(queue, req, 0);
}

static void
handle_websocket_send_close_frame(struct bc_queue *queue, struct bc_connection *connection)
{
    struct bc_io *req = queue_push(queue, IOU_SEND);
    
    req->buf[0] = FRAME_CLOSE | 0x80;
    req->buf[1] = 0;
    
    req->send.socket = connection->socket;
    req->send.start = 0;
    req->send.total = 2;
    req->send.buf = (char *) req->buf;
    
    connection->state = CONNECTION_CLOSING;
    
    queue_send(queue, req, 0);
}

static void
handle_websocket_send_pong_frame(struct bc_queue *queue, struct bc_connection *connection)
{
    struct bc_io *req = queue_push(queue, IOU_SEND);
    
    req->buf[0] = FRAME_PONG | 0x80;
    req->buf[1] = 0;
    
    req->send.socket = connection->socket;
    req->send.start = 0;
    req->send.total = 2;
    req->send.buf =  (char *) req->buf;
    
    queue_send(queue, req, 0);
}

static void
handle_send_pushpoll(struct bc_server *server)
{
    struct bc2_transient *views = &server->views;
    for (int i = 0; i < buffer_size(views->user_info); ++i) {
        struct bc_user_info *user_info = views->user_info + i;
        if (user_info->status == STATUS_ONLINE && unix_utcnow() - user_info->last_online > AWAY_INACTIVITY_PERIOD) {
            user_info->status = STATUS_AWAY;
            pr_send_user_status_info(server, user_info->user_id);
        }
    }
}

static void
handle_complete_websocket_frame(struct bc_server *server, struct bc_queue *queue, struct bc_connection *connection, struct bc_websocket_frame *frame)
{
    if (frame->opcode == FRAME_BINARY) {
        enum bc_wsmessage_type opcode = frame->payload.udata[0];
        
        log_debug("Got opcode %s\n", WEBSOCKET_OPCODES[opcode]);
        
        /* Unauthorized clients can only send AUTH || INIT || ADD_USER (sign up) */
        
        struct bc_persist_session *user_session = 0;
        u32 user_id = -1;
        if (opcode != WS_CLIENT_AUTH && opcode != WS_CLIENT_INIT) {
            user_session = connection_session(server, connection);
            if (user_session) {
                user_id = user_session->user_id;
            }
        }
        
#if DEBUG_ALLOW_EVERYTHING
        
#else
        if (opcode != WS_CLIENT_AUTH && opcode != WS_CLIENT_INIT && opcode != WS_CLIENT_ADD_USER) {
            user_session = connection_session(server, connection);
            if (!user_session) {
                log_warning("Got opcode %d but unauthorized\n", opcode);
                //pr_send_auth(queue, connection, 0);
                return;
            } else {
                user_id = user_session->user_id;
            }
        }
#endif
        
        switch (opcode) {
            case WS_CLIENT_AUTH: {
                struct bc_persist_session *session = pr_recv_auth(server, connection, frame);
                pr_send_auth(queue, connection, session);
                break;
            }
            
            case WS_CLIENT_LOGOUT: {
                pr_recv_logout(server, user_session);
                
                made_user_offline(server, user_session);
                break;
            }
            
            case WS_CLIENT_INIT: {
                user_session = pr_recv_init(server, connection, frame);
                if (!user_session) {
                    pr_send_auth(queue, connection, 0);
                    break;
                } else {
                    user_id = user_session->user_id;
                }
                
                // TODO: join these messages for @speed
                pr_send_init(queue, server, connection, user_id);
                pr_send_syn_all_channels(queue, server, connection, user_id);
                
                update_user_online(server, user_id);
                break;
            }
            
            case WS_CLIENT_SYNC: {
                pr_recv_syn(queue, server, connection, frame, user_id);
                update_user_online(server, user_id);
                break;
            }
            
            case WS_CLIENT_ACK: {
                pr_recv_ack(server, frame, user_session->sid);
                break;
            }
            
            case WS_CLIENT_SEEN: {
                pr_recv_seen(server, frame, user_id);
                update_user_online(server, user_id);
                break;
            }
            
            case WS_CLIENT_ADD_USER: {
                pr_recv_add_user(server, frame);
                break;
            }
            
            case WS_CLIENT_ADD_CHANNEL: {
                pr_recv_add_channel(queue, server, connection, frame, user_id);
                update_user_online(server, user_id);
                break;
            }
            
            case WS_CLIENT_ADD_DIRECT: {
                pr_recv_add_direct(queue, server, frame, user_id);
                update_user_online(server, user_id);
                break;
            }
            
            case WS_CLIENT_REQUEST_CHANNEL_INFO: {
                pr_recv_request_channel_info(queue, server, connection, frame, user_id);
                update_user_online(server, user_id);
                break;
            }
            
            case WS_CLIENT_IS_TYPING: {
                pr_send_all_user_is_typing(server, frame, user_id);
                break;
            }
            
            case WS_CLIENT_SET_USER_AVATAR: {
                pr_recv_set_user_avatar(server, frame, user_id);
                break;
            }
            
            case WS_CLIENT_SET_CHANNEL_AVATAR: {
                pr_recv_set_channel_avatar(server, frame);
                break;
            }
            
            case WS_CLIENT_CHANGE_PASSWORD: {
                pr_recv_change_password(server, frame, user_id);
                break;
            }
            
            case WS_CLIENT_SAVE_UTF8: {
                pr_recv_save_user_blob(server, connection, frame, user_id);
                break;
            }
            
            case WS_CLIENT_REQUEST_UTF8: {
                pr_recv_get_user_blob(server, connection, frame, user_id);
                break;
            }
            
            default: {
                log_warning("Unhandled protocol opcode %d\n", opcode);
            }
        }
    } else if (frame->opcode == FRAME_CLOSE) {
        log_debug("CLOSE frame received from client %d\n", connection->socket);
        handle_websocket_send_close_frame(queue, connection);
        struct bc_persist_session *session = mem_session_find(&server->memory, connection->session_id);
        if (session) {
            made_user_offline(server, session);
        }
    } else if (frame->opcode == FRAME_PING) {
        handle_websocket_send_pong_frame(queue, connection);
    } else {
        log_warning("Unhandled websocket frame opcode %d\n", frame->opcode);
    }
}
