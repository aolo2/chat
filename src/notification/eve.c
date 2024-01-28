#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ece.h>

int
main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s endpoint p256dh auth\n", argv[0]);
        return(1);
    }
    
    // The endpoint, public key, and auth secret for the push subscription. These
    // are exposed via `JSON.stringify(pushSubscription)` in the browser.
    const char* endpoint = argv[1];
    const char* p256dh = argv[2];
    const char* auth = argv[3];
    
    // The message to encrypt.
    const void* plaintext = "{\"title\": \"FROM C!\", \"options\": {\"body\": \"yep..\"}}";
    size_t plaintextLen = strlen(plaintext);
    
    // How many bytes of padding to include in the encrypted message. Padding
    // obfuscates the plaintext length, making it harder to guess the contents
    // based on the encrypted payload length.
    size_t padLen = 0;
    
    // Base64url-decode the subscription public key and auth secret. `recv` is
    // short for "receiver", which, in our case, is the browser.
    uint8_t rawRecvPubKey[ECE_WEBPUSH_PUBLIC_KEY_LENGTH];
    size_t rawRecvPubKeyLen =
        ece_base64url_decode(p256dh, strlen(p256dh), ECE_BASE64URL_REJECT_PADDING,
                             rawRecvPubKey, ECE_WEBPUSH_PUBLIC_KEY_LENGTH);
    assert(rawRecvPubKeyLen > 0);
    uint8_t authSecret[ECE_WEBPUSH_AUTH_SECRET_LENGTH];
    size_t authSecretLen =
        ece_base64url_decode(auth, strlen(auth), ECE_BASE64URL_REJECT_PADDING,
                             authSecret, ECE_WEBPUSH_AUTH_SECRET_LENGTH);
    assert(authSecretLen > 0);
    
    // Allocate a buffer large enough to hold the encrypted payload. The payload
    // length depends on the record size, padding, and plaintext length, plus a
    // fixed-length header block. Smaller records and additional padding take
    // more space. The maximum payload length rounds up to the nearest whole
    // record, so the actual length after encryption might be smaller.
    size_t payloadLen = ece_aes128gcm_payload_max_length(ECE_WEBPUSH_DEFAULT_RS,
                                                         padLen, plaintextLen);
    assert(payloadLen > 0);
    uint8_t* payload = calloc(payloadLen, sizeof(uint8_t));
    assert(payload);
    
    // Encrypt the plaintext. `payload` holds the header block and ciphertext;
    // `payloadLen` is an in-out parameter set to the actual payload length.
    int err = ece_webpush_aes128gcm_encrypt(
                                            rawRecvPubKey, rawRecvPubKeyLen, authSecret, authSecretLen,
                                            ECE_WEBPUSH_DEFAULT_RS, padLen, plaintext, plaintextLen, payload,
                                            &payloadLen);
    assert(err == ECE_OK);
    
    // Write the payload out to a file.
    const char* filename = "aes128gcm.bin";
    FILE* payloadFile = fopen(filename, "wb");
    assert(payloadFile);
    size_t payloadFileLen =
        fwrite(payload, sizeof(uint8_t), payloadLen, payloadFile);
    assert(payloadLen == payloadFileLen);
    fclose(payloadFile);
    
    printf(
           "curl -v -X POST -H \"TTL: 30\" -H \"Content-Encoding: aes128gcm\" --data-binary @%s %s\n",
           filename, endpoint);
    
    free(payload);
    
    return 0;
}