/* Internal */
static bool
mem_block_init(struct bc2_block_storage *dest)
{
    dest->autoid = 0;
    dest->total = 0;
    dest->vm = mapping_reserve(GB(1));
    
    if (!dest->vm.size) {
        return(false);
    }
    
    return(true);
}

static struct bc_str
mem_block_find(struct bc2_block_storage *block_storage, int id)
{
    struct bc_str result = { 0 };
    u64 offset = 0;
    
    while (offset < block_storage->total) {
        struct bc_block_header *header = (struct bc_block_header *) (block_storage->vm.base + offset);
        
        if (header->id == id) {
            result.data = POINTER_INC(header, sizeof(*header));
            result.length = header->data_size;
            break;
        }
        
        offset += sizeof(*header) + header->data_size;
    }
    
    return(result);
}

static int
mem_block_add2(struct bc2_block_storage *block_storage, struct bc_str *data, struct bc_block_header *header)
{
    u64 size_with_header = sizeof(*header) + data->length;
    
    /* Commit more pages if needed */
    if (block_storage->total + size_with_header > block_storage->vm.commited) {
        u64 commit_size = round_up_to_page_size(size_with_header);
        
        if (!mapping_expand(&block_storage->vm, commit_size)) {
            log_critical_die("Failed to expand block storage. vm.reserve = %lu, vm.commited = %lu, commit_size = %lu",
                             block_storage->vm.size, block_storage->vm.commited, commit_size);
            /* unreachable */
            return(BAD_BLOCK_ID);
        }
        
        block_storage->vm.commited += commit_size;
    }
    
    /* Write header */
    memcpy(block_storage->vm.base + block_storage->total, header, sizeof(*header));
    
    /* Write the actual data */
    memcpy(block_storage->vm.base + block_storage->total + sizeof(*header), data->data, data->length);
    
    block_storage->total += size_with_header;
    
    return(header->id);
}

static int
mem_block_add(struct bc2_block_storage *block_storage, struct bc_str *data)
{
    struct bc_block_header header = { 0 };
    
    header.id = block_storage->autoid++;
    header.data_size = data->length;
    
    return(mem_block_add2(block_storage, data, &header));
}

static bool
mem_block_remove(struct bc2_block_storage *block_storage, u32 id)
{
    struct bc_str block = mem_block_find(block_storage, id);
    
    if (block.data) {
        struct bc_block_header *header = (struct bc_block_header *) POINTER_DEC(block.data, sizeof(struct bc_block_header));
        u64 size_with_header = header->data_size + sizeof(*header);
        
        void *dest = header;
        void *src = POINTER_INC(header, size_with_header);
        u64 move_size = block_storage->total - POINTER_DIFF(src, block_storage->vm.base);
        
        if (move_size > 0) {
            memmove(dest, src, move_size);
        }
        
        block_storage->total -= size_with_header;
    }
    
    return(false);
}

static bool
mem_init(struct bc2_memory *dest)
{
    dest->users = buffer_init(MAX_POSSIBLE_USERS, sizeof(struct bc_persist_user));
    dest->sessions = buffer_init(MAX_POSSIBLE_SESSIONS, sizeof(struct bc_persist_session));
    dest->channels = buffer_init(MAX_POSSIBLE_CHANNELS, sizeof(struct bc_persist_channel));
    dest->channel_users = buffer_init(MAX_POSSIBLE_CHANNELS * MAX_POSSIBLE_USERS, sizeof(struct bc_persist_channel_user));
    dest->channel_sessions = buffer_init(MAX_POSSIBLE_CHANNELS * MAX_POSSIBLE_SESSIONS, sizeof(struct bc_persist_channel_session));
    
    /* Don't forget to add new fields to this check */
    if (!dest->users || !dest->sessions || !dest->channels || !dest->channel_users || !dest->channel_sessions) {
        return(false);
    }
    
    if (!mem_block_init(&dest->block_storage)) {
        return(false);
    }
    
    return(true);
}

static bool
mem_load_fixed(struct bc2_disk *disk, enum bdisk_fileid file_id, void *field, int field_size)
{
    struct bc2_file *file = bdisk_find_file(disk, file_id);
    
    if (!file) {
        return(false);
    }
    
    if (file->size) {
        char *data = mmap(0, file->size, PROT_READ, MAP_PRIVATE, file->fd, 0);
        
        if (data == MAP_FAILED) {
            log_fperror(__func__, "mmap");
            return(false);
        }
        
        u32 offset = BDISK_HEADER_SIZE
            + sizeof(int);  /* chunk_size */
        
        while (offset < file->size) {
            int block_id = reads32(data + offset);
            
            offset += sizeof(int);
            
            /* id == -1 means the block is marked as "deleted" */
            if (block_id != -1) {
                buffer_push_typeless(field, data + offset, field_size);
            }
            
            offset += file->chunk_size;
        }
        
        munmap(data, file->size);
    }
    
    return(true);
}

static bool
mem_load_strings(struct bc2_disk *disk, enum bdisk_fileid file_id, struct bc2_block_storage *block_storage)
{
    struct bc2_file *file = bdisk_find_file(disk, file_id);
    
    if (!file) {
        return(false);
    }
    
    if (file->size) {
        char *data = mmap(0, file->size, PROT_READ, MAP_PRIVATE, file->fd, 0);
        
        if (data == MAP_FAILED) {
            log_fperror(__func__, "mmap");
            return(false);
        }
        
        u32 offset = BDISK_HEADER_SIZE;
        int max_id = -1;
        
        while (offset < file->size) {
            struct bc_block_header block_header = { 0 };
            struct bc_str block_data = { 0 };
            
            memcpy(&block_header, data + offset, sizeof(block_header));
            
            /* id == -1 means the block is marked as "deleted" */
            if (block_header.id != -1) {
                block_data.length = block_header.data_size;
                block_data.data = data + offset + sizeof(block_header);
                
                if (block_header.id > max_id) {
                    max_id = block_header.id;
                }
                
                if (mem_block_add2(block_storage, &block_data, &block_header) != block_header.id) {
                    log_ferror(__func__, "Failed to load data block\n");
                    return(false);
                }
            }
            
            offset += block_header.advance;
        }
        
        block_storage->autoid = max_id + 1;
        
        munmap(data, file->size);
    }
    
    return(true);
}

static bool
mem_load(struct bc2_disk *disk, struct bc2_memory *memory)
{
    if (!mem_load_fixed(disk, BDISK_FILEID_USERS, memory->users, sizeof(*memory->users))) return(false);
    if (!mem_load_fixed(disk, BDISK_FILEID_SESSIONS, memory->sessions, sizeof(*memory->sessions))) return(false);
    if (!mem_load_fixed(disk, BDISK_FILEID_CHANNELS, memory->channels, sizeof(*memory->channels))) return(false);
    if (!mem_load_fixed(disk, BDISK_FILEID_CHANNELUSERS, memory->channel_users, sizeof(*memory->channel_users))) return(false);
    if (!mem_load_fixed(disk, BDISK_FILEID_CHANNELSESSIONS, memory->channel_sessions, sizeof(*memory->channel_sessions))) return(false);
    if (!mem_load_strings(disk, BDISK_FILEID_STRINGS, &memory->block_storage)) return(false);
    
    return(true);
}

/*********************************/
/* USERS */
/*********************************/
static struct bc_persist_user *
mem_user_find(struct bc2_memory *memory, u32 id)
{
    for (int i = 0; i < buffer_size(memory->users); ++i) {
        struct bc_persist_user *user = memory->users + i;
        if (user->id == id) {
            return(user);
        }
    }
    
    return(0);
}

static struct bc_persist_user *
mem_user_find_login(struct bc2_memory *memory, struct bc_str login)
{
    for (int i = 0; i < buffer_size(memory->users); ++i) {
        struct bc_persist_user *user = memory->users + i;
        struct bc_str saved_login = mem_block_find(&memory->block_storage, user->login_block);
        if (streq(saved_login, login)) {
            return(user);
        }
    }
    
    return(0);
}

static struct bc_persist_user *
mem_user_add(struct bc2_memory *memory, struct bc_persist_user user, struct bc_str login, struct bc_str name)
{
    buffer_push(memory->users, user);
    
    struct bc_persist_user *saved = memory->users + buffer_size(memory->users) - 1;
    
    saved->login_block = mem_block_add(&memory->block_storage, &login);
    saved->name_block = mem_block_add(&memory->block_storage, &name);
    
    /* TODO: update views */
    
    return(saved);
}

/* NOTE: if you want to update the fixed part, just call mem_user_find and modify the struct directly */
static bool
mem_user_update(struct bc2_memory *memory, u32 id, struct bc_str *login, struct bc_str *name)
{
    struct bc_persist_user *old = mem_user_find(memory, id);
    
    if (old) {
        if (login) {
            mem_block_remove(&memory->block_storage, old->login_block);
            old->login_block = mem_block_add(&memory->block_storage, login);
        }
        
        if (name) {
            mem_block_remove(&memory->block_storage, old->name_block);
            old->name_block = mem_block_add(&memory->block_storage, name);
        }
        
        /* TODO: update views */
        
        return(true);
    }
    
    return(false);
}

static bool
mem_user_remove(struct bc2_memory *memory, u32 id)
{
    for (int i = 0; i < buffer_size(memory->users); ++i) {
        struct bc_persist_user *user = memory->users + i;
        if (user->id == id) {
            mem_block_remove(&memory->block_storage, user->login_block);
            mem_block_remove(&memory->block_storage, user->name_block);
            
            buffer_remove(memory->users, i);
            
            /* TODO: update views */
            
            return(true);
        }
    }
    
    return(false);
}

/*********************************/
/* SESSIONS */
/*********************************/
static struct bc_persist_session *
mem_session_find(struct bc2_memory *memory, u64 sid)
{
    for (int i = 0; i < buffer_size(memory->sessions); ++i) {
        struct bc_persist_session *session = memory->sessions + i;
        if (session->sid == sid) {
            return(session);
        }
    }
    
    return(0);
}

static struct bc_persist_session *
mem_session_add(struct bc2_memory *memory, struct bc_persist_session session)
{
    buffer_push(memory->sessions, session);
    
    struct bc_persist_session *saved = memory->sessions + buffer_size(memory->sessions) - 1;
    
    /* TODO: update views */
    
    return(saved);
}

static bool
mem_session_remove(struct bc2_memory *memory, u64 sid)
{
    for (int i = 0; i < buffer_size(memory->sessions); ++i) {
        struct bc_persist_session *session = memory->sessions + i;
        
        if (session->sid == sid) {
            buffer_remove(memory->sessions, i);
            /* TODO: update views */
            return(true);
        }
    }
    
    return(false);
}

/*********************************/
/* CHANNELS */
/*********************************/
static struct bc_persist_channel *
mem_channel_find(struct bc2_memory *memory, u32 id)
{
    for (int i = 0; i < buffer_size(memory->channels); ++i) {
        struct bc_persist_channel *channel = memory->channels + i;
        if (channel->id == id) {
            return(channel);
        }
    }
    
    return(0);
}

static struct bc_persist_channel *
mem_channel_add(struct bc2_memory *memory, struct bc_persist_channel channel, struct bc_str title)
{
    buffer_push(memory->channels, channel);
    
    struct bc_persist_channel *saved = memory->channels + buffer_size(memory->channels) - 1;
    
    saved->title_block = mem_block_add(&memory->block_storage, &title);
    
    return(saved);
}

/* NOTE: if you want to update the fixed part, just call mem_channel_find and modify the struct directly */
static bool
mem_channel_update(struct bc2_memory *memory, u32 id, struct bc_str *title)
{
    struct bc_persist_channel *old = mem_channel_find(memory, id);
    
    if (old) {
        if (title) {
            mem_block_remove(&memory->block_storage, old->title_block);
            old->title_block = mem_block_add(&memory->block_storage, title);
        }
        
        return(true);
    }
    
    return(false);
}

static bool
mem_channel_remove(struct bc2_memory *memory, u32 id) 
{
    for (int i = 0; i < buffer_size(memory->channels); ++i) {
        struct bc_persist_channel *channel = memory->channels + i;
        if (channel->id == id) {
            mem_block_remove(&memory->block_storage, channel->title_block);
            
            buffer_remove(memory->channels, i);
            
            return(true);
        }
    }
    
    return(false);
}

/*********************************/
/* CHANNELUSERS */
/*********************************/
static struct bc_persist_channel_user *
mem_channeluser_find(struct bc2_memory *memory, u32 user_id, u32 channel_id)
{
    for (int i = 0; i < buffer_size(memory->channel_users); ++i) {
        struct bc_persist_channel_user *channel_user = memory->channel_users + i;
        if (channel_user->user_id == user_id && channel_user->channel_id == channel_id) {
            return(channel_user);
        }
    }
    
    return(0);
}

static struct bc_persist_channel_user *
mem_channeluser_add(struct bc2_memory *memory, struct bc_persist_channel_user channel_user)
{
    buffer_push(memory->channel_users, channel_user);
    
    struct bc_persist_channel_user *saved = memory->channel_users + buffer_size(memory->channel_users) - 1;
    
    return(saved);
}

static bool
mem_channeluser_remove(struct bc2_memory *memory, u32 user_id, u32 channel_id)
{
    for (int i = 0; i < buffer_size(memory->channel_users); ++i) {
        struct bc_persist_channel_user *channel_user = memory->channel_users + i;
        
        if (channel_user->user_id == user_id && channel_user->channel_id == channel_id) {
            buffer_remove(memory->channel_users, i);
            return(true);
        }
    }
    
    return(false);
}

/*********************************/
/* CHANNELSESSIONS */
/*********************************/
static struct bc_persist_channel_session *
mem_channelsession_find(struct bc2_memory *memory, u64 sid, u32 channel_id)
{
    for (int i = 0; i < buffer_size(memory->channel_sessions); ++i) {
        struct bc_persist_channel_session *channel_session = memory->channel_sessions + i;
        if (channel_session->session_id == sid && channel_session->channel_id == channel_id) {
            return(channel_session);
        }
    }
    
    return(0);
}

static struct bc_persist_channel_session *
mem_channelsession_add(struct bc2_memory *memory, struct bc_persist_channel_session channel_session)
{
    buffer_push(memory->channel_sessions, channel_session);
    
    struct bc_persist_channel_session *saved = memory->channel_sessions + buffer_size(memory->channel_sessions) - 1;
    
    return(saved);
}

static bool
mem_channelsession_remove(struct bc2_memory *memory, u64 sid, u32 channel_id)
{
    for (int i = 0; i < buffer_size(memory->channel_sessions); ++i) {
        struct bc_persist_channel_session *channel_session = memory->channel_sessions + i;
        
        if (channel_session->session_id == sid && channel_session->channel_id == channel_id) {
            buffer_remove(memory->channel_sessions, i);
            return(true);
        }
    }
    
    return(false);
}

/*********************************/
/* MESSAGES */
/* HOT! HOT! HOT! The hottest, actually! */
/*********************************/
static struct bc_persist_record *
mem_message_add(struct bc_server *server, int channel_id, struct bc_persist_record *record)
{
    if (!record) {
        return(0);
    }
    
    struct bc_channel_info *channel_info = view_find_channel_info(&server->views, channel_id);
    
    if (!channel_info) {
        log_ferror(__func__, "Channel info not found for channel %d\n", channel_id);
        return(0);
    }
    
    char *messages = channel_info->messages;
    u32 result_offset = buffer_size(messages);
    
    struct bc_persist_record *result = (struct bc_persist_record *) (messages + result_offset);
    
    buffer_append(messages, record, record->size); /* record->size includes aux data that's in memory right after the record */
    
    ++channel_info->message_count;
    
    buffer_push(channel_info->offsets, result_offset);
    
    return(result);
}

static struct bc_persist_record *
mem_message_find(struct bc_server *server, u32 channel_id, u32 message_id)
{
    struct bc_channel_info *channel_info = view_find_channel_info(&server->views, channel_id);
    
    if (!channel_info) {
        return(0);
    }
    
    if (message_id >= channel_info->message_count) {
        return(0);
    }
    
    struct bc_persist_record *result = (struct bc_persist_record *) (channel_info->messages + channel_info->offsets[message_id]);
    
    return(result);
}