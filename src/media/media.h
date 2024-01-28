#include <mqueue.h>  /* mq_open, mq_send, mq_receive, mq_close, mq_unlink */
#include <errno.h>   /* EAGAIN */

#define DEBUG_PRINT 1
#define IMAGE_PROCESS_COUNT 6
#define MQ_NAME "/preview_image"
#define MQ_MAX_MSG 10

#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM

#define STBIR_ASSERT(boolval) 

#define PREVIEW_LVL0 768
#define PREVIEW_LVL1 256
#define PREVIEW_LVL2 128
#define PREVIEW_LVL3 32

static const int PREVIEW_LEVELS[] = { PREVIEW_LVL0, PREVIEW_LVL1, PREVIEW_LVL2, PREVIEW_LVL3 };

enum bc_extension {
    EXT_OTHER = 0,
    
    EXT_JPEG,
    EXT_PNG,
    EXT_BMP,
    EXT_GIF,
    
    EXT_AUDIO,
    EXT_VIDEO,
    EXT_IMAGE,
    EXT_IMAGE_VECTOR,
    EXT_TEXT,
    EXT_ARCHIVE,
};

enum mq_msg_type {
    START_GENERATE_PREVIEW = 1,
};

struct bc_image {
    unsigned char *data;
    char filename[16 + 1];
    int width;
    int height;
    int comps;
};

struct bc_server {
    int fd;
    int mq_desc;
    struct bc_queue queue;
    struct bc_connection *connections;
};

struct mq_message {
    enum mq_msg_type type;
    u64 file_id;
};