// Temporary/helper functions which don't have a home for now..

static int
readall(int fd, void *buf, int size)
{
    int consumed = 0;
    
    while (consumed < size) {
        int rv = read(fd, POINTER_INC(buf, consumed), size - consumed);
        if (rv < 0) {
            log_perror("[ERROR] read");
            return(-1);
        }
        consumed += rv;
    }
    
    return(consumed);
}

static int
writeall(int fd, void *buf, int size)
{
    int consumed = 0;
    
    while (consumed < size) {
        int rv = write(fd, POINTER_INC(buf, consumed), size - consumed);
        if (rv < 0) {
            log_perror("[ERROR] write");
            return(-1);
        }
        consumed += rv;
    }
    
    return(consumed);
}

static void
prst(struct bc_str str)
{
    printf("%.*s\n", str.length, str.data);
}

#if 0
static void
hex_dump(struct bc_str str)
{
    for (int i = 0; i < str.length; ++i) {
        if (i > 0) {
            printf(" %hhx", str.data[i]);
        } else {
            printf("%hhx", str.data[i]);
        }
    }
    printf("\n");
}
#endif

static bool
streq(struct bc_str a, struct bc_str b)
{
    if (a.length != b.length) {
        return(false);
    }
    
    return(strncmp(a.data, b.data, a.length) == 0);
}

static u64
msec_now(void)
{
    struct timespec ts = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return(ts.tv_nsec / 1000000ULL + ts.tv_sec * 1000ULL);
}

static bool
little_endian(void)
{
    u32 v = 1;
    char vc[4];
    
    memcpy(vc, &v, 4);
    
    if (vc[0] == 1) {
        return(true);
    }
    
    return(false);
}

static bool
directory_exists(const char *dir)
{
    struct stat ds;
    
    if (stat(dir, &ds) == 0 && S_ISDIR(ds.st_mode)) {
        return(true);
    }
    
    return(false);
}

static u32
random_u31(void)
{
    u32 r = 0;
    
    if (getrandom(&r, 4, 0) != 4) {
        log_error("Didn't get enough entropy\n");
        return(0);
    }
    
    return(r & 0x7FFFFFFF);
}

static u64
round_up_to_page_size(u64 size)
{
    if (size & (PAGE_SIZE - 1)) {
        size &= ~(PAGE_SIZE - 1);
        size += PAGE_SIZE;
    }
    
    return(size);
}

static u16
readu16(void *at)
{
    u16 result;
    memcpy(&result, at, 2);
    return(result);
}

static u32
readu32(void *at)
{
    u32 result;
    memcpy(&result, at, 4);
    return(result);
}

static s32
reads32(void *at)
{
    s32 result;
    memcpy(&result, at, 4);
    return(result);
}

static u64
readu64(void *at)
{
    u64 result;
    memcpy(&result, at, 8);
    return(result);
}

static u64
unix_utcnow(void)
{
    struct timeval tv = { 0 };
    gettimeofday(&tv, NULL); /* tz = NULL means UTC */ 
    return(tv.tv_sec);
}

static u64
get_file_size(int fd)
{
    struct stat st = { 0 };
    fstat(fd, &st);
    return(st.st_size);
}

static bool
fd_valid(int fd)
{
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

static struct bc_str
str_from_literal(char *literal)
{
    struct bc_str result = { 0 };

    result.data = literal;
    result.length = strlen(literal);

    return(result);
}
