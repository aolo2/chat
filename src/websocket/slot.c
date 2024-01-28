static int
slot_reserve(struct bc_slots *slots)
{
    for (int i = 0; i < slots->count; ++i) {
        struct bc_slot *slot = slots->occupancy + i;
        if (!slot->taken) {
            if (!slot->commited) {
                if (!mapping_commit(&slots->vm, i * slots->slot_size, slots->slot_size)) {
                    return(-1);
                }
                
                slot->commited = true;
            }
            
            slot->taken = true;
            
            return(i);
        }
    }
    
    return(-1);
}

static struct bc_str
slot_buffer(struct bc_slots *slots, int slot_id)
{
    struct bc_str result = { 0 };
    
    result.data = (char *) slots->vm.base + slot_id * slots->slot_size;
    result.length = slots->slot_size;
    
    return(result);
}

static void
slot_free(struct bc_slots *slots, int slot_id)
{
    slots->occupancy[slot_id].taken = false;
}

static bool
slot_init(struct bc_slots *slots, int nslots, int slot_size)
{
    u64 size = round_up_to_page_size(nslots * slot_size);
    slots->vm = mapping_reserve(size);
    
    if (!slots->vm.size) {
        log_ferror(__func__, "Failed to create slot mapping\n");
        return(false);
    }
    
    slots->occupancy = buffer_init(nslots, sizeof(struct bc_slot));
    slots->slot_size = slot_size;
    slots->count = nslots;
    
    for (int i = 0; i < nslots; ++i) {
        struct bc_slot slot = { 0 };
        buffer_push(slots->occupancy, slot);
    }
    
    return(true);
}