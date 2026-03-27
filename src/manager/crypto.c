#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <openssl/evp.h>

static int base64_decode(const char *input, size_t input_len, uint8_t *output, size_t output_len) {
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO *mem = BIO_new_mem_buf(input, (int)input_len);
    BIO *chain = BIO_push(b64, mem);
    int len = BIO_read(chain, output, (int)output_len);
    BIO_free_all(chain);
    return len;
}

static int parse_ssh_ed25519_pubkey(const char *ssh_key, uint8_t *raw_key_out) {
    if (strncmp(ssh_key, "ssh-ed25519 ", 12) != 0) return -1;

    const char *b64 = ssh_key + 12;
    const char *b64_end = strchr(b64, ' ');
    size_t b64_len = b64_end ? (size_t)(b64_end - b64) : strlen(b64);
    while (b64_len > 0 && (b64[b64_len - 1] == '\n' || b64[b64_len - 1] == '\r'))
        b64_len--;

    uint8_t blob[256];
    int blob_len = base64_decode(b64, b64_len, blob, sizeof(blob));
    if (blob_len < 0) return -1;

    size_t off = 0;
    if (off + 4 > (size_t)blob_len) return -1;
    uint32_t type_len = ((uint32_t)blob[off] << 24) | (blob[off+1] << 16) | (blob[off+2] << 8) | blob[off+3];
    off += 4 + type_len;

    if (off + 4 > (size_t)blob_len) return -1;
    uint32_t key_len = ((uint32_t)blob[off] << 24) | (blob[off+1] << 16) | (blob[off+2] << 8) | blob[off+3];
    off += 4;

    if (key_len != 32 || off + 32 > (size_t)blob_len) return -1;
    memcpy(raw_key_out, blob + off, 32);
    return 0;
}

static int parse_ssh_ed25519_sig(const uint8_t *sig, size_t sig_len, uint8_t *raw_sig_out) {
    size_t off = 0;
    if (off + 4 > sig_len) return -1;
    uint32_t type_len = ((uint32_t)sig[off] << 24) | (sig[off+1] << 16) | (sig[off+2] << 8) | sig[off+3];
    off += 4 + type_len;

    if (off + 4 > sig_len) return -1;
    uint32_t raw_len = ((uint32_t)sig[off] << 24) | (sig[off+1] << 16) | (sig[off+2] << 8) | sig[off+3];
    off += 4;

    if (raw_len != 64 || off + 64 > sig_len) return -1;
    memcpy(raw_sig_out, sig + off, 64);
    return 0;
}

static bool verify_ed25519(const char *pubkey_ssh, const uint8_t *challenge, size_t challenge_len,
                           const uint8_t *sig, size_t sig_len) {
    uint8_t raw_key[32];
    if (parse_ssh_ed25519_pubkey(pubkey_ssh, raw_key) != 0) {
        fprintf(stderr, "[ERR] Failed to parse Ed25519 public key\n");
        return false;
    }

    uint8_t raw_sig[64];
    if (sig_len == 64) {
        memcpy(raw_sig, sig, 64);
    } else if (parse_ssh_ed25519_sig(sig, sig_len, raw_sig) != 0) {
        fprintf(stderr, "[ERR] Failed to parse Ed25519 signature (len=%zu)\n", sig_len);
        return false;
    }

    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, raw_key, 32);
    if (!pkey) {
        fprintf(stderr, "[ERR] Failed to create Ed25519 EVP_PKEY\n");
        return false;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) { EVP_PKEY_free(pkey); return false; }

    bool ok = false;
    if (EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pkey) == 1) {
        if (EVP_DigestVerify(mdctx, raw_sig, 64, challenge, challenge_len) == 1)
            ok = true;
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return ok;
}

static bool verify_rsa(const char *pubkey_pem, const uint8_t *challenge, size_t challenge_len,
                       const uint8_t *sig, size_t sig_len) {
    BIO *bio = BIO_new_mem_buf((void*)pubkey_pem, -1);
    if (!bio) return false;
    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) return false;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) { EVP_PKEY_free(pkey); return false; }

    bool ok = false;
    if (EVP_DigestVerifyInit(mdctx, NULL, EVP_sha256(), NULL, pkey) == 1) {
        if (EVP_DigestVerifyUpdate(mdctx, challenge, challenge_len) == 1) {
            if (EVP_DigestVerifyFinal(mdctx, sig, sig_len) == 1) ok = true;
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return ok;
}

bool verify_signature(const char *pubkey_pem, const uint8_t *challenge, size_t challenge_len,
                      const uint8_t *sig, size_t sig_len) {
    if (strncmp(pubkey_pem, "ssh-ed25519 ", 12) == 0) {
        return verify_ed25519(pubkey_pem, challenge, challenge_len, sig, sig_len);
    }
    return verify_rsa(pubkey_pem, challenge, challenge_len, sig, sig_len);
}
