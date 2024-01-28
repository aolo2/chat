static char response_upload_req_template_200[] = "HTTP/1.1 200 OK\r\n"
"Content-length: 0\r\n"
"Access-Control-Allow-Origin: *\r\n"
"X-File-Id: %lu\r\n"
"Access-Control-Expose-Headers: X-File-Id\r\n"
"\r\n";

static char response_upload_do_200[] = "HTTP/1.1 200 OK\r\n"
"Content-length: 0\r\n"
"Access-Control-Allow-Origin: *\r\n"
"\r\n";

static char response_upload_status_req_template_200[] = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/plain\r\n"
"Content-length: %d\r\n"
"Access-Control-Allow-Origin: *\r\n"
"\r\n"
"%lu";

static char response_access_headers_200[] = "HTTP/1.1 200 OK\r\n"
"Access-Control-Allow-Origin: *\r\n"
"Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
"Access-Control-Allow-Headers: X-File-Id, X-Loaded-Bytes, Content-Type\r\n"
"Content-length: 0\r\n"
"\r\n";

static char response_template_404[] = "HTTP/1.1 404 Not Found\r\n"
"Content-length: 0\r\n"
"Access-Control-Allow-Origin: *\r\n"
"\r\n";

static char response_template_500[] = "HTTP/1.1 500 Internal Error\r\n"
"Content-length: 0\r\n"
"Access-Control-Allow-Origin: *\r\n"
"\r\n";

static void
handle_send_simple(struct bc_queue *rqueue, struct bc_connection *connection,
                   char *text, int total)
{
    struct bc_io *req = queue_push(rqueue, IOU_SEND);
    
    req->send.socket = connection->socket;
    req->send.buf = text;
    req->send.start = 0;
    req->send.total = total;
    
    connection->state = CONNECTION_SENDING_SIMPLE;
    
    queue_send(rqueue, req, 0);
}

static void
handle_500(struct bc_queue *rqueue, struct bc_connection *connection)
{
    handle_send_simple(rqueue, connection, response_template_500, sizeof(response_template_500) - 1);
}

static void
handle_404(struct bc_queue *rqueue, struct bc_connection *connection)
{
    handle_send_simple(rqueue, connection, response_template_404, sizeof(response_template_404) - 1);
}

static void
handle_200_upload_do(struct bc_queue *rqueue, struct bc_connection *connection)
{
    handle_send_simple(rqueue, connection, response_upload_do_200, sizeof(response_upload_do_200) - 1);
}

static void
handle_200_access_headers(struct bc_queue *rqueue, struct bc_connection *connection)
{
    handle_send_simple(rqueue, connection, response_access_headers_200, sizeof(response_access_headers_200) - 1);
}

static void
handle_200_upload_req(struct bc_queue *rqueue, struct bc_connection *connection, u64 file_id)
{
    struct bc_io *req = queue_push(rqueue, IOU_SEND);
    int total = snprintf(connection->send_buf, SEND_BUFFER_SIZE, response_upload_req_template_200, file_id);
    
    req->send.socket = connection->socket;
    req->send.buf = connection->send_buf;
    req->send.start = 0;
    req->send.total = total;
    
    connection->state = CONNECTION_SENDING_SIMPLE;
    
    queue_send(rqueue, req, 0);
}

static void
handle_200_upload_status(struct bc_queue *rqueue, struct bc_connection *connection, u64 have_bytes)
{
    struct bc_io *req = queue_push(rqueue, IOU_SEND);
    char digits[20 + 1] = { 0 };
    int ndigits = snprintf(digits, 21, "%lu", have_bytes);
    
    int total = snprintf(connection->send_buf, SEND_BUFFER_SIZE, 
                         response_upload_status_req_template_200, 
                         ndigits, have_bytes);
    
    req->send.socket = connection->socket;
    req->send.buf = connection->send_buf;
    req->send.start = 0;
    req->send.total = total;
    
    connection->state = CONNECTION_SENDING_SIMPLE;
    
    queue_send(rqueue, req, 0);
}

static void
handle_save_file(struct bc_queue *queue, struct bc_connection *connection, int file_fd, u64 file_size, u64 file_id)
{
    struct bc_io *req_read = queue_push(queue, IOU_SPLICE_READ);
    struct bc_io *req_write = queue_push(queue, IOU_SPLICE_WRITE);
    
    req_read->id = req_write->id = connection->socket;
    
    req_read->splice.fd_in = connection->socket;
    req_read->splice.fd_out = connection->pipe_in;
    req_read->splice.total = file_size;
    req_read->splice.sent = 0;
    
    req_write->splice.fd_in = connection->pipe_out;
    req_write->splice.fd_out = file_fd;
    req_write->splice.total = file_size;
    req_write->splice.sent = 0;
    req_write->file_id = file_id;
    
    queue_splice(queue, req_read);
    queue_splice(queue, req_write);
}

static u64
generate_file_id(void)
{
    u64 id = 0;
    getrandom(&id, 8, 0);
    return(id);
}

static int
open_file(u64 id, int flags, mode_t mode)
{
    char filename[16 + 1] = { 0 };
    snprintf(filename, 16 + 1, "%lx", id);
    int file_fd = open(filename, flags, mode); 
    return(file_fd);
}


static void
handle_create_new_upload(struct bc_server *server, struct bc_connection *connection)
{
    u64 file_id = generate_file_id();
    handle_200_upload_req(&server->queue, connection, file_id);
}

static void
handle_upload_status(struct bc_server *server, struct bc_connection *connection, struct bc_http_request *request)
{
    struct bc_str x_file_id = get_header_value(request, "X-File-Id");
    u64 file_id = str_to_u64(x_file_id);
    int fd = open_file(file_id, O_RDONLY, 0);
    
    if (fd == -1) {
        handle_404(&server->queue, connection);
        return;
    }
    
    u64 have_bytes = get_file_size(fd);
    
    close(fd);
    
    handle_200_upload_status(&server->queue, connection, have_bytes);
}

static void
handle_upload(struct bc_server *server, struct bc_connection *connection, struct bc_http_request *request)
{
    struct bc_str h_file_id = get_header_value(request, "X-File-Id");
    struct bc_str h_content_length = get_header_value(request, "Content-Length");
    
    if (!h_content_length.data) {
        handle_404(&server->queue, connection);
        return;
    }
    
    u64 file_id = str_to_u64(h_file_id);
    int content_length = str_to_u64(h_content_length);
    
    // No O_APPEND here, because splice doesn't support writing to files opened in append mode
    int file_fd = open_file(file_id, O_RDWR | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR);
    if (file_fd == -1) {
        log_fperror(__func__, "open");
        handle_500(&server->queue, connection);
        return;
    }
    
    lseek(file_fd, 0, SEEK_END);
    
    /* 
Ending boundary is prefixed AND postfixed with two dashes 
Example: boundary="123123"

--123123
CONTENT
--123123--
*/
    if (content_length == request->post.body.length) {
        /* Whole file fit into the initial request body */
        log_debug("File %lx fit into request\n", file_id);
        
        // CLRF (+2)
        // -- (+2) BOUNDARY -- (+2)
        // CLRF (+2)
        int file_size = request->post.payload.length - (2 + 2 + request->post.boundary.length + 2 + 2);
        
        struct bc_io *req = queue_push(&server->queue, IOU_WRITE);
        
        req->id = connection->socket;
        req->file_id = file_id;
        
        req->write.fd = file_fd;
        req->write.buf = request->post.payload.data;
        req->write.size = file_size;
        
        connection->state = CONNECTION_WRITING_SIMPLE;
        
        queue_write(&server->queue, req);
    } else {
        /* File is too big, rest is going to be spliced from socket to disk */
        log_debug("File %lx is too big, will get splice()-ed to disk\n", file_id);
        
        struct bc_io *req = queue_push(&server->queue, IOU_WRITE);
        u64 body_header = request->post.body.length - request->post.payload.length;
        u64 total_file_size = content_length - body_header - (2 + 2 + request->post.boundary.length + 2 + 2);
        
        connection->file_size = total_file_size;
        
        req->id = connection->socket;
        
        req->write.fd = file_fd;
        req->write.buf = request->post.payload.data;
        req->write.size = request->post.payload.length;
        req->file_id = file_id;
        
        connection->state = CONNECTION_WRITING_LARGE;
        
        queue_write(&server->queue, req);
    }
}

static void
handle_http_post_request(struct bc_server *server, struct bc_connection *connection, struct bc_http_request *request)
{
    if (p_eqlit(request->path, "/upload-req")) {
        handle_create_new_upload(server, connection);
    } else if (p_eqlit(request->path, "/upload-status")) {
        handle_upload_status(server, connection, request);
    } else if (p_eqlit(request->path, "/upload-do")) {
        handle_upload(server, connection, request);
    } else {
        handle_404(&server->queue, connection);
    }
}

static void
handle_complete_http_request(struct bc_server *server, struct bc_connection *connection, struct bc_http_request *request)
{
    // printf("%.*s\n", request->path.length, request->path.data);
    
    switch (request->method) {
        case METHOD_POST: {
            handle_http_post_request(server, connection, request);
            break;
        }
        
        case METHOD_OPTIONS: {
            handle_200_access_headers(&server->queue, connection);
            break;
        }
        
        default: {
            fprintf(stderr, "[ERROR] Unexpected http method %d\n", request->method);
        }
    }
}

static void
handle_file_written(struct bc_server *server, u64 file_id)
{
    struct mq_message msg_send = { 0 };
    msg_send.type = START_GENERATE_PREVIEW;
    msg_send.file_id = file_id;
    send_msg(server->mq_desc, &msg_send);
}