#ifndef ED25519_H
#define ED25519_H

#include <stdint.h>
#include <stddef.h>

void ed25519_keypair(uint8_t pk[32], uint8_t seed[32]);
void ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t msg_len,
                  const uint8_t seed[32], const uint8_t pk[32]);

#endif
