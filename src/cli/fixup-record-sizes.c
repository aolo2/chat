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
#include "../websocket/storage.c"

static bool
fixup_record_sizes(char *dir)
{
    if (chdir(dir) == -1) {
        perror("chdir dir");
        return(false);
    }
    
    struct bc_server server = { 0 };
    
    if (!queue_init(&server.queue)) return(false);
    if (!st2_init(&server)) return(false);
    
    char *data;
    u64 size;
    u64 read_offset;
    struct stat st = { 0 };
    int nops = 0;
    
    int channel_count = buffer_size(server.memory.channels);
    char *messages_arena = malloc(10000000);
    int write_head = 0;
    
    if (!messages_arena) {
        perror("malloc");
        return(false);
    }
    
    for (int i = 0; i < channel_count; ++i) {
        struct bc_persist_channel *channel = server.memory.channels + i;
        
        char filename[256] = { 0 }; /* NOTE: max 10 digits for an integer, 11 for negative */
        
        printf("==== Channel #%d, id = %d\n", i, channel->id);
        
        if (snprintf(filename, 256, "messages/messages-%d.bchat", channel->id) < 0) {
            log_fperror(__func__, "snprintf");
            continue;
        }
        
        int fd = open(filename, O_RDONLY, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            perror("open");
            log_ferror(__func__, "Failed to open old message file %s\n", filename);
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
            
            memcpy(messages_arena, data, BDISK_HEADER_SIZE);
            
            write_head = BDISK_HEADER_SIZE;
            read_offset = BDISK_HEADER_SIZE;
            
            while (read_offset < size) {
                struct bc_persist_record *record = (struct bc_persist_record *) (data + read_offset);
                
                if (record->type >= WS_RECORD_TYPE_COUNT) {
                    __builtin_trap();
                }
                
                if (record->size == 0) {
                    __builtin_trap();
                    break;
                }
                
                if (record->type != WS_ATTACH) {
                    memcpy(messages_arena + write_head, record, record->size);
                    read_offset += record->size;
                    write_head += record->size;
                    continue;
                }
                
                char *filename_maybe = record_data(record);
                
                bool is_image_probably = false;
                
                for (int j = 0; j < record->attach.filename_length - 4; ++j) {
                    char *c = filename_maybe + j;
                    
                    if (strncmp(c, ".jpeg", 5) == 0) {
                        is_image_probably = true;
                        break;
                    } else if (strncmp(c, ".png", 4) == 0) {
                        is_image_probably = true;
                        break;
                    } else if (strncmp(c, ".jpg", 4) == 0) {
                        is_image_probably = true;
                        break;
                    }
                }
                
                if (is_image_probably) {
                    printf("%.*s\n", record->attach.filename_length, record_data(record));
                }
                
                if (record_attach_ext_is_supported_image(record->attach.file_ext)) {
                    read_offset += record->size;
                    record->size = sizeof(*record);
                    memcpy(messages_arena + write_head, record, sizeof(*record));
                    write_head += sizeof(*record);
                } else {
                    read_offset += record->size;
                    memcpy(messages_arena + write_head, record, record->size);
                    write_head += record->size;
                }
            }
            
            memset(filename, 0, 256);
            
            if (snprintf(filename, 256, "messages/messages-%d.bchat.updated", channel->id) < 0) {
                log_fperror(__func__, "snprintf");
                continue;
            }
            
            int write_fd = open(filename, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
            
            //int written = write(write_fd, messages_arena, write_head);
            int written = write_head;
            
            if (write_fd == -1) {
                log_perror("open updated file\n");
                return(false);
            }
            
            if (written == -1) {
                log_perror("write\n");
                return(false);
            }
            
            if (written != write_head) {
                log_error("short write\n");
                return(false);
            }
            
            munmap(data, size);
        }
    }
    
    return(true);
}

int
main(int argc, char **argv)
{
    PAGE_SIZE = 4096;
    SOCKET_TIMEOUT_SECONDS = 10;
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s DATA_DIR\n", argv[0]);
        return(1);
    }
    
    if (!fixup_record_sizes(argv[1])) {
        fprintf(stderr, "FAIL\n");
        return(1);
    }
    
    printf("SUCCESS\n");
    
    return(0);
}
