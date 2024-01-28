#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/pem.h>

#include <curl/curl.h>

#include <ece.h>
#include <ece/keys.h>

#include <pthread.h>

#define DEBUG_PRINT 1
#define TIMEOUT_INTERVAL 5
#define SUB_EXP_PADDING 60
#define SUB_LENGTH_SECONDS (60 * 60 * 12)

#define MAX_POSSIBLE_VAPID_CREDS 10000

struct bc_subscription {
    u32 user_id;
    
    struct bc_str endpoint;
    struct bc_str p256dh;
    struct bc_str auth;
};

struct bc_vapid {
    char *token;
    struct bc_str aud; /* "audience" - push service protocol://host */
    struct bc_str sub; /* contact email */
    u32 exp;
};

struct bc_notifier {
    int fd;
    
    struct bc_vapid *vapid;
    struct bc_queue queue;
    struct bc_connection *connections;
};

/* For passing to pthread_create */
struct bc_notification_work {
    char *vapid_token;
    struct bc_subscription subscription;
    u8 *payload;
    int payload_length;
};