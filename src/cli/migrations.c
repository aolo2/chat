#include "../shared/shared.h"
#include "../websocket/websocket.h"

// Pure helpers (no dependencies)
#include "../websocket/record.c"
#include "../websocket/sha1.c"
#include "../shared/aux.c"

// System layer (shared)
#include "../shared/log.c"
#include "../shared/mapping.c"
#include "../shared/queue.c"
#include "../shared/buffer.c"

// System layer (websocket-only)
#include "../websocket/slot.c"
#include "../websocket/disk.c"
#include "../websocket/views.c"
#include "../websocket/memory.c"
#include "../websocket/auth.c"
#include "../websocket/connection.c"

// Application layer
#include "../websocket/parse.c"
#include "../websocket/storage.c"
#include "../websocket/protocol.c"
#include "../websocket/handle.c"

#pragma pack(push, 1)

struct bc_persist_channel_v0 {
    u16 size;
    u32 id;
    u8 title_length;
};

struct bc_persist_channel_v1 {
    u16 size;
    u32 id;
    u32 flags;
    u8 title_length;
};

struct bc_persist_channel_v2 {
    u32 id;
    
    u32 flags;
    u64 avatar_id;
    
    u32 title_block;
};

struct bc_persist_session_v0 {
    bool alive;
    u64 sid;
    s32 user_id;
};

struct bc_persist_session_v1 {
    u32 id;
    u64 sid;
    s32 user_id;
};

struct bc_persist_user_v0 {
    u16 size;
    u32 id;
    u64 avatar_id;
    char password_hash[PASSWORD_MAX_HASH_LENGTH];
    u8 login_length;
    u8 name_length;
};

struct bc_persist_user_v1 {
    u32 id;
    
    u64 avatar_id;
    char password_hash[PASSWORD_MAX_HASH_LENGTH];
    
    u32 name_block;
    u32 login_block;
};

struct bc_persist_channel_user_v0 {
    u32 user_id;
    u32 channel_id;
    u32 first_unrecved;
    u32 first_unsent;
    u32 first_unseen;
};

struct bc_persist_channel_user_v1 {
    u32 id;
    u32 user_id;
    u32 channel_id;
    u32 first_unrecved;
    u32 first_unsent;
    u32 first_unseen;
};

struct bc_persist_record_v0 {
    u8 type;
    u16 size;
    u32 message_id; // shared
    
    union {
        // message
        struct {
            u32 author_id;
            u64 timestamp;
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
        } attach;
    };
};

struct bc_persist_record_v1 {
    u8 type;
    u16 size;
    u32 message_id; // shared
    
    union {
        // message
        struct {
            u32 author_id;
            u64 timestamp;
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

struct bc_persist_record_v2 {
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

#if 0

#pragma pack(pop)
static bool
migrate_add_flags_to_channels(void)
{
    int read_fd = open("data/channels.bchat", O_RDONLY, S_IRUSR | S_IWUSR);
    if (read_fd == -1) {
        perror("open");
        return(false);
    }
    
    int write_fd = open("data/channels_v1.bchat", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if (write_fd == -1) {
        perror("open");
        return(false);
    }
    
    struct stat st = { 0 };
    fstat(read_fd, &st);
    
    u64 file_size = st.st_size;
    
    char *disk = mmap(0, file_size, PROT_READ, MAP_PRIVATE, read_fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        return(false);
    }
    
    u32 read_offset = 0;
    
    enum bc_fileop_type fileop = OP_CHANNEL_ADD;
    
    while (read_offset < file_size) {
        read_offset += sizeof(enum bc_fileop_type);
        
        struct bc_persist_channel_v0 *old = (struct bc_persist_channel_v0 *) (disk + read_offset);
        struct bc_persist_channel_v1 new = { 0 };
        
        new.size = old->size + sizeof(new.flags);
        new.id = old->id;
        new.flags = 0;
        new.title_length = old->title_length;
        
        if (write(write_fd, &fileop, sizeof(fileop)) != sizeof(fileop)) {
            return(false);
        }
        
        if (write(write_fd, &new, sizeof(new)) != sizeof(new)) {
            return(false);
        }
        
        if (write(write_fd, POINTER_INC(old, sizeof(*old)), old->title_length) != old->title_length) {
            return(false);
        }
        
        read_offset += old->size;
    }
    
    return(true);
}

u32 OP_CHANNEL_USER_ADD = 40;
u32 OP_CHANNEL_USER_REMOVE = 41;
u32 OP_CHANNEL_USER_UPDATE = 42;


static bool
migrate_compact_channel_users(void)
{
    int read_fd = open("data/channel_users.bchat", O_RDONLY, S_IRUSR | S_IWUSR);
    if (read_fd == -1) {
        perror("open");
        return(false);
    }
    
    int write_fd = open("data/channel_users_v1.bchat", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if (write_fd == -1) {
        perror("open");
        return(false);
    }
    
    struct stat st = { 0 };
    fstat(read_fd, &st);
    
    u64 file_size = st.st_size;
    
    char *disk = mmap(0, file_size, PROT_READ, MAP_PRIVATE, read_fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        return(false);
    }
    
    char *buf = malloc(MB(1));
    if (!buf) {
        perror("malloc");
        return(false);
    }
    
    u32 use = 0;
    
    u32 offset = 0;
    
    while (offset < file_size) {
        enum bc_fileop_type type;
        memcpy(&type, disk + offset, sizeof(type));
        offset += sizeof(type);
        
        if (type == OP_CHANNEL_USER_ADD) {
            struct bc_persist_channel_user *cu = (struct bc_persist_channel_user *) (disk + offset);
            memcpy(buf + use, cu, sizeof(*cu));
            use += sizeof(*cu);
            offset += sizeof(*cu);
        } else if (type == OP_CHANNEL_USER_REMOVE) {
            u32 user_id = readu32(disk + offset);
            u32 channel_id = readu32(disk + offset + sizeof(user_id));
            
            // does not happen
            return(false);
            
            offset += sizeof(user_id) + sizeof(channel_id);
        } else if (type == OP_CHANNEL_USER_UPDATE) {
            struct bc_persist_channel_user *cu = (struct bc_persist_channel_user *) (disk + offset);
            
            struct bc_persist_channel_user *cus = (struct bc_persist_channel_user *) buf;
            int ncus = use / sizeof(struct bc_persist_channel_user);
            
            for (int i = 0; i < ncus; ++i) {
                struct bc_persist_channel_user *other = cus + i;
                if (cu->user_id == other->user_id && cu->channel_id == other->channel_id) {
                    *other = *cu;
                    break;
                }
            }
            
            offset += sizeof(*cu);
        } else {
            return(false);
        }
    }
    
    if (write(write_fd, buf, use) != use) {
        return(false);
    }
    
    return(true);
}
#endif
#if 0

static bool
migrate_to_full_attaches(void)
{
    int read_fd = open("data/channels.bchat", O_RDONLY, S_IRUSR | S_IWUSR);
    if (read_fd == -1) {
        perror("open");
        return(false);
    }
    
    struct stat st = { 0 };
    fstat(read_fd, &st);
    
    u64 file_size = st.st_size;
    
    char *disk = mmap(0, file_size, PROT_READ, MAP_PRIVATE, read_fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        return(false);
    }
    
    u32 read_offset = 0;
    
    enum bc_fileop_type fileop = OP_CHANNEL_ADD;
    
    while (read_offset < file_size) {
        read_offset += sizeof(enum bc_fileop_type);
        
        struct bc_persist_channel *channel = (struct bc_persist_channel *) (disk + read_offset);
        char fullpath[256] = { 0 };
        
        printf("%d\n", channel->id);
        
        int l = snprintf(fullpath, 256, "data/messages/messages-%d.bchat", channel->id);
        int fd = open(fullpath, O_RDONLY, S_IRUSR | S_IWUSR);
        
        if (fd == -1) {
            perror("open");
            return(false);
        }
        
        struct stat st = { 0 };
        fstat(fd, &st);
        u64 size = st.st_size;
        
        if (!size) {
            read_offset += channel->size;
            continue;
        }
        
        char *records = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
        char *updated_records = malloc(size * 2);
        
        if (!updated_records) {
            perror("malloc");
            return(false);
        }
        
        if (records == MAP_FAILED) {
            perror("mmap");
            return(false);
        }
        
        u32 records_offset = 0;
        u32 write_offset = 0;
        
        while (records_offset < size) {
            struct bc_persist_record_v0 *old = (struct bc_persist_record_v0 *) (records + records_offset);
            
            if (old->size == 0) {
                printf("sdfsdf\n");
                return(false);
            }
            
            if (old->type == WS_ATTACH) {
                struct bc_persist_record_v1 new = { 0 };
                
                new.type = old->type;
                new.size = sizeof(new);
                new.message_id = old->message_id;
                
                new.attach.file_id = old->attach.file_id;
                new.attach.file_ext = 7;
                new.attach.filename_length = 0;
                
                memcpy(updated_records + write_offset, &new, sizeof(new));
                write_offset += sizeof(new);
            } else {
                memcpy(updated_records + write_offset, old, old->size);
                write_offset += old->size;
#if 0
                struct bc_persist_record_v1 new = { 0 };
                
                new.type = old->type;
                new.size = old->size;
                new.message_id = old->message_id;
                
                switch (old->type) {
                    case WS_MESSAGE: {
                        new.message.author_id = old->message.author_id;
                        new.message.timestamp = old->message.timestamp;
                        new.message.length = old->message.length;
                        break;
                    }
                    
                    case WS_EDIT: {
                        new.edit.length = old->edit.length;                        
                        break;
                    }
                    
                    case WS_REPLY: {
                        new.reply.to = old->reply.to;
                        break;
                    }
                    
                    case WS_REACTION_REMOVE: {
                        case WS_REACTION_ADD: {
                            new.reaction.id = old->reaction.id;
                            new.reaction.author_id = old->reaction.author_id;
                            break;
                        }
                    }
                    
                    memcpy(updated_records + write_offset, &new, sizeof(new));
#endif
                }
                
                records_offset += old->size;
            }
            
            snprintf(fullpath, 256, "data/messages/messages-%d_v1.bchat", channel->id);
            int fd_write = open(fullpath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
            
            if (fd_write == -1) {
                perror("open");
                return(false);
            }
            
            if (write(fd_write, updated_records, write_offset) != write_offset) {
                perror("write");
                return(false);
            }
            
            
            read_offset += channel->size;
        }
        
        
        return(true);
    }
}
#endif
#if 0

static bool
migrate_to_st2(char *dir_in, char *dir_out)
{
    if (chdir(dir_in) == -1) {
        perror("chdir dir_in");
        return(false);
    }
    
    (void) dir_out; // TODO: use this
    
    struct bc_server server = { 0 };
    
    if (!queue_init(&server.queue)) return(false);
    if (!st2_init(&server)) return(false);
    
    char *data;
    u64 size;
    u64 offset;
    struct stat st = { 0 };
    int nops = 0;
    
    int fd_users = open("users_old.bchat", O_RDONLY, S_IRUSR | S_IWUSR);
    int fd_channels = open("channels_old.bchat", O_RDONLY, S_IRUSR | S_IWUSR);
    int fd_channelusers = open("channel_users_old.bchat", O_RDONLY, S_IRUSR | S_IWUSR);
    
    //////////////////////////////// Users
    struct bc_persist_user_v0 *old_user = 0;
    struct bc_persist_user new_user = { 0 };
    
    fstat(fd_users, &st);
    size = st.st_size;
    
    
    if (size > 0) {
        data = mmap(0, size, PROT_READ, MAP_PRIVATE, fd_users, 0);
        
        if (data == MAP_FAILED) {
            perror("mmap users");
            return(false);
        }
        
        offset = 0;
        
        while (offset < size) {
            enum bc_fileop_type type;
            memcpy(&type, data + offset, sizeof(type));
            offset += sizeof(type);
            
            if (type == OP_USER_ADD) {
                old_user = (struct bc_persist_user_v0 *) (data + offset);
                
                new_user.id = old_user->id;
                new_user.avatar_id = old_user->avatar_id;
                new_user.blob_block = -1;
                
                memcpy(new_user.password_hash, old_user->password_hash, sizeof(new_user.password_hash));
                
                struct bc_str login = { 0 };
                struct bc_str name = { 0 };
                
                login.data = data + offset + sizeof(struct bc_persist_user_v0);
                login.length = old_user->login_length;
                
                name.data = login.data + login.length;
                name.length = old_user->name_length;
                
                nops = st2_user_add(&server.disk, &server.memory, new_user, login, name);
                
                if (nops == 0) {
                    return(false);
                }
                
                submit_and_wait1(&server.queue, nops);
                
                offset += old_user->size;
            } else if (type == OP_USER_UPDATE) {
                old_user = (struct bc_persist_user_v0 *) (data + offset);
                
                struct bc_persist_user *saved_user = mem_user_find(&server.memory, old_user->id);
                if (!saved_user) {
                    return(false);
                }
                
                saved_user->avatar_id = old_user->avatar_id;
                memcpy(saved_user->password_hash, old_user->password_hash, sizeof(saved_user->password_hash));
                
                nops = st2_user_update(&server.disk, &server.memory, saved_user, 0, 0);
                
                if (nops == 0) {
                    return(false);
                }
                
                submit_and_wait1(&server.queue, nops);
                
                offset += old_user->size;
            } else if (type == OP_USER_REMOVE) {
                u32 user_id = readu32(data + offset);
                // unimplemented
                exit(1);
                offset += sizeof(user_id);
            } else {
                fprintf(stderr, "unexpected file op %d\n", type);
                return(false);
            }
        }
        
        munmap(data, size);
    }
    
    close(fd_users);
    fd_users = -1;
    
    //////////////////////////////// Sesssions
    
    // I'm too lazy lol
    
    //////////////////////////////// Channels
    
    struct bc_persist_channel_v1 *old_channel = 0;
    struct bc_persist_channel new_channel = { 0 };
    
    fstat(fd_channels, &st);
    size = st.st_size;
    
    if (size > 0) {
        data = mmap(0, size, PROT_READ, MAP_PRIVATE, fd_channels, 0);
        
        if (data == MAP_FAILED) {
            perror("mmap channels");
            return(false);
        }
        
        offset = 0;
        
        while (offset < size) {
            enum bc_fileop_type type;
            memcpy(&type, data + offset, sizeof(type));
            offset += sizeof(type);
            
            if (type == OP_CHANNEL_ADD) {
                old_channel = (struct bc_persist_channel_v1 *) (data + offset);
                
                new_channel.id = old_channel->id;
                new_channel.flags = old_channel->flags;
                new_channel.avatar_id = 0;
                
                struct bc_str title = { 0 };
                
                title.data = data + offset + sizeof(struct bc_persist_channel_v1);
                title.length = old_channel->title_length;
                
                nops = st2_channel_add(&server, new_channel, title);
                
                if (nops == 0) {
                    return(false);
                }
                
                submit_and_wait1(&server.queue, nops);
                
                offset += old_channel->size;
            } else {
                fprintf(stderr, "unexpected file op %d\n", type);
                return(false);
            }
        }
        
        munmap(data, size);
    }
    
    close(fd_channels);
    fd_channels = -1;
    
    //////////////////////////////// Channel users
    
    struct bc_persist_channel_user_v0 *old_channeluser = 0;
    struct bc_persist_channel_user new_channeluser = { 0 };
    
    fstat(fd_channelusers, &st);
    size = st.st_size;
    
    if (size > 0) {
        data = mmap(0, size, PROT_READ, MAP_PRIVATE, fd_channelusers, 0);
        
        if (data == MAP_FAILED) {
            perror("mmap channelusers");
            return(false);
        }
        
        int count = size / sizeof(*old_channeluser);
        for (int i = 0; i < count; ++i) {
            old_channeluser = (struct bc_persist_channel_user_v0 *) data + i;
            
            new_channeluser.id = i;
            new_channeluser.user_id = old_channeluser->user_id;
            new_channeluser.channel_id = old_channeluser->channel_id;
            new_channeluser.first_unrecved = old_channeluser->first_unrecved;
            new_channeluser.first_unsent = old_channeluser->first_unsent;
            new_channeluser.first_unseen = old_channeluser->first_unseen;
            
            nops = st2_channeluser_add(server, new_channeluser);
            
            if (nops == 0) {
                return(false);
            }
            
            submit_and_wait1(&server.queue, nops);
        }
        
        munmap(data, size);
    }
    
    close(fd_channelusers);
    fd_channelusers = -1;
    
    //////////////////////////////// Messages
    
    // (just add a header)
    
    int channel_count = buffer_size(server.memory.channels);
    for (int i = 0; i < channel_count; ++i) {
        struct bc_persist_channel *channel = server.memory.channels + i;
        char filename[256] = { 0 }; /* NOTE: max 10 digits for an integer, 11 for negative */
        
        if (snprintf(filename, 256, "messages-old/messages-%d.bchat", channel->id) < 0) {
            log_fperror(__func__, "snprintf");
            continue;
        }
        
        int fd = open(filename, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            perror("open");
            log_ferror(__func__, "Failed to open old message file %s for migration\n", filename);
            continue;
        }
        
        fstat(fd, &st);
        size = st.st_size;
        
        if (size > 0) {
            data = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
            
            if (data == MAP_FAILED) {
                perror("mmap messages");
                return(false);
            }
            
            offset = 0;
            
            if (size >= sizeof(BDISK_MAGIC)) {
                u32 magic = readu32(data);
                if (magic == BDISK_MAGIC) {
                    fprintf(stderr, "%s is a file already in a new format, skipping\n", filename);
                    munmap(data, size);
                    continue;
                }
            }
            
            while (offset < size) {
                struct bc_persist_record *record = (struct bc_persist_record *) (data + offset);
                nops = st2_message_add(&server, channel->id, record);
                submit_and_wait1(&server.queue, nops);
                offset += record->size;
            }
            
            munmap(data, size);
        }
    }
    
    
    return(true);
}
#endif


static bool
migrate_to_threads(char *dir_in)
{
    if (chdir(dir_in) == -1) {
        perror("chdir dir_in");
        return(false);
    }
    
    
    
    int read_fd = open("channels.bchat", O_RDONLY, S_IRUSR | S_IWUSR);
    if (read_fd == -1) {
        perror("Open channels file");
        return(false);
    }
    
    struct stat st = { 0 };
    fstat(read_fd, &st);
    
    u64 file_size = st.st_size;
    
    if (file_size < 0) {
        close(read_fd);
        return(true);
    }
    
    char *disk = mmap(0, file_size, PROT_READ, MAP_PRIVATE, read_fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        return(false);
    }
    
    u32 read_offset = BDISK_HEADER_SIZE + sizeof(u32);
    u32 read_step = sizeof(struct bc_persist_channel) + sizeof(int);
    
    while (read_offset < file_size) {
        
        struct bc_persist_channel *channel = (struct bc_persist_channel *) (disk + read_offset);
        char fullpath[256] = { 0 };
        
        int l = snprintf(fullpath, 256, "old_messages/messages-%d.bchat", channel->id);
        int fd = open(fullpath, O_RDONLY, S_IRUSR | S_IWUSR);
        
        if (fd == -1) {
            perror("Open messages file");
            fprintf(stderr, "%s\n", fullpath);
            read_offset += read_step;
            continue;
        }
        
        struct stat st = { 0 };
        fstat(fd, &st);
        u64 size = st.st_size;
        
        if (!size) {
            read_offset += read_step;
            continue;
        }
        
        char *records = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
        
        if (records == MAP_FAILED) {
            perror("mmap");
            return(false);
        }
        
        char *updated_records = malloc(size * 1.5);
        
        if (!updated_records) {
            perror("malloc");
            return(false);
        }
        
        memcpy(updated_records, records, BDISK_HEADER_SIZE);
        
        u32 records_read_offset, write_offset;
        records_read_offset = write_offset = BDISK_HEADER_SIZE;
        
        u64 const_record_size = sizeof(struct bc_persist_record_v1);
        
        while (records_read_offset < size) {
            struct bc_persist_record_v1 *old = (struct bc_persist_record_v1 *) (records + records_read_offset);
            
            if (old->size == 0) {
                printf("Something went wrong #1\n");
                return(false);
            }
            
            if (old->type == WS_MESSAGE) {
                struct bc_persist_record_v2 new = { 0 };
                
                new.type = old->type;
                new.size = old->size + sizeof(u32);
                new.message_id = old->message_id;
                
                new.message.author_id = old->message.author_id;
                new.message.timestamp = old->message.timestamp;
                new.message.thread_id = random_u31();
                new.message.length = old->message.length;
                
                memcpy(updated_records + write_offset, &new, sizeof(new));
                
                write_offset += sizeof(new);
            } else {
                /* All other records (AT THIS POINT!) fit into "const_record_size" bytes */
                memcpy(updated_records + write_offset, old, const_record_size);
                
                struct bc_persist_record_v2 *new = (struct bc_persist_record_v2 *) (updated_records + write_offset);
                new->size += sizeof(u32);
                
                write_offset += const_record_size + sizeof(u32);
            }
            
            // Write dynamic data
            if (old->size > const_record_size) {
                memcpy(updated_records + write_offset, POINTER_INC(old, const_record_size), old->size - const_record_size);
                write_offset += old->size - const_record_size;
            }
            
            records_read_offset += old->size;
        }
        
        close(fd);
        
        snprintf(fullpath, 256, "messages/messages-%d.bchat", channel->id);
        int fd_write = open(fullpath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd_write == -1) {
            perror("Open new messages file");
            return(false);
        }
        
        if (write(fd_write, updated_records, write_offset) != write_offset) {
            perror("Write new messages file");
            return(false);
        }
        close(fd_write);
        free(updated_records);
        
        read_offset += read_step;
    }
    close(read_fd);
    return(true);
}


int
main(int argc, char **argv)
{
    PAGE_SIZE = 4096;
    SOCKET_TIMEOUT_SECONDS = 10;
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s DIR_IN\n", argv[0]);
        return(1);
    }
    
    if (!migrate_to_threads(argv[1])) {
        fprintf(stderr, "FAIL\n");
        return(1);
    }
    
    printf("SUCCESS\n");
    
    return(0);
}

