#include "../shared/shared.h"
#include "websocket.h"

// Pure helpers (no dependencies)
#include "record.c"
#include "sha1.c"
#include "../shared/aux.c"

// System layer (shared)
#include "../shared/log.c"
#include "../shared/mapping.c"
#include "../shared/queue.c"
#include "../shared/buffer.c"

// System layer (websocket-only)
#include "slot.c"
#include "disk.c"
#include "views.c"
#include "memory.c"
#include "auth.c"
#include "connection.c"

// Application layer
#include "parse.c"
#include "storage.c"
#include "protocol.c"
#include "handle.c"

// TODO:
// lock/unlock files

volatile sig_atomic_t on = 1;
volatile sig_atomic_t terminating_signal = 0;

static void
sigterm_handler(int signum)
{
    terminating_signal = signum;
    on = 0;
}

static bool
io_completion_accept(struct bc_server *server, struct bc_queue *queue, int res)
{
    int client_socket = res;
    
    if (client_socket < 0) {
        log_warning("ACCEPT errored out (%s), restarting\n", strerror(-client_socket));
        struct bc_io *req = queue_push(queue, IOU_ACCEPT);
        req->accept.socket = server->fd;
        queue_accept(queue, req);
        return(true);
    }
    
    log_debug("ACCEPT completed, new connection = %d\n", client_socket);
    
    // NOTE(aolo2): set timeout for socket
    struct timeval tv;
    
    tv.tv_sec = SOCKET_TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof(tv)) == -1) {
        log_perror("setsockopt (accept)");
    }
    
    struct bc_connection *connection = connection_create(server, client_socket);
    
    if (!connection) {
        // TODO: server stops accepting connections after this point because we assume there are no resources left to do so. Re-issue the accept somewhere
        log_error("Failed to create a bc_connection for accepted socket!\n");
        return(false);
    }
    
    struct bc_io *req_recv = queue_push(queue, IOU_RECV);
    struct bc_io *req_accept = queue_push(queue, IOU_ACCEPT);
    
    req_recv->recv.buf = connection->recv_buf;
    req_recv->recv.socket = connection->socket;
    req_recv->recv.start = 0;
    req_recv->recv.total = READ_BUFFER_SIZE;
    
    req_accept->accept.socket = server->fd;
    
    //printf("[QUEUE] recv from accept\n");
    queue_recv(queue, req_recv);
    queue_accept(queue, req_accept);
    return(true);
}

static bool
io_completion_send(struct bc_server *server, struct bc_queue *queue, struct bc_io *req, int res)
{
    int nsent = res;
    
    log_debug("SEND completed, for connection %d, size = %d\n", req->send.socket, nsent);
    
    struct bc_connection *connection = connection_get(server, req->send.socket);
    if (!connection) {
        log_warning("SEND on a dead connection (socket = %d)!\n", req->send.socket);
        return(true);
    }
    
    if (nsent < 0) {
        log_warning("SEND errored out (%d): %s. Dropping the connection immediately\n", -1 * res, strerror(-1 * res));
        handle_connection_drop(server, connection);
        return(true);
    }
    
    if (req->send.start + nsent < req->send.total) {
        /* Partial send */
        log_debug("Partial send! Sent %d now + %d earlier = %d out of %d\n", nsent, req->send.start, req->send.start + nsent, req->send.total);
        struct bc_io *next_req = queue_pushr(queue, req);
        next_req->send.start += nsent;
        queue_send(queue, next_req, IOUF_SUBMIT_IMMEDIATELY);
    } else {
        if (connection->state == CONNECTION_CLOSING) {
            /* We have sent a CLOSE frame */
            connection_shutdown(queue, connection);
        } else if (connection->state == CONNECTION_OPENING) {
            /* We have finished the handshake */
            connection->state = CONNECTION_OPENED;
            log_info("Established a websocket connection with socket = %d\n", req->send.socket);
        } else if (connection->state == CONNECTION_OPENED) {
            /* We have sent a complete websocket frame */
            queue_send(queue, req, IOUF_SUBMIT_QUEUED);
        }
    }
    
    return(true);
}

static bool
io_completion_sendv(struct bc_server *server, struct bc_queue *queue, struct bc_io *req, int res)
{
    int nsent = res;
    
    log_debug("SENDV completed, for connection %d, size = %d\n", req->sendv.socket, nsent);
    
    struct bc_connection *connection = connection_get(server, req->sendv.socket);
    
    if (!connection) {
        log_warning("SENDV on a dead connection (socket = %d)!\n", req->sendv.socket);
        return(true);
    }
    
    if (nsent < 0) {
        handle_connection_drop(server, connection);
        log_warning("SENDV errored out (%d): %s. Dropping the connection immediately\n", -1 * res, strerror(-1 * res));
        return(true);
    }
    
    if (req->sendv.start + nsent < req->sendv.total) {
        /* Partial send */
        log_debug("Partial sendv! Sent %d now + %d earlier = %d out of %d\n", nsent, req->sendv.start, req->sendv.start + nsent, req->sendv.total);
        struct bc_io *next_req = queue_pushr(queue, req);
        next_req->sendv.start += nsent;
        queue_sendv(queue, next_req, nsent, IOUF_SUBMIT_IMMEDIATELY);
    } else {
        /* sendv finished */
        if (req->slot_id != -1) {
            slot_free(&server->slots, req->slot_id);
            req->slot_id = -1;
        }
        
        //disk_unlock_file(&server->persist, req->sendv.file_lock); TODO
        
        queue_sendv(queue, req, 0, IOUF_SUBMIT_QUEUED);
    }
    
    return(true);
}

static bool
io_completion_recv(struct bc_server *server, struct bc_queue *queue, struct bc_io *req, int res)
{
    int rsize = res;
    
    log_debug("RECV completed, for socket %d, size = %d\n", req->recv.socket, res);
    
    struct bc_connection *connection = connection_get(server, req->recv.socket);
    if (!connection) {
        log_warning("RECV of size %d on a non-existant connection (socket = %d)!\n", rsize, req->recv.socket);
        return(true);
    }
    
    if (rsize < 0 && -res == EAGAIN) {
        // NOTE(aolo2): recv timed out, restart
        struct bc_io *next_req = queue_pushr(queue, req);
        log_debug("RECV timeout, restart\n");
        queue_recv(queue, next_req);
        return(true);
    }
    
    if (connection->state == CONNECTION_SHUTTING_DOWN) {
        // We have already shut down the connection, and are waiting for recv of size 0 to close the socket
        if (rsize == 0 || -rsize == ECONNRESET) {
            connection_drop(server, connection);
            log_info("Closed connection with socket = %d\n", connection->socket);
            
            return(true);
        } else if (rsize > 0) {
            struct bc_io *next_req = queue_pushr(queue, req);
            log_debug("Got data in RECV after calling shutdown\n");
            queue_recv(queue, next_req);
            return(true);
        }
        
        // rsize < 0 gets handled lower down
    }
    
    if (rsize == 0) {
        log_error("RECV unexpectedly returned 0. Dropping the connection immediately\n", -1 * res, strerror(-1 * res));
        handle_connection_drop(server, connection);
        return(true);
    }
    
    if (rsize < 0) {
        log_error("RECV errored out (%d): %s. Dropping the connection immediately\n", -1 * res, strerror(-1 * res));
        return(false);
    }
    
    connection->recv_start += rsize;
    
    /* 
    Try to parse out and process complete messages from the recv_buffer until there's stuff left 
    It's possible that the buffer has more that one complete message, because network reasons..
    */
    while (connection->recv_start > 0) {
        struct bc_str message = { connection->recv_start, { connection->recv_buf }};
        int consumed = 0;
        
        if (connection->state == CONNECTION_CLOSING || connection->state == CONNECTION_CLOSED) {
            /* The CLOSE frame send hasn't been completed yet, but soon will */
            return(true);
        } else if (connection->state == CONNECTION_OPENED) {
            /* Main path (websocket mode) */
            struct bc_websocket_frame frame = parse_websocket_frame(message);
            if (frame.complete) {
                /* Probably the hot path */
                handle_complete_websocket_frame(server, queue, connection, &frame);
                consumed = frame.raw_length;
            } else {
                /* partical recv probably */
            }
        } else if (connection->state == CONNECTION_CREATED) {
            /* Fresh connection (websocket handshake needed) */
            struct bc_http_request request = parse_http_request(message);
            
            if (request.complete) {
                if (is_websocket_upgrade(&request)) {
                    handle_websocket_handshake(queue, connection, &request);
                    connection->state = CONNECTION_OPENING;
                    consumed = request.raw_length;
                } else {
                    log_warning("Incorrect handshake request\n");
                }
            }
        } else {
            log_warning("Received message from connection in state %d, ignoring!\n", connection->state);
        }
        
        if (consumed > 0) {
            /* a complete websocket frame / http request has been consumed */
            
            if (connection->recv_start - consumed > 0) {
                /* probably a cold path (something else left in the buffer) */
                memmove(connection->recv_buf, connection->recv_buf + consumed, connection->recv_start - consumed);
            }
            
            connection->recv_start -= consumed;
        } else {
            break;
        }
    }
    
    /* 
    NOTE(aolo2): we could have parsed a CLOSE frame. If so, we MUST NOT queue another recv.
    If we do (we did earlier and got a _nasty_ bug on Firefox only because of it), then there
    will be an extra recv queued after the connection is closed. And if the connection was closed
    because we pressed F5 in the browser, the sockfd would probably be soon reused for a new connection,
    thus giving us an extra recv in the queue for the new connection
    */
    if (connection->state != CONNECTION_CLOSING) {
        struct bc_io *next_req = queue_push(queue, IOU_RECV);
        
        next_req->recv.buf = connection->recv_buf;
        next_req->recv.socket = connection->socket;
        next_req->recv.start = connection->recv_start;
        next_req->recv.total = READ_BUFFER_SIZE;
        
        //printf("[QUEUE] recv from recv\n");
        queue_recv(queue, next_req);
    }
    
    return(true);
}

static bool
io_completion_writev(struct bc_queue *queue, struct bc_io *req, int res)
{
    int nwritten = res;
    
    log_debug("WRITEV completed, size = %d\n", res);
    
    if (nwritten < 0) {
        log_error("WRITEV errored out (%d): %s\n", -1 * res, strerror(-1 * res));
        return(false);
    }
    
    if (req->writev.start + nwritten < req->writev.total) {
        /* Partial writev */
        struct bc_io *next_req = queue_pushr(queue, req);
        next_req->writev.start += nwritten;
        // TODO!!!!!!!!!
        log_critical_die("ACTUALLY HANDLE THIS AND DO CORRECT OFFSET!!*!&!&!&!&!&!\nIF WE GET AN ACTUAL SHORT WRITE TO DISK WE FUCKED\n");
        queue_writev(queue, next_req, nwritten);
    } else {
        /* writev completed */
    }
    
    return(true);
}

static bool
io_completion_timeout(struct bc_server *server, struct bc_queue *queue, int res)
{
    log_debug("Timeout complete ret = %d\n", res);
    
    if (res != 0 && res != -ETIME) {
        return(false);
    }
    
    handle_send_pushpoll(server);
    
    struct bc_io *pollpush_req = queue_push(queue, IOU_TIMEOUT);
    
    queue_timeout(queue, pollpush_req, TIMEOUT_INTERVAL);
    
    return(true);
}

static void
server_loop(struct bc_server *server)
{
    struct io_uring_cqe *cqe = 0;
    struct bc_queue *queue = &server->queue;
    
    /* NOTE: Queue first accept. Everything else launches from the accept completion handler */
    struct bc_io *accept_req = queue_push(queue, IOU_ACCEPT);
    
    accept_req->accept.socket = server->fd;
    
    queue_accept(queue, accept_req);
    
#if ENABLE_TIMER
    struct bc_io *pollpush_req = queue_push(queue, IOU_TIMEOUT);
    queue_timeout(queue, pollpush_req, TIMEOUT_INTERVAL);
#endif
    
    queue_submit(queue);
    
    while (on) {
        
        int cqe_status = io_uring_wait_cqe(&queue->ring, &cqe);
        
        if (cqe_status < 0) {
            if (-cqe_status != EINTR) {
                log_error("io_uring_wait_cqe: %s\n", strerror(-cqe_status));
            }
            
            break; // <-- early exit from server loop
        }
        
        struct bc_io *req = io_uring_cqe_get_data(cqe);
        int res = cqe->res;
        
        switch (req->type) {
            case IOU_ACCEPT: {
                io_completion_accept(server, queue, res);
                break;
            }
            
            case IOU_RECV: {
                io_completion_recv(server, queue, req, res);
                break;
            }
            
            case IOU_SEND: {
                io_completion_send(server, queue, req, res);
                break;
            }
            
            case IOU_SENDV: {
                io_completion_sendv(server, queue, req, res);
                break;
            }
            
            case IOU_WRITEV: {
                io_completion_writev(queue, req, res);
                break;
            }
            
            case IOU_TIMEOUT: {
                io_completion_timeout(server, queue, res);
                break;
            }
            
            case IOU_UNDEFINED: {
                log_ferror(__func__, "UNDEFINED io type! This should not happen!\n");
                break;
            }
            
            // NOTE(aolo2): we now have a default case, because enums for this server and the
            // media server got merged
            default: {
                log_ferror(__func__, "Unhandled io type %d\n", req->type);
                break;
            }
        }
        
        queue_pop(req);
        
        queue_submit(queue);
        
        io_uring_cqe_seen(&queue->ring, cqe);
    }
}

static bool
ensure_environment(int argc, char **argv, char *real_path)
{
    if (argc != 3 && argc != 4) {
        log_error("Usage: %s port data_directory [--dev]\n", argv[0]);
        return(false);
    }
    
    if (!directory_exists(argv[2])) {
        log_fwarning(__func__, "Data directory %s not found, creating\n", argv[2]);
        if (mkdir(argv[2], 0755) == -1) {
            log_fperror(__func__, "mkdir");
            return(false);
        }
    }
    
    if (!realpath(argv[2], real_path)) {
        log_fperror(__func__, "realpath");
        return(false);
    }
    
    if (sizeof(time_t) != 8) {
        log_error("This server should only be run on systems where time_t is 64 bits, but its %ld\n", sizeof(time_t) * 8);
        return(false);
    }
    
    if (!little_endian()) {
        log_error("This server should only be run on little endian systems!\n");
        return(false);
    }
    
    if (!directory_exists(argv[2])) {
        log_warning("Data directory %s does not exist, will try to create it now\n", argv[2]);
        if (mkdir(argv[2], 0700) != 0) {
            log_perror("mkdir");
            return(false);
        }
    }
    
    if (chdir(argv[2]) == -1) {
        log_perror("chdir");
        return(false);
    }
    
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    
    return(true);
}

int
main(int argc, char **argv)
{
    u64 before_init = msec_now();
    
    // TODO: use getopt here, we are linking libc anyway
    char *port = argv[1];
    char real_data_dir[PATH_MAX] = { 0 };
    
    if (!ensure_environment(argc, argv, real_data_dir)) return(1);
    
    bool dev_mode = (argc == 4) && (strncmp(argv[3], "--dev", 5) == 0);
    
    struct sigaction sa_sigint = { 0 };
    struct sigaction sa_sigterm = { 0 };
    struct sigaction sa_sigpipe = { 0 };
    
    sa_sigint.sa_handler = SIG_IGN;
    sa_sigpipe.sa_handler = SIG_IGN; // https://stackoverflow.com/a/108192/11420590
    sa_sigterm.sa_handler = sigterm_handler;
    
    if (dev_mode) {
        log_info("Running in DEV mode, use Ctrl+C to terminate\n");
        sigaction(SIGINT, &sa_sigterm, 0);
        SOCKET_TIMEOUT_SECONDS = 10;
    } else {
        log_info("Running in PROD mode, use SIGTERM to terminate\n");
        sigaction(SIGINT, &sa_sigint, 0);
        SOCKET_TIMEOUT_SECONDS = 60 * 30;
        LOGS_PROD_MODE = true;
    }
    
    sigaction(SIGTERM, &sa_sigterm, 0);
    sigaction(SIGPIPE, &sa_sigpipe, 0);
    
    struct bc_server server = { 0 };
    
    if (!queue_init(&server.queue)) return(1);
    if (!st2_init(&server)) return(1);
    if (!connection_init(&server, port)) return(1);
    if (!slot_init(&server.slots, URING_SIZE, MAX_TRANSIENT_REQUEST_SEND)) return(1);
    
    u64 after_init = msec_now();
    
    log_info("-----------------------------\n");
    log_info("    Bullet.Chat WEBSOCKET server v0.09a\n");
    log_info("    Init completed in %lu msec\n", after_init - before_init);
    log_info("    Listening on port: %s\n", port);
    log_info("    Data directory: %s\n", real_data_dir);
    log_info("-----------------------------\n");
    
    server_loop(&server);
    
    log_info("Terminated by signal %d (%s)\n", terminating_signal, strsignal(terminating_signal));
    log_info("Shutting down... Bye!\n");
    
    bdisk_finalize(&server.disk);
    
    queue_finalize(&server.queue);
    
    return(0);
}
