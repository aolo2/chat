static struct bc_io *
queue_push(struct bc_queue *queue, enum bc_io_type type)
{
    for (int i = 0; i < queue->size; ++i) {
        struct bc_io *req = queue->reqs + i;
        if (req->type == IOU_UNDEFINED) {
            memset(req, 0, sizeof(*req));
            
            req->type = type;
            req->slot_id = -1;
            
            return(req);
        }
    }
    
    log_ferror(__func__, "Too many requests\n");
    // Too many requests @load
    
    return(0);
}

static struct bc_io *
queue_pushr(struct bc_queue *queue, struct bc_io *req)
{
    struct bc_io *new_req = 0;
    
    for (int i = 0; i < queue->size; ++i) {
        struct bc_io *r = queue->reqs + i;
        if (r->type == IOU_UNDEFINED) {
            new_req = r;
            break;
        }
    }
    
    if (!new_req) {
        return(0);
    } 
    
    *new_req = *req;
    
    return(new_req);
}

static struct bc_io *
queue_find_other_send_or_write_to_socket(struct bc_queue *queue, int fd, struct bc_io *req)
{
    for (int i = 0; i < queue->size; ++i) {
        struct bc_io *r = queue->reqs + i;
        
        if (r->type == IOU_WRITEV && r->writev.fd == fd) {
            if (r != req) return(r);
        } else if (r->type == IOU_SENDV && r->sendv.socket == fd) {
            if (r != req) return(r);
        } else if (r->type == IOU_SEND && r->send.socket == fd) {
            if (r != req) return(r);
        }
    }
    
    return(0);
}

static void
queue_pop(struct bc_io *req)
{
    if (req) {
        req->type = IOU_UNDEFINED;
    }
}

static struct bc_io *
queue_cancel_all_writes_to_socket(struct bc_queue *queue, int fd)
{
    for (int i = 0; i < queue->size; ++i) {
        struct bc_io *r = queue->reqs + i;
        
        if (r->type == IOU_WRITEV && r->writev.fd == fd) {
            queue_pop(r);
        } else if (r->type == IOU_SENDV && r->sendv.socket == fd) {
            queue_pop(r);
        } else if (r->type == IOU_SEND && r->send.socket == fd) {
            queue_pop(r);
        }
    }
    
    return(0);
}

static bool
queue_accept(struct bc_queue *queue, struct bc_io *req)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&queue->ring);
    
    if (sqe) {
        req->accept.client_addrlen = sizeof(req->accept.client_addr);
        io_uring_prep_accept(sqe, req->accept.socket, (struct sockaddr *) &req->accept.client_addr, &req->accept.client_addrlen, 0);
        io_uring_sqe_set_data(sqe, req);
        return(true);
    }
    
    return(false);
}

static bool
queue_recv(struct bc_queue *queue, struct bc_io *req)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&queue->ring);
    
    if (sqe) {
        //printf("[QUEUE] recv size = %d\n", req->recv.total - req->recv.start);
        io_uring_prep_recv(sqe, req->recv.socket, req->recv.buf + req->recv.start, req->recv.total - req->recv.start, 0);
        io_uring_sqe_set_data(sqe, req);
        return(true);
    }
    
    return(false);
}

static bool
queue_submit_write_or_send(struct bc_queue *queue, struct bc_io *req, int sent)
{
    switch (req->type) {
        case IOU_SEND: {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&queue->ring);
            
            if (sqe) {
                io_uring_prep_send(sqe, req->send.socket, req->send.buf + req->send.start, req->send.total - req->send.start, 0);
                io_uring_sqe_set_data(sqe, req);
                return(true);
            }
            
            return(false);
        }
        
        case IOU_SENDV: {
            if (req->sendv.nvecs > MAX_SENDV_VECTORS) {
                return(false);
            }
            
            int last_iov = 0;
            
            while (sent > 0) {
                if ((int) req->sendv.vectors[last_iov].iov_len > sent) {
                    req->sendv.vectors[last_iov].iov_len -= sent;
                    req->sendv.vectors[last_iov].iov_base = POINTER_INC(req->sendv.vectors[last_iov].iov_base, sent);
                    sent = 0;
                } else {
                    /* this vector has already been fully written */
                    sent -= req->sendv.vectors[last_iov].iov_len;
                    req->sendv.vectors[last_iov].iov_len = 0;
                    ++last_iov;
                }
            }
            
            struct io_uring_sqe *sqe = io_uring_get_sqe(&queue->ring);
            
            if (sqe) {
                io_uring_prep_writev(sqe, req->sendv.socket, req->sendv.vectors, req->sendv.nvecs, 0);
                io_uring_sqe_set_data(sqe, req);
                return(true);
            }
            
            return(false);
        }
        
        case IOU_WRITEV: {
            if (req->sendv.nvecs > MAX_WRITEV_VECTORS) {
                return(false);
            }
            
            int last_iov = 0;
            
            while (sent > 0) {
                if ((int) req->writev.vectors[last_iov].iov_len > sent) {
                    req->writev.vectors[last_iov].iov_len -= sent;
                    req->writev.vectors[last_iov].iov_base = POINTER_INC(req->writev.vectors[last_iov].iov_base, sent);
                    sent = 0;
                } else {
                    /* this vector has already been fully written */
                    sent -= req->writev.vectors[last_iov].iov_len;
                    req->writev.vectors[last_iov].iov_len = 0;
                    ++last_iov;
                }
            }
            
            struct io_uring_sqe *sqe = io_uring_get_sqe(&queue->ring);
            
            if (sqe) {
                io_uring_prep_writev(sqe, req->writev.fd, req->writev.vectors, req->writev.nvecs, req->writev.offset);
                io_uring_sqe_set_data(sqe, req);
                return(true);
            }
            
            return(false);
        }
        
        default: {
            log_critical_die("I/O of type %d submitted in %s\n. This should not happen\n", __func__);
        }
    }
    
    return(true);
}

static bool
queue_send_or_write(struct bc_queue *queue, struct bc_io *req, int sent, int flags)
{
    int socket = -1;
    
    if (req->type == IOU_SEND) {
        socket = req->send.socket;
    } else if (req->type == IOU_SENDV) {
        socket = req->sendv.socket;
    } else if (req->type == IOU_WRITEV) {
        socket = req->writev.fd;
    } else {
        log_critical_die("I/O of type %d submitted in %s\n. This should not happen\n", __func__);
    }
    
    struct bc_io *other_req = queue_find_other_send_or_write_to_socket(queue, socket, req);
    
    if (flags & IOUF_SUBMIT_QUEUED) {
        if (other_req) {
            req = other_req;
            flags |= IOUF_SUBMIT_IMMEDIATELY;
        } else {
            return(true);
        }
    }
    
    if ((flags & IOUF_SUBMIT_IMMEDIATELY) || !other_req) {
        return(queue_submit_write_or_send(queue, req, sent));
    }
    
    return(true);
}

static bool
queue_send(struct bc_queue *queue, struct bc_io *req, int flags)
{
    return(queue_send_or_write(queue, req, 0, flags));
}

static bool
queue_writev(struct bc_queue *queue, struct bc_io *req, int sent)
{
    return(queue_send_or_write(queue, req, sent, IOUF_SUBMIT_IMMEDIATELY));
}

static bool
queue_sendv(struct bc_queue *queue, struct bc_io *req, int sent, int flags)
{
    return(queue_send_or_write(queue, req, sent, flags));
}


static bool
queue_timeout(struct bc_queue *queue, struct bc_io *req, int seconds)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&queue->ring);
    
    if (sqe) {
        req->timeout.ts.tv_sec = seconds;
        req->timeout.ts.tv_nsec = 0;
        
        io_uring_prep_timeout(sqe, &req->timeout.ts, 0, 0);
        io_uring_sqe_set_data(sqe, req);
        
        return(true);
    }
    
    return(false);
}

static void
queue_submit(struct bc_queue *queue)
{
    io_uring_submit(&queue->ring);
}

static bool
queue_init(struct bc_queue *queue)
{
    queue->reqs = mmap(NULL, URING_QUEUE_SIZE * sizeof(struct bc_io), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (queue->reqs == MAP_FAILED) {
        return(false);
    }
    
    queue->size = URING_QUEUE_SIZE;
    
    int status = 0;
    if ((status = io_uring_queue_init(URING_SIZE, &queue->ring, 0)) != 0) {
        log_error("Failed liburing init: %s\n", strerror(-status));
        return(false);
    }
    
    return(true);
}

static bool
queue_splice(struct bc_queue *rqueue, struct bc_io *req)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&rqueue->ring);
    
    if (sqe) {
        //log_debug("Queue SPLICE\n");
        u64 portion = MIN(req->splice.total - req->splice.sent, SPLICE_PORTION);
        io_uring_prep_splice(sqe, req->splice.fd_in, -1, req->splice.fd_out, -1, portion, 0);
        io_uring_sqe_set_data(sqe, req);
        return(true);
    }
    
    return(false);
}

static bool
queue_write(struct bc_queue *rqueue, struct bc_io *req)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&rqueue->ring);
    
    if (sqe) {
        /*
 On files that support seeking, if the offset is set to -1, the write 
operation commences at the file offset, and the file offset is  
incremented  by the number of bytes written.
*/
        log_debug("Queue WRITE\n");
        io_uring_prep_write(sqe, req->write.fd, req->write.buf, req->write.size, -1);
        io_uring_sqe_set_data(sqe, req);
        return(true);
    }
    
    return(false);
}

static void
queue_finalize(struct bc_queue *queue)
{
    io_uring_queue_exit(&queue->ring);
}

static void
submit_and_wait1(struct bc_queue *queue, int count)
{
    queue_submit(queue);
    
    for (int i = 0; i < count; ++i) {
        struct io_uring_cqe *cqe = 0;
        io_uring_wait_cqe(&queue->ring, &cqe);
        struct bc_io *req = io_uring_cqe_get_data(cqe);
        fsync(req->writev.fd);
        io_uring_cqe_seen(&queue->ring, cqe);
        queue_pop(req);
    }
}