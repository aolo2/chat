#include "../shared/shared.h"
#include "../websocket/websocket.h"

//#include "sha1.c"
#include "../shared/aux.c"
#include "../websocket/record.c"

#include "../shared/log.c"
#include "../shared/mapping.c"
#include "../shared/buffer.c"
#include "../shared/queue.c"

#include "../websocket/slot.c"
#include "../websocket/auth.c"
#include "../websocket/disk.c"
#include "../websocket/views.c"
#include "../websocket/memory.c"
#include "../websocket/connection.c"

#include "../websocket/storage.c"

#define __USE_XOPEN_EXTENDED 1
#include <ftw.h> /* nftw */

/*************************************/
/**********   UTILS   ****************/
/*************************************/
static int DBG_test_depth = 0;
static int DBG_tests_ignored = 0;
static char const * DBG_test_group = 0;

#define BEGIN_TEST_GROUP(title) do { \
fprintf(stderr, "Starting test group '%s'\n", title); \
DBG_test_group = title; \
DBG_test_depth += 1; \
} while (0);

#define END_TEST_GROUP() do { \
DBG_test_depth -= 1; \
fprintf(stderr, "\n"); \
} while (0)


#define DO_TEST(title, call) do { \
int test_number = __COUNTER__;\
int rt = call; \
if (rt == 0) { \
if (DBG_test_depth) fprintf(stderr, "%*c", DBG_test_depth * 4, ' '); \
fprintf(stderr, "[%d] %s \033[1m\033[32msuccess\033[0m ...\n", test_number + 1, title); \
} else { \
if (DBG_test_depth) fprintf(stderr, "%*c", DBG_test_depth * 4, ' '); \
fprintf(stderr, "[%d] %s \033[1m\033[31mFAIL\033[0m\n", test_number + 1, title); \
return(1); \
} \
} while(0)

#define IGNORE_TEST(title, call) do { \
int test_number = __COUNTER__;\
DBG_tests_ignored += 1; \
if (DBG_test_depth) fprintf(stderr, "%*c", DBG_test_depth * 4, ' '); \
fprintf(stderr, "[%d] %s \033[1m\033[33mignored\033[0m ...\n", test_number + 1, title); \
} while (0)

#define CHECK(cond, ...) do { if (!(cond)) { log_ferror(__func__, __VA_ARGS__); return 1; } } while (0)

static int 
unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    (void) sb;
    (void) typeflag;
    (void) ftwbuf;
    
    int rv = remove(fpath);
    if (rv) perror(fpath);
    return rv;
}

static int
rmrf(char *path)
{
    int rt = nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
    return(rt);
}

/*************************************/
/********** QUEUE  ****************/
/*************************************/
static int
test_queue_init(struct bc_server *server)
{
    bool success = queue_init(&server->queue);
    return(success ? 0 : 1);
}

/*********************************/
/* Connection */
/*********************************/
static int
test_connection_init(struct bc_server *server)
{
    CHECK(connection_init(server, "7777"), "Failed to init connection subsystem\n");
    
    return(0);
}

static int
test_create_connection(struct bc_server *server)
{
    int client_socket = 333;
    struct bc_connection *connection = connection_create(server, client_socket);
    
    CHECK(connection, "Failed to create fresh connection\n");
    CHECK(connection->socket == client_socket, "Unexpected socket: expected %d, got %d\n", client_socket, connection->socket);
    CHECK(connection->state == CONNECTION_CREATED, "Unexpected connection state: expected %d, got %d\n", CONNECTION_CREATED, connection->state);
    
    return(0);
}

static int
test_get_connection(struct bc_server *server)
{
    int client_socket = 333;
    
    struct bc_connection *get = connection_get(server, client_socket);
    
    CHECK(get, "Connection just created, but not found\n");
    
    return(0);
}

static int
test_authorized_connection(struct bc_server *server)
{
    int client_socket = 333;
    struct bc_connection *get = connection_get(server, client_socket);
    struct bc_persist_session *session = connection_session(server, get);
    
    CHECK(get, "Connection just created, but not found\n");
    CHECK(session == 0, "Connection is authorized, but should not be\n");
    
    return(0);
}

static int
test_remove_connection(struct bc_server *server)
{
    int client_socket = 333;
    bool success = connection_remove(server, client_socket);
    return(success ? 0 : 1);
}

/*********************************/
/* Mapping */
/*********************************/
static int
test_mapping_reserve(void)
{
    struct bc_vm vm = mapping_reserve(MB(1));
    bool success = (vm.base != 0);
    return(success ? 0 : 1);
}

static int
test_mapping_commit(void)
{
    struct bc_vm vm = mapping_reserve(MB(1));
    CHECK(vm.base, "mapping_reserve failed\n");
    bool ok = mapping_commit(&vm, 0, PAGE_SIZE * 2);
    return(ok ? 0 : 1);
}

static int
test_mapping_commit_invalid(void)
{
    struct bc_vm vm = mapping_reserve(KB(1));
    
    CHECK(vm.base, "mapping_reserve failed\n");
    
    log_silence(); // it prints an error
    bool ok = mapping_commit(&vm, 20000, PAGE_SIZE * 2);
    CHECK(!ok, "mapping_commit succeded, but should have failed (the requested commit range is bigger than the initial reserve)\n");
    log_unsilence();
    
    return(!ok ? 0 : 1);
}

static int
test_mapping_expand(void)
{
    struct bc_vm vm = mapping_reserve(MB(1));
    
    CHECK(vm.base, "mapping_reserve failed\n");
    CHECK(mapping_expand(&vm, 4000), "mapping_expand failed\n");
    CHECK(vm.commited == 0x1000, "Unexpected vm.commited value: expected %d, got %d\n", 0x1000, vm.commited);
    
    return(0);
}

static int
test_mapping_decommit(void)
{
    struct bc_vm vm = mapping_reserve(MB(1));
    
    CHECK(vm.base, "mapping_reserve failed\n");
    
    bool ok = mapping_commit(&vm, 0, PAGE_SIZE * 2);
    CHECK(ok, "mapping_commmit failed\n");
    
    bool ok2 = mapping_decommit(&vm, PAGE_SIZE, PAGE_SIZE);
    CHECK(ok2, "mapping_decommmit failed\n");
    
    return(0);
}

static int
test_mapping_release(void)
{
    struct bc_vm vm = mapping_reserve(MB(1));
    CHECK(vm.base, "mapping_reserve failed\n");
    
    bool ok = mapping_release(&vm);
    CHECK(ok, "mapping_release failed\n");
    
    return(0);
}

/*********************************/
/* Buffer */
/*********************************/
static int
test_buffer_init(void)
{
    int *buf = buffer_init(20, sizeof(int));
    
    CHECK(buf, "Failed to create a 10KB buffer\n");
    
    /* This just reserved address space, 100GB should succeed */
    char *buf2 = buffer_init(GB(100), sizeof(int));
    
    CHECK(buf2, "Failed to create a 100GB buffer\n");
    
    return(0);
}

static int
test_buffer_push(void)
{
    /* Make it bigger than one page so that additional commits are triggered */
    int *buf = buffer_init(100000, sizeof(int));
    
    CHECK(buf, "Buffer init returned zero\n");
    
    for (int i = 0; i < 10000; ++i) {
        buffer_push(buf, i);
    }
    
    for (int i = 0; i < 10000; ++i) {
        CHECK(buf[i] == i, "Wrong value in buffer: expected %d, got %d\n", i, buf[i]);
    }
    
    CHECK(buffer_release(buf), "Failed to release buffer mapping\n");
    
    return(0);
}

static int
test_buffer_insert(void)
{
    int *buf = buffer_init(11, sizeof(int));
    
    CHECK(buf, "Buffer init returned zero\n");
    
    for (int i = 0; i < 10; ++i) {
        buffer_push(buf, i);
    }
    
    buffer_insert(buf, 5, 999);
    
    CHECK(buffer_size(buf) == 11, "Wrong buffer size: expected %d, got %d\n", 11, buffer_size(buf));
    
    for (int i = 0; i < buffer_size(buf); ++i) {
        if (i < 5) {
            CHECK(buf[i] == i, "Wrong value in buffer: expected %d, got %d\n", i, buf[i]);
        } else if (i == 5) {
            CHECK(!(buf[i] != 999), "Wrong value in buffer: expected %d, got %d\n", 999, buf[i]);
        } else {
            CHECK(!(buf[i] != i - 1), "Wrong value in buffer: expected %d, got %d\n", i - 1, buf[i]);
        }
    }
    
    return(0);
}

static int
test_buffer_remove(void)
{
    int *buf = buffer_init(10, sizeof(int));
    
    CHECK(!(!buf), "Buffer init returned zero\n");
    
    for (int i = 0; i < 10; ++i) {
        buffer_push(buf, i);
    }
    
    buffer_remove(buf, 5);
    
    CHECK(!(buffer_size(buf) != 9), "Wrong buffer size: expected %d, got %d\n", 9, buffer_size(buf));
    
    for (int i = 0; i < buffer_size(buf); ++i) {
        if (i < 5) {
            CHECK(!(buf[i] != i), "Wrong value in buffer: expected %d, got %d\n", i, buf[i]);
        } else {
            CHECK(!(buf[i] != i + 1), "Wrong value in buffer: expected %d, got %d\n", i + 1, buf[i]);
        }
    }
    
    return(0);
}

static int
test_buffer_push_typeless(void)
{
    int *buf = buffer_init(10, sizeof(int));
    
    buffer_push(buf, 1);
    buffer_push(buf, 2);
    buffer_push(buf, 3);
    
    int four = 4;
    
    buffer_push_typeless(buf, &four, sizeof(four));
    
    CHECK(buffer_size(buf) == 4, "Wrong buffer size: expected %d, got %d\n", 4, buffer_size(buf));
    CHECK(buf[3] == 4, "Wrong value in buffer: expected %d, got %d\n", 4, buf[3]);
    
    return(0);
}

/*********************************/
/* Slots */
/*********************************/
static int
test_slot_init(struct bc_slots *slots)
{
    bool ok = slot_init(slots, 100, KB(4));
    CHECK(ok, "Slot init failed\n");
    return(0);
}

static int
test_slot_usage(struct bc_slots *slots)
{
    /* Reserve 10 slots */
    for (int i = 0; i < 10; ++i) {
        int slot_id = slot_reserve(slots);
        CHECK(slot_id == i, "Unexpected slot id value: expected %d, got %d\n", i, slot_id);
        
        /* Get buffers and write to them */
        struct bc_str buffer = slot_buffer(slots, slot_id);
        memcpy(buffer.data, &i, sizeof(i));
    }
    
    /* Validate data is still there */
    for (int i = 0; i < 10; ++i) {
        struct bc_str buffer = slot_buffer(slots, i);
        int check = 0xfefefefe;
        memcpy(&check, buffer.data, sizeof(check));
        CHECK(check == i, "Unexpected data in slot buffer: expected %d, got %d\n", i, check);
    }
    
    /* Free slot #5 */
    slot_free(slots, 5);
    
    /* Reserve a slot and ensure slot #5 is given back */
    int slot_id = slot_reserve(slots);
    CHECK(slot_id == 5, "Expected to get slot 5 back because it has just been freed, got slot %d instead\n", slot_id);
    
    /* Overbook */
    for (int i = 0; i < 100; ++i) {
        slot_reserve(slots);
    }
    
    slot_id = slot_reserve(slots);
    CHECK(slot_id == -1, "Expected to get slot_id -1 because we must be out of slots by now, got slot id %d instead\n", slot_id);
    
    return(0);
}

/*********************************/
/* Storage */
/*********************************/
static int
test_storage_init(struct bc_server *server)
{
    log_silence();
    bool ok = st2_init(server);
    log_unsilence();
    CHECK(ok, "Storage initialization failed\n");
    server->disk.queue = &server->queue;
    return(0);
}

static int
test_user_add(struct bc2_disk *disk, struct bc2_memory *mem)
{
    int nops = 0;
    struct bc_persist_user *saved_user = 0;
    struct bc_persist_user disk_user = { 0 };
    struct bc2_file *file = 0;
    struct bc2_file *file_strings = 0;
    int block_returned_size = 0;
    
    char strbuf[1024] = { 0 };
    
    struct bc_str login = { 0 };
    struct bc_str name = { 0 };
    struct bc_persist_user user = { 0 };
    
    user.id = 3;
    user.avatar_id = 0xdeadbeef;
    
    login.data = "aolo2";
    login.length = 5;
    
    name.data = "Alexey";
    name.length = 6;
    
    nops = st2_user_add(disk, mem, user, login, name); /* THIS */
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    saved_user = mem_user_find(mem, user.id);
    
    file = bdisk_find_file(disk, BDISK_FILEID_USERS);
    file_strings = bdisk_find_file(disk, BDISK_FILEID_STRINGS);
    
    /* Block ids are set inside mem_ functions, API user doesn't know what are they gonna be */
    user.login_block = saved_user->login_block;
    user.name_block = saved_user->name_block;
    
    /* header + chunk_size + block_id + user struct */
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(user);
    
    CHECK(buffer_size(mem->users) == 1, "Unexpected buffer size: expected %d, got %d\n", 1, buffer_size(mem->users));
    CHECK(saved_user != 0, "Could not find user in memory\n");
    CHECK(memcmp(&user, saved_user, sizeof(user)) == 0, "User in memory is different from original\n");
    CHECK(file != 0, "User file not found\n");
    CHECK(file_strings != 0, "Strings file not found\n");
    CHECK(file->size == expected_file_size, "Unexpected users file size: expected %d, got %d\n", expected_file_size, file->size);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_USERS, user.id, &disk_user);
    CHECK(block_returned_size == sizeof(user), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(user), block_returned_size);
    CHECK(memcmp(&user, &disk_user, sizeof(user)) == 0, "User on disk is different from original\n");
    
    block_returned_size = bdisk_string_read(disk, BDISK_FILEID_STRINGS, user.login_block, strbuf);
    CHECK(block_returned_size == login.length, "Unexpected block size read back from disk: expected %d, got %d\n", login.length, block_returned_size);
    CHECK(memcmp(login.data, strbuf, login.length) == 0, "User login on disk is different from original\n");
    
    block_returned_size = bdisk_string_read(disk, BDISK_FILEID_STRINGS, user.name_block, strbuf);
    CHECK(block_returned_size == name.length, "Unexpected block size read back from disk: expected %d, got %d\n", name.length, block_returned_size);
    CHECK(memcmp(name.data, strbuf, name.length) == 0, "User name on disk is different from original\n");
    
    return(0);
}

static int
test_user_update(struct bc2_disk *disk, struct bc2_memory *mem)
{
    struct bc_persist_user *saved_user = mem_user_find(mem, 3);
    struct bc_persist_user disk_user = { 0 };
    struct bc_str name = { 0 };
    char strbuf[1024] = { 0 };
    int nops = 0;
    int block_returned_size = 0;
    
    struct bc2_file *file = bdisk_find_file(disk, BDISK_FILEID_USERS);
    
    /* header + chunk_size + block_id + user struct */
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_user);
    
    CHECK(saved_user != 0, "User not found in memory\n");
    
    saved_user->avatar_id = 0xdead2222;
    
    name.data = "UpdatedAlexey";
    name.length = 13;
    
    nops = st2_user_update(disk, mem, saved_user, 0, &name); /* TESING THIS */
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops); 
    CHECK(buffer_size(mem->users) == 1, "Unexpected buffer size: expected %d, got %d\n", 1, buffer_size(mem->users));
    CHECK(file->size == expected_file_size, "Unexpected users file size: expected %d, got %d\n", expected_file_size, file->size);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_USERS, saved_user->id, &disk_user);
    
    CHECK(block_returned_size == sizeof(disk_user), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(disk_user), block_returned_size);
    CHECK(memcmp(saved_user, &disk_user, sizeof(disk_user)) == 0, "User on disk is different from original\n");
    
    block_returned_size = bdisk_string_read(disk, BDISK_FILEID_STRINGS, saved_user->name_block, strbuf);
    CHECK(block_returned_size == name.length, "Unexpected block size read back from disk: expected %d, got %d\n", name.length, block_returned_size);
    CHECK(memcmp(name.data, strbuf, name.length) == 0, "User on disk is different from original\n");
    
    return(0);
}

static int
test_user_remove(struct bc2_disk *disk, struct bc2_memory *mem)
{
    struct bc_persist_user *saved_user = mem_user_find(mem, 3);
    struct bc_persist_user disk_user = { 0 };
    char strbuf[1024] = { 0 };
    int nops = 0;
    int block_returned_size = 0;
    int offset = 0;
    int rt = 0;
    int id = -1;
    int saved_id = -1;
    
    struct bc2_file *file = bdisk_find_file(disk, BDISK_FILEID_USERS);
    
    /* header + chunk_size + block_id + user struct */
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_user);
    
    id = saved_user->id;
    saved_id = saved_user->id;
    
    nops = st2_user_remove(disk, mem, id); /* TESING THIS */
    
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    offset = bdisk_fixed_header_size();
    
    CHECK(buffer_size(mem->users) == 0, "Unexpected buffer size: expected %d, got %d\n", 0, buffer_size(mem->users));
    CHECK(mem_user_find(mem, id) == 0, "Deleted used still found in memory storage\n");
    CHECK(file->size == expected_file_size, "Unexpected users file size: expected %d, got %d\n", expected_file_size, file->size);
    
    rt = pread(file->fd, &id, sizeof(id), offset);
    if (rt == -1) {
        log_fperror(__func__, "pread");
        return(1);
    }
    
    CHECK(rt == sizeof(id), "Unexpected return value from pread: expected %d, got %d\n", sizeof(id), rt);
    CHECK(id == -1, "Unexpected ID value on disk: expected -1, got %d\n", id);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_USERS, saved_id, &disk_user);
    CHECK(block_returned_size == 0, "Unexpected block size read back from disk: expected %d, got %d\n", 0, block_returned_size);
    
    block_returned_size = bdisk_string_read(disk, BDISK_FILEID_STRINGS, saved_user->name_block, strbuf);
    CHECK(block_returned_size == 0, "Unexpected block size read back from disk: expected %d, got %d\n", 0, block_returned_size);
    
    block_returned_size = bdisk_string_read(disk, BDISK_FILEID_STRINGS, saved_user->login_block, strbuf);
    CHECK(block_returned_size == 0, "Unexpected block size read back from disk: expected %d, got %d\n", 0, block_returned_size);
    
    return(0);
}

static int
test_session_add(struct bc_server *server)
{
    struct bc2_disk *disk = &server->disk;
    struct bc2_memory *mem = &server->memory;
    
    struct bc_persist_session *saved_session = 0;
    struct bc_persist_session disk_session = { 0 };
    struct bc_persist_session session = { 0 };
    
    struct bc2_file *file = 0;
    int nops = 0;
    int block_returned_size = 0;
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(session);
    
    session.id = 3;
    session.sid = 0x0123456789ABCDEFULL;
    session.user_id = 2;
    
    nops = st2_session_add(server, session); /* TESING THIS */
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    saved_session = mem_session_find(mem, session.sid);
    file = bdisk_find_file(disk, BDISK_FILEID_SESSIONS);
    
    CHECK(buffer_size(mem->sessions) == 1, "Wrong buffer size: expected %d, got %d\n", 1, buffer_size(mem->sessions));
    CHECK(saved_session != 0, "Could not find user in memory\n");
    CHECK(memcmp(&session, saved_session, sizeof(session)) == 0, "Session in memory is different from original\n");
    CHECK(file != 0, "Session file not found\n");
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_SESSIONS, session.id, &disk_session);
    
    CHECK(block_returned_size == sizeof(session), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(session), block_returned_size);
    CHECK(memcmp(&session, &disk_session, sizeof(session)) == 0, "Session on disk is different from original\n");
    
    return(0);
}

static int
test_session_update(struct bc2_disk *disk, struct bc2_memory *mem)
{
    struct bc_persist_session *saved_session = mem_session_find(mem,  0x0123456789ABCDEFULL);
    struct bc_persist_session disk_session = { 0 };
    
    struct bc2_file *file = bdisk_find_file(disk, BDISK_FILEID_SESSIONS);
    int nops = 0;
    int block_returned_size = 0;
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_session);
    
    CHECK(file != 0, "Session file not found\n");
    CHECK(saved_session != 0, "Session not found in memory\n");
    
    saved_session->user_id = 333;
    nops = st2_session_update(disk, saved_session); /* TESING THIS */
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    
    CHECK(buffer_size(mem->sessions) == 1, "Wrong buffer size: expected %d, got %d\n", 1, buffer_size(mem->sessions));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_SESSIONS, saved_session->id, &disk_session);
    
    CHECK(block_returned_size == sizeof(disk_session), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(disk_session), block_returned_size);
    CHECK(memcmp(saved_session, &disk_session, sizeof(disk_session)) == 0, "Session on disk is different from original\n");
    
    return(0);
}

static int
test_session_remove(struct bc2_disk *disk, struct bc2_memory *mem)
{
    struct bc_persist_session *saved_session = mem_session_find(mem,  0x0123456789ABCDEFULL);
    struct bc_persist_session disk_session = { 0 };
    
    struct bc2_file *file = bdisk_find_file(disk, BDISK_FILEID_SESSIONS);
    int nops = 0;
    int block_returned_size = 0;
    int id = -1;
    int rt = 0;
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_session);
    int offset = 0;
    int saved_id = -1;
    
    CHECK(file != 0, "Session file not found\n");
    CHECK(saved_session != 0, "Session not found in memory\n");
    
    saved_id = saved_session->id;
    nops = st2_session_remove(disk, mem, saved_session->sid); /* TESING THIS */
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    
    CHECK(buffer_size(mem->sessions) == 0, "Wrong buffer size: expected %d, got %d\n", 0, buffer_size(mem->sessions));
    CHECK(mem_session_find(mem, saved_session->sid) == 0, "Deleted session still found in memory storage\n");
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    
    offset = bdisk_fixed_header_size();
    
    rt = pread(file->fd, &id, sizeof(id), offset);
    
    if (rt == -1) {
        log_fperror(__func__, "pread");
        return(1);
    }
    
    CHECK(rt == sizeof(id), "Unexpected return value from pread: expected %d, got %d\n", sizeof(id), rt);
    CHECK(id == -1, "Unexpected ID value on disk: expected -1, got %d\n", id);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_SESSIONS, saved_id, &disk_session);
    
    CHECK(block_returned_size == 0, "Unexpected block size read back from disk: expected %d, got %d\n", 0, block_returned_size);
    
    return(0);
}

static int
test_channel_add(struct bc_server *server)
{
    struct bc2_disk *disk = &server->disk;
    struct bc2_memory *mem = &server->memory;
    
    struct bc_persist_channel *saved_channel = 0;
    struct bc_persist_channel disk_channel = { 0 };
    struct bc_persist_channel channel = { 0 };
    struct bc_str title = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(channel);
    int block_returned_size = 0;
    int nops = 0;
    
    char strbuf[1024] = { 0 };
    
    channel.id = 3;
    channel.flags = 0;
    channel.avatar_id = 0x01012323454556;
    
    title.data = "#test";
    title.length = 5;
    
    nops = st2_channel_add(server, channel, title); /* TESING THIS */
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    saved_channel = mem_channel_find(mem, channel.id);
    file = bdisk_find_file(disk, BDISK_FILEID_CHANNELS);
    
    CHECK(file != 0, "User file not found\n");
    CHECK(saved_channel != 0, "Could not find channel in memory\n");
    
    /* Block ids are set inside mem_ functions, API channel doesn't know what are they gonna be */
    channel.title_block = saved_channel->title_block;
    
    CHECK(buffer_size(mem->channels) == 1, "Wrong buffer size: expected %d, got %d\n", 1, buffer_size(mem->channels));
    CHECK(memcmp(&channel, saved_channel, sizeof(channel)) == 0, "Channel in memory is different from original\n");
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_CHANNELS, channel.id, &disk_channel);
    CHECK(block_returned_size == sizeof(channel), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(channel), block_returned_size);
    CHECK(memcmp(&channel, &disk_channel, sizeof(channel)) == 0, "Channel on disk is different from original\n");
    
    block_returned_size = bdisk_string_read(disk, BDISK_FILEID_STRINGS, channel.title_block, strbuf);
    CHECK(block_returned_size == title.length, "Unexpected block size read back from disk: expected %d, got %d\n", title.length, block_returned_size);
    CHECK(memcmp(title.data, strbuf, title.length) == 0, "Channel title on disk is different from original\n");
    
    return(0);
}

static int
test_channel_update(struct bc2_disk *disk, struct bc2_memory *mem)
{
    struct bc_persist_channel *saved_channel = 0;
    struct bc_persist_channel disk_channel = { 0 };
    struct bc_str title = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_channel);
    int block_returned_size = 0;
    int nops = 0;
    
    char strbuf[1024] = { 0 };
    
    saved_channel = mem_channel_find(mem, 3);
    file = bdisk_find_file(disk, BDISK_FILEID_CHANNELS);
    
    CHECK(file != 0, "User file not found\n");
    CHECK(saved_channel != 0, "Could not find channel in memory\n");
    
    saved_channel->avatar_id = 0xdead00005;
    
    title.data = "#renamed";
    title.length = 7;
    
    nops = st2_channel_update(disk, mem, saved_channel, &title);
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    
    CHECK(buffer_size(mem->channels) == 1, "Wrong buffer size: expected %d, got %d\n", 1, buffer_size(mem->channels));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_CHANNELS, saved_channel->id, &disk_channel);
    CHECK(block_returned_size == sizeof(disk_channel), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(disk_channel), block_returned_size);
    CHECK(memcmp(saved_channel, &disk_channel, sizeof(disk_channel)) == 0, "Channel on disk is different from original\n");
    
    block_returned_size = bdisk_string_read(disk, BDISK_FILEID_STRINGS, saved_channel->title_block, strbuf);
    CHECK(block_returned_size == title.length, "Unexpected block size read back from disk: expected %d, got %d\n", title.length, block_returned_size);
    CHECK(memcmp(title.data, strbuf, title.length) == 0, "Channel title on disk is different from original\n");
    
    return(0);
}


static int
test_channel_remove(struct bc2_disk *disk, struct bc2_memory *mem)
{
    struct bc_persist_channel *saved_channel = 0;
    struct bc_persist_channel disk_channel = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_channel);
    int block_returned_size = 0;
    int nops = 0;
    int id = 3;
    int rt = -1;
    int offset = 0;
    
    char strbuf[1024] = { 0 };
    
    saved_channel = mem_channel_find(mem, 3);
    file = bdisk_find_file(disk, BDISK_FILEID_CHANNELS);
    
    CHECK(file != 0, "Channel file not found\n");
    CHECK(saved_channel != 0, "Could not find channel in memory\n");
    
    nops = st2_channel_remove(disk, mem, id);
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    
    CHECK(buffer_size(mem->channels) == 0, "Wrong buffer size: expected %d, got %d\n", 0, buffer_size(mem->channels));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    
    offset = bdisk_fixed_header_size();
    rt = pread(file->fd, &id, sizeof(id), offset);
    
    if (rt == -1) {
        log_fperror(__func__, "pread");
        return(1);
    }
    
    CHECK(rt == sizeof(id), "Unexpected return value from pread: expected %d, got %d\n", sizeof(id), rt);
    CHECK(id == -1, "Unexpected ID value on disk: expected -1, got %d\n", id);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_CHANNELS, saved_channel->id, &disk_channel);
    CHECK(block_returned_size == 0, "Unexpected block size read back from disk: expected %d, got %d\n", 0, block_returned_size);
    block_returned_size = bdisk_string_read(disk, BDISK_FILEID_STRINGS, saved_channel->title_block, strbuf);
    CHECK(block_returned_size == 0, "Unexpected block size read back from disk: expected %d, got %d\n", 0, block_returned_size);
    
    return(0);
}

static int
test_channeluser_add(struct bc_server *server)
{
    struct bc_persist_channel_user *saved_channel_user = 0;
    struct bc_persist_channel_user disk_channel_user = { 0 };
    struct bc_persist_channel_user channel_user = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(channel_user);
    int block_returned_size = 0;
    int nops = 0;
    
    channel_user.id = 125;
    channel_user.user_id = 5;
    channel_user.channel_id = 6;
    channel_user.first_unseen = 103;
    
    nops = st2_channeluser_add(server, channel_user);
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(&server->queue, nops);
    saved_channel_user = mem_channeluser_find(&server->memory, channel_user.user_id, channel_user.channel_id);
    file = bdisk_find_file(&server->disk, BDISK_FILEID_CHANNELUSERS);
    
    CHECK(file != 0, "Channel user file not found\n");
    CHECK(saved_channel_user != 0, "Could not find channel user in memory\n");
    CHECK(buffer_size(server->memory.channel_users) == 1, "Wrong buffer size: expected %d, got %d\n", 1, buffer_size(server->memory.channel_users));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    CHECK(memcmp(&channel_user, saved_channel_user, sizeof(channel_user)) == 0,  "Channel user in memory is different from original\n");
    
    block_returned_size = bdisk_fixed_read(&server->disk, BDISK_FILEID_CHANNELUSERS, channel_user.id, &disk_channel_user);
    
    CHECK(block_returned_size == sizeof(channel_user), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(channel_user), block_returned_size);
    CHECK(memcmp(&channel_user, &disk_channel_user, sizeof(channel_user)) == 0, "Channel user on disk is different from original\n");
    
    return(0);
}

static int
test_channeluser_update(struct bc2_disk *disk, struct bc2_memory *mem)
{
    struct bc_persist_channel_user *saved_channel_user = 0;
    struct bc_persist_channel_user disk_channel_user = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_channel_user);
    int block_returned_size = 0;
    int nops = 0;
    
    saved_channel_user = mem_channeluser_find(mem, 5, 6);
    file = bdisk_find_file(disk, BDISK_FILEID_CHANNELUSERS);
    
    CHECK(file != 0, "Channel user file not found\n");
    CHECK(saved_channel_user != 0, "Could not find channel user in memory\n");
    
    saved_channel_user->first_unseen = 332;
    
    nops = st2_channeluser_update(disk, saved_channel_user);
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    CHECK(buffer_size(mem->channel_users) == 1, "Wrong buffer size: expected %d, got %d\n", 1, buffer_size(mem->channel_users));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_CHANNELUSERS, saved_channel_user->id, &disk_channel_user);
    
    CHECK(block_returned_size == sizeof(disk_channel_user), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(disk_channel_user), block_returned_size);
    CHECK(memcmp(saved_channel_user, &disk_channel_user, sizeof(disk_channel_user)) == 0, "Channel user on disk is different from original\n");
    
    return(0);
}

static int
test_channeluser_remove(struct bc_server *server)
{
    struct bc_persist_channel_user *saved_channel_user = 0;
    struct bc_persist_channel_user disk_channel_user = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_channel_user);
    int block_returned_size = 0;
    int nops = 0;
    int id = 0;
    int offset = 0;
    int rt = -1;
    int saved_id = -1;
    
    saved_channel_user = mem_channeluser_find(&server->memory, 5, 6);
    file = bdisk_find_file(&server->disk, BDISK_FILEID_CHANNELUSERS);
    
    CHECK(file != 0, "Channel user file not found\n");
    CHECK(saved_channel_user != 0, "Could not find channel user in memory\n");
    
    saved_id = saved_channel_user->id;
    nops = st2_channeluser_remove(server, saved_channel_user->user_id, saved_channel_user->channel_id);
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(&server->queue, nops);
    
    CHECK(buffer_size(server->memory.channel_users) == 0, "Wrong buffer size: expected %d, got %d\n", 0, buffer_size(server->memory.channel_users));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    CHECK(mem_channeluser_find(&server->memory, 5, 6) == 0, "Deleted channel user still found in memory storage\n");
    
    offset = bdisk_fixed_header_size();
    
    rt = pread(file->fd, &id, sizeof(id), offset);
    
    if (rt == -1) {
        log_fperror(__func__, "pread");
        return(1);
    }
    
    CHECK(rt == sizeof(id), "Unexpected return value from pread: expected %d, got %d\n", sizeof(id), rt);
    CHECK(id == -1, "Unexpected ID value on disk: expected -1, got %d\n", id);
    
    block_returned_size = bdisk_fixed_read(&server->disk, BDISK_FILEID_CHANNELUSERS, saved_id, &disk_channel_user);
    
    CHECK(block_returned_size == 0, "Unexpected block size read back from disk: expected %d, got %d\n", 0, block_returned_size);
    
    return(0);
}

static int
test_channelsession_add(struct bc_server *server)
{
    struct bc_persist_channel_session *saved_channel_session = 0;
    struct bc_persist_channel_session disk_channel_session = { 0 };
    struct bc_persist_channel_session channel_session = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(channel_session);
    int block_returned_size = 0;
    int nops = 0;
    
    channel_session.id = 125;
    channel_session.session_id = 5;
    channel_session.channel_id = 6;
    channel_session.first_unsent = 101;
    channel_session.first_unrecved = 102;
    
    nops = st2_channelsession_add(server, channel_session);
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(&server->queue, nops);
    saved_channel_session = mem_channelsession_find(&server->memory, channel_session.session_id, channel_session.channel_id);
    file = bdisk_find_file(&server->disk, BDISK_FILEID_CHANNELSESSIONS);
    
    CHECK(file != 0, "Channel session file not found\n");
    CHECK(saved_channel_session != 0, "Could not find channel session in memory\n");
    CHECK(buffer_size(server->memory.channel_sessions) == 1, "Wrong buffer size: expected %d, got %d\n", 1, buffer_size(server->memory.channel_sessions));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    CHECK(memcmp(&channel_session, saved_channel_session, sizeof(channel_session)) == 0,  "Channel session in memory is different from original\n");
    
    block_returned_size = bdisk_fixed_read(&server->disk, BDISK_FILEID_CHANNELSESSIONS, channel_session.id, &disk_channel_session);
    
    CHECK(block_returned_size == sizeof(channel_session), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(channel_session), block_returned_size);
    CHECK(memcmp(&channel_session, &disk_channel_session, sizeof(channel_session)) == 0, "Channel session on disk is different from original\n");
    
    return(0);
}

static int
test_channelsession_update(struct bc_server *server)
{
    struct bc2_disk *disk = &server->disk;
    struct bc2_memory *mem = &server->memory;
    
    struct bc_persist_channel_session *saved_channel_session = 0;
    struct bc_persist_channel_session disk_channel_session = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_channel_session);
    int block_returned_size = 0;
    int nops = 0;
    
    saved_channel_session = mem_channelsession_find(mem, 5, 6);
    file = bdisk_find_file(disk, BDISK_FILEID_CHANNELSESSIONS);
    
    CHECK(file != 0, "Channel session file not found\n");
    CHECK(saved_channel_session != 0, "Could not find channel session in memory\n");
    
    saved_channel_session->first_unsent = 332;
    
    nops = st2_channelsession_update(disk, saved_channel_session);
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(disk->queue, nops);
    CHECK(buffer_size(mem->channel_sessions) == 1, "Wrong buffer size: expected %d, got %d\n", 1, buffer_size(mem->channel_sessions));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    
    block_returned_size = bdisk_fixed_read(disk, BDISK_FILEID_CHANNELSESSIONS, saved_channel_session->id, &disk_channel_session);
    
    CHECK(block_returned_size == sizeof(disk_channel_session), "Unexpected block size read back from disk: expected %d, got %d\n", sizeof(disk_channel_session), block_returned_size);
    CHECK(memcmp(saved_channel_session, &disk_channel_session, sizeof(disk_channel_session)) == 0, "Channel session on disk is different from original\n");
    
    return(0);
}

static int
test_channelsession_remove(struct bc_server *server)
{
    struct bc_persist_channel_session *saved_channel_session = 0;
    struct bc_persist_channel_session disk_channel_session = { 0 };
    struct bc2_file *file = 0;
    
    u32 expected_file_size = BDISK_HEADER_SIZE + sizeof(int) + sizeof(int) + sizeof(disk_channel_session);
    int block_returned_size = 0;
    int nops = 0;
    int id = 0;
    int offset = 0;
    int rt = -1;
    int saved_id = -1;
    
    saved_channel_session = mem_channelsession_find(&server->memory, 5, 6);
    file = bdisk_find_file(&server->disk, BDISK_FILEID_CHANNELSESSIONS);
    
    CHECK(file != 0, "Channel session file not found\n");
    CHECK(saved_channel_session != 0, "Could not find channel session in memory\n");
    
    saved_id = saved_channel_session->id;
    nops = st2_channelsession_remove(server, saved_channel_session->session_id, saved_channel_session->channel_id);
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(&server->queue, nops);
    
    CHECK(buffer_size(server->memory.channel_sessions) == 0, "Wrong buffer size: expected %d, got %d\n", 0, buffer_size(server->memory.channel_sessions));
    CHECK(file->size == expected_file_size, "Unexpected file size: expected %d, got %d\n", expected_file_size, file->size);
    CHECK(mem_channelsession_find(&server->memory, 5, 6) == 0, "Deleted channel session still found in memory storage\n");
    
    offset = bdisk_fixed_header_size();
    
    rt = pread(file->fd, &id, sizeof(id), offset);
    
    if (rt == -1) {
        log_fperror(__func__, "pread");
        return(1);
    }
    
    CHECK(rt == sizeof(id), "Unexpected return value from pread: expected %d, got %d\n", sizeof(id), rt);
    CHECK(id == -1, "Unexpected ID value on disk: expected -1, got %d\n", id);
    
    block_returned_size = bdisk_fixed_read(&server->disk, BDISK_FILEID_CHANNELSESSIONS, saved_id, &disk_channel_session);
    
    CHECK(block_returned_size == 0, "Unexpected block size read back from disk: expected %d, got %d\n", 0, block_returned_size);
    
    return(0);
}

static int
_test_message(struct bc_server *server, int channel_id, struct bc_persist_record *record)
{
    char disk_saved[256];
    char *text = record_data(record);
    
    struct bc_channel_info *channel_info = view_find_channel_info(&server->views, channel_id);
    
    CHECK(channel_info, "Channel info not found for channel %d\n", channel_id);
    
    u64 size_before = 0;
    
    struct bc2_file *file = bdisk_find_file(&server->disk, BDISK_FILEID_MESSAGES_BASE + channel_id);
    
    if (file) {
        size_before = get_file_size(file->fd);
    } else {
        size_before = BDISK_HEADER_SIZE;
    }
    
    u32 nmessages = channel_info->message_count;
    
    log_silence();
    int nops = st2_message_add(server, channel_id, record);
    log_unsilence();
    CHECK(nops != 0, "Disk operation submitted zero requests\n");
    
    submit_and_wait1(&server->queue, nops);
    
    file = bdisk_find_file(&server->disk, BDISK_FILEID_MESSAGES_BASE + channel_id);
    struct bc_persist_record *saved = mem_message_find(server, channel_id, channel_info->message_count - 1);
    
    CHECK(file, "Message file not found for channel %d\n", channel_id);
    CHECK(saved, "Message just added but not found\n");
    CHECK(channel_info->message_count == nmessages + 1, "Unexpected message count in channel info: expected %d got %d\n", nmessages + 1, channel_info->message_count);
    CHECK(memcmp(record, saved, sizeof(*record)) == 0, "Message in memory is different from original\n");
    
    if (record->message.length > 0) {
        CHECK(strncmp(text, record_data(saved), record->message.length) == 0, "Message text in memory is different from the original text\n");
    }
    
    u64 size_after = get_file_size(file->fd);
    
    CHECK(size_after - size_before == record->size, "Unexpected file size increase: expected %d (record->size), got %d\n", record->size, size_after - size_before);
    
    int rt = pread(file->fd, disk_saved, record->size, size_before);
    
    if (rt == -1) {
        log_fperror(__func__, "pread");
        return(1);
    }
    
    CHECK(rt == record->size, "Unexpected return value from pread: expected %d, got %d\n", record->size, rt);
    CHECK(memcmp(record, disk_saved, record->size) == 0, "Record on disk is different from original\n");
    
    return(0);
}

static int
test_message_add(struct bc_server *server)
{
    struct bc2_disk *disk = &server->disk;
    
    struct bc_persist_channel channel = { 0 };
    struct bc_str title = { 0 };
    
    int nops = 0;
    int n_tested = 0;
    
    title.data = "Channel for messages";
    title.length = strlen(title.data);
    
    channel.id = 777;
    
    nops = st2_channel_add(server, channel, title);
    submit_and_wait1(disk->queue, nops);
    
    u64 now = unix_utcnow();
    char text[] = "Hello, this is a text";
    char edit[] = "Bye, this is an edit!";
    
    struct bc_persist_record record;
    
    char fake_connection_buf[1024];
    
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_MESSAGE;
    record.message.author_id = 1;
    record.message.timestamp = now;
    record.message.length = strlen(text);
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_EDIT;
    record.message_id = 123;
    record.edit.length = strlen(edit);
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_DELETE;
    record.message_id = 42;
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_REPLY;
    record.message_id = 84;
    record.reply.to = 42;
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_REACTION_ADD;
    record.message_id = 85;
    record.reaction.id = 2;
    record.reaction.author_id = 5;
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_REACTION_REMOVE;
    record.message_id = 85;
    record.reaction.id = 2;
    record.reaction.author_id = 5;
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_PIN;
    record.message_id = 55;
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_ATTACH;
    record.message_id = 55;
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_USER_LEFT;
    record.leave.user_id = 125;
    record.leave.timestamp = now;
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    ////////////////////
    memset(&record, 0, sizeof(record));
    record.type = WS_USER_JOINED;
    record.join.user_id = 126;
    record.join.timestamp = now;
    memcpy(fake_connection_buf, &record, sizeof(record));
    memcpy(fake_connection_buf + sizeof(record), text, sizeof(text));
    if (_test_message(server, 777, (struct bc_persist_record *) fake_connection_buf)) return(1);
    ++n_tested;
    ////////////////////
    
    CHECK(!(n_tested < WS_RECORD_TYPE_COUNT), "Not all record types tested\n");
    
    return(0);
}

static int
test_storage_multisubmit(struct bc2_disk *disk, struct bc2_memory *mem)
{
    int nusers = 10;
    int nops = 0;
    
    for (int i = 0; i < nusers; ++i) {
        struct bc_persist_user user_i = { 0 };
        struct bc_str login_i = { 0 };
        struct bc_str name_i = { 0 };
        
        user_i.id = (i + 1) * 10;
        user_i.avatar_id = i << 4;
        
        login_i.data = "cycle_user";
        login_i.length = 10;
        
        name_i.data = "User Cyclovich";
        name_i.length = 14;
        
        nops += st2_user_add(disk, mem, user_i, login_i, name_i);
    }
    
    submit_and_wait1(disk->queue, nops);
    bdisk_finalize(disk);
    
    CHECK(bdisk_init(disk), "Disk init failed\n");
    CHECK(buffer_size(mem->users) == nusers, "Wrong buffer size: expected %d, got %d\n", nusers, buffer_size(mem->users));
    
    for (int i = 0; i < nusers; ++i) {
        struct bc_persist_user *saved_user_i = mem_user_find(mem, (i + 1) * 10);
        CHECK(saved_user_i != 0, "Could not find user in memory\n");
        CHECK(saved_user_i->avatar_id == (u64) (i << 4), "Unexpected user avatar_id value: expected %lu, got %lu\n", (u64) (i << 4), saved_user_i->avatar_id);
        
        struct bc_str login_i = mem_block_find(&mem->block_storage, saved_user_i->login_block);
        struct bc_str name_i = mem_block_find(&mem->block_storage, saved_user_i->name_block);
        
        CHECK(login_i.data != 0, "Could not find user login in memory\n");
        CHECK(name_i.data != 0, "Could not find user name in memory\n");
        
        CHECK(memcmp(login_i.data, "cycle_user", login_i.length) == 0, "User login in memory is different from original\n");
        CHECK(memcmp(name_i.data, "User Cyclovich", name_i.length) == 0, "User name in memory is different from original\n");
    }
    
    return(0);
}

static int
test_memory_load(struct bc2_disk *disk)
{
    struct bc2_memory mem2 = { 0 };
    int nusers = 10;
    
    CHECK(mem_init(&mem2), "Memory init failed\n");
    CHECK(mem_load(disk, &mem2), "Memory load failed\n");
    
    CHECK(buffer_size(mem2.users) == nusers, "Wrong users buffer size: expected %d, got %d\n", nusers, buffer_size(mem2.users));
    CHECK(buffer_size(mem2.sessions) == 0, "Wrong sessions buffer size: expected 0, got %d\n", buffer_size(mem2.sessions));
    CHECK(buffer_size(mem2.channels) == 1, "Wrong channels buffer size: expected 1, got %d\n", buffer_size(mem2.channels));
    CHECK(buffer_size(mem2.channel_users) == 0, "Wrong channel users buffer size: expected 0, got %d\n", buffer_size(mem2.channel_users));
    
    struct bc2_file *file_users = bdisk_find_file(disk, BDISK_FILEID_USERS);
    struct bc2_file *file_sessions = bdisk_find_file(disk, BDISK_FILEID_SESSIONS);
    struct bc2_file *file_channels = bdisk_find_file(disk, BDISK_FILEID_CHANNELS);
    struct bc2_file *file_channelusers = bdisk_find_file(disk, BDISK_FILEID_CHANNELUSERS);
    struct bc2_file *file_strings = bdisk_find_file(disk, BDISK_FILEID_STRINGS);
    
    CHECK(file_users != 0, "Users file not found\n");
    CHECK(file_sessions != 0, "Sessions file not found\n");
    CHECK(file_channels != 0, "Channels file not found\n");
    CHECK(file_channelusers != 0, "Channel users file not found\n");
    CHECK(file_strings != 0, "Strings file not found\n");
    
    // 10 users from multiadd should have rewrote deleted user
    // Other entities have been added once and deleted once, leaving a single BLOCK_DELETED block id
    // String blocks don't get deleted yet, so we should have 27 strings = old user name + login + updated user name + login + old channel title + updated channel title + autocreated channel title + 20 new users
    CHECK(buffer_size(file_users->blocks) == 10, "Wrong block count for users file: expected 10, got %d\n", buffer_size(file_users->blocks));
    CHECK(buffer_size(file_sessions->blocks) == 1, "Wrong block count for sessions file: expected 1, got %d\n", buffer_size(file_sessions->blocks));
    CHECK(buffer_size(file_channels->blocks) == 1, "Wrong block count for channels file: expected 1, got %d\n", buffer_size(file_channels->blocks));
    CHECK(buffer_size(file_channelusers->blocks) == 1, "Wrong block count for channel users file: expected 1, got %d\n", buffer_size(file_channelusers->blocks));
    CHECK(buffer_size(file_strings->strings) == 27, "Wrong string count for strings file: expected 27, got %d\n", buffer_size(file_strings->strings));
    
    CHECK(file_sessions->blocks[0] == BDISK_DELETED, "Unexpected session block id: expected %d, got %d\n", BDISK_DELETED, file_sessions->blocks[0]);
    CHECK(file_channels->blocks[0] == 777, "Unexpected channel block id: expected %d, got %d\n", 777, file_channels->blocks[0]);
    CHECK(file_channelusers->blocks[0] == BDISK_DELETED, "Unexpected channeluser block id: expected %d, got %d\n", BDISK_DELETED, file_channelusers->blocks[0]);
    
    for (int i = 0; i < nusers; ++i) {
        struct bc_persist_user *saved_user_i = mem_user_find(&mem2, (i + 1) * 10);
        CHECK(saved_user_i != 0, "Could not find user with id = %d\n", (i + 1) * 10);
        
        struct bc_str login_i = mem_block_find(&mem2.block_storage, saved_user_i->login_block);
        struct bc_str name_i = mem_block_find(&mem2.block_storage, saved_user_i->name_block);
        
        CHECK(login_i.data != 0, "Could not find user login in memory\n");
        CHECK(name_i.data != 0, "Could not find user name in memory\n");
        
        CHECK(memcmp(login_i.data, "cycle_user", login_i.length) == 0, "User login in memory is different from original\n");
        CHECK(memcmp(name_i.data, "User Cyclovich", name_i.length) == 0, "User name in memory is different from original\n");
    }
    
    bdisk_finalize(disk);
    
    return(0);
}

static int
test_sorted_views(struct bc_server *server)
{
    struct bc_persist_channel_user channel_user = { 0 };
    int nops = 0;
    
    struct bc_v2i *sorted1 = server->views.users_per_channel;
    struct bc_v2i *sorted2 = server->views.channels_per_user;
    
    int count1 = buffer_size(sorted1);
    int count2 = buffer_size(sorted2);
    
    CHECK(count1 == 0, "Users per channel view has unexpected length: extected %d, got %d\n", 0, count1);
    CHECK(count2 == 0, "Channels per user view has unexpected length: extected %d, got %d\n", 0, count2);
    
    channel_user.user_id = 1;
    channel_user.channel_id = 1;
    
    nops += st2_channeluser_add(server, channel_user);
    
    channel_user.user_id = 2;
    channel_user.channel_id = 1;
    
    nops += st2_channeluser_add(server, channel_user);
    
    channel_user.user_id = 1;
    channel_user.channel_id = 2;
    
    nops += st2_channeluser_add(server, channel_user);
    
    channel_user.user_id = 2;
    channel_user.channel_id = 2;
    
    nops += st2_channeluser_add(server, channel_user);
    
    channel_user.user_id = 4;
    channel_user.channel_id = 4;
    
    nops += st2_channeluser_add(server, channel_user);
    
    channel_user.user_id = 6;
    channel_user.channel_id = 1;
    
    nops += st2_channeluser_add(server, channel_user);
    
    submit_and_wait1(&server->queue, nops);
    
    channel_user.user_id = 2;
    channel_user.channel_id = 2;
    
    bool ok = view_channeluser_remove(&server->views, &channel_user);
    CHECK(ok, "view_channeluser_remove returned false\n");
    
    count1 = buffer_size(sorted1);
    count2 = buffer_size(sorted2);
    
    CHECK(count1 == 5, "Users per channel view has unexpected length: extected %d, got %d\n", 5, count1);
    CHECK(count2 == 5, "Channels per user view has unexpected length: extected %d, got %d\n", 5, count2);
    
    for (int i = 0; i < count1 - 1; ++i) {
        struct bc_v2i item = sorted1[i + 0];
        struct bc_v2i next = sorted1[i + 1];
        
        CHECK(!(item.channel_id == 2 && item.user_id == 2), "Deleted channel user pair still present in users per channel\n");
        CHECK(item.channel_id <= next.channel_id, "'Users per channel' view must be sorted by the channel_id!\n");
    }
    
    for (int i = 0; i < count2 - 1; ++i) {
        struct bc_v2i item = sorted2[i + 0];
        struct bc_v2i next = sorted2[i + 1];
        
        CHECK(!(item.channel_id == 2 && item.user_id == 2), "Deleted channel user pair still present in channels per user\n");
        CHECK(item.user_id <= next.user_id, "'Channels per user' view must be sorted by the user id!\n");
    }
    
    return(0);
}

static int
test_generate_session_id(void)
{
    u64 sid_1 = auth_generate_session();
    u64 sid_2 = auth_generate_session();
    
    CHECK(!(sid_1 == 0 || ((sid_1 & 0xFFFFFFFFULL) == 0)), "Bad session_id: %ul\n", sid_1);
    CHECK(!(sid_2 == 0 || ((sid_2 & 0xFFFFFFFFULL) == 0)), "Bad session_id: %ul\n", sid_2);
    CHECK(!(sid_1 == sid_2), "Got same session ids\n");
    
    return(0);
}

static int
test_hash_password(void)
{
    struct bc_persist_user user1 = { 0 };
    struct bc_persist_user user2 = { 0 };
    struct bc_str password;
    
    char pwd[] = "test-password123";
    
    password.data = pwd;
    password.length = strlen(password.data);
    
    CHECK(auth_write_hash(&user1, password), "Failed to write hash (user 1)\n");
    CHECK(auth_write_hash(&user2, password), "Failed to write hash (user 2)\n");
    CHECK(memcmp(user1.password_hash, user2.password_hash, PASSWORD_MAX_HASH_LENGTH) != 0, "Two hashes of the same password are the same (but MUST be different)\n");
    
    char long_pwd[] = "Loooooooooooooo-o-o-o-oo--o-o-o-o-oo--o"
        "-o-o-o-oo--o-o-o-o-oo--o-o-o-o-oo--o-o-"
        "o-o-oo--o-o-o-o-oo--o-o-o-o-oo--o-o-o-o"
        "-oo--o-o-o-o-oo--o-o-o-o-oo--o";
    
    password.data = long_pwd;
    password.length = strlen(password.data);
    
    log_silence();
    CHECK(!auth_write_hash(&user1, password), "Password is too long, but got hashed anyway\n");
    log_unsilence();
    
    return(0);
}

static int
test_check_password(void)
{
    struct bc_persist_user user1 = { 0 };
    struct bc_persist_user user2 = { 0 };
    struct bc_str password;
    
    char pwd[] = "test-password123";
    
    password.data = pwd;
    password.length = strlen(password.data);
    
    CHECK(auth_write_hash(&user1, password), "Failed to write hash (user 1)\n");
    CHECK(auth_write_hash(&user2, password), "Failed to write hash (user 2)\n");
    CHECK(auth_check_password(&user1, password), "Password is CORRECT, but WAT NOT accepted (user 1)\n");
    CHECK(auth_check_password(&user2, password), "Password is CORRECT, but WAS NOT accepted (user 2)\n");
    
    password.length -= 1;
    
    CHECK(!auth_check_password(&user1, password), "Password is INCORRECT, but WAS accepted (user 1)\n");
    CHECK(!auth_check_password(&user2, password), "Password is INCORRECT, but WAS accepted (user 2)\n");
    
    char long_pwd[] = "Loooooooooooooo-o-o-o-oo--o-o-o-o-oo--o"
        "-o-o-o-oo--o-o-o-o-oo--o-o-o-o-oo--o-o-"
        "o-o-oo--o-o-o-o-oo--o-o-o-o-oo--o-o-o-o"
        "-oo--o-o-o-o-oo--o-o-o-o-oo--o";
    
    password.data = long_pwd;
    password.length = strlen(password.data);
    
    log_silence();
    CHECK(!auth_check_password(&user1, password), "Password is too long, but got hashed anyway\n");
    log_unsilence();
    
    return(0);
}

static int
test_finalize_queue(struct bc_server *server)
{
    // NOTE(aolo2): this is not a test, because it always succeeds, but we want to call queue_finalize anyway, so I wrapped in a "test"
    queue_finalize(&server->queue);
    return(0);
}

static int
do_tests(void)
{
    SOCKET_TIMEOUT_SECONDS = 10;
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    
    struct bc_server server = { 0 };
    struct bc_slots slots = { 0 };
    
    BEGIN_TEST_GROUP("Queue");
    DO_TEST("Queue init", test_queue_init(&server));
    END_TEST_GROUP();
    
    BEGIN_TEST_GROUP("Connections");
    DO_TEST("Connection init", test_connection_init(&server));
    DO_TEST("Connection create", test_create_connection(&server));
    DO_TEST("Connection get", test_get_connection(&server));
    DO_TEST("Connection authorized", test_authorized_connection(&server));
    DO_TEST("Connection remove", test_remove_connection(&server));
    END_TEST_GROUP();
    
    BEGIN_TEST_GROUP("Virtual memory");
    DO_TEST("Reserve range", test_mapping_reserve());
    DO_TEST("Commit valid range", test_mapping_commit());
    DO_TEST("Commit invalid range", test_mapping_commit_invalid());
    DO_TEST("Expand range", test_mapping_expand());
    DO_TEST("Decommit range", test_mapping_decommit());
    DO_TEST("Release range", test_mapping_release());
    END_TEST_GROUP();
    
    BEGIN_TEST_GROUP("Buffer");
    DO_TEST("Create buffer", test_buffer_init());
    DO_TEST("Push regular", test_buffer_push());
    DO_TEST("Insert into middle", test_buffer_insert());
    DO_TEST("Remove from middle", test_buffer_remove());
    DO_TEST("Push typeless", test_buffer_push_typeless());
    END_TEST_GROUP();
    
    BEGIN_TEST_GROUP("Slots");
    DO_TEST("Init slots", test_slot_init(&slots));
    DO_TEST("Reserve and free slots", test_slot_usage(&slots));
    END_TEST_GROUP();
    
    BEGIN_TEST_GROUP("Storage");
    DO_TEST("Init disk and memory", test_storage_init(&server));
    
    DO_TEST("Add user", test_user_add(&server.disk, &server.memory));
    DO_TEST("Update user", test_user_update(&server.disk, &server.memory));
    DO_TEST("Remove user", test_user_remove(&server.disk, &server.memory));
    
    DO_TEST("Session add", test_session_add(&server));
    DO_TEST("Session update", test_session_update(&server.disk, &server.memory));
    DO_TEST("Session remove", test_session_remove(&server.disk, &server.memory));
    
    DO_TEST("Channel add", test_channel_add(&server));
    DO_TEST("Channel update", test_channel_update(&server.disk, &server.memory));
    DO_TEST("Channel remove", test_channel_remove(&server.disk, &server.memory));
    
    DO_TEST("Channel user add", test_channeluser_add(&server));
    DO_TEST("Channel user update", test_channeluser_update(&server.disk, &server.memory));
    DO_TEST("Channel user remove", test_channeluser_remove(&server));
    
    DO_TEST("Channel session add", test_channelsession_add(&server));
    DO_TEST("Channel session update", test_channelsession_update(&server));
    DO_TEST("Channel session remove", test_channelsession_remove(&server));
    
    DO_TEST("Message add", test_message_add(&server));
    
    DO_TEST("Multisubmit", test_storage_multisubmit(&server.disk, &server.memory));
    DO_TEST("Load from disk to memory", test_memory_load(&server.disk));
    DO_TEST("Test views", test_sorted_views(&server));
    END_TEST_GROUP();
    
    BEGIN_TEST_GROUP("Auth");
    DO_TEST("Generate session id", test_generate_session_id());
    DO_TEST("Hash password", test_hash_password());
    DO_TEST("Check password", test_check_password());
    END_TEST_GROUP();
    
    BEGIN_TEST_GROUP("Finalize");
    DO_TEST("Finalize queue", test_finalize_queue(&server));
    END_TEST_GROUP();
    
    return(0);
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        log_error("Usage: %s test_data_dir\n", argv[0]);
        return(1);
    }
    
    char cwd[PATH_MAX] = { 0 };
    if (!getcwd(cwd, sizeof(cwd))) {
        log_perror("getcwd");
        return(1);
    }
    
    char *dir = argv[1];
    if (directory_exists(dir)) {
        log_error("Directory %s already exists. It's possible it contains live data! Stopping now!\n", dir);
        return(1);
    }
    
    if (mkdir(dir, 0700) != 0) {
        log_perror("mkdir");
        return(1);
    }
    
    if (chdir(dir) == -1) {
        log_perror("chdir (1)");
        return(1);
    }
    
    u64 before = msec_now();
    int result = do_tests();
    u64 after = msec_now();
    
    if (chdir(cwd) == -1) {
        log_perror("chdir (2)");
        return(1);
    }
    
    rmrf(dir);
    
    if (result == 0) {
        log_info("\033[1m\033[32mAll tests passed!\033[0m (%ld msec)\n", __COUNTER__, after - before);
        log_info("%d test%s ignored\n", DBG_tests_ignored, DBG_tests_ignored == 1 ? "" : "s");
    }
    
    return(result);
}
