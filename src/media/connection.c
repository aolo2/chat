/* TODO: maybe factor out repating parts of this and websocket/connection.c into shared/... */
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
    int pipes[2] = { 0 };
    
    if (pipe(pipes) == -1) {
        return(0);
    }
    
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
    
    result->socket = socket;
    result->state = CONNECTION_CREATED;
    result->pipe_out = pipes[0]; /* man pipe: pipefd[0] refers to the read end of the pipe. */
    result->pipe_in = pipes[1];
    result->recv_start = 0; /* need to reset this in case we reused an old connection struct */
    
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
connection_drop(struct bc_server *server, struct bc_connection *connection)
{
    int socket = connection->socket;
    
    if (socket != -1) {
        close(socket);
        close(connection->pipe_in);
        close(connection->pipe_out);
        connection_remove(server, socket);
    }
}