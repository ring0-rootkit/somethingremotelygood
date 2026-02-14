#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <openssl/evp.h>

int pem_to_openssh(const char *pem, char *out, size_t outlen);
int evp_pkey_to_openssh(EVP_PKEY *pkey, char *out, size_t outlen);
bool verify_signature(const char *pubkey_pem, const uint8_t *challenge, size_t challenge_len,
                      const uint8_t *sig, size_t sig_len);

#endif
