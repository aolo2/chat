static struct bc_vm
mapping_reserve(u64 size)
{
    struct bc_vm result = { 0 };
    
    u8 *region = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); /* PROT_NONE + MAP_ANONYMOUS = do not commit */
    
    if (region == MAP_FAILED) {
        log_perror("mmap (reserve)");
        return(result);
    }
    
    result.base = region;
    result.size = size;
    result.commited = 0;
    
    return(result);
}

static bool
mapping_commit(struct bc_vm *vm, u64 offset, u64 size)
{
    if (offset + size > vm->size) {
        log_ferror(__func__, "Mapping commit failed offset (%d) + commit_size (%d) > reserve_size (%d)\n", offset, size, vm->size);
        return(false);
    }
    
    if (mprotect(vm->base + offset, size, PROT_READ | PROT_WRITE)) {
        log_perror("mprotect (commit)");
        return(false);
    }
    
    return(true);
}

static bool
mapping_decommit(struct bc_vm *vm, u64 offset, u64 size)
{
    if (offset + size > vm->size) {
        return(false);
    }
    
    if (mprotect(vm->base + offset, size, PROT_NONE)) {
        log_perror("mprotect (decommit)");
        return(false);
    }
    
    return(true);
}

static bool
mapping_release(struct bc_vm *vm)
{
    if (munmap(vm->base, vm->size)) {
        log_perror("munmap (release)");
        return(false);
    }
    
    return(true);
}

static bool
mapping_expand(struct bc_vm *vm, u64 size)
{
    size = round_up_to_page_size(size);
    
    if (!mapping_commit(vm, vm->commited, size)) {
        return(false);
    }
    
    vm->commited += size;
    
    return(true);
}