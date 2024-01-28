static char response_template_201[] = "HTTP/1.1 201 CREATED\r\n"
"Content-length: 0\r\n"
"\r\n";

static char response_template_404[] = "HTTP/1.1 404 Not Found\r\n"
"Content-length: 0\r\n"
"\r\n";

static char response_access_headers_200[] = "HTTP/1.1 200 OK\r\n"
"Access-Control-Allow-Origin: *\r\n"
"Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
"Access-Control-Allow-Headers: Content-Type\r\n"
"Content-length: 0\r\n"
"\r\n";

static void
handle_send_simple(struct bc_queue *rqueue, struct bc_connection *connection,
                   char *text, int total)
{
    struct bc_io *req = queue_push(rqueue, IOU_SEND);
    
    req->send.socket = connection->socket;
    req->send.buf = text;
    req->send.start = 0;
    req->send.total = total;
    
    connection->state = CONNECTION_SENDING_SIMPLE;
    
    queue_send(rqueue, req);
}

static void
handle_201(struct bc_queue *rqueue, struct bc_connection *connection)
{
    handle_send_simple(rqueue, connection, response_template_201, sizeof(response_template_201) - 1);
}

static void
handle_404(struct bc_queue *rqueue, struct bc_connection *connection)
{
    handle_send_simple(rqueue, connection, response_template_404, sizeof(response_template_404) - 1);
}

static bool
audience_matches_endpoint(struct bc_subscription *subscription, struct bc_vapid *vapid)
{
    struct bc_str endpoint = subscription->endpoint;
    struct bc_str audience = vapid->aud;
    
    return(p_startswith_str(endpoint, audience));
}

static struct bc_str
audience_from_endpoint(struct bc_str endpoint)
{
    int nslashes = 0;
    struct bc_str result = { 0 };
    
    for (int i = 0; i < endpoint.length; ++i) {
        if (endpoint.data[i] == '/') {
            ++nslashes;
        }
        
        if (nslashes == 3) {
            result.data = endpoint.data;
            result.length = i;
            break;
        }
    }
    
    return(result);
}

// TODO: regenerate this and store this in a file that is not checked in to git
#define VAPID_PRIVATE_KEY "aW-j_I7ahJfbhIiFPnizsaQyl5UReDlzIeF0CrNjkRw"
#define VAPID_PUBLIC_KEY "BPRfHPMgUIxmNPejlYNMEEDaVqBzKK3F1MDw9gyy32GRVVizg_Xd8_DR-SVNSt-hkk7UNFlyAaGoskp656FJ_Es"

static void
fill_vapid_token_for_subscription(struct bc_subscription subscription, struct bc_vapid *result)
{
    EC_KEY *key = vapid_import_public_and_private_key(VAPID_PRIVATE_KEY, VAPID_PUBLIC_KEY);
    u32 exp = unix_utcnow() + SUB_LENGTH_SECONDS;
    struct bc_str audience = audience_from_endpoint(subscription.endpoint);
    struct bc_str sub = str_from_literal("mailto:a.olokhtonov@norsi-trans.ru");
    
    result->token = vapid_build_token(key, audience.data, audience.length, exp, sub.data, sub.length);
    result->aud = audience;
    result->sub = sub;
    result->exp = exp;
}

static char *
get_vapid_token_for_subscription(struct bc_notifier *server, struct bc_subscription subscription)
{
    const u64 now = unix_utcnow();
    struct bc_vapid *creds_to_use = 0;
    
    for (int i = 0; i < buffer_size(server->vapid); ++i) {
        struct bc_vapid *v = server->vapid + i;
        
        if (audience_matches_endpoint(&subscription, v)) {
            if (now + SUB_EXP_PADDING > v->exp) {
                /* token will expire soon, renew */
                v->exp += SUB_LENGTH_SECONDS;
                fill_vapid_token_for_subscription(subscription, v);
            }
            
            creds_to_use = v;
            
            break;
        }
    }
    
    if (!creds_to_use) {
        struct bc_vapid new_creds = { 0 };
        buffer_push(server->vapid, new_creds);
        creds_to_use = server->vapid + buffer_size(server->vapid) - 1;
        fill_vapid_token_for_subscription(subscription, creds_to_use);
    }
    
    return(creds_to_use->token);
}

static void *
send_notification_worker(void *arg)
{
    struct bc_notification_work *work = arg;
    
    CURL *curl = curl_easy_init();
    
    struct curl_slist *chunk = NULL;
    
    char authorization[1024] = { 0 };
    char endpoint[512] = { 0 };
    
    snprintf(authorization, 1024, "Authorization: vapid t=%s,k=%s", work->vapid_token, VAPID_PUBLIC_KEY);
    snprintf(endpoint, 512, "%.*s", work->subscription.endpoint.length, work->subscription.endpoint.data); 
    
    chunk = curl_slist_append(chunk, "TTL: 60");
    chunk = curl_slist_append(chunk, "Content-Encoding: aes128gcm");
    chunk = curl_slist_append(chunk, "Connection: close");
    chunk = curl_slist_append(chunk, authorization);
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, work->payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, work->payload_length);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(chunk);
    
    if (res != CURLE_OK) {
        log_ferror(__func__, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
    
    curl_easy_cleanup(curl);
    
    free(arg);
    
    return(0);
}

static bool
send_notification(struct bc_notifier *server, struct bc_subscription subscription, struct bc_str plaintext)
{
    u64 payload_length = ece_aes128gcm_payload_max_length(ECE_WEBPUSH_DEFAULT_RS, 0, plaintext.length);
    
    if (payload_length == 0) {
        log_ferror(__func__, "Failed to calculate encrypted payload length\n");
        return(false);
    }
    
    u8 *payload = calloc(payload_length, sizeof(uint8_t)); // TODO: use some other allocation scheme maybe?
    
    if (!payload) {
        log_ferror(__func__, "Failed to callocate memory for encrypted payload\n");
        return(false);
    }
    
    /* 'payload_length' is an in-out parameter set to the actual payload length. */
    int err = ece_webpush_aes128gcm_encrypt(subscription.p256dh.udata, subscription.p256dh.length, 
                                            subscription.auth.udata, subscription.auth.length,
                                            ECE_WEBPUSH_DEFAULT_RS, 0, plaintext.udata, plaintext.length, 
                                            payload, &payload_length);
    if (err != ECE_OK) {
        log_ferror(__func__, "ece_webpush_aes128gcm_encrypt failed with error %d\n", err);
        return(false);
    }
    
    char *vapid_token = get_vapid_token_for_subscription(server, subscription);
    
    if (!vapid_token) {
        log_ferror(__func__, "Failed get/generate vapid token for subscription!\n");
        return(false);
    }
    
    /* TODO: notification_work should be alive for the duration of the worker */
    pthread_t thread = 0;
    struct bc_notification_work *notification_work = calloc(1, sizeof(*notification_work));
    int pc_error = 0;
    
    notification_work->vapid_token = vapid_token;
    notification_work->subscription = subscription;
    notification_work->payload = payload;
    notification_work->payload_length = payload_length;
    
    if ((pc_error = pthread_create(&thread, NULL, send_notification_worker, &notification_work)) != 0) {
        log_ferror(__func__, "pthread_create error: %s\n", strerror(pc_error));
        return(false);
    }
    
    pthread_detach(thread);
    
    log_debug("Before sleep\n");
    
    sleep(5);
    
    log_debug("After sleep\n");
    
    return(true);
}

static void
handle_subscribe(struct bc_notifier *server, struct bc_http_request *request, 
                 struct bc_queue *queue, struct bc_connection *connection)
{
    struct bc_str body = request->post.body;
    struct bc_subscription sub = { 0 };
    
    sub.user_id = readu32(body.data);
    
    sub.endpoint.data = body.data + 16;
    sub.endpoint.length = readu32(body.data + 4);
    
    sub.auth.data = body.data + 16 + sub.endpoint.length;
    sub.auth.length = readu32(body.data + 8);
    
    sub.p256dh.data = body.data + 16 + sub.endpoint.length + sub.auth.length;
    sub.p256dh.length = readu32(body.data + 12);
    
    struct bc_str test_text = str_from_literal("{\"title\": \"real test title!\", \"options\": {\"body\": \"realest test body\"}}");
    
    if (!send_notification(server, sub, test_text)) {
        log_ferror(__func__, "Failed to send notification!\n");
        handle_404(queue, connection);
        return;
    }
    
    handle_201(queue, connection);
}

static void
handle_200_access_headers(struct bc_queue *rqueue, struct bc_connection *connection)
{
    handle_send_simple(rqueue, connection, response_access_headers_200, sizeof(response_access_headers_200) - 1);
}

static void
handle_complete_http_request(struct bc_notifier *server, struct bc_connection *connection, struct bc_http_request *request)
{
    bool handled = false;
    
    switch (request->method) {
        case METHOD_POST: {
            if (p_eqlit(request->path, "/subscribe")) {
                handle_subscribe(server, request, &server->queue, connection);
                handled = true;
            }
            
            break;
        }
        
        case METHOD_OPTIONS: {
            handle_200_access_headers(&server->queue, connection);
            handled = true;
            break;
        }
        
        default: {
            fprintf(stderr, "[ERROR] Unexpected http method %d\n", request->method);
        }
    }
    
    if (!handled) {
        handle_404(&server->queue, connection);
    }
}
