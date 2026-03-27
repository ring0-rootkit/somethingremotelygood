#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <openssl/evp.h>

bool verify_signature(const char *pubkey_pem, const uint8_t *challenge, size_t challenge_len,
                      const uint8_t *sig, size_t sig_len);

#endif
