static bool
p_eqlit(struct bc_str str, const char *literal)
{
    int literal_length = strlen(literal);
    
    if (str.length != literal_length) {
        return(false);
    }
    
    return(memcmp(str.data, literal, literal_length) == 0);
}

static bool
p_startswith(struct bc_str str, const char *literal)
{
    int literal_length = strlen(literal);
    
    if (str.length < literal_length) {
        return(false);
    }
    
    return(memcmp(str.data, literal, literal_length) == 0);
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

static bool
p_expect_literal(struct bc_str *str, const char *lit)
{
    if (!str->data) {
        return(false);
    }
    
    int litlen = strlen(lit);
    
    if (str->length < litlen) {
        return(false);
    }
    
    if (memcmp(str->data, lit, litlen) == 0) {
        str->data += litlen;
        str->length -= litlen;
        return(true);
    }
    
    return(false);
}

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
p_skip_past(struct bc_str *str, char sym)
{
    if (p_skip_to(str, sym) == -1) return(false);
    if (!p_advance(str, 1)) return(false);
    return(true);
}

static struct bc_str
get_header_value(struct bc_http_request *request, const char *key)
{
    struct bc_http_headers *headers = &request->headers;
    struct bc_str result = { 0 };
    
    for (int i = 0; i < headers->nheaders; ++i) {
        if (strncmp(key, headers->keys[i].data, headers->keys[i].length) == 0) {
            return(headers->values[i]);
        }
    }
    
    return result;
}

static u64
str_to_u64(struct bc_str str)
{
    u64 res = 0;
    
    for (int i = 0; i < str.length; ++i) {
        char c = str.data[i];
        int val = c - '0';
        if (val > 9 || val < 0) {
            break;
        }
        res = res * 10 + val;
    }
    
    return(res);
}

static s64
str_to_s64(struct bc_str str)
{
    s64 res = 0;
    bool negate = false;
    
    for (int i = 0; i < str.length; ++i) {
        char c = str.data[i];
        
        if (c == '-' && i == 0) {
            negate = true;
            continue;
        }
        
        int val = c - '0';
        if (val > 9 || val < 0) {
            break;
        }
        
        res = res * 10 + val;
    }
    
    if (negate) {
        res *= -1;
    }
    
    return(res);
}

/*
multipart/form-data - RFC7578
 https://www.rfc-editor.org/rfc/rfc7578

                            (MIME) Part Two: Media Types - RFC2046 Section 5.1
https://www.rfc-editor.org/rfc/rfc2046#section-5.1
*/

static bool
parse_post_payload(struct bc_str *input, struct bc_http_request *request)
{
    request->post.body = *input;
    
    struct bc_str content_type = get_header_value(request, "Content-Type");
    
    if (p_expect_literal(&content_type, "multipart/form-data; boundary=")) {
        bool boundary_quoted = p_expect(&content_type, '\"');
        
        /* get boundary in header */
        request->post.boundary = content_type;
        
        if (boundary_quoted) {
            request->post.boundary.length -= 1;
        }
        
        /* skip boundary in body */
        if (!p_skip_past(input, '\n')) return(false);
        
        if (!p_expect_literal(input, "Content-Disposition: form-data; name=\"")) {
            return(false);
        }
        
        /* get filename */
        while (input->length > 0) {
            if (!p_skip_past(input, ' ')) return(false);
            if (p_expect_literal(input, "filename=\"")) {
                request->post.filename.data = input->data;
                request->post.filename.length = p_skip_to(input, '\"');
                
                /* skip until empty line */
                while (input->length > 0) {
                    if (!p_skip_past(input, '\n')) return(false);
                    
                    /* skip the empty line itself, so that input points at beginning of payload */
                    if (p_expect_literal(input, "\r\n")) {
                        break;
                    }
                }
                
                break;
            }
        }
        
        if (input->length == 0) {
            return(false);
        }
    }
    
    request->post.payload = *input;
    
    return(true);
}

static bool
parse_headers(struct bc_str *parse, struct bc_http_headers *headers) 
{
    /* returns true if headers are found to be valid, false otherwise */
    
    while (parse->length) {
        /* skip to and after clrf */
        if (!p_skip_past(parse, '\n')) return(false);
        
        /* another clrf? header is over */
        if (parse->length >= 2 && parse->data[0] == '\r' && parse->data[1] == '\n') {
            p_advance(parse, 2);
            return(true);
        }
        
        headers->keys[headers->nheaders].data = parse->data;
        
        /* skip to ':' */
        headers->keys[headers->nheaders].length = p_skip_to(parse, ':');
        if (headers->keys[headers->nheaders].length == -1) {
            return(false);
        }
        
        /* skip ':' and space */
        if (!p_expect(parse, ':')) return(false);
        if (!p_expect(parse, ' ')) return(false);
        
        headers->values[headers->nheaders].data = parse->data;
        headers->values[headers->nheaders].length = p_skip_to(parse, '\r');
        
        if (headers->values[headers->nheaders].length == -1) {
            return(false);
        }
        
        ++headers->nheaders;
    }
    
    return(false);
}

static struct bc_http_request
parse_http_request(struct bc_str input)
{
    struct bc_http_request result = { 0 }; // NOTE(aolo2): implies valid = false
    struct bc_str parse = input;
    
    /* request line */
    if (p_expect_literal(&parse, "GET")) {
        result.method = METHOD_GET;
    } else if (p_expect_literal(&parse, "OPTIONS")) {
        result.method = METHOD_OPTIONS;
    } else if (p_expect_literal(&parse, "POST")) {
        result.method = METHOD_POST;
    } else if (p_expect_literal(&parse, "PUT")) {
        result.method = METHOD_PUT;
    } else if (p_expect_literal(&parse, "HEAD")) {
        result.method = METHOD_HEAD;
    } else if (p_expect_literal(&parse, "DELETE")) {
        result.method = METHOD_DELETE;
    } else {
        result.method = METHOD_OTHER;
        return(result);
    }
    
    if (!p_expect(&parse, ' ')) {
        return(result);
    }
    
    result.path.data = parse.data;
    result.path.length = p_skip_to(&parse, ' ');
    
    if (result.path.length == -1) {
        return(result);
    }
    
    /* unescape path in-place */
    for (int i = 0; i < result.path.length; ++i) {
        if (i < result.path.length - 2 && result.path.data[i] == '%') {
            char sym = (result.path.data[i + 1] - '0') * 16 + (result.path.data[i + 2] - '0');
            memmove(result.path.data + i + 1, result.path.data + i + 3, result.path.length - i - 3);
            result.path.data[i] = sym;
            result.path.length -= 2;
        }
    }
    
    /* headers */
    if (!parse_headers(&parse, &result.headers)) {
        return(result);
    }
    
    struct bc_str h_content_type = get_header_value(&result, "Content-Type");
    if (p_expect_literal(&h_content_type, "multipart/form-data")) {
        result.multipart = true;
    }
    
    if (result.method == METHOD_POST) {
        if (!parse_post_payload(&parse, &result)) {
            return(result);
        }
    }
    
    struct bc_str h_content_length = get_header_value(&result, "Content-Length");
    if (h_content_length.data) {
        int content_length = str_to_u64(h_content_length);
        if (content_length > parse.length && !result.multipart) {
            return(result);
        }
    }
    
    result.valid = true;
    
    return(result);
}
