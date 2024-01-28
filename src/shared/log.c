static bool LOGS_ENABLED = true;
static bool LOGS_PROD_MODE = false;

static void
log_silence(void)
{
    LOGS_ENABLED = false;
}

static void
log_unsilence(void)
{
    LOGS_ENABLED = true;
}

static void
log_time(FILE *restrict stream)
{
    if (LOGS_PROD_MODE) return;
    if (!LOGS_ENABLED) return;
    time_t now = unix_utcnow();
    struct tm *local = localtime(&now);
    fprintf(stream, "[%d.%02d.%02d %02d:%02d:%02d] ", 
            local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
            local->tm_hour, local->tm_min, local->tm_sec);
}

static void
log_perror(const char *text)
{
    if (!LOGS_ENABLED) return;
    fprintf(stderr, "\033[1m\033[31m[ERRNO] ");
    log_time(stderr);
    fprintf(stderr, "%s: %s\033[0m\n", text, strerror(errno));
}

static void 
log_critical_die(const char* format, ...)
{
    if (!LOGS_ENABLED) exit(1);
    va_list argptr;
    va_start(argptr, format);
    fprintf(stderr, "\033[1m\033[31m[CRITICAL] ");
    log_time(stderr);
    vfprintf(stderr, format, argptr);
    fprintf(stderr, "\033[0m");
    va_end(argptr);
    exit(1);
}

static void 
log_error(const char* format, ...)
{
    if (!LOGS_ENABLED) return;
    va_list argptr;
    va_start(argptr, format);
    fprintf(stderr, "\033[1m\033[31m[ERROR] "); 
    log_time(stderr);
    vfprintf(stderr, format, argptr);
    fprintf(stderr, "\033[0m");
    va_end(argptr);
}

static void 
log_ferror(const char *func, const char* format, ...)
{
    if (!LOGS_ENABLED) return;
    va_list argptr;
    va_start(argptr, format);
    fprintf(stderr, "\033[1m\033[31m[ERROR] "); 
    log_time(stderr);
    fprintf(stderr, "%s: ", func);
    vfprintf(stderr, format, argptr);
    fprintf(stderr, "\033[0m");
    va_end(argptr);
}

static void 
log_fwarning(const char *func, const char* format, ...)
{
    if (!LOGS_ENABLED) return;
    va_list argptr;
    va_start(argptr, format);
    fprintf(stderr, "[WARN]  "); 
    log_time(stderr);
    fprintf(stderr, "%s: ", func);
    vfprintf(stderr, format, argptr);
    va_end(argptr);
}

static void 
log_fperror(const char *func, const char *text)
{
    if (!LOGS_ENABLED) return;
    fprintf(stderr, "\033[1m\033[31m[ERRNO] ");
    log_time(stderr);
    fprintf(stderr, "%s: %s: %s\033[0m\n", func, text, strerror(errno));
}

static void 
log_warning(const char* format, ...)
{
    if (!LOGS_ENABLED) return;
    va_list argptr;
    va_start(argptr, format);
    fprintf(stderr, "[WARN]  ");
    log_time(stderr);
    vfprintf(stderr, format, argptr);
    va_end(argptr);
}

static void
log_info(const char* format, ...)
{
    if (!LOGS_ENABLED) return;
    va_list argptr;
    va_start(argptr, format);
    printf("[INFO]  ");
    log_time(stdout);
    vfprintf(stdout, format, argptr);
    va_end(argptr);
}

static void
log_debug(const char* format, ...)
{
    if (!LOGS_ENABLED) return;
#if DEBUG_PRINT
    va_list argptr;
    va_start(argptr, format);
    fprintf(stdout, "[DEBUG] ");
    log_time(stdout);
    vfprintf(stdout, format, argptr);
    va_end(argptr);
#else
    (void) format;
#endif
}
