static u16
p_readbe16(u8 *at)
{
    u16 low = at[0];
    u16 high = at[1];
    u16 result = (low << 8) | high;
    return(result);
}

#if 0
static u32
p_readbe32(u8 *at)
{
    u32 result;
    memcpy(&result, at, 4);
    result = be32toh(result);
    return(result);
}
#endif

static void
p_writebe16(u16 value, u8 *dest)
{
    u8 low = (value & 0xFF00) >> 8;
    u8 high = value & 0xFF;
    dest[0] = low;
    dest[1] = high;
}

static void
p_writebe64(u64 value, u8 *dest)
{
    u64 be = htobe64(value);
    memcpy(dest, &be, 8);
}

#if 0
static bool
p_explit(struct bc_str *str, const char *lit)
{
    int litlen = strlen(lit);
    
    if (str->length < litlen) {
        return(false);
    }
    
    if (strncmp(str->data, lit, litlen) == 0) {
        str->data += litlen;
        str->length -= litlen;
        return(true);
    }
    
    return(false);
}

static bool
p_eqlit(struct bc_str str, const char *lit)
{
    int litlen = strlen(lit);
    
    if (str.length < litlen) {
        return(false);
    }
    
    if (strncmp(str.data, lit, litlen) == 0) {
        return(true);
    }
    
    return(false);
}
#endif


static bool
p_expect(struct bc_str *str, char sym)
{
    if (str->length == 0 || str->data[0] != sym) {
        return(false);
    }
    
    str->data += 1;
    str->length -= 1;
    
    return(true);
}

static int
p_skip_to(struct bc_str *str, char sym)
{
    int result = 0;
    
    while (str->data[0] != sym && str->length > 0) {
        str->data += 1;
        str->length -= 1;
        ++result;
    }
    
    if (str->data[0] != sym) {
        return(-1);
    }
    
    return(result);
}

static bool
p_advance(struct bc_str *str, int len)
{
    if (str->length < len) {
        return(false);
    }
    
    str->data += len;
    str->length -= len;
    
    return(true);
}

static struct bc_websocket_frame
parse_websocket_frame(struct bc_str str)
{
    struct bc_websocket_frame result = { 0 };
    
    //hex_dump(str);
    
    if (str.length < 2) {
        return(result);
    }
    
    result.fin = str.data[0] & 0x80;
    result.opcode = str.data[0] & 0xF;
    result.mask = str.data[1] & 0x80;
    result.payload.length = str.data[1] & 0x7F;
    
    int next = 2;
    
    if (result.payload.length == 126 && str.length >= 4) {
        result.payload.length = p_readbe16((u8 *) (str.data + 2));
        next = 4;
    } else if (result.payload.length == 127 && str.length >= 10) {
        log_warning("We do not handle websocket frames > 64K\n");
        return(result);
        //__builtin_trap();
        //result.payload.length = *(u64 *) (str.data + 2);
        //next = 10;
    }
    
    if (result.mask) {
        if (str.length < next + 4) {
            return(result);
        }
        
        result.mask_key[0] = str.data[next + 0];
        result.mask_key[1] = str.data[next + 1];
        result.mask_key[2] = str.data[next + 2];
        result.mask_key[3] = str.data[next + 3];
        next += 4;
    }
    
    result.payload.data = str.data + next;
    
    if (str.length < next + result.payload.length) {
        return(result);
    }
    
    /* all client->server frames must be masked as per RFC, but for our test client we allow non-masked frames */
    if (result.mask) {
        for (int i = 0; i < result.payload.length; ++i) {
            result.payload.data[i] = result.payload.data[i] ^ result.mask_key[i % 4];
        }
    }
    
    result.complete = true;
    result.raw_length = next + result.payload.length;
    
    return(result);
}

static struct bc_http_request
parse_http_request(struct bc_str input)
{
    struct bc_http_request result = { 0 };
    struct bc_str parse = input;
    
    struct bc_http_headers *headers = &result.headers;
    
    /* request line ignored */
    
    /* headers */
    for (;;) {
        /* skip to and after clrf */
        if (p_skip_to(&parse, '\r') == -1) break;
        if (p_skip_to(&parse, '\n') == -1) break;
        if (!p_advance(&parse, 1)) break;
        
        /* another clrf? header is over */
        if (parse.length >= 2 && parse.data[0] == '\r' && parse.data[1] == '\n') {
            p_advance(&parse, 2);
            result.complete = true;
            result.raw_length = input.length - parse.length;
            break;
        }
        
        headers->keys[headers->nheaders].data = parse.data;
        
        /* skip to ':' */
        headers->keys[headers->nheaders].length = p_skip_to(&parse, ':');
        if (headers->keys[headers->nheaders].length == -1) {
            break;
        }
        
        /* skip ':' and space */
        if (!p_expect(&parse, ':')) break;
        if (!p_expect(&parse, ' ')) break;
        
        headers->values[headers->nheaders].data = parse.data;
        headers->values[headers->nheaders].length = p_skip_to(&parse, '\r');
        if (headers->values[headers->nheaders].length == -1) {
            break;
        }
        
        ++headers->nheaders;
    }
    
    return(result);
}