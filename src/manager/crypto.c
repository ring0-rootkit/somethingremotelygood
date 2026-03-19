#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <openssl/evp.h>

int pem_to_openssh(const char *pem, char *out, size_t outlen) {
    if (!pem || !out) return -1;

    BIO *bio = BIO_new_mem_buf(pem, -1);
    if (!bio) return -1;

    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) return -1;

    if (EVP_PKEY_base_id(pkey) != EVP_PKEY_RSA) {
        EVP_PKEY_free(pkey);
        fprintf(stderr, "Only RSA keys are supported\n");
        return -1;
    }

    BIGNUM *n = NULL, *e = NULL;
    if (!EVP_PKEY_get_bn_param(pkey, "n", &n) || !EVP_PKEY_get_bn_param(pkey, "e", &e)) {
        EVP_PKEY_free(pkey);
        if (n) BN_free(n);
        if (e) BN_free(e);
        fprintf(stderr, "Failed to extract RSA parameters\n");
        return -1;
    }

    unsigned char buf[4096], *p = buf;

    // write "ssh-rsa"
    const char *ssh_rsa_str = "ssh-rsa";
    int slen = strlen(ssh_rsa_str);
    *p++ = (slen >> 24) & 0xFF;
    *p++ = (slen >> 16) & 0xFF;
    *p++ = (slen >> 8) & 0xFF;
    *p++ = (slen & 0xFF);
    memcpy(p, ssh_rsa_str, slen); p += slen;

    // Exponent
    int len = BN_num_bytes(e);
    *p++ = (len >> 24) & 0xFF;
    *p++ = (len >> 16) & 0xFF;
    *p++ = (len >> 8) & 0xFF;
    *p++ = (len & 0xFF);
    BN_bn2bin(e, p);
    p += len;

    // Modulus
    len = BN_num_bytes(n);
    *p++ = (len >> 24) & 0xFF;
    *p++ = (len >> 16) & 0xFF;
    *p++ = (len >> 8) & 0xFF;
    *p++ = (len & 0xFF);
    BN_bn2bin(n, p);
    p += len;

    size_t binlen = p - buf;

    // Base64 encode
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO *mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);

    if (BIO_write(b64, buf, binlen) <= 0) { BIO_free_all(b64); BN_free(n); BN_free(e); EVP_PKEY_free(pkey); return -1; }
    BIO_flush(b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(mem, &bptr);
    if ((size_t)bptr->length + 32 > outlen) { BIO_free_all(b64); BN_free(n); BN_free(e); EVP_PKEY_free(pkey); return -1; }

    snprintf(out, outlen, "ssh-rsa %.*s generated-by-manager\n", (int)bptr->length, bptr->data);

    BIO_free_all(b64);
    BN_free(n);
    BN_free(e);
    EVP_PKEY_free(pkey);

    return 0;
}

int evp_pkey_to_openssh(EVP_PKEY *pkey, char *out, size_t outlen) {
    if (!pkey || !out) return -1;

    if (EVP_PKEY_id(pkey) != EVP_PKEY_RSA) {
        fprintf(stderr, "Only RSA keys are supported\n");
        return -1;
    }

    BIGNUM *n = NULL;
    BIGNUM *e = NULL;

    if (!EVP_PKEY_get_bn_param(pkey, "n", &n) || !EVP_PKEY_get_bn_param(pkey, "e", &e)) {
        fprintf(stderr, "Failed to get RSA key parameters\n");
        if (n) BN_free(n);
        if (e) BN_free(e);
        return -1;
    }

    unsigned char buf[4096];
    unsigned char *p = buf;

    // Write "ssh-rsa"
    const char *ssh_rsa_str = "ssh-rsa";
    int len = strlen(ssh_rsa_str);
    *p++ = (len >> 24) & 0xFF; *p++ = (len >> 16) & 0xFF;
    *p++ = (len >> 8) & 0xFF; *p++ = (len & 0xFF);
    memcpy(p, ssh_rsa_str, len); p += len;

    // Exponent
    len = BN_num_bytes(e);
    *p++ = (len >> 24) & 0xFF;
    *p++ = (len >> 16) & 0xFF;
    *p++ = (len >> 8) & 0xFF;
    *p++ = (len & 0xFF);
    BN_bn2bin(e, p);
    p += len;

    // Modulus
    len = BN_num_bytes(n);
    *p++ = (len >> 24) & 0xFF;
    *p++ = (len >> 16) & 0xFF;
    *p++ = (len >> 8) & 0xFF;
    *p++ = (len & 0xFF);
    BN_bn2bin(n, p);
    p += len;

    size_t binlen = p - buf;

    // Base64 encode
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO *mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_write(b64, buf, binlen);
    BIO_flush(b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(mem, &bptr);
    if (bptr->length + 32 > outlen) { BIO_free_all(b64); return -1; }

    snprintf(out, outlen, "ssh-rsa %.*s generated-by-manager\n", (int)bptr->length, bptr->data);

    BIO_free_all(b64);
    BN_free(n);
    BN_free(e);

    return 0;
}

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
