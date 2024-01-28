#include "../shared/shared.h"
#include "../websocket/websocket.h"

#include "../shared/aux.c"
#include "../shared/log.c"
#include "../shared/mapping.c"
#include "../shared/buffer.c"
#include "../shared/queue.c"

#include "../media/parse.c"

#include "../websocket/record.c"
#include "../websocket/disk.c"
#include "../websocket/views.c"
#include "../websocket/memory.c"

#include "../websocket/storage.c"

struct user_mapping {
    struct bc_str telegram_userid;
    int user_id;
};

static char base64_encode_table[64] = 
{
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};


static char base64_decode_table[256];

static void
fill_decoding_table(void) {
    for (int i = 0; i < 64; i++) {
        base64_decode_table[(unsigned char) base64_encode_table[i]] = i;
    }
}

/* https://stackoverflow.com/a/6782480/11420590 */
static int
base64_decode(char *result, u8 *input, int length)
{
    int output_length = length / 4 * 3;
    
    if (input[length - 1] == '=') output_length--;
    if (input[length - 2] == '=') output_length--;
    
    for (int i = 0, j = 0; i < length; ) {
        u32 sextet_a = input[i] == '=' ? 0 & i++ : base64_decode_table[input[i++]];
        u32 sextet_b = input[i] == '=' ? 0 & i++ : base64_decode_table[input[i++]];
        u32 sextet_c = input[i] == '=' ? 0 & i++ : base64_decode_table[input[i++]];
        u32 sextet_d = input[i] == '=' ? 0 & i++ : base64_decode_table[input[i++]];
        
        u32 triple 
            = (sextet_a << 3 * 6)
            + (sextet_b << 2 * 6)
            + (sextet_c << 1 * 6)
            + (sextet_d << 0 * 6);
        
        if (j < output_length) result[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < output_length) result[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < output_length) result[j++] = (triple >> 0 * 8) & 0xFF;
    }
    
    return(output_length);
}

static void
import(struct bc_server *server, char *data, u64 size)
{
    struct bc_str input = { .data = data, .length = size };
    struct bc_str channel_title;
    
    channel_title.data = input.data;
    channel_title.length = p_skip_to(&input, '\n');
    p_advance(&input, 1);
    
    printf("Channel title: ");
    prst(channel_title);
    
    struct bc_persist_channel channel = { 0 };
    
    channel.id = 2048574631; //random_u31();
    
    int nops = st2_channel_add(server, channel, channel_title);
    
    if (nops == 0) {
        log_error("Channel not created!\n");
        return;
    }
    
    submit_and_wait1(&server->queue, nops);
    
    printf("Channel_id = %d\n", channel.id);
    
    struct bc_str nusers_str;
    struct bc_str nmessages_str;
    
    nusers_str.data = input.data;
    nusers_str.length = p_skip_to(&input, '\n');
    p_advance(&input, 1);
    
    nmessages_str.data = input.data;
    nmessages_str.length = p_skip_to(&input, '\n');
    p_advance(&input, 1);
    
    int nusers = str_to_u64(nusers_str);
    int nmessages = str_to_u64(nmessages_str);
    
    printf("%d users, %d messages\n", nusers, nmessages);
    
    struct user_mapping *users = malloc(nusers * sizeof(struct user_mapping));
    int user_head = 0;
    
    for (int i = 0; i < nusers; ++i) {
        struct bc_str userid_str;
        struct bc_str username_str;
        
        userid_str.data = input.data;
        userid_str.length = p_skip_to(&input, ' ');
        p_advance(&input, 1);
        
        username_str.data = input.data;
        username_str.length = p_skip_to(&input, '\n');
        p_advance(&input, 1);
        
        printf("Please enter Bullet.Chat id for telegram user ");
        prst(username_str);
        
        int user_id;
        scanf("%d", &user_id);
        
        users[user_head].user_id = user_id;
        users[user_head].telegram_userid = userid_str;
        
        user_head++;
    }
    
    for (int i = 0; i < nusers; ++i) {
        struct user_mapping *um = users + i;
        printf("%.*s -> %d\n", um->telegram_userid.length, um->telegram_userid.data, um->user_id);
    }
    
    char *buf = malloc(64000);
    
    for (int i = 0; i < nmessages; ++i) {
        struct bc_str message_id;
        struct bc_str message_type;
        struct bc_str message_timestamp;
        struct bc_str message_filename;
        struct bc_str message_user;
        struct bc_str message_base64;
        struct bc_str message_ext;
        struct bc_str message_fileid;
        struct bc_str message_reply;
        
        struct bc_persist_record *record = (struct bc_persist_record *) buf;
        memset(record, 0, sizeof(*record));
        
        message_id.data = input.data;
        message_id.length = p_skip_to(&input, ' ');
        p_advance(&input, 1);
        
        message_type.data = input.data;
        message_type.length = p_skip_to(&input, ' ');
        p_advance(&input, 1);
        
        s64 id = str_to_s64(message_id);
        
        if (p_eqlit(message_type, "reply")) {
            message_reply.data = input.data;
            message_reply.length = p_skip_to(&input, '\n');
            p_advance(&input, 1);
            int reply_to = str_to_s64(message_reply);
            
            //printf("REPLY: %ld to %d\n", id, reply_to);
            
            record->type = WS_REPLY;
            record->message_id = id;
            record->reply.to = reply_to;
        } else if (p_eqlit(message_type, "attach")) {
            message_ext.data = input.data;
            message_ext.length = p_skip_to(&input, ' ');
            p_advance(&input, 1);
            
            message_fileid.data = input.data;
            message_fileid.length = p_skip_to(&input, ' ');
            p_advance(&input, 1);
            
            message_filename.data = input.data;
            message_filename.length = p_skip_to(&input, '\n');
            p_advance(&input, 1);
            
            int ext = str_to_u64(message_ext);
            u64 fileid = str_to_u64(message_fileid);
            
            //printf("ATTACH: fileid = %lu, ext = %d, to %ld\n", fileid, ext, id);
            
            record->type = WS_ATTACH;
            record->message_id = id;
            record->attach.file_id = fileid;
            record->attach.file_ext = ext;
            record->attach.filename_length = message_filename.length;
            
            memcpy(buf + sizeof(*record), message_filename.data, message_filename.length);
        } else if (p_eqlit(message_type, "text")) {
            message_timestamp.data = input.data;
            message_timestamp.length = p_skip_to(&input, ' ');
            p_advance(&input, 1);
            
            message_user.data = input.data;
            message_user.length = p_skip_to(&input, ' ');
            p_advance(&input, 1);
            
            message_base64.data = input.data;
            message_base64.length = p_skip_to(&input, '\n');
            p_advance(&input, 1);
            
            int decoded_length = base64_decode(buf + sizeof(*record), message_base64.udata, message_base64.length);
            
            int user_id = -1;
            
            for (int i = 0; i < nusers; ++i) {
                struct user_mapping *um = users + i;
                if (streq(um->telegram_userid, message_user)) {
                    user_id = um->user_id;
                    break;
                }
            }
            
            u64 timestamp = str_to_u64(message_timestamp);
            
            //printf("TEXT: id = %ld, time = %lu, user = %d\n", id, timestamp, user_id);
            
            
            record->type = WS_MESSAGE;
            record->message.author_id = user_id;
            record->message.timestamp = timestamp;
            record->message.length = decoded_length;
            
            printf("%.*s\n", decoded_length, buf + sizeof(*record));
        }
        
        int nops = st2_message_add(server, channel.id, record);
        if (nops == 0) {
            log_ferror(__func__, "Failed to save message!\n");
            return;
        }
        
        submit_and_wait1(&server->queue, nops);
    }
}

int
main(int argc, char **argv)
{
    (void) SOCKET_TIMEOUT_SECONDS;
    PAGE_SIZE = 4096;
    
    char real_data_dir[PATH_MAX] = { 0 };
    char real_input_file[PATH_MAX] = { 0 };
    
    if (!realpath(argv[1], real_data_dir)) {
        log_fperror(__func__, "realpath");
        return(1);
    }
    
    if (!realpath(argv[2], real_input_file)) {
        log_fperror(__func__, "realpath");
        return(1);
    }
    
    if (argc != 3) {
        log_error("Usage: %s data_directory input_file\n", argv[0]);
        return(1);
    }
    
    fill_decoding_table();
    
    struct bc_server server = { 0 };
    
    if (chdir(real_data_dir) == -1) {
        log_fperror(__func__, "chdir");
        return(1);
    }
    
    if (!queue_init(&server.queue)) return(1);
    if (!st2_init(&server)) return(1);
    
    struct bc2_file file = { .filename = real_input_file };
    
    if (!bdisk_open_file(&file)) {
        return(1);
    }
    
    if (file.size == 0) {
        log_info("File is empty\n");
        return(0);
    }
    
    char *data = mmap(0, file.size, PROT_READ, MAP_PRIVATE, file.fd, 0);
    if (data == MAP_FAILED) {
        log_fperror(__func__, "mmap");
        return(1);
    }
    
#if 0
    import(&server, data, file.size);
#endif
    
#if 0
    struct bc_persist_channel_user cu = { 0 };
    
    cu.id = random_u31();
    cu.user_id = 1341685782;
    cu.channel_id = 1863249819;
    
    int nops = st2_channeluser_add(&server, cu);
    submit_and_wait1(&server.queue, nops);
    
    cu.id = random_u31();
    cu.user_id = 1341685782;
    cu.channel_id = 2048574631;
    
    nops = st2_channeluser_add(&server, cu);
    submit_and_wait1(&server.queue, nops);
#endif
    
#if 1
    struct bc_str title;
    struct bc_persist_channel *saved_channel = mem_channel_find(&server.memory, 2048574631);
    
    title.data = "Mossaic Dev";
    title.length = strlen(title.data);
    
    int nops = st2_channel_update(&server.disk, &server.memory, saved_channel, &title);
    
    submit_and_wait1(&server.queue, nops);
    
#endif
    
    return(0);
}
