// TODO: maybe validate that data pointers are coming from inside the in-memory address ranges

static struct bc2_file BDISK_INITIAL_FILES[] = {
    { 
        /* First because HOT! HOT! HOT! */
        .id = BDISK_FILEID_CHANNELUSERS,
        .filetype = BDISK_FILETYPE_FIXED,
        .filename = "channelusers.bchat",
        .fd = -1,
        .chunk_size = sizeof(struct bc_persist_channel_user),
    },
    
    { 
        .id = BDISK_FILEID_STRINGS,
        .filetype = BDISK_FILETYPE_STRING,
        .filename = "strings.bchat",
        .fd = -1,
    },
    
    { 
        .id = BDISK_FILEID_USERS,
        .filetype = BDISK_FILETYPE_FIXED,
        .filename = "users.bchat",
        .fd = -1,
        .chunk_size = sizeof(struct bc_persist_user),
    },
    
    { 
        .id = BDISK_FILEID_SESSIONS,
        .filetype = BDISK_FILETYPE_FIXED,
        .filename = "sessions.bchat",
        .fd = -1,
        .chunk_size = sizeof(struct bc_persist_session),
    },
    
    { 
        .id = BDISK_FILEID_CHANNELS,
        .filetype = BDISK_FILETYPE_FIXED,
        .filename = "channels.bchat",
        .fd = -1,
        .chunk_size = sizeof(struct bc_persist_channel),
    },
    
    { 
        .id = BDISK_FILEID_CHANNELSESSIONS,
        .filetype = BDISK_FILETYPE_FIXED,
        .filename = "channelsessions.bchat",
        .fd = -1,
        .chunk_size = sizeof(struct bc_persist_channel_session),
    },
};

static bool
bdisk_open_file(struct bc2_file *file)
{
    // IMPORTANT(aolo2): if someone passes O_TRUNC here, THE DATABASE GETS WIPED!
    int flags = O_RDWR | O_CREAT;
    int mode = S_IRUSR | S_IWUSR;
    
    if (file->filetype == BDISK_FILETYPE_APPPEND) {
        flags |= O_APPEND;
    }
    
    int fd = open(file->filename, flags, mode);
    if (fd == -1) {
        log_fperror(__func__, "open");
        return(false);
    }
    
    struct stat st = { 0 };
    
    if (fstat(fd, &st) == -1) {
        log_fperror(__func__, "fstat");
        return(false);
    }
    
    file->size = st.st_size;
    file->fd = fd;
    
    return(true);
}

static bool
bdisk_ensure_header(struct bc2_file *file)
{
    if (file->size) {
        char header[64] = { 0 };
        
        if ((int) file->size < BDISK_HEADER_SIZE) {
            log_error("File size is nonzero, but less then the size of a valid file header: expected at least %d bytes, got %d\n",
                      BDISK_HEADER_SIZE, file->size);
            return(false);
        }
        
        if (read(file->fd, header, BDISK_HEADER_SIZE) != BDISK_HEADER_SIZE) {
            log_ferror(__func__, "Unexpected return value from read (header)\n");
            return(false);
        }
        
        u32 magic = readu32(header);
        if (magic != BDISK_MAGIC) {
            log_error("Wrong magic value: expected %#x, got %#x\n", BDISK_MAGIC, magic);
            return(false);
        }
        
        u8 version_major = header[sizeof(BDISK_MAGIC)];
        u8 version_minor = header[sizeof(BDISK_MAGIC) + sizeof(BDISK_VERSION_MAJOR)];
        enum bdisk_filetype filetype = header[sizeof(BDISK_MAGIC) +  + sizeof(BDISK_VERSION_MAJOR) + sizeof(BDISK_VERSION_MINOR)];
        enum bdisk_fileid   file_id = readu32(header + sizeof(BDISK_MAGIC) + sizeof(BDISK_VERSION_MAJOR) + sizeof(BDISK_VERSION_MINOR) + sizeof(u8));
        
        if (version_major != BDISK_VERSION_MAJOR) {
            log_error("MAJOR file version mismatch (RIP): expected %d, got %d\n", BDISK_VERSION_MAJOR, version_major);
            return(false);
        }
        
        if (version_minor != BDISK_VERSION_MINOR) {
            log_error("MINOR file version mismatch, please perform a migration with the provided bc-migrate tool: expected %d, got %d\n", BDISK_VERSION_MINOR, version_minor);
            return(false);
        }
        
        if (filetype != file->filetype) {
            log_error("Unexpected filetype: expected %d, got %d\n", file->filetype, filetype);
            return(false);
        }
        
        if (file_id != file->id) {
            log_error("Unexpected file id: expected %d, got %d\n", file->id, file_id);
            return(false);
        }
        
        if (filetype == BDISK_FILETYPE_FIXED) {
            int chunk_size = 0;
            if (pread(file->fd, &chunk_size, sizeof(chunk_size), BDISK_HEADER_SIZE) != sizeof(chunk_size)) {
                log_error("Unexpected return value from read (chunk_size)\n");
                return(false);
            }
            
            if (chunk_size != file->chunk_size) {
                log_error("Unexpected chunk_size: expected %d, got %d\n", file->chunk_size, chunk_size);
                return(false);
            }
        }
    } else {
        char header[64] = { 0 };
        
        /* Waiting for fixed-width enums... */
        u8 filetype = file->filetype;
        u32 file_id = file->id;
        
        memcpy(header, &BDISK_MAGIC, sizeof(BDISK_MAGIC));
        memcpy(header + sizeof(BDISK_MAGIC), &BDISK_VERSION_MAJOR, sizeof(BDISK_VERSION_MAJOR));
        memcpy(header + sizeof(BDISK_MAGIC) + sizeof(BDISK_VERSION_MAJOR), &BDISK_VERSION_MINOR, sizeof(BDISK_VERSION_MINOR));
        memcpy(header + sizeof(BDISK_MAGIC) + sizeof(BDISK_VERSION_MAJOR) + sizeof(BDISK_VERSION_MINOR), &filetype, sizeof(filetype));
        memcpy(header + sizeof(BDISK_MAGIC) + sizeof(BDISK_VERSION_MAJOR) + sizeof(BDISK_VERSION_MINOR) + sizeof(filetype), &file_id, sizeof(file_id));
        
        int header_size = BDISK_HEADER_SIZE;
        
        if (filetype == BDISK_FILETYPE_FIXED) {
            memcpy(header + header_size, &file->chunk_size, sizeof(file->chunk_size));
            header_size += sizeof(file->chunk_size);
        }
        
        if (write(file->fd, header, header_size) != header_size) {
            log_ferror(__func__, "Unexpected return value from write\n");
            return(false);
        }
        
        file->size = header_size;
    }
    
    return(true);
}

static struct bc2_file *
bdisk_ensure_message_file(struct bc2_disk *disk, int channel_id)
{
    struct bc2_file file = { 0 };
    char filename[256] = { 0 }; /* NOTE: max 10 digits for an integer, 11 for negative */
    
    if (snprintf(filename, 256, "%s/messages-%d.bchat", disk->messages_directory, channel_id) < 0) {
        log_fperror(__func__, "snprintf");
        return(0);
    }
    
    file.id = BDISK_FILEID_MESSAGES_BASE + channel_id;
    file.filetype = BDISK_FILETYPE_APPPEND;
    file.filename = filename;
    
    if (!bdisk_open_file(&file)) {
        log_fwarning(__func__, "Failed to open file %s\n", filename);
        return(0);
    }
    
    if (!bdisk_ensure_header(&file)) {
        return(0);
    }
    
    /* Wouldn't have been valid anyway (points to a string on the stack) */
    file.filename = 0;
    
    struct bc2_file *result = disk->files + buffer_size(disk->files);
    
    buffer_push(disk->files, file);
    
    return(result);
}

static bool
bdisk_init_message_files(struct bc2_disk *disk, struct bc2_memory *memory)
{
    for (int i = 0; i < buffer_size(memory->channels); ++i) {
        struct bc_persist_channel *channel = memory->channels + i;
        bdisk_ensure_message_file(disk, channel->id);
    }
    
    return(true);
}

static struct bc2_file *
bdisk_find_file(struct bc2_disk *disk, enum bdisk_fileid id)
{
    for (int i = 0; i < buffer_size(disk->files); ++i) {
        struct bc2_file *file = disk->files + i;
        if (file->id == id) {
            return(file);
        }
    }
    
    return(0);
}

static int
bdisk_get_block_index(struct bc2_file *file, int block_id)
{
    for (int i = 0; i < buffer_size(file->blocks); ++i) {
        if (file->blocks[i] == block_id) {
            return(i);
        }
    }
    
    return(-1);
}

static int
bdisk_get_free_block_index(struct bc2_file *file)
{
    for (int i = 0; i < buffer_size(file->blocks); ++i) {
        if (file->blocks[i] == BDISK_DELETED) {
            return(i);
        }
    }
    
    int block_index = buffer_size(file->blocks);
    
    buffer_push(file->blocks, BDISK_DELETED);
    
    return(block_index);
}

/*************************************************/
/* Fixed-length block storage */
/*************************************************/
static int
bdisk_fixed_header_size(void)
{
    int size = BDISK_HEADER_SIZE + sizeof(u32);
    return(size);
}

static int
bdisk_fixed_block_size(struct bc2_file *file)
{
    int size = sizeof(int) + file->chunk_size;
    return(size);
}

static int
bdisk_fixed_read(struct bc2_disk *disk, enum bdisk_fileid file_id, u32 block_id, void *dest)
{
    if (!dest) {
        return(0);
    }
    
    /* It is the responsibility of the user to make sure dest points to a valid memory region of size enough for the fixed block */
    struct bc2_file *file = bdisk_find_file(disk, file_id);
    
    if (file) {
        int block_index = bdisk_get_block_index(file, block_id);
        int offset = bdisk_fixed_header_size() + block_index * bdisk_fixed_block_size(file) + sizeof(block_id);
        
        if (pread(file->fd, dest, file->chunk_size, offset) == file->chunk_size) {
            return(file->chunk_size);
        }
    }
    
    return(0);
}

static int
bdisk_fixed_write(struct bc_queue *queue, struct bc2_file *file, int block_id, void *buf, int offset)
{
    struct bc_io *req = queue_push(queue, IOU_WRITEV);
    
    if (!req) {
        return(0);
    }
    
    req->writev.fd = file->fd;
    req->writev.offset = offset;
    req->writev.start = 0;
    req->writev.total = 0;
    
    memcpy(req->buf, &block_id, sizeof(block_id));
    
    /* Write a 4-byte block_id */
    req->writev.vectors[0].iov_base = req->buf;
    req->writev.vectors[0].iov_len = sizeof(block_id);
    
    /* Then the data */
    req->writev.vectors[1].iov_base = buf;
    req->writev.vectors[1].iov_len = file->chunk_size;
    
    req->writev.total += req->writev.vectors[0].iov_len + req->writev.vectors[1].iov_len;
    req->writev.nvecs = 2;
    
    if (queue_writev(queue, req, 0)) {
        return(1);
    }
    
    return(0);
}

static int
bdisk_fixed_add(struct bc2_disk *disk, struct bc2_file *file, int block_id, void *buf)
{
    if (!file || file->filetype != BDISK_FILETYPE_FIXED) {
        return(0);
    }
    
    int block_index = bdisk_get_free_block_index(file);
    if (block_index == buffer_size(file->blocks) - 1) { // WRONG! If we used last block this would falsely increaze file sizE
        file->size += sizeof(block_id) + file->chunk_size; /* TODO: Is incrementing file->size correct even before we issued the writev? What if file operations are performed before we did io_uring_submit? */
    }
    
    file->blocks[block_index] = block_id;
    
    u32 offset = bdisk_fixed_header_size() + block_index * bdisk_fixed_block_size(file);
    int nops = bdisk_fixed_write(disk->queue, file, block_id, buf, offset);
    
    return(nops);
}

static int
bdisk_fixed_remove(struct bc2_disk *disk, struct bc2_file *file, int block_id)
{
    /* "Soft" deletion. Nothing gets moved on disk. Just mark as deleted both in metadata and on disk */
    for (int i = 0; i < buffer_size(file->blocks); ++i) {
        if (file->blocks[i] == block_id) {
            /* Just write a BDISK_DELETED id instead of the old id */
            
            file->blocks[i] = BDISK_DELETED; /* Write to in-memory metadata */
            
            struct bc_io *req = queue_push(disk->queue, IOU_WRITEV);
            
            if (!req) {
                return(0);
            }
            
            req->writev.fd = file->fd;
            req->writev.start = 0;
            
            memcpy(req->buf, &BDISK_DELETED, sizeof(BDISK_DELETED));
            
            req->writev.vectors[0].iov_base = req->buf;
            req->writev.vectors[0].iov_len = sizeof(block_id);
            req->writev.total = req->writev.vectors[0].iov_len;
            req->writev.nvecs = 1;
            
            u32 offset = bdisk_fixed_header_size() + i * bdisk_fixed_block_size(file);
            
            req->writev.offset = offset;
            
            /* Write to disk */
            if (queue_writev(disk->queue, req, 0)) {
                return(1);                                                                                                                                                                                                                                                                                                                                                                                                                                                          
            }
            
            return(0);
        }
    }
    
    return(0);
}

static int
bdisk_fixed_update(struct bc2_disk *disk, struct bc2_file *file, int block_id, void *data)
{
    for (int i = 0; i < buffer_size(file->blocks); ++i) {
        if (file->blocks[i] == block_id) {
            u32 offset = bdisk_fixed_header_size() + i * bdisk_fixed_block_size(file);
            return(bdisk_fixed_write(disk->queue, file, block_id, data, offset)); /* This overwrites the id with the same value */
        }
    }
    
    return(0);
}

static bool
bdisk_fixed_init(struct bc2_file *file)
{
    if (!file->size) {
        return(true);
    }
    
    // TODO: we can probably unify this with mem_load to not map the file twice
    char *data = mmap(0, file->size, PROT_READ, MAP_PRIVATE, file->fd, 0);
    
    if (data == MAP_FAILED) {
        log_fperror(__func__, "mmap");
        return(false);
    }
    
    u32 offset = bdisk_fixed_header_size();
    
    while (offset < file->size) { 
        u32 block_id = readu32(data + offset);
        buffer_push(file->blocks, block_id);
        offset += file->chunk_size + sizeof(file->chunk_size);
    }
    
    munmap(data, file->size);
    
    return(true);
}

/*************************************************/
/* Variable-length block (a.k.a. string) storage */
/*************************************************/
static struct bc_block_header *
bdisk_string_get_block_header(struct bc2_file *file, int block_id)
{
    for (int i = 0; i < buffer_size(file->strings); ++i) {
        struct bc_block_header *header = file->strings + i;
        if (header->id == block_id) {
            return(header);
        }
    }
    
    return(0);
}

static int
bdisk_string_read(struct bc2_disk *disk, enum bdisk_fileid file_id, u32 block_id, void *dest)
{
    if (!dest) {
        return(0);
    }
    
    /* It is the responsibility of the user to make sure dest points to a valid memory region of size enough for the fixed block */
    struct bc2_file *file = bdisk_find_file(disk, file_id);
    
    if (file) {
        struct bc_block_header *header = bdisk_string_get_block_header(file, block_id);
        if (header) {
            int offset = header->offset + sizeof(*header);
            if (pread(file->fd, dest, header->data_size, offset) == header->data_size) {
                return(header->data_size);
            }
        }
    }
    
    return(0);
}

static int
bdisk_string_write(struct bc_queue *queue, struct bc2_file *file, struct bc_block_header *header, struct bc_str *data)
{
    struct bc_io *req = queue_push(queue, IOU_WRITEV);
    
    if (!req) {
        return(0);
    }
    
    req->writev.fd = file->fd;
    req->writev.start = 0;
    req->writev.total = 0;
    
    memcpy(req->buf, header, sizeof(*header));
    
    /* Write the block header */
    req->writev.vectors[0].iov_base = req->buf;
    req->writev.vectors[0].iov_len = sizeof(*header);
    
    /* Then the data */
    req->writev.vectors[1].iov_base = data->data;
    req->writev.vectors[1].iov_len = data->length;
    
    req->writev.total += req->writev.vectors[0].iov_len + req->writev.vectors[1].iov_len;
    req->writev.nvecs = 2;
    req->writev.offset = header->offset;
    
    if (queue_writev(queue, req, 0)) {
        return(1);
    }
    
    return(0);
}

static int
bdisk_string_add(struct bc2_disk *disk, struct bc2_file *file, int block_id, struct bc_str *data)
{
    /* TODO: reuse big enough deleted blocks instead of always creating a new one */
    
    struct bc_block_header header = { 0 };
    
    header.id        = block_id;
    header.data_size = data->length;
    header.advance   = sizeof(header) + data->length;
    header.offset    = file->size;
    
    file->size += sizeof(header) + data->length;
    
    buffer_push(file->strings, header);
    
    return(bdisk_string_write(disk->queue, file, &header, data));
}

static int
bdisk_string_remove(struct bc2_disk *disk, struct bc2_file *file, int block_id)
{
    for (int i = 0; i < buffer_size(file->strings); ++i) {
        struct bc_block_header *header = file->strings + i;
        if (header->id == block_id) {
            /* Just write a BDISK_DELETED id instead of the old id */
            
            header->id = BDISK_DELETED; /* Write to in-memory metadata */
            
            struct bc_io *req = queue_push(disk->queue, IOU_WRITEV);
            
            if (!req) {
                return(0);
            }
            
            req->writev.fd = file->fd;
            req->writev.start = 0;
            
            memcpy(req->buf, header, sizeof(*header));
            
            req->writev.vectors[0].iov_base = req->buf;
            req->writev.vectors[0].iov_len = sizeof(*header);
            req->writev.total = req->writev.vectors[0].iov_len;
            req->writev.nvecs = 1;
            req->writev.offset = header->offset;
            
            /* Write to disk */
            if (queue_writev(disk->queue, req, 0)) {
                return(1);                                                                                                                                                                                                                                                                                                                                                                                                                                                          
            }
            
            return(0);
        }
    }
    
    return(0);
}

static bool
bdisk_string_init(struct bc2_file *file)
{
    if (!file->size) {
        return(true);
    }
    
    // TODO: we can probably unify this with mem_load to not map the file twice
    char *data = mmap(0, file->size, PROT_READ, MAP_PRIVATE, file->fd, 0);
    
    if (data == MAP_FAILED) {
        log_fperror(__func__, "mmap");
        return(false);
    }
    
    u32 offset = BDISK_HEADER_SIZE;
    
    while (offset < file->size) { 
        struct bc_block_header header = { 0 };
        memcpy(&header, data + offset, sizeof(header));
        buffer_push(file->strings, header);
        offset += header.advance;
    }
    
    munmap(data, file->size);
    
    return(true);
}

/*************************************************/
/* Append-only storage */
/*************************************************/
static int
bdisk_append_record(struct bc2_disk *disk, struct bc2_file *file,
                    struct bc_persist_record *record)
{
    struct bc_io *req = queue_push(disk->queue, IOU_WRITEV);
    
    if (!req) {
        return(0);
    }
    
    req->writev.fd = file->fd;
    req->writev.start = 0;
    req->writev.total = record_size(record);
    
    req->writev.vectors[0].iov_base = record;
    req->writev.vectors[0].iov_len = record_size(record);
    
    req->writev.nvecs = 1;
    req->writev.offset = 0;
    
    if (!queue_writev(disk->queue, req, 0)) {
        return(0);
    }
    
    return(1);
}

/*********************************/
/* USERS */
/*********************************/
static int
bdisk_user_add(struct bc2_disk *disk, struct bc_persist_user *user, struct bc_str *login, struct bc_str *name)
{
    int nops = 0;
    
    struct bc2_file *file_users = bdisk_find_file(disk, BDISK_FILEID_USERS);
    struct bc2_file *file_strings = bdisk_find_file(disk, BDISK_FILEID_STRINGS);
    
    if (!file_users || !file_strings) {
        return(0);
    }
    
    nops += bdisk_fixed_add(disk, file_users, user->id, user);
    nops += bdisk_string_add(disk, file_strings, user->login_block, login);
    nops += bdisk_string_add(disk, file_strings, user->name_block, name);
    
    return(nops);
}

static int
bdisk_user_update(struct bc2_disk *disk, struct bc_persist_user *user, struct bc_str *login, struct bc_str *name)
{
    int nops = 0;
    
    struct bc2_file *file_users = bdisk_find_file(disk, BDISK_FILEID_USERS);
    struct bc2_file *file_strings = bdisk_find_file(disk, BDISK_FILEID_STRINGS);
    
    if (!file_users || !file_strings) {
        return(0);
    }
    
    nops += bdisk_fixed_update(disk, file_users, user->id, user);
    
    if (login) {
        nops += bdisk_string_remove(disk, file_strings, user->login_block);
        nops += bdisk_string_add(disk, file_strings, user->login_block, login);
    }
    
    if (name) {
        nops += bdisk_string_remove(disk, file_strings, user->name_block);
        nops += bdisk_string_add(disk, file_strings, user->name_block, name);
    }
    
    return(nops);
}

static int
bdisk_user_remove(struct bc2_disk *disk, struct bc_persist_user *user)
{
    int nops = 0;
    
    struct bc2_file *file_users = bdisk_find_file(disk, BDISK_FILEID_USERS);
    struct bc2_file *file_strings = bdisk_find_file(disk, BDISK_FILEID_STRINGS);
    
    if (!file_users || !file_strings) {
        return(0);
    }
    
    nops += bdisk_fixed_remove(disk, file_users, user->id);
    nops += bdisk_string_remove(disk, file_strings, user->login_block);
    nops += bdisk_string_remove(disk, file_strings, user->name_block);
    
    return(nops);
}

/*********************************/
/* SESSIONS */
/*********************************/
static int
bdisk_session_add(struct bc2_disk *disk, struct bc_persist_session *session)
{
    struct bc2_file *file_sessions = bdisk_find_file(disk, BDISK_FILEID_SESSIONS);
    
    if (!file_sessions) {
        return(0);
    }
    
    int nops = bdisk_fixed_add(disk, file_sessions, session->id, session);
    
    return(nops);
}

static int
bdisk_session_update(struct bc2_disk *disk, struct bc_persist_session *session)
{
    struct bc2_file *file_sessions = bdisk_find_file(disk, BDISK_FILEID_SESSIONS);
    
    if (!file_sessions) {
        return(0);
    }
    
    int nops = bdisk_fixed_update(disk, file_sessions, session->id, session);
    
    return(nops);
}

static int
bdisk_session_remove(struct bc2_disk *disk, u64 session_id)
{
    struct bc2_file *file_sessions = bdisk_find_file(disk, BDISK_FILEID_SESSIONS);
    
    if (!file_sessions) {
        return(0);
    }
    
    int nops = bdisk_fixed_remove(disk, file_sessions, session_id);
    
    return(nops);
}

/*********************************/
/* CHANNELS */
/*********************************/
static int
bdisk_channel_add(struct bc2_disk *disk, struct bc_persist_channel *channel, struct bc_str *channel_title)
{
    int nops = 0;
    
    struct bc2_file *file_channels = bdisk_find_file(disk, BDISK_FILEID_CHANNELS);
    struct bc2_file *file_strings = bdisk_find_file(disk, BDISK_FILEID_STRINGS);
    
    if (!file_channels || !file_strings) {
        return(0);
    }
    
    nops += bdisk_fixed_add(disk, file_channels, channel->id, channel);
    nops += bdisk_string_add(disk, file_strings, channel->title_block, channel_title);
    
    return(nops);
}

static int
bdisk_channel_update(struct bc2_disk *disk, struct bc_persist_channel *channel, struct bc_str *title)
{
    int nops = 0;
    
    struct bc2_file *file_channels = bdisk_find_file(disk, BDISK_FILEID_CHANNELS);
    struct bc2_file *file_strings = bdisk_find_file(disk, BDISK_FILEID_STRINGS);
    
    if (!file_channels || !file_strings) {
        return(0);
    }
    
    nops += bdisk_fixed_update(disk, file_channels, channel->id, channel);
    
    if (title) {
        nops += bdisk_string_remove(disk, file_strings, channel->title_block);
        nops += bdisk_string_add(disk, file_strings, channel->title_block, title);
    }
    
    return(nops);
}

static int
bdisk_channel_remove(struct bc2_disk *disk, struct bc_persist_channel *channel)
{
    int nops = 0;
    
    struct bc2_file *file_channels = bdisk_find_file(disk, BDISK_FILEID_CHANNELS);
    struct bc2_file *file_strings = bdisk_find_file(disk, BDISK_FILEID_STRINGS);
    
    if (!file_channels || !file_strings) {
        return(0);
    }
    
    nops += bdisk_fixed_remove(disk, file_channels, channel->id);
    nops += bdisk_string_remove(disk, file_strings, channel->title_block);
    
    return(nops);
}

/*********************************/
/* CHANNELUSERS */
/*********************************/
static int
bdisk_channeluser_add(struct bc2_disk *disk, struct bc_persist_channel_user *channel_user)
{
    struct bc2_file *file_channel_users = bdisk_find_file(disk, BDISK_FILEID_CHANNELUSERS);
    
    if (!file_channel_users) {
        return(0);
    }
    
    int nops = bdisk_fixed_add(disk, file_channel_users, channel_user->id, channel_user);
    
    return(nops);
}

static int
bdisk_channeluser_update(struct bc2_disk *disk, struct bc_persist_channel_user *channel_user)
{
    /* HOT! HOT! HOT! */
    struct bc2_file *file_channel_users = bdisk_find_file(disk, BDISK_FILEID_CHANNELUSERS);
    
    if (!file_channel_users) {
        return(0);
    }
    
    int nops = bdisk_fixed_update(disk, file_channel_users, channel_user->id, channel_user);
    
    return(nops);
}

static int
bdisk_channeluser_remove(struct bc2_disk *disk, u32 channel_user_id)
{
    struct bc2_file *file_channel_users = bdisk_find_file(disk, BDISK_FILEID_CHANNELUSERS);
    
    if (!file_channel_users) {
        return(0);
    }
    
    int nops = bdisk_fixed_remove(disk, file_channel_users, channel_user_id);
    
    return(nops);
}

/*********************************/
/* CHANNELSESSIONS */
/*********************************/
static int
bdisk_channelsession_add(struct bc2_disk *disk, struct bc_persist_channel_session *channel_session)
{
    struct bc2_file *file_channel_sessions = bdisk_find_file(disk, BDISK_FILEID_CHANNELSESSIONS);
    
    if (!file_channel_sessions) {
        return(0);
    }
    
    int nops = bdisk_fixed_add(disk, file_channel_sessions, channel_session->id, channel_session);
    
    return(nops);
}

static int
bdisk_channelsession_update(struct bc2_disk *disk, struct bc_persist_channel_session *channel_session)
{
    /* HOT! HOT! HOT! */
    struct bc2_file *file_channel_sessions = bdisk_find_file(disk, BDISK_FILEID_CHANNELSESSIONS);
    
    if (!file_channel_sessions) {
        return(0);
    }
    
    int nops = bdisk_fixed_update(disk, file_channel_sessions, channel_session->id, channel_session);
    
    return(nops);
}

static int
bdisk_channelsession_remove(struct bc2_disk *disk, u32 channel_session_id)
{
    struct bc2_file *file_channel_sessions = bdisk_find_file(disk, BDISK_FILEID_CHANNELSESSIONS);
    
    if (!file_channel_sessions) {
        return(0);
    }
    
    int nops = bdisk_fixed_remove(disk, file_channel_sessions, channel_session_id);
    
    return(nops);
}

/*********************************/
/* MESSAGES (HOT! HOT! HOT!) */
/*********************************/
static int
bdisk_message_add(struct bc2_disk *disk, int channel_id, struct bc_persist_record *record)
{
    /* TODO: @speed maybe buffer up these in case of high load? */
    
    struct bc2_file *message_file = bdisk_find_file(disk, BDISK_FILEID_MESSAGES_BASE + channel_id);
    
    if (!message_file) {
        log_info("Creating message file for channel %d\n", channel_id);
        message_file = bdisk_ensure_message_file(disk, channel_id);
    }
    
    if (!message_file) {
        log_ferror(__func__, "Message file for channel %d is NULL\n", channel_id);
        return(0);
    }
    
    int nops = bdisk_append_record(disk, message_file, record);
    
    return(nops);
}

static bool
bdisk_init(struct bc2_disk *dest)
{
    dest->files = buffer_init(MAX_POSSIBLE_FILES, sizeof(struct bc2_file));
    
    if (!dest->files) {
        return(false);
    }
    
    if (!directory_exists(dest->messages_directory)) {
        if (mkdir(dest->messages_directory, 0700) != 0) {
            log_fperror(__func__, "mkdir messages");
            return(false);
        }
    }
    
    for (int i = 0; i < STATIC_ARRAY_SIZE(BDISK_INITIAL_FILES); ++i) {
        struct bc2_file file = BDISK_INITIAL_FILES[i];
        
        if (!bdisk_open_file(&file)) {
            return(false);
        }
        
        if (!bdisk_ensure_header(&file)) {
            return(false);
        }
        
        if (file.filetype == BDISK_FILETYPE_FIXED) {
            file.blocks = buffer_init(MAX_POSSIBLE_BLOCKS, sizeof(int));
            if (!bdisk_fixed_init(&file)) {
                return(false);
            }
        } else if (file.filetype == BDISK_FILETYPE_STRING) {
            file.strings = buffer_init(MAX_POSSIBLE_STRINGS, sizeof(struct bc_block_header));
            if (!bdisk_string_init(&file)) {
                return(false);
            }
        }
        
        buffer_push(dest->files, file);
    }
    
    return(true);
}

static void
bdisk_finalize(struct bc2_disk *disk)
{
    /* No error handling here because we can't do nothing anyway */
    for (int i = 0; i < buffer_size(disk->files); ++i) {
        struct bc2_file *file = disk->files + i;
        fsync(file->fd);
        close(file->fd);
    }
    
    /* fsync the directory (why? need to read up on this) */
    int fd = open(".", O_RDONLY);
    fsync(fd);
    close(fd);
}