static struct bc_v2i *
view_find_sorted_1(struct bc_v2i *data, int value)
{
    // @speed binsearch
    for (int i = 0; i < buffer_size(data); ++i) {
        struct bc_v2i *item = data + i;
        if (item->first == value) {
            return(item);
        }
    }
    
    return(0);
}

static struct bc_v2i *
view_find_sorted_2(struct bc_v2i *data, int value)
{
    // @speed binsearch
    for (int i = 0; i < buffer_size(data); ++i) {
        struct bc_v2i *item = data + i;
        if (item->second == value) {
            return(item);
        }
    }
    
    return(0);
}

static struct bc_v2i *
view_insert_sorted_1(struct bc_v2i *data, struct bc_v2i insert)
{
    
    // @speed binsearch
    for (int i = 0; i < buffer_size(data); ++i) {
        struct bc_v2i *item = data + i;
        if (item->first > insert.first) {
            buffer_insert(data, i, insert);
            return(data + i);
        }
    }
    
    struct bc_v2i *result = data + buffer_size(data);
    
    buffer_push(data, insert);
    
    return(result);
}

static struct bc_v2i *
view_insert_sorted_2(struct bc_v2i *data, struct bc_v2i insert)
{
    // @speed binsearch
    for (int i = 0; i < buffer_size(data); ++i) {
        struct bc_v2i *item = data + i;
        if (item->second > insert.second) {
            buffer_insert(data, i, insert);
            return(data + i);
        }
    }
    
    struct bc_v2i *result = data + buffer_size(data);
    
    buffer_push(data, insert);
    
    return(result);
}

static int
view_remove_all_pairs_2(struct bc_v2i *data, int second)
{
    int removed = 0;
    
    for (int i = 0; i < buffer_size(data); ++i) {
        struct bc_v2i *item = data + i;
        if (item->second == second) {
            ++removed;
            buffer_remove(data, i);
            --i;
        }
    }
    
    return(removed);
}

static bool
view_remove_pair(struct bc_v2i *data, struct bc_v2i value)
{
    // @speed binsearch
    for (int i = 0; i < buffer_size(data); ++i) {
        struct bc_v2i *item = data + i;
        if (item->first == value.first && item->second == value.second) {
            buffer_remove(data, i);
            return(true);
        }
    }
    
    return(false);
}

static struct bc_channel_info *
view_channel_add(struct bc2_transient *views, struct bc_persist_channel *channel)
{
    struct bc_channel_info channel_info = { 0 };
    
    channel_info.channel_id = channel->id;
    channel_info.flags = channel->flags;
    channel_info.messages = buffer_init(1, MAX_POSSIBLE_MESSAGE_MEMORY);
    channel_info.offsets = buffer_init(MAX_POSSIBLE_MESSAGES_PER_CHANNEL, sizeof(u32));
    
    struct bc_channel_info *result = views->channel_info + buffer_size(views->channel_info);
    
    buffer_push(views->channel_info, channel_info);
    
    return(result);
}

static struct bc_user_info *
view_user_add(struct bc2_transient *views,
              struct bc_persist_user *user)
{
    struct bc_user_info user_info = { 0 };
    
    user_info.user_id = user->id;
    user_info.last_online = 0;
    user_info.status = STATUS_OFFLINE;
    
    struct bc_user_info *result = views->user_info + buffer_size(views->user_info);
    
    buffer_push(views->user_info, user_info);
    
    return(result);
}

static void
view_channeluser_add(struct bc2_transient *views,
                     struct bc_persist_channel_user *channel_user)
{
    struct bc_v2i pair = { 0 };
    
    pair.channel_id = channel_user->channel_id;
    pair.user_id    = channel_user->user_id;
    
    view_insert_sorted_1(views->users_per_channel, pair);
    view_insert_sorted_2(views->channels_per_user, pair);
}

static bool
view_channeluser_remove(struct bc2_transient *views,
                        struct bc_persist_channel_user *channel_user)
{
    struct bc_v2i pair = { 0 };
    
    pair.channel_id = channel_user->channel_id;
    pair.user_id    = channel_user->user_id;
    
    bool ok1 = view_remove_pair(views->users_per_channel, pair);
    bool ok2 = view_remove_pair(views->channels_per_user, pair);
    
    return(ok1 && ok2);
}

static struct bc_user_info *
view_find_user_info(struct bc2_transient *views, u32 user_id)
{
    for (int i = 0; i < buffer_size(views->user_info); ++i) {
        struct bc_user_info *user_info = views->user_info + i;
        if (user_info->user_id == user_id) {
            return(user_info);
        }
    }
    
    return(0);
}

static struct bc_channel_info *
view_find_channel_info(struct bc2_transient *views, u32 channel_id)
{
    for (int i = 0; i < buffer_size(views->channel_info); ++i) {
        struct bc_channel_info *channel_info = views->channel_info + i;
        if (channel_info->channel_id == channel_id) {
            return(channel_info);
        }
    }
    
    return(0);
}

static bool
view_init(struct bc_server *server)
{
    struct bc2_memory *memory = &server->memory;
    struct bc2_transient *views = &server->views;
    struct bc2_disk *disk = &server->disk;
    
    views->channel_info = buffer_init(MAX_POSSIBLE_CHANNELS, sizeof(struct bc_channel_info));
    views->user_info = buffer_init(MAX_POSSIBLE_USERS, sizeof(struct bc_user_info));
    views->connections_per_channel = buffer_init(MAX_POSSIBLE_CONNECTIONS * MAX_POSSIBLE_CHANNELS, sizeof(struct bc_v2i));
    views->users_per_channel = buffer_init(MAX_POSSIBLE_USERS * MAX_POSSIBLE_CHANNELS, sizeof(struct bc_v2i));
    views->channels_per_user = buffer_init(MAX_POSSIBLE_USERS * MAX_POSSIBLE_CHANNELS, sizeof(struct bc_v2i));
    
    if (!views->channel_info || !views->user_info || !views->connections_per_channel || !views->users_per_channel || !views->channels_per_user) {
        return(false);
    }
    
    int channel_count = buffer_size(memory->channels);
    int user_count = buffer_size(memory->users);
    int cu_count = buffer_size(memory->channel_users);
    
    for (int i = 0; i < channel_count; ++i) {
        struct bc_persist_channel *channel = memory->channels + i;
        struct bc_channel_info *channel_info = view_channel_add(views, channel);
        
        struct bc2_file *message_file = bdisk_find_file(disk, BDISK_FILEID_MESSAGES_BASE + channel->id);
        
        if (message_file) {
            char *data = mmap(0, message_file->size, PROT_READ, MAP_PRIVATE, message_file->fd, 0);
            
            if (data == MAP_FAILED) {
                log_fperror(__func__, "mmap");
                return(false);
            }
            
            /* Copy all records at once, no need to do it one by one... */
            buffer_append(channel_info->messages, data + BDISK_HEADER_SIZE, message_file->size - BDISK_HEADER_SIZE);
            
            int offset = 0;
            channel_info->message_count = 0;
            while (offset < buffer_size(channel_info->messages)) {
                /* ...but to count the records and save offsets we still need to iterate */
                struct bc_persist_record *record = (struct bc_persist_record *) (channel_info->messages + offset); 
                buffer_push(channel_info->offsets, offset);
                ++channel_info->message_count;
                offset += record->size;
            }
            
            munmap(data, message_file->size);
        }
    }
    
    for (int i = 0; i < user_count; ++i) {
        struct bc_persist_user *user = memory->users + i;
        view_user_add(views, user);
    }
    
    for (int i = 0; i < cu_count; ++i) {
        struct bc_persist_channel_user *cu = memory->channel_users + i;
        view_channeluser_add(views, cu);
    }
    
    return(true);
}