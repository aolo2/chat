static bool
auth_check_password(struct bc_persist_user *user, struct bc_str password)
{
    if (!user) {
        return(false);
    }
    
    if (password.length > 64) {
        log_warning("Password too long (check)\n");
        return(false);
    }
    
    char password_buf[65] = { 0 };
    memcpy(password_buf, password.data, password.length);
    password_buf[password.length] = 0;
    
    char *hash = crypt(password_buf, user->password_hash); /* hash starts with prefix, options, and salt (!), so the hash can be used as setting for subsequient crypt calls */
    int hash_length = strlen(hash);
    
    if (memcmp(hash, user->password_hash, hash_length) == 0) {
        return(true);
    }
    
    return(false);
}

static bool
auth_write_hash(struct bc_persist_user *user, struct bc_str password)
{
    if (password.length > 64) {
        log_warning("Provided password too long\n");
        return(false);
    }
    
    char password_buf[65] = { 0 };
    memcpy(password_buf, password.data, password.length);
    password_buf[password.length] = 0;
    
    char salt[25] = { 0 };
    unsigned char salt_random[24];
    
    if (getrandom(salt_random, 24, 0) != 24) {
        log_error("Didn't get enough entropy\n");
        return(false);
    }
    
    static char asciibytes[] = { 
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
        'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
        'u', 'v', 'w', 'x', 'y', 'z',
        
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
        'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
        'U', 'V', 'W', 'X', 'Y', 'Z',
        
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
        
        '.', '/'
    };
    
    for (int i = 0; i < 24; ++i) {
        salt[i] = asciibytes[salt_random[i] % 64];
    }
    
    char setting[32] = { 0 };
    snprintf(setting, 32, "$2b$10$%.24s", salt);  /* NOTE(aolo2): bcrypt, 2^10 iterations */
    
    char *hash = crypt(password_buf, setting);
    if (!hash || hash[0] == '*') {
        log_perror("crypt");
        return(false);
    }
    
    int hash_length = strlen(hash);
    
    if (hash_length > PASSWORD_MAX_HASH_LENGTH) {
        log_warning("Crypt returned a passphrase too long\n");
        return(false);
    }
    
    memcpy(user->password_hash, hash, hash_length); /* set hash */
    
    return(true);
}

static u64
auth_generate_session(void)
{
    u64 session_id;
    getrandom(&session_id, 8, 0);
    return(session_id);
}