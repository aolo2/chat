#include <stdint.h>  /* fixed-width integer types (uint32_t etc) */

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t  s8;

typedef float  f32;
typedef double f64;

#include <stdlib.h>  /* strtod, realpath */
#include <stdbool.h> /* bool, true, false */
#include <stdarg.h>  /* va_last, va_start, va_end */
#include <stdio.h>      /* fprintf, stderr, stdout, snprintf */

#include <sys/types.h>  /* getaddrinfo, freeaddrinfo,  gai_strerror, opendir, readdir, closedir */
#include <sys/socket.h> /* socket, setsockopt, accept, listen, bind, getaddrinfo, freeaddrinfo, gai_strerror */
#include <sys/stat.h>   /* fstat, stat, mkdir */
#include <sys/mman.h>   /* mmap */
#include <sys/time.h>   /* gettimeofday */
#include <sys/random.h> /* getrandom */

#include <time.h>       /* clock_gettime */
#include <fcntl.h>      /* open */
#include <netdb.h>      /* getaddrinfo, freeaddrinfo, gai_strerror */
#include <string.h>     /* strncmp, strsignal */
#include <liburing.h>   /* io_uring_xxx */
#include <signal.h>     /* sigaction */
#include <unistd.h>     /* close */
#include <endian.h>     /* htobe64 etc */
#include <dirent.h>     /* opendir, readdir, closedir */

#define KB(v) (1024LL * (v))
#define MB(v) (1024LL * KB(v))
#define GB(v) (1024LL * MB(v))

#define MAX_SENDV_VECTORS 10
#define MAX_WRITEV_VECTORS MAX_SENDV_VECTORS

#define MAX_POSSIBLE_CONNECTIONS 20000

#define SMALL_BUFFER_SIZE 32
#define READ_BUFFER_SIZE KB(64)
#define SEND_BUFFER_SIZE KB(1)

#define MAX_HTTP_HEADERS 64
#define URING_SIZE 512
#define URING_QUEUE_SIZE 4096
#define LISTEN_BACKLOG 128
#define SPLICE_PORTION MB(4) /* portion going between (fd -> [pipe] -> socket) */

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define POINTER_INC(p, inc) ((char *) (p) + (inc))
#define POINTER_DEC(p, dec) ((char *) (p) - (dec))
#define POINTER_DIFF(p1, p2) ((char *)(p1) - (char *)(p2))

#define STATIC_ARRAY_SIZE(a) ((int) (sizeof(a) ? sizeof(a) / sizeof(a[0]) : 0))

u32 PAGE_SIZE = 4096; // updated later by code with sysconf()

enum bc_io_type {
    IOU_UNDEFINED = 0,
    IOU_ACCEPT,
    IOU_RECV,
    IOU_SEND,   
    IOU_SENDV,  // to a socket
    IOU_WRITE,
    IOU_WRITEV, // to disk
    IOU_TIMEOUT,
    IOU_SPLICE_READ,
    IOU_SPLICE_WRITE,
};

#if 0
static const char *IO_TYPES[] = {
    [IOU_UNDEFINED] = "IOU_UNDEFINED",
    [IOU_ACCEPT] = "IOU_ACCEPT",
    [IOU_RECV] = "IOU_RECV",
    [IOU_SEND] = "IOU_SEND",
    [IOU_SENDV] = "IOU_SENDV",
    [IOU_WRITE] = "IOU_WRITE",
    [IOU_WRITEV] = "IOU_WRITEV",
    [IOU_TIMEOUT] = "IOU_TIMEOUT",
    [IOU_SPLICE_READ] = "IOU_SPLICE_READ",
    [IOU_SPLICE_WRITE] = "IOU_SPLICE_WRITE",
};
#endif

enum bc_connection_state {
    CONNECTION_CLOSED = 0, // NOTE: so that connections are closed by default (when 0-initialized)
    CONNECTION_CREATED,
    CONNECTION_OPENING,
    CONNECTION_OPENED,
    CONNECTION_WRITING_SIMPLE, // started a simple write to disk
    CONNECTION_WRITING_LARGE, // started a splice() to disk
    CONNECTION_SENDING_SIMPLE, // started a simple http response (no file)
    CONNECTION_READ_REMAINDER, // 
    CONNECTION_CLOSING, // sent close frame
    CONNECTION_SHUTTING_DOWN, // did shutdown(SHUT_WR)
};

enum bc_method {
    METHOD_OTHER = 0,
    METHOD_OPTIONS, 
    METHOD_GET, 
    METHOD_HEAD, 
    METHOD_POST,
    METHOD_PUT, 
    METHOD_DELETE, 
    METHOD_TRACE, 
    METHOD_CONNECT
};

enum bc_io_flag {
    IOUF_SUBMIT_IMMEDIATELY = 0x1,
    IOUF_SUBMIT_QUEUED = 0x2,
};

struct bc_vm {
    u8 *base;
    u64 size;
    u64 commited;
};

struct bc_v2i {
    union {
        struct { int first; int second; };
        struct {
            int channel_id;
            union { 
                int user_id;
                int socket;
                int message_count;
            };
        };
    };
};

struct bc_iovec {
    void *base;
    int len;
};

struct bc_str {
    s32 length;
    union {
        char *data;
        u8 *udata;
    };
};

struct bc_buf {
    u32 use;
    u32 cap;
    struct bc_vm vm;
    union {
        char *data;
        u8   *udata;
        int  *idata;
    };
    
    int id; // use this for whatever you want
};

struct bc_io_accept {
    int socket;
    struct sockaddr_storage client_addr;
    socklen_t client_addrlen;
};

struct bc_io_recv {
    int socket;
    char *buf;
    int start;
    int total;
};

struct bc_io_send {
    int socket;
    char *buf;
    int start;
    int total;
};

struct bc_io_sendv {
    int socket;
    int start;
    int total;
    struct iovec vectors[MAX_SENDV_VECTORS];
    int nvecs;
};

struct bc_io_write {
    int fd;
    char *buf;
    int size;
};

struct bc_io_writev {
    int fd;
    int start;
    int total;
    struct iovec vectors[MAX_SENDV_VECTORS];
    int nvecs;
    u64 offset;
};

struct bc_io_splice {
    int fd_in;
    int fd_out;
    
    u64 total;
    u64 sent;
};

struct bc_io_timeout {
    struct __kernel_timespec ts;
};

struct bc_io {
    enum bc_io_type type;
    
    u8 buf[SMALL_BUFFER_SIZE];
    
    int id; // use this however you want
    int slot_id; // if small buffer is not enough, we reserve a stable slot for the contents
    
    u64 file_id;
    
    union {
        struct bc_io_accept accept;
        struct bc_io_recv recv;
        struct bc_io_send send;
        struct bc_io_sendv sendv;
        struct bc_io_write write;
        struct bc_io_writev writev;
        struct bc_io_timeout timeout;
        struct bc_io_splice splice;
    };
};

struct bc_queue {
    struct bc_io *reqs;
    int size;
    struct io_uring ring;
};

// NOTE connection is everything associated with a socket:
// buffers, connection state, session_id. A connection
// can be authenicated or not. This is determined by an 'alive'
// flag of the associated session field (i.e. session_id > 0 && sessions.find(session_id).alive = true)
struct bc_connection {
    int socket;
    
    enum bc_connection_state state;
    
    u64 session_id;
    
    int pipe_in;
    int pipe_out;
    u64 file_size;
    
    int recv_start;
    char recv_buf[READ_BUFFER_SIZE];
    char send_buf[SEND_BUFFER_SIZE];
};

struct bc_http_headers {
    struct bc_str keys[MAX_HTTP_HEADERS];
    struct bc_str values[MAX_HTTP_HEADERS];
    int nheaders;
};

struct bc_post {
    struct bc_str boundary;
    struct bc_str filename;
    struct bc_str payload; // without the boundaries and meta-info
    struct bc_str body;    // raw
};

struct bc_http_request {
    bool valid;
    bool multipart;
    bool complete;
    
    enum bc_method method;
    
    struct bc_str path;
    struct bc_http_headers headers;
    struct bc_post post;
    
    int raw_length;
};

static void log_time(FILE *stream);
static void log_perror(const char *text);
static void log_critical_die(const char* format, ...);
static void log_error(const char* format, ...);
static void log_ferror(const char *func, const char* format, ...);
static void log_fwarning(const char *func, const char* format, ...);
static void log_fperror(const char *func, const char *text);
static void log_warning(const char* format, ...);
static void log_info(const char* format, ...);
static void log_debug(const char* format, ...);
