#include <ece.h>

int
main() {
    // The subscription private key. This key should never be sent to the app
    // server. It should be persisted with the endpoint and auth secret, and used
    // to decrypt all messages sent to the subscription.
    uint8_t rawRecvPrivKey[ECE_WEBPUSH_PRIVATE_KEY_LENGTH];
    
    // The subscription public key. This key should be sent to the app server,
    // and used to encrypt messages. The Push DOM API exposes the public key via
    // `pushSubscription.getKey("p256dh")`.
    uint8_t rawRecvPubKey[ECE_WEBPUSH_PUBLIC_KEY_LENGTH];
    
    // The shared auth secret. This secret should be persisted with the
    // subscription information, and sent to the app server. The DOM API exposes
    // the auth secret via `pushSubscription.getKey("auth")`.
    uint8_t authSecret[ECE_WEBPUSH_AUTH_SECRET_LENGTH];
    
    int err = ece_webpush_generate_keys(
                                        rawRecvPrivKey, ECE_WEBPUSH_PRIVATE_KEY_LENGTH, rawRecvPubKey,
                                        ECE_WEBPUSH_PUBLIC_KEY_LENGTH, authSecret, ECE_WEBPUSH_AUTH_SECRET_LENGTH);
    if (err) {
        return 1;
    }
    
    return 0;
}