/*********************************/
/* USERS */
/*********************************/
static int
st2_user_add(struct bc2_disk *disk, struct bc2_memory *mem,
             struct bc_persist_user user, struct bc_str login, struct bc_str name)
{
    struct bc_persist_user *saved_user = mem_user_add(mem, user, login, name);
    struct bc_str saved_login = mem_block_find(&mem->block_storage, saved_user->login_block);
    struct bc_str saved_name = mem_block_find(&mem->block_storage, saved_user->name_block);
    
    /* TODO: update views */
    
    if (saved_user && saved_login.data && saved_name.data) {
        return(bdisk_user_add(disk, saved_user, &saved_login, &saved_name));
    }
    
    return(0);
}

static int
st2_user_update(struct bc2_disk *disk, struct bc2_memory *mem,
                struct bc_persist_user *user, struct bc_str *login, struct bc_str *name)
{
    if (mem_user_update(mem, user->id, login, name)) {
        struct bc_str updated_login = mem_block_find(&mem->block_storage, user->login_block);
        struct bc_str updated_name = mem_block_find(&mem->block_storage, user->name_block);
        
        if (updated_login.data && updated_name.data) {
            return(bdisk_user_update(disk, user, &updated_login, &updated_name));
        }
    }
    
    return(0);
}

static int
st2_user_remove(struct bc2_disk *disk, struct bc2_memory *mem, u32 user_id)
{
    struct bc_persist_user *saved = mem_user_find(mem, user_id);
    
    /* We remove from disk first, because name_block and login_block fields are needed in disk_XXX */
    int nops = bdisk_user_remove(disk, saved);
    
    if (nops) {
        mem_user_remove(mem, user_id);
        /* TODO: update views */
        return(nops);
    }
    
    return(0);
}

/*********************************/
/* CHANNELSESSIONS */
/*********************************/
static int
st2_channelsession_add(struct bc_server *server,
                       struct bc_persist_channel_session channel_session)
{
    struct bc_persist_channel_session *saved_channel_session = mem_channelsession_add(&server->memory, channel_session);
    
    if (saved_channel_session) {
        return(bdisk_channelsession_add(&server->disk, saved_channel_session));
    }
    
    return(0);
}

static int
st2_channelsession_update(struct bc2_disk *disk, struct bc_persist_channel_session *channel_session)
{
    if (channel_session) {
        return(bdisk_channelsession_update(disk, channel_session));
    }
    
    return(0);
}

static int
st2_channelsession_remove(struct bc_server *server, u64 sid, u32 channel_id)
{
    struct bc_persist_channel_session *saved = mem_channelsession_find(&server->memory, sid, channel_id);
    u32 block_id = saved->id;
    
    if (mem_channelsession_remove(&server->memory, sid, channel_id)) {
        return(bdisk_channelsession_remove(&server->disk, block_id));
    }
    
    return(0);
}

/*********************************/
/* SESSIONS */
/*********************************/
static int
st2_session_add(struct bc_server *server, struct bc_persist_session session)
{
    int nops = 0;
    
    struct bc_persist_session *saved_session = mem_session_add(&server->memory, session);
    
    struct bc_v2i *cpu = server->views.channels_per_user;
    struct bc_v2i *cu = view_find_sorted_2(cpu, session.user_id);
    
    /* TODO: update views */
    
    if (saved_session) {
        nops = bdisk_session_add(&server->disk, saved_session);
        
        // Create channel sessions for all channels this user-o is-o in-o
        if (cu) {
            int cu_index = POINTER_DIFF(cu, cpu) / sizeof(*cpu);
            int cu_total_count = buffer_size(cpu);
            
            while (cu_index < cu_total_count && cu->user_id == session.user_id) {
                struct bc_persist_channel_session cs = { 0 };
                
                cs.id = random_u31();
                cs.session_id = session.sid;
                cs.channel_id = cu->channel_id;
                
                // This is a new session, we could not have sent/recved anything yet
                cs.first_unsent = 0;
                cs.first_unrecved = 0;
                
                nops += st2_channelsession_add(server, cs);
                
                ++cu;
                ++cu_index;
            }
        }
        
        return(nops);
    }
    
    return(0);
}

static int
st2_session_update(struct bc2_disk *disk, struct bc_persist_session *session)
{
    if (session) {
        return(bdisk_session_update(disk, session));
    }
    
    return(0);
}

static int
st2_session_remove(struct bc2_disk *disk, struct bc2_memory *mem, u64 sid)
{
    struct bc_persist_session *session = mem_session_find(mem, sid);
    
    /* TODO: update views */
    /* TODO: remove channel-sessions */
    
    if (session) {
        if (mem_session_remove(mem, session->sid)) {
            return(bdisk_session_remove(disk, session->id));
        }
    }
    
    return(0);
}

/*********************************/
/* CHANNELS */
/*********************************/
static int
st2_channel_add(struct bc_server *server,
                struct bc_persist_channel channel, struct bc_str title)
{
    struct bc2_memory *mem = &server->memory;
    struct bc2_disk *disk = &server->disk;
    
    struct bc_persist_channel *saved_channel = mem_channel_add(mem, channel, title);
    struct bc_str saved_title = mem_block_find(&mem->block_storage, saved_channel->title_block);
    
    view_channel_add(&server->views, saved_channel);
    
    if (saved_channel && saved_title.data) {
        return(bdisk_channel_add(disk, saved_channel, &saved_title));
    }
    
    return(0);
}

static int
st2_channel_update(struct bc2_disk *disk, struct bc2_memory *mem,
                   struct bc_persist_channel *channel, struct bc_str *title)
{
    if (mem_channel_update(mem, channel->id, title)) {
        struct bc_str updated_title = mem_block_find(&mem->block_storage, channel->title_block);
        
        if (updated_title.data) {
            return(bdisk_channel_update(disk, channel, &updated_title));
        }
    }
    
    return(0);
}

static int
st2_channel_remove(struct bc2_disk *disk, struct bc2_memory *mem, u32 channel_id)
{
    struct bc_persist_channel *saved = mem_channel_find(mem, channel_id);
    
    /* We remove from disk first, because name_block and login_block fields are needed in disk_XXX */
    int nops = bdisk_channel_remove(disk, saved);
    
    if (nops) {
        mem_channel_remove(mem, channel_id);
        /* TODO: update views */
        return(nops);
    }
    
    return(0);
}

/*********************************/
/* CHANNELUSERS */
/*********************************/
static int
st2_channeluser_add(struct bc_server *server,
                    struct bc_persist_channel_user channel_user)
{
    int nops = 0;
    
    struct bc_persist_channel_user *saved_channel_user = mem_channeluser_add(&server->memory, channel_user);
    
    // Create channel sessions for all sessions of this user
    // TODO: only online sessions
    for (int i = 0; i < buffer_size(server->memory.sessions); ++i) {
        struct bc_persist_session *session = server->memory.sessions + i;
        if (session->user_id == (int) channel_user.user_id) {
            struct bc_persist_channel_session channel_session = { 0 };
            
            channel_session.id = random_u31();
            channel_session.session_id = session->sid;
            channel_session.channel_id = channel_user.channel_id;
            
            nops += st2_channelsession_add(server, channel_session);
        }
    }
    
    view_channeluser_add(&server->views, saved_channel_user);
    
    if (saved_channel_user) {
        nops += bdisk_channeluser_add(&server->disk, saved_channel_user);
    }
    
    return(nops);
}

static int
st2_channeluser_update(struct bc2_disk *disk, struct bc_persist_channel_user *channel_user)
{
    if (channel_user) {
        return(bdisk_channeluser_update(disk, channel_user));
    }
    
    return(0);
}

static int
st2_channeluser_remove(struct bc_server *server, u32 user_id, u32 channel_id)
{
    struct bc_persist_channel_user *saved = mem_channeluser_find(&server->memory, user_id, channel_id);
    u32 block_id = saved->id;
    
    view_channeluser_remove(&server->views, saved);
    
    if (mem_channeluser_remove(&server->memory, user_id, channel_id)) {
        return(bdisk_channeluser_remove(&server->disk, block_id));
    }
    
    return(0);
}

/*********************************/
/* MESSAGES */
/*********************************/
static int
st2_message_add(struct bc_server *server, int channel_id, struct bc_persist_record *record)
{
    /* The record comes already serialized from the client */
    
    if (!record) {
        return(0);
    }
    
    record->size = record_size(record);
    
    struct bc_persist_record *saved = mem_message_add(server, channel_id, record);
    
    if (saved) {
        return(bdisk_message_add(&server->disk, channel_id, saved));
    }
    
    return(0);
}

static int
st2_string_put(struct bc_server *server, struct bc_str data, /* inout */ int *block)
{
    struct bc2_file *file_strings = bdisk_find_file(&server->disk, BDISK_FILEID_STRINGS);
    
    if (!file_strings) {
        return(0);
    }
    
    int nops = 0;
    
    if (*block != -1) {
        if (!mem_block_remove(&server->memory.block_storage, *block)) {
            return(0);
        }
        
        nops += bdisk_string_remove(&server->disk, file_strings, *block);
    }
    
    int block_id = mem_block_add(&server->memory.block_storage, &data);
    *block = block_id;
    
    struct bc_str saved = mem_block_find(&server->memory.block_storage, block_id);
    nops += bdisk_string_add(&server->disk, file_strings, block_id, &saved);
    
    *block = block_id;
    
    return(nops);
}

static bool
st2_init(struct bc_server *server)
{
    server->disk.messages_directory = "messages";
    
    if (!directory_exists(server->disk.messages_directory)) {
        log_fwarning(__func__, "Creating \"messages\" directory\n");
        if (mkdir(server->disk.messages_directory, 0755) == -1) {
            log_fperror(__func__, "mkdir");
            return(false);
        }
    }
    
    if (!mem_init(&server->memory)) {
        return(false);
    }
    
    if (!bdisk_init(&server->disk)) {
        return(false);
    }
    
    if (!mem_load(&server->disk, &server->memory)) {
        return(false);
    }
    
    if (!bdisk_init_message_files(&server->disk, &server->memory)) {
        return(false);
    }
    
    if (!view_init(server)) {
        return(false);
    }
    
    server->disk.queue = &server->queue;
    
    return(true);
}