#define buffer_overhead   (sizeof(s64) + sizeof(struct bc_vm))
#define buffer_vmp(buf)   (struct bc_vm *) (POINTER_DEC(buf, buffer_overhead))
#define buffer_sizep(buf) (s64 *) (POINTER_DEC(buf, sizeof(s64)))
#define buffer_size(buf)  (buf ? *buffer_sizep(buf) : 0)

#define buffer_maybegrow(buf, itemsize) do { \
u64 used = buffer_size(buf) * itemsize + buffer_overhead; \
struct bc_vm *vm = buffer_vmp(buf);               \
if (used + itemsize > vm->commited) {                                \
u64 by = round_up_to_page_size(used + itemsize - vm->commited);     \
buffer_grow((void **) &buf, by);             \
}                                                 \
} while (0)

#define buffer_push(buf, item) do {                       \
buffer_maybegrow(buf, sizeof(item));              \
u64 size__ = buffer_size(buf);                    \
buf[size__] = item;                               \
s64 *psize = buffer_sizep(buf);                   \
(*psize)++;                                       \
} while (0)

#define buffer_push_typeless(buf, data, size) do {        \
buffer_maybegrow(buf, size);                      \
u64 buf_size = buffer_size(buf);                  \
memcpy(POINTER_INC(buf, size * buf_size), data, size);   \
s64 *psize = buffer_sizep(buf);                   \
(*psize)++;                                       \
} while (0)

#define buffer_insert(buf, index, item) do {                                  \
buffer_maybegrow(buf, sizeof(item));                                  \
u64 size = buffer_size(buf);                                          \
memmove(buf + index + 1, buf + index, (size - index) * sizeof(item)); \
buf[index] = item;                                                    \
s64 *psize = buffer_sizep(buf);                                       \
(*psize)++;                                                           \
} while (0)

#define buffer_remove(buf, index) do {                                            \
u64 size = buffer_size(buf);                                              \
memmove(buf + index, buf + index + 1, (size - index - 1) * sizeof(*buf)); \
s64 *psize = buffer_sizep(buf);                                           \
(*psize)--;                                                               \
} while (0)

static void *
buffer_init(u64 max_count, int itemsize)
{
    u64 reserve_size = round_up_to_page_size(max_count * itemsize);
    struct bc_vm vm = mapping_reserve(reserve_size);
    if (!vm.size) return(0);
    if (!mapping_expand(&vm, PAGE_SIZE)) return(0); /* Commit one page immediately to use for metadata */
    
    memcpy(vm.base, &vm, sizeof(vm)); /* vm metadata */
    /* size = 0, so no need to memcpy anything */
    
    return(vm.base + sizeof(vm) + sizeof(s64));
}

// TODO: buffer_initzero (init + commit whole buffer)

static void
buffer_grow(void **data, u64 by)
{
    /* Commit more */
    u64 commit_size = MAX(by, PAGE_SIZE);
    struct bc_vm vm = { 0 };
    memcpy(&vm, POINTER_DEC(*data, buffer_overhead), sizeof(vm));
    if (!mapping_expand(&vm, commit_size)) {
        log_critical_die("Failed to expand buffer mapping. vm.reserve = %lu, vm.commited = %lu, commit_size = %lu\n", 
                         vm.size, vm.commited, commit_size);
        return;
    }
}

static void
buffer_append(char *buf, void *data, s64 size)
{
    s64 used = buffer_size(buf);
    struct bc_vm *vm = buffer_vmp(buf);
    if ((s64) (used + size + buffer_overhead) > (s64) vm->commited) {
        u64 by = round_up_to_page_size(used + size + buffer_overhead - vm->commited);
        buffer_grow((void **) &buf, by);
    }
    
    memcpy(buf + used, data, size);
    
    s64 *pused = buffer_sizep(buf);
    (*pused) += size;
}
static bool
buffer_release(void *buf)
{
    if (!buf) return(true);
    struct bc_vm *vm = buffer_vmp(buf);
    return(mapping_release(vm));
}