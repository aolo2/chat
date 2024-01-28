static int
_create_server_socket(const char *port)
{
    int status = 0;
    
    struct addrinfo hints = { 0 };
    struct addrinfo *servinfo = 0;
    
    hints.ai_family   = AF_UNSPEC;   /* Don't care if IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* TCP */
    hints.ai_flags    = AI_PASSIVE;  /* Set my IP for me */
    
    if ((status = getaddrinfo(0, port, &hints, &servinfo)) != 0) {
        log_error("getaddrinfo: %s\n", gai_strerror(status));
        return(-1);
    }
    
    int result = -1;
    
    for (struct addrinfo *info = servinfo; info; info = info->ai_next) {
        result = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        
        if (result != -1) {
            int yes = 1;
            if (setsockopt(result, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
                log_perror("setsockopt");
                return(-1);
            }
            
            if (bind(result, info->ai_addr, info->ai_addrlen) == -1) {
                log_perror("bind");
                return(-1);
            }
            
            if (listen(result, LISTEN_BACKLOG) == -1) {
                log_perror("bind");
                return(-1);
            }
            
            break;
        }
    }
    
    freeaddrinfo(servinfo);
    
    return(result);
}

static struct bc_persist_session *
connection_session(struct bc_server *server, struct bc_connection *connection)
{
    if (!connection) {
        return (0);
    }
    
    if (connection->state != CONNECTION_OPENED) {
        return(0);
    }
    
    if (connection->session_id == (u64) -1) {
        return(0);
    }
    
    struct bc_persist_session *session = mem_session_find(&server->memory, connection->session_id);
    
    return (session);
}

static struct bc_connection *
connection_get(struct bc_server *server, int connection_socket)
{
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *connection = server->connections + i;
        if (connection->state != CONNECTION_CLOSED && connection->socket == connection_socket) {
            return(connection);
        }
    }
    
    return(0);
}

static struct bc_connection *
connection_create(struct bc_server *server, int socket)
{
    struct bc_connection *result = 0;
    
    for (int i = 0; i < buffer_size(server->connections); ++i) {
        struct bc_connection *connection = server->connections + i;
        if (connection->state == CONNECTION_CLOSED) {
            result = connection;
            break;
        }
    }
    
    if (!result) {
        struct bc_connection new_connection = { 0 };
        buffer_push(server->connections, new_connection);
        result = server->connections + buffer_size(server->connections) - 1;
    }
    
    result->state = CONNECTION_CREATED;
    result->socket = socket;
    
    return(result);
}

static bool
connection_remove(struct bc_server *server, int socket)
{
    struct bc_connection *connection = connection_get(server, socket);
    
    if (connection) {
        connection->state = CONNECTION_CLOSED;
        return(true);
    }
    
    log_warning("Attempt to remove a non-existent connection\n");
    
    return(false);
}

static bool
connection_init(struct bc_server *server, const char *port)
{
    server->connections = buffer_init(MAX_POSSIBLE_CONNECTIONS, sizeof(struct bc_connection));
    
    if (!server->connections) {
        log_error("Connection init failed");
        return(false);
    }
    
    int server_socket = _create_server_socket(port); 
    if (server_socket == -1) {
        log_error("Failed to create the websocket server socket\n");
        return(false);
    }
    
    server->fd = server_socket;
    
    return(true);
}

static void
connection_shutdown(struct bc_queue *queue, struct bc_connection *connection)
{
    int socket = connection->socket;
    
    /*
    
As per Websocket RFC:

    "As an example of how to obtain a clean closure in C using Berkeley
       sockets, one would call shutdown() with SHUT_WR on the socket, call
       recv() until obtaining a return value of 0 indicating that the peer
       has also performed an orderly shutdown, and finally call close() on
       the socket."
    
    */
    
    if (socket != -1) {
        shutdown(socket, SHUT_WR);
        connection->state = CONNECTION_SHUTTING_DOWN;
        log_info("Shut down connection with socket = %d\n", socket);
        
        struct bc_io *req_recv = queue_push(queue, IOU_RECV);
        
        req_recv->recv.buf = connection->recv_buf;
        req_recv->recv.socket = connection->socket;
        req_recv->recv.start = 0;
        req_recv->recv.total = READ_BUFFER_SIZE;
        
        queue_recv(queue, req_recv);
    }
}

static void
connection_drop(struct bc_server *server, struct bc_connection *connection)
{
    int socket = connection->socket;

    if (socket != -1) {
        close(socket);
        int removed = view_remove_all_pairs_2(server->views.connections_per_channel, socket);
        log_debug("Removed %d channel connections\n", removed);
        connection_remove(server, socket);
    }
}