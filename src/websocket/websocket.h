#include <crypt.h>      /* crypt, crypt_gensalt */

#define DEBUG_PRINT 1
#define DEBUG_ALLOW_EVERYTHING 0
#define ENABLE_TIMER 1

#define TIMEOUT_INTERVAL 5

/* These are all for virtual memory only */
#define MAX_POSSIBLE_USERS 2000
#define MAX_POSSIBLE_SESSIONS 10000
#define MAX_POSSIBLE_CHANNELS 5000
#define MAX_POSSIBLE_MESSAGE_MEMORY MB(100)
#define MAX_POSSIBLE_MESSAGES_PER_CHANNEL 1000000
#define MAX_POSSIBLE_FILES 10000
#define MAX_POSSIBLE_BLOCKS 1000000
#define MAX_POSSIBLE_STRINGS 1000000

#define MAX_TRANSIENT_REQUEST_SEND KB(16) /* mostly because we send usernames of all users */
#define PASSWORD_MAX_HASH_LENGTH 128

#define BAD_BLOCK_ID ((u32) -1)
#define MAX_BLOCKS_PER_FILE 10000
#define MAX_BLOCK_NEXT_CHAIN 10

static u32 SOCKET_TIMEOUT_SECONDS = 0;

enum bc_websocket_opcode {
    FRAME_CONTINUATION = 0x0,
    FRAME_TEXT         = 0x1,
    FRAME_BINARY       = 0x2,
    FRAME_CLOSE        = 0x8,
    FRAME_PING         = 0x9,
    FRAME_PONG         = 0xA
};

#define SYSTEM_RECORD(a) (a >= 100)

enum bc_record_type {
    WS_MESSAGE = 0,
    WS_DELETE = 1,
    WS_EDIT = 2,
    WS_REPLY = 3,
    WS_REACTION_ADD = 4,
    WS_REACTION_REMOVE = 5,
    WS_PIN = 6,
    WS_ATTACH = 7,
    WS_USER_LEFT = 100,
    WS_USER_JOINED = 101,
    WS_TITLE_CHANGED = 102,
    
    WS_RECORD_TYPE_COUNT,
};

enum bc_wsmessage_type {
    WS_CLIENT_AUTH   = 0,
    WS_CLIENT_INIT   = 1,
    WS_CLIENT_LOGOUT = 2,
    WS_CLIENT_SYNC   = 3,
    WS_CLIENT_ACK    = 4,
    WS_CLIENT_SEEN   = 5,
    
    /* Actions not strictly related to messages */
    WS_CLIENT_ADD_USER            = 10,
    WS_CLIENT_ADD_CHANNEL         = 11,
    WS_CLIENT_REMOVE_USER         = 13,
    WS_CLIENT_REMOVE_CHANNEL      = 14,
    WS_CLIENT_REMOVE_CHANNEL_USER = 15,
    WS_CLIENT_ADD_DIRECT          = 16,
    WS_CLIENT_IS_TYPING           = 17,
    
    WS_CLIENT_REQUEST_CHANNEL_INFO = 20,
    
    WS_CLIENT_SET_USER_AVATAR = 30,
    WS_CLIENT_SET_CHANNEL_AVATAR = 31,
    
    WS_CLIENT_CHANGE_PASSWORD = 40,
    
    WS_CLIENT_SAVE_UTF8 = 50,
    WS_CLIENT_REQUEST_UTF8 = 51,
    
    WS_SERVER_AUTH_SUCCESS = 100,
    WS_SERVER_AUTH_FAIL    = 101,
    WS_SERVER_INIT         = 102,
    WS_SERVER_ACK          = 103,
    WS_SERVER_SYNC         = 104,
    
    WS_SERVER_INVITED_TO_CHANNEL = 105,
    WS_SERVER_CHANNEL_INFO       = 106,
    WS_SERVER_USER_STATUS        = 107,
    WS_SERVER_USER_SEEN          = 108,
    WS_SERVER_PUSHPOLL           = 109,
    WS_SERVER_USER_IS_TYPING     = 110,
    
    WS_SERVER_UTF8_SAVED = 200,
    WS_SERVER_UTF8_DATA = 201,
};

static const char *WEBSOCKET_OPCODES[0xff] = {
    [WS_CLIENT_AUTH] = "WS_CLIENT_AUTH",
    [WS_CLIENT_INIT] = "WS_CLIENT_INIT",
    [WS_CLIENT_LOGOUT] = "WS_CLIENT_LOGOUT",
    [WS_CLIENT_SYNC] = "WS_CLIENT_SYNC",
    [WS_CLIENT_ACK] = "WS_CLIENT_ACK",
    [WS_CLIENT_SEEN] = "WS_CLIENT_SEEN",
    [WS_CLIENT_ADD_USER] = "WS_CLIENT_ADD_USER",
    [WS_CLIENT_ADD_CHANNEL] = "WS_CLIENT_ADD_CHANNEL",
    [WS_CLIENT_REMOVE_USER] = "WS_CLIENT_REMOVE_USER",
    [WS_CLIENT_REMOVE_CHANNEL] = "WS_CLIENT_REMOVE_CHANNEL",
    [WS_CLIENT_REMOVE_CHANNEL_USER] = "WS_CLIENT_REMOVE_CHANNEL_USER",
    [WS_CLIENT_ADD_DIRECT] = "WS_CLIENT_ADD_DIRECT",
    [WS_CLIENT_IS_TYPING] = "WS_CLIENT_IS_TYPING",
    [WS_CLIENT_REQUEST_CHANNEL_INFO] = "WS_CLIENT_REQUEST_CHANNEL_INFO",
    [WS_CLIENT_SET_USER_AVATAR] = "WS_CLIENT_SET_USER_AVATAR",
    [WS_CLIENT_SET_CHANNEL_AVATAR] = "WS_CLIENT_SET_CHANNEL_AVATAR",
    [WS_CLIENT_CHANGE_PASSWORD] = "WS_CLIENT_CHANGE_PASSWORD",
    [WS_CLIENT_SAVE_UTF8] = "WS_CLIENT_SAVE_UTF8",
    [WS_CLIENT_REQUEST_UTF8] = "WS_CLIENT_REQUEST_UTF8",
    [WS_SERVER_AUTH_SUCCESS] = "WS_SERVER_AUTH_SUCCESS",
    [WS_SERVER_AUTH_FAIL] = "WS_SERVER_AUTH_FAIL",
    [WS_SERVER_INIT] = "WS_SERVER_INIT",
    [WS_SERVER_ACK] = "WS_SERVER_ACK",
    [WS_SERVER_SYNC] = "WS_SERVER_SYNC",
    [WS_SERVER_INVITED_TO_CHANNEL] = "WS_SERVER_INVITED_TO_CHANNEL",
    [WS_SERVER_CHANNEL_INFO] = "WS_SERVER_CHANNEL_INFO",
    [WS_SERVER_USER_STATUS] = "WS_SERVER_USER_STATUS",
    [WS_SERVER_USER_SEEN] = "WS_SERVER_USER_SEEN",
    [WS_SERVER_PUSHPOLL] = "WS_SERVER_PUSHPOLL",
    [WS_SERVER_USER_IS_TYPING] = "WS_SERVER_USER_IS_TYPING",
    [WS_SERVER_UTF8_SAVED] = "WS_SERVER_UTF8_SAVED",
    [WS_SERVER_UTF8_DATA] = "WS_SERVER_UTF8_DATA",
};

enum bc_user_status {
    STATUS_OFFLINE = 0,
    STATUS_ONLINE = 1,
    STATUS_AWAY = 2,
    STATUS_BUSY = 3,
};

#define AWAY_INACTIVITY_PERIOD 15 * 60

enum bdisk_filetype {
    BDISK_FILETYPE_INVALID = 0, 
    
    BDISK_FILETYPE_FIXED,     /* array of fixed-size chunks. size of chunks is written in the header */
    BDISK_FILETYPE_STRING,    /* array of length-prefixed blocks */
    BDISK_FILETYPE_APPPEND,   /* append-only file */
    
    BDISK_FILETYPE_COUNT
};

enum bdisk_fileid { 
    BDISK_FILEID_INVALID = 0,
    
    BDISK_FILEID_STRINGS = 1,
    
    BDISK_FILEID_USERS = 3,
    BDISK_FILEID_SESSIONS = 4,
    BDISK_FILEID_CHANNELS = 5,
    BDISK_FILEID_CHANNELUSERS = 6,
    BDISK_FILEID_CHANNELSESSIONS = 7,
    
    /* add new fixed files here */
    
    BDISK_FILEID_MESSAGES_BASE = 16,
    /* actual files have ids incremented from this base
 by the channel id */
};


struct bc2_block_storage {
    u32 autoid;
    u64 total;
    struct bc_vm vm;
};

struct bc2_memory {
    struct bc_persist_user *users;
    struct bc_persist_session *sessions;
    struct bc_persist_channel *channels;
    struct bc_persist_channel_user *channel_users;
    struct bc_persist_channel_session *channel_sessions;
    struct bc2_block_storage block_storage;
};

#pragma pack(push, 1)
struct bc_block_header {
    s32 id;
    u32 offset;     /* offset of this header in the file (to get data from disk) */
    u32 data_size;  /* how much actual data (not counting this header) */
    u32 advance;    /* how much to add to the start of the header to get to the start of the next header (if it exists) */
};
#pragma pack(pop)

struct bc2_file {
    enum bdisk_fileid id;
    enum bdisk_filetype filetype;
    
    int fd;
    u64 size;
    const char *filename;
    
    /* used when filetype == BDISK_FILETYPE_FIXED */
    int *blocks; 
    int chunk_size;
    
    /* used when filetype == BDISK_FILETYPE_STRING */
    struct bc_block_header *strings; 
};

struct bc2_disk {
    const char *messages_directory;
    struct bc2_file *files;
    struct bc_queue *queue;
};

static const u32 BDISK_MAGIC = 0x0BB0CEFA;
static const u8  BDISK_VERSION_MAJOR = 2;
static const u8  BDISK_VERSION_MINOR = 0;
static const int BDISK_DELETED = (u32) -1;

static const int BDISK_HEADER_SIZE = sizeof(BDISK_MAGIC) 
+ sizeof(BDISK_VERSION_MAJOR) 
+ sizeof(BDISK_VERSION_MINOR)
+ sizeof(u8)   /* filetype */
+ sizeof(u32); /* file id */

enum bc_channel_flags {
    CHANNEL_DM = 0x1,
};

struct bc_sha1 {
    u8 data[20];
};

struct bc_websocket_frame {
    bool complete;
    bool fin;
    bool mask;
    int raw_length;
    enum bc_websocket_opcode opcode;
    u8 mask_key[4];
    struct bc_str payload;
};

// Non-persist info associated with a channel
struct bc_channel_info {
    u32 channel_id;
    u32 flags;
    
    char *messages; // packed message records
    u32 *offsets;
    
    u32 message_count;
    u32 first_unseen;
};

#pragma pack(push, 1)
// Non-persist info associated with a user
struct bc_user_info {
    u32 user_id;
    u64 last_online;
    u8 status;
};
#pragma pack(pop)

struct bc2_transient {
    struct bc_channel_info *channel_info;
    struct bc_user_info *user_info;
    struct bc_v2i *connections_per_channel; // (sorted_1) to broadcast the channel messages to all connected
    struct bc_v2i *users_per_channel;       // (sorted_1) to iterate users per of channel
    struct bc_v2i *channels_per_user;       // (sorted_2) to iterate channels for user (e.g. for init)
};

struct bc_slot {
    bool taken;
    bool commited;
};

struct bc_slots {
    struct bc_slot *occupancy;
    struct bc_vm vm;
    int count;
    int slot_size;
};

struct bc_server {
    int fd;
    
    struct bc_queue queue;
    struct bc_connection *connections;
    
    struct bc_slots slots;
    
    // Transient data
    struct bc2_transient views;
    
    // Persistent data and it's in-memory representation
    struct bc2_memory memory;
    struct bc2_disk disk;
};

#pragma pack(push, 1)
struct bc_persist_record {
    u8 type;
    u16 size;
    u32 message_id; // shared
    
    union {
        // message
        struct {
            u32 author_id;
            u64 timestamp;
            u32 thread_id;
            u32 length; // @size this can be a u16, because record.size is u16
        } message;
        
        // reaction add/remove
        struct {
            u32 id;
            u32 author_id;
        } reaction;
        
        // reply
        struct {
            u32 to;
        } reply;
        
        // edit
        struct {
            u32 length; // @size this can be a u16, because record.size is u16
        } edit;
        
        // attach
        struct {
            u64 file_id;
            u32 file_ext;
            
            union {
                u16 filename_length;
                u16 width;
            };
            
            u16 height;
        } attach;
        
        // user joined channel
        struct {
            u32 user_id;
            u64 timestamp;
        } join;
        
        // user left channel
        struct {
            u32 user_id;
            u64 timestamp;
        } leave;
        
        //changed title
        struct {
            u32 user_id;
            u64 timestamp;
            u32 length;
        } title_changed;
    };
    
    // variable-length data:
    //  - message text
    //  - edit text
    //  - attachment filename
};

struct bc_persist_session {
    u32 id;
    
    u64 sid;
    s32 user_id;
};

struct bc_persist_user {
    u32 id;
    
    u64 avatar_id;
    char password_hash[PASSWORD_MAX_HASH_LENGTH];
    
    u32 name_block;
    u32 login_block;
    s32 blob_block; /* can be -1 if no blob */
};

struct bc_persist_channel {
    u32 id;
    
    u32 flags;
    u64 avatar_id;
    
    u32 title_block;
};

// Used to store which user is in which channel and how many
// messages have they seen
struct bc_persist_channel_user {
    u32 id;
    
    u32 user_id;
    u32 channel_id;
    
    u32 blank1; // used to be client->server SN, blanked out to avoid migrating
    u32 blank2; // used to be server->client SN, blanked out to avoid migrating
    
    u32 first_unseen; // THIS is actually per user, everything else is per session!
};

// Used to store sequence numbers for sending and receiving
// messages in a particular channel to and from particular sessions
// This is needed because there can be several sessions per user, and they
// will have different sequence numbers
struct bc_persist_channel_session {
    u32 id;
    
    u64 session_id;
    u32 channel_id;
    
    u32 first_unsent;   // server->client SN (used to syncronise server -> client sends)
    u32 first_unrecved; // client->server SN (used to syncronise client -> server sends)
};
#pragma pack(pop)
