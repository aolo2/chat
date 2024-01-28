static int
generate_process(void) {
    int pid = fork();
    return pid;
}

static int
init_posix_queue(int parent) {
    struct mq_attr attr;
    
    attr.mq_flags = 0;
    attr.mq_maxmsg = MQ_MAX_MSG;
    attr.mq_msgsize = sizeof(struct mq_message);
    attr.mq_curmsgs = 0;
    int flags = parent ? O_WRONLY : O_RDONLY;
    flags |= O_CREAT;
    mqd_t mq_desc = mq_open(MQ_NAME, flags, S_IRWXU, &attr);
    
    if (mq_desc == -1) {
        perror("init posix queue");
        exit(1);
    }
    
    return mq_desc;
}

static int
send_msg(int mq_desc, struct mq_message *msg) {
    int rt = mq_send(mq_desc, (char *) msg, sizeof(*msg), 1);
    
    //printf("%ld\n", sizeof(*msg));
    
    if (rt == -1) {
        perror("send mesage in queue");
        return rt;
    }
    
    return rt;
}

static void
receive_msg(int mq_desc, struct mq_message *dest) {
    int rt = mq_receive(mq_desc, (char *) dest, sizeof(*dest), NULL);
    if (rt == -1) {
        int errsv = errno;
        // EAGAIN means the queue is empty
        if (errsv != EAGAIN) {
            perror("receive msg from queue");
            //sleep(1);
        }
    } else {
        log_info("Received a new file resize request (file id = %lx)\n", dest->file_id);
    }
}

static int
start_generate_preview(struct mq_message *msg) {
    struct bc_image img = image_load(msg->file_id);
    
    if (img.data) {
        int rt = image_resize(img);
        return(rt);
    }
    
    return(0);
}

static void
close_queue(int mq_desc) {
    int rt = mq_close(mq_desc);
    if (rt == -1) {
        perror("close posix queue");
    }
}

static void 
unlink_name(void) {
    int rt = mq_unlink(MQ_NAME);
    if (rt == -1) {
        perror("close posix queue");
    }
}
