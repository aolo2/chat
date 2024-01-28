#include "../shared/shared.h"

#include "media.h"

#include "../shared/aux.c"
#include "../shared/log.c"
#include "../shared/mapping.c"
#include "../shared/queue.c"
#include "../shared/buffer.c"

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "external/stb_image_resize.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

volatile sig_atomic_t on = 1; // used in image.c

#include "parse.c"
#include "connection.c"
#include "image.c"
#include "posix_queue.c"
#include "handle.c"

static void
sigterm_handler(int signum)
{
    (void) signum;
    on = 0;
}

static struct bc_io *
connection_recv(struct bc_server *server, struct bc_connection *connection)
{
    struct bc_io *next_req = queue_push(&server->queue, IOU_RECV);
    
    next_req->recv.buf = connection->recv_buf;
    next_req->recv.socket = connection->socket;
    next_req->recv.start = connection->recv_start;
    next_req->recv.total = READ_BUFFER_SIZE;
    
    queue_recv(&server->queue, next_req);
    
    return(next_req);
}

static void
io_completion_accept(struct bc_server *server, int res)
{
    int sock = res;
    
    log_debug("ACCEPT completed, new connection = %d\n", sock);
    
    struct bc_connection *connection = connection_create(server, sock);
    
    if (connection) {
        connection_recv(server, connection);
    }
    
    // Next accept
    struct bc_io *req_accept = queue_push(&server->queue, IOU_ACCEPT);
    req_accept->accept.socket = server->fd;
    queue_accept(&server->queue, req_accept);
}

static void
io_completion_recv(struct bc_server *server, struct bc_io *req, int res)
{
    /*
Once and for all: browsers DO send multiple HTTP requests over one TCP connection.
This is default in HTTP/1.1, which we target.
This means we only close the connection if recv() returned 0.
*/
    
    int rsize = res;
    
    log_debug("RECV completed for connection %d, size = %d\n", req->recv.socket, rsize);
    
    struct bc_connection *connection = connection_get(server, req->recv.socket);
    if (!connection) {
        log_warning("RECV on a dead connection %d\n", req->recv.socket);
        return;
    }
    
    //log_debug("Connection state = %d\n", connection->state);
    
    if (rsize == 0) {
        log_info("Normal connection shutdown. Closing and dropping\n");
        connection_drop(server, connection);
        return;
    }
    
    if (rsize < 0) {
        log_warning("RECV returned %d. Dropping the connection immediately\n", rsize);
        connection_drop(server, connection);
        return;
    }
    
    if (connection->state == CONNECTION_READ_REMAINDER) {
        handle_file_written(server, req->file_id);
        handle_200_upload_do(&server->queue, connection);
    } else {
        connection->recv_start += rsize;
        
        struct bc_str message = { connection->recv_start, { connection->recv_buf }};
        // printf("%.*s\n", message.length, message.data);
        
        struct bc_http_request request = parse_http_request(message);
        if (request.valid) {
            handle_complete_http_request(server, connection, &request);
            connection->recv_start = 0;
            // NOTE: if we expected more than one http request per connection, we would have
            // to do a memmove() here.
        } else {
            log_debug("Incomplete request on connection %d\n", connection->socket);
        }
        
        // TODO: decide when the request is not going to be valid and drop the connection
    }
    
    if (connection->state != CONNECTION_WRITING_LARGE) {
        connection_recv(server, connection);
    }
}

static void
io_completion_send(struct bc_server *server, struct bc_io *req, int res)
{
    int nsent = res;
    
    log_debug("SEND completed, for connection %d, size = %d\n", req->send.socket, nsent);
    
    if (req->send.start + nsent < req->send.total) {
        /* Partial send */
        struct bc_io *next_req = queue_pushr(&server->queue, req);
        next_req->send.start += nsent;
        //queue_send(&server->queue, next_req, IOUF_SUBMIT_IMMEDIATELY);
        queue_send(&server->queue, next_req, 0);
    } else {
        /* Send completed */
        struct bc_connection *connection = connection_get(server, req->send.socket);
        
        if (!connection) {
            log_warning("SEND completed on a dead connection %d\n", req->send.socket);
            return;
        }
        
        //queue_send(&server->queue, req, IOUF_SUBMIT_QUEUED);
    }
}

static void
io_completion_splice(struct bc_server *server, struct bc_io *req, int res)
{
    int nwritten = res;
    
    if (nwritten < 0) {
        log_ferror(__func__, "error: %s\n", strerror(-res));
        return;
    }
    
    if (nwritten == 0) {
        log_debug("SPLICE %s reached EOF\n", req->type == IOU_SPLICE_WRITE ? "WRITE" : "READ");
        return;
    }
    
    log_debug("SPLICE %s completed with %d bytes\n", req->type == IOU_SPLICE_WRITE ? "WRITE" : "READ", nwritten);
    
    if (req->splice.sent + nwritten < req->splice.total) {
        /* Splice portion was not enough ("partial splice") 
This works for both SPLICE_READs and SPLICE_WRITEs */
        struct bc_io *next_req = queue_pushr(&server->queue, req);
        next_req->splice.sent += nwritten;
        queue_splice(&server->queue, next_req);
    } else {
        if (req->type == IOU_SPLICE_WRITE) {
            log_debug("Last SPLICE WRITE completed for connection %d\n", req->id);
            
            /* Last splice write completed */
            struct bc_connection *connection = 0;
            int client_socket = req->id;
            
            connection = connection_get(server, client_socket);
            
            if (!connection) {
                log_error("Splice write completed for dead connection %d\n", client_socket);
                return;
            }
            
            /* We are ready the receive the remainding data on this connection */
            connection->state = CONNECTION_READ_REMAINDER;
            
            struct bc_io *new_req = connection_recv(server, connection);
            
            new_req->id = req->splice.fd_out;
            new_req->file_id = req->file_id;
        }
    }
}

static void
io_completion_write(struct bc_server *server, struct bc_io *req, int res)
{
    int nwritten = res;
    
    log_debug("WRITE completed, for fd %d, size = %d\n", req->write.fd, nwritten);
    
    if (nwritten < 0) {
        log_ferror(__func__, "error: %s\n", strerror(-res));
        return;
    }
    
    if (nwritten < req->write.size) {
        /* Partial write */
        struct bc_io *next_req = queue_pushr(&server->queue, req);
        next_req->write.size -= nwritten;
        next_req->write.buf += nwritten;
        queue_write(&server->queue, next_req);
    } else {
        /* write completed */
        int client_socket = req->id;
        struct bc_connection *connection = connection_get(server, client_socket); 
        if (connection) {
            if (connection->state == CONNECTION_WRITING_SIMPLE) {
                handle_file_written(server, req->file_id);
                handle_200_upload_do(&server->queue, connection);
            } else if (connection->state == CONNECTION_WRITING_LARGE) {
                handle_save_file(&server->queue, connection, req->write.fd, connection->file_size - req->write.size, req->file_id);
            }
        } else {
            log_fwarning(__func__, "WRITE completed for a dead connection %d\n", client_socket);
        }
    }
}

static void
server_loop(struct bc_server *server)
{
    struct io_uring_cqe *cqe = 0;
    struct bc_queue *rqueue = &server->queue;
    
    /* NOTE: Queue first accept. Everything else launches from the accept completion handler */
    struct bc_io *accept_req = queue_push(rqueue, IOU_ACCEPT);
    accept_req->accept.socket = server->fd;
    queue_accept(rqueue, accept_req);
    queue_submit(rqueue);
    
    while (on) {
        int cqe_status = io_uring_wait_cqe(&rqueue->ring, &cqe);
        
        if (cqe_status < 0) {
            fprintf(stderr, "[ERROR] io_uring_wait_cqe: %s\n", strerror(-cqe_status));
            return;
        }
        
        struct bc_io *req = io_uring_cqe_get_data(cqe);
        int res = cqe->res;
        
        switch (req->type) {
            case IOU_ACCEPT: {
                io_completion_accept(server, res);
                break;
            }
            
            case IOU_RECV: {
                io_completion_recv(server, req, res);
                break;
            }
            
            case IOU_SEND: {
                io_completion_send(server, req, res);
                break;
            }
            
            case IOU_SPLICE_READ:
            case IOU_SPLICE_WRITE: {
                io_completion_splice(server, req, res);
                break;
            }
            
            case IOU_WRITE: {
                io_completion_write(server, req, res);
                break;
            }
            
            default: {
                log_ferror(__func__, "Unhandled io type %d\n", req->type);
            }
        }
        
        queue_pop(req);
        queue_submit(rqueue);
        io_uring_cqe_seen(&rqueue->ring, cqe);
    }
}

static bool
ensure_environment(int argc, char **argv, char *real_path)
{
    if (argc != 3 && argc != 4) {
        log_error("Usage: %s port data_directory [--dev]\n", argv[0]);
        return(false);
    }
    
    if (!realpath(argv[2], real_path)) {
        log_fperror(__func__, "realpath");
        return(false);
    }
    
    if (!directory_exists(real_path)) {
        log_warning("Data directory %s does not exist, will try to create it now\n", argv[2]);
        if (mkdir(real_path, 0700) != 0) {
            log_perror("mkdir");
            return(false);
        }
    }
    
    if (chdir(real_path) == -1) {
        log_fperror(__func__, "chdir");
        return(false);
    }
    
    if (!little_endian()) {
        log_error("This server should only be run on little endian systems!\n");
        return(false);
    }
    
#ifndef PAGE_SIZE
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
#endif
    
    return(true);
}

int
main(int argc, char **argv)
{
    u64 before_init = msec_now();
    
    char *port = argv[1];
    char real_data_dir[PATH_MAX] = { 0 };
    
    if (!ensure_environment(argc, argv, real_data_dir)) return(1);
    
    bool dev_mode = (argc == 4) && (strncmp(argv[3], "--dev", 5) == 0);
    
    struct sigaction sa_sigint = { 0 };
    struct sigaction sa_sigterm = { 0 };
    
    sa_sigint.sa_handler = SIG_IGN;
    sa_sigterm.sa_handler = sigterm_handler;
    
    if (dev_mode) {
        log_info("Running in DEV mode, use Ctrl+C to terminate\n");
        sigaction(SIGINT, &sa_sigterm, 0);
    } else {
        log_info("Running in PROD mode, use SIGTERM to terminate\n");
        sigaction(SIGINT, &sa_sigint, 0);
    }
    
    sigaction(SIGTERM, &sa_sigterm, 0);
    
    struct bc_server server = { 0 };
    
    if (!queue_init(&server.queue)) return(1);
    if (!connection_init(&server, port)) return(1);
    
    if (dev_mode) {
        generate_all_missing_previews(real_data_dir);
    }
    
    for (int i = 0; i < IMAGE_PROCESS_COUNT; ++i) {
        int p_id = generate_process();
        
        if (p_id < 0) {
            log_perror("Could not create image resize process, exiting\n");
            return(1);
        }
        
        if (p_id == 0) {
            int mq_desc = init_posix_queue(0);
            
            while(on) {
                struct mq_message msg_recv = { 0 };
                receive_msg(mq_desc, &msg_recv);
                if (msg_recv.type == START_GENERATE_PREVIEW) {
                    start_generate_preview(&msg_recv);
                }
            }
            
            close_queue(mq_desc);
            
            return(0);
        }
    }
    
    int mq_desc = init_posix_queue(1);
    server.mq_desc = mq_desc;
    
    u64 after_init = msec_now();
    
    log_info("-----------------------------\n");
    log_info("    Bullet.Chat MEDIA server v0.01a\n");
    log_info("    Init completed in %lu msec\n", after_init - before_init);
    log_info("    Listening on port: %s\n", port);
    log_info("    Data directory: %s\n", real_data_dir);
    log_info("    Running with %d resizer threads\n", IMAGE_PROCESS_COUNT);
    log_info("-----------------------------\n");
    
    server_loop(&server);
    
    // TODO: properly wait for all children to terminate
    
    log_info("Shutting down... Bye!\n");
    
    queue_finalize(&server.queue);
    close_queue(mq_desc);
    unlink_name();
    
    return(0);
}
