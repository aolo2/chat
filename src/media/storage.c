static bool
storage_init(struct bc_server *server)
{
    server->storage.files = buffer_makevm(MAX_POSSIBLE_FILES * sizeof(struct bc_file));
    
    if (!server->storage.files.vm.size) {
        return(false);
    }
    
    char *filename = "files.bmedia";
    int fd = open(filename, O_RDWR | O_CREAT | O_SYNC | O_APPEND, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        log_fperror(__func__, "open");
        return(false);
    }
    
    server->storage.fd = fd;
    
    u64 file_size = get_file_size(fd);
    
    if (file_size > 0) {
        char *data = mmap(0, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            log_fperror(__func__, "mmap");
            return(false);
        }
        
        if (!buffer_appendvm(&server->storage.files, data, file_size)) return(false);
    }
    
    return(true);
}

static bool
storage_add_file(struct bc_server *server, struct bc_file *file)
{
    if (!file) return(false);
    
    struct bc_file *saved = (struct bc_file *) buffer_head(&server->storage.files);
    
    if (!saved || !buffer_appendvm(&server->storage.files, file, sizeof(*file))) {
        return(false);
    }
    
    struct bc_io *req = queue_push(&server->queue, IOU_WRITE);
    
    req->write.fd = server->storage.fd;
    req->write.buf = (char *) saved;
    req->write.size = sizeof(*saved);
    
    queue_write(&server->queue, req);
    
    return(true);
}

static struct bc_file *
storage_find_file(struct bc_server *server, u64 file_id)
{
    struct bc_file *files = (struct bc_file *) (server->storage.files.data);
    int file_count = server->storage.files.use / sizeof(struct bc_file);
    
    for (int i = 0; i < file_count; ++i) {
        struct bc_file *file = files + i;
        if (file->id == file_id) {
            return(file);
        }
    }
    
    return(0);
}