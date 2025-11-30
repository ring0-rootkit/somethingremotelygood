
// Secure VM/container manager - polished implementation
// - SQLCipher-encrypted SQLite database layer (init, add user/container, fetch keys)
// - Docker helpers (create container record, start/stop/unlock, inject SSH pubkey)
// - Manager server (TCP challenge-response flow). USB token handling is left as stubs
//   per your request.
//
// BUILD (example):
//   gcc -o manager manager.c -lsqlite3 -lssl -lcrypto
// NOTE: For SQLCipher use a build linking against sqlcipher instead of libsqlite3.

#include <arpa/inet.h>
#include <fcntl.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/buffer.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


// -----------------------------------------------------------------------------
// CONFIG
// -----------------------------------------------------------------------------
#define DB_PATH "encrypted_users.db"
#define MANAGER_PORT 5555
#define MAX_CONTAINER_ID 128
#define CHALLENGE_SIZE 32
#define CONTAINER_KEY_LEN 32

// -----------------------------------------------------------------------------
// GLOBALS
// -----------------------------------------------------------------------------
static sqlite3 *db = NULL;

// -----------------------------------------------------------------------------
// UTIL - secure temp file writer
// -----------------------------------------------------------------------------
// Helper: convert PEM public key to OpenSSH "ssh-rsa ..." string

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
    #define PUT_BIGNUM(bn) do { \
        int len = BN_num_bytes(bn); \
        *p++ = (len >> 24) & 0xFF; \
        *p++ = (len >> 16) & 0xFF; \
        *p++ = (len >> 8) & 0xFF; \
        *p++ = (len & 0xFF); \
        BN_bn2bin(bn, p); \
        p += len; \
    } while(0)

    // write "ssh-rsa"
    const char *ssh_rsa_str = "ssh-rsa";
    int slen = strlen(ssh_rsa_str);
    *p++ = (slen >> 24) & 0xFF;
    *p++ = (slen >> 16) & 0xFF;
    *p++ = (slen >> 8) & 0xFF;
    *p++ = (slen & 0xFF);
    memcpy(p, ssh_rsa_str, slen); p += slen;

    PUT_BIGNUM(e);
    PUT_BIGNUM(n);

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

static int write_temp(const char *template, const void *data, size_t len, char *out_path, size_t out_len) {
    char path[256];
    snprintf(path, sizeof(path), "%s.XXXXXX", template);
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    ssize_t w = write(fd, data, len);
    if ((size_t)w != len) { close(fd); unlink(path); return -1; }
    close(fd);
    // restrict permissions
    chmod(path, S_IRUSR | S_IWUSR);
    if (out_path && out_len) strncpy(out_path, path, out_len);
    return 0;
}

// run command with args (no shell) and wait
static int run_cmdv(char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        // child
        execvp(argv[0], argv);
        _exit(127);
    } else if (pid < 0) {
        return -1;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

static int run_cmd_shell(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    } else if (pid < 0) return -1;
    int status = 0; waitpid(pid, &status, 0); return status;
}

// -----------------------------------------------------------------------------
// DATABASE LAYER (SQLCipher-based encrypted SQLite implementation)
// -----------------------------------------------------------------------------

int db_open(const char *password) {
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "[DB] Failed to open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // Apply SQLCipher key - if using sqlcipher, PRAGMA key applies
    char key_cmd[512];
    snprintf(key_cmd, sizeof(key_cmd), "PRAGMA key=\"%s\";", password);

    if (sqlite3_exec(db, key_cmd, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] Failed to apply key: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // Verify DB decrypts correctly
    if (sqlite3_exec(db, "SELECT count(*) FROM sqlite_master;", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] Wrong database password or corrupted DB.\n");
        return -1;
    }

    return 0;
}

int db_init_schema(void) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        " user_id TEXT PRIMARY KEY,"
        " public_key_pem TEXT NOT NULL"
        " );"
        "CREATE TABLE IF NOT EXISTS containers ("
        " container_id TEXT PRIMARY KEY,"
        " user_id TEXT NOT NULL,"
        " container_key BLOB NOT NULL,"
        " FOREIGN KEY(user_id) REFERENCES users(user_id)"
        " );";

    if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] Schema init failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

int db_add_user(const char *user_id, const char *public_key_pem) {
    const char *sql = "INSERT OR REPLACE INTO users(user_id, public_key_pem) VALUES(?,?)";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, public_key_pem, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_add_container(const char *container_id, const char *user_id, const uint8_t *key_blob, size_t key_len) {
    const char *sql = "INSERT OR REPLACE INTO containers(container_id, user_id, container_key) VALUES(?,?,?)";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, container_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 3, key_blob, (int)key_len, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_user_pubkey(const char *user_id, char *pubkey_out, size_t outlen) {
    const char *sql = "SELECT public_key_pem FROM users WHERE user_id = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *pk = sqlite3_column_text(stmt, 0);
        if (pk) {
            strncpy(pubkey_out, (const char *)pk, outlen-1);
            pubkey_out[outlen-1] = '\0';
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    sqlite3_finalize(stmt);
    return -1;
}

int db_get_container_key(const char *user_id, const char *container_id, uint8_t *key_out, size_t key_len) {
    const char *sql = "SELECT container_key FROM containers WHERE user_id = ? AND container_id = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, container_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(stmt, 0);
        int blob_len = sqlite3_column_bytes(stmt, 0);
        if ((size_t)blob_len != key_len) {
            fprintf(stderr, "[DB] Key length mismatch. Expected %zu, got %d\n", key_len, blob_len);
            sqlite3_finalize(stmt);
            return -1;
        }
        memcpy(key_out, blob, key_len);
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return -1;
}

// -----------------------------------------------------------------------------
// CRYPTO: signature verification using PEM public key
// -----------------------------------------------------------------------------

// Convert EVP_PKEY to OpenSSH "ssh-rsa AAAAB3Nza..." format
static int evp_pkey_to_openssh(EVP_PKEY *pkey, char *out, size_t outlen) {
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

    // Build OpenSSH binary blob
    unsigned char buf[4096];
    unsigned char *p = buf;

    // helper to write length + data
    #define PUT_BIGNUM(bn) do { \
        int bn_len = BN_num_bytes(bn); \
        *p++ = (bn_len >> 24) & 0xFF; \
        *p++ = (bn_len >> 16) & 0xFF; \
        *p++ = (bn_len >> 8) & 0xFF; \
        *p++ = (bn_len) & 0xFF; \
        BN_bn2bin(bn, p); \
        p += bn_len; \
    } while(0)

    // Write "ssh-rsa"
    const char *ssh_rsa_str = "ssh-rsa";
    int len = strlen(ssh_rsa_str);
    *p++ = (len >> 24) & 0xFF; *p++ = (len >> 16) & 0xFF;
    *p++ = (len >> 8) & 0xFF; *p++ = (len & 0xFF);
    memcpy(p, ssh_rsa_str, len); p += len;

    // Exponent and modulus
    PUT_BIGNUM(e);
    PUT_BIGNUM(n);

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

bool verify_signature(const char *pubkey_pem, const uint8_t *challenge, size_t challenge_len,
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

// -----------------------------------------------------------------------------
// USB TOKEN VERIFICATION (stubs - left for your implementation)
// -----------------------------------------------------------------------------

bool usb_get_user_id(char *userid_out, size_t outlen) {
    // Left unimplemented - integration point for your USB/PIV code.
    // For testing, you can return a fixed user id.
    snprintf(userid_out, outlen, "testuser");
    return true;
}

bool usb_sign_challenge(const uint8_t *challenge, size_t len, uint8_t *sig_out, size_t *sig_len) {
    // Left unimplemented - USB device should sign the challenge and return signature.
    // For testing you can set sig_len=0 and skip verification path.
    (void)challenge; (void)len; (void)sig_out; if (sig_len) *sig_len = 0; return false;
}

// -----------------------------------------------------------------------------
// DOCKER CONTROL HELPERS
// -----------------------------------------------------------------------------

int docker_create_basic_container(const char *container_id, const char *image, const char *user_id) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "docker create -p 0:22 --name %s --hostname %s %s /bin/sh -c 'while true; do sleep 3600; done'",
        container_id, container_id, image);
    return run_cmd_shell(cmd);
}

int docker_start_container(const char *container_id) {
    char *argv[] = {"docker","start",(char *)container_id,NULL};
    return run_cmdv(argv);
}

int docker_stop_container(const char *container_id) {
    char *argv[] = {"docker","stop",(char *)container_id,NULL};
    return run_cmdv(argv);
}

int docker_remove_container(const char *container_id) {
    char *argv[] = {"docker","rm","-f",(char *)container_id,NULL};
    return run_cmdv(argv);
}

int docker_create_secret_from_key(const char *secret_name, const uint8_t *key, size_t key_len) {
    char tmp[256];
    if (write_temp("/tmp/secret", key, key_len, tmp, sizeof(tmp)) != 0) return -1;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "docker secret create %s %s >/dev/null 2>&1 || true", secret_name, tmp);
    int r = run_cmd_shell(cmd);
    unlink(tmp);
    return r;
}

int docker_assign_secret_to_container(const char *secret_name, const char *container_id) {
    // For simplicity attach secret by creating a new service is complex; instead copy secret into container
    // Using docker cp to copy secret file into container is easier: docker cp localfile container:/path
    // Lookup secret content via 'docker secret inspect' is non-trivial; we'll store temp file during unlock step.
    (void)secret_name; (void)container_id;
    return 0;
}

int docker_create_user_and_inject_ssh(const char *container_id, const char *userid, const char *pubkey_pem) {
    // 1. Create user if it doesn't exist
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "docker exec %s id -u %s >/dev/null 2>&1 || "
        "docker exec %s useradd -m -s /bin/bash %s",
        container_id, userid, container_id, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to create user %s in container %s\n", userid, container_id);
        return -1;
    }

    // 2. Ensure ~/.ssh exists
    snprintf(cmd, sizeof(cmd),
        "docker exec %s mkdir -p /home/%s/.ssh && "
        "docker exec %s chmod 700 /home/%s/.ssh && "
        "docker exec %s chown %s:%s /home/%s/.ssh",
        container_id, userid, container_id, userid, container_id, userid, userid, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to create .ssh dir for %s in container %s\n", userid, container_id);
        return -1;
    }

    // 3. Convert PEM -> OpenSSH
    char sshkey[4096];
    if (pem_to_openssh(pubkey_pem, sshkey, sizeof(sshkey)) != 0) {
        fprintf(stderr, "[ERR] Failed to convert PEM to OpenSSH key\n");
        return -1;
    }

    // 4. Write temporary OpenSSH key file
    char tmp[256];
    if (write_temp("/tmp/sshkey", sshkey, strlen(sshkey), tmp, sizeof(tmp)) != 0) {
        fprintf(stderr, "[ERR] Failed to write temp SSH key\n");
        return -1;
    }

    // 5. Copy into container
    snprintf(cmd, sizeof(cmd),
        "docker cp %s %s:/home/%s/.ssh/authorized_keys",
        tmp, container_id, userid);
    int r = run_cmd_shell(cmd);
    unlink(tmp);
    if (r != 0) return -1;

    // 6. Set proper permissions
    snprintf(cmd, sizeof(cmd),
        "docker exec %s chmod 600 /home/%s/.ssh/authorized_keys && "
        "docker exec %s chown %s:%s /home/%s/.ssh/authorized_keys",
        container_id, userid, container_id, userid, userid, userid);
    return run_cmd_shell(cmd);
}

int docker_unlock_container(const char *container_id, const uint8_t *key, size_t key_len) {
    // Create a temp key file and docker cp into container to a path where a startup script inside the container
    // will use it to unlock the encrypted mount. This is intentionally generic; the container must have
    // logic to consume /root/container_key.bin and perform the unlock (LUKS or similar). Manager's role
    // is to deliver the key file and start the container.

    char tmp[256];
    if (write_temp("/tmp/ctrkey", key, key_len, tmp, sizeof(tmp)) != 0) return -1;

    // Copy into container
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "docker cp %s %s:/root/container_key.bin", tmp, container_id);
    int r = run_cmd_shell(cmd);
    unlink(tmp);
    if (r != 0) return -1;

    // Signal container via touch file or exec that the key is present (optional)
    snprintf(cmd, sizeof(cmd), "docker exec %s test -f /root/container_key.bin || true", container_id);
    return run_cmd_shell(cmd);
}

int docker_ensure_sshd_running(const char *container_id) {
    printf("[sshd] Ensuring OpenSSH server is installed and configured inside %s\n", container_id);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "docker exec %s sh -c '"
        "echo \"[sshd] Checking if sshd is running...\" ; "
        "pgrep -x sshd >/dev/null && echo \"[sshd] sshd already running\" && exit 0 ; "
        "echo \"[sshd] Installing openssh-server\" ; "
        "apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y openssh-server ; "
        "echo \"[sshd] Creating /var/run/sshd\" ; "
        "mkdir -p /var/run/sshd ; "
        "echo \"[sshd] Updating sshd_config\" ; "
        "sed -i \"s/^#*PasswordAuthentication.*/PasswordAuthentication no/\" /etc/ssh/sshd_config ; "
        "sed -i \"s/^#*PubkeyAuthentication.*/PubkeyAuthentication yes/\" /etc/ssh/sshd_config ; "
        "sed -i \"s/^#*ChallengeResponseAuthentication.*/ChallengeResponseAuthentication no/\" /etc/ssh/sshd_config ; "
        "echo \"[sshd] Starting sshd\" ; "
        "/usr/sbin/sshd ; "
        "echo \"[sshd] sshd started successfully\"'"
        , container_id);

    printf("[sshd] EXEC:\n%s\n", cmd);

    return run_cmd_shell(cmd);
}

int docker_get_ssh_port(const char *container_id) {
    // Map container 22 -> host port using docker port
    // Returns port number, or -1 if error
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "docker port %s 22", container_id);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    char buf[128];
    if (!fgets(buf, sizeof(buf), f)) { pclose(f); return -1; }
    pclose(f);
    // buf = "0.0.0.0:32768\n" or ":::32768\n"
    char *p = strrchr(buf, ':');
    if (!p) return -1;
    int port = atoi(p+1);
    return port;
}

// -----------------------------------------------------------------------------
// ACCESS MANAGER SERVER
// -----------------------------------------------------------------------------

void handle_client(int client_fd) {
    char userid[128];
    char containerid[128];
    uint8_t challenge[CHALLENGE_SIZE];
    uint8_t signature[1024];
    size_t sig_len = 0;

    // -----------------------------
    // 1. Read user ID from client
    // -----------------------------
    ssize_t r = read(client_fd, userid, sizeof(userid)-1);
    if (r <= 0) {
        write(client_fd, "ERR NO_USER_ID", 13);
        return;
    }
    userid[r] = '\0';  // ensure null-terminated

    // -----------------------------
    // 2. Generate challenge and send to client
    // -----------------------------
    if (RAND_bytes(challenge, sizeof(challenge)) != 1) {
        write(client_fd, "ERR RAND", 8);
        return;
    }
    // Send challenge as raw bytes
    if (write(client_fd, challenge, sizeof(challenge)) != sizeof(challenge)) {
        write(client_fd, "ERR SEND_CHAL", 12);
        return;
    }

    // -----------------------------
    // 3. Receive signature from client
    // -----------------------------
    r = read(client_fd, signature, sizeof(signature));
    if (r <= 0) {
        write(client_fd, "ERR NOSIG", 8);
        return;
    }
    sig_len = (size_t)r;

    // -----------------------------
    // 4. Fetch user's public key from DB
    // -----------------------------
    char pubkey_pem[4096];
    if (db_get_user_pubkey(userid, pubkey_pem, sizeof(pubkey_pem)) != 0) {
        write(client_fd, "ERR NO_USER", 10);
        return;
    }

    // -----------------------------
    // 5. Verify signature
    // -----------------------------
    if (!verify_signature(pubkey_pem, challenge, sizeof(challenge), signature, sig_len)) {
        write(client_fd, "ERR VERIFY_FAIL", 15);
        fprintf(stderr, "challenge: %128s\nsignature: %128s\n", challenge, signature);
        return;
    }
    write(client_fd, "REQ CID", 7);

    r = read(client_fd, containerid, sizeof(containerid)-1);
    if (r <= 0) {
        write(client_fd, "ERR NO_USER_ID", 13);
        return;
    }
    containerid[r] = '\0';  // ensure null-terminated


    // -----------------------------
    // 6. Fetch container key
    // -----------------------------
    uint8_t container_key[CONTAINER_KEY_LEN];
    if (db_get_container_key(userid, containerid, container_key, sizeof(container_key)) != 0) {
        write(client_fd, "ERR NO_CONTAINER_KEY", 20);
        return;
    }

    // -----------------------------
    // 7. Start and unlock container
    // -----------------------------
    if (docker_start_container(containerid) != 0) {
        printf("[INFO] Container %s not found, creating...\n", containerid);
        int res = docker_create_basic_container(containerid, "ubuntu:22.04", userid);
        if (res != 0) {
            printf("[INFO] Container %s setup returned error, %d\n", containerid, res);
            return;
        }
        docker_start_container(containerid);
    }
    if (docker_unlock_container(containerid, container_key, sizeof(container_key)) != 0) {
        write(client_fd, "ERR UNLOCK_FAIL", 15);
        return;
    }

    // -----------------------------
    // 8. Ensure sshd is running
    // -----------------------------
    if (docker_ensure_sshd_running(containerid) != 0) {
        write(client_fd, "ERR SSHD_FAIL", 13);
        return;
    }

    printf("sshd is running\n");

    // -----------------------------
    // 9. Inject user's SSH public key
    // -----------------------------
    if (docker_create_user_and_inject_ssh(containerid, userid, pubkey_pem) != 0) {
        write(client_fd, "ERR INJECT_SSH", 13);
        return;
    }

    printf("ssh injected\n");

    // -----------------------------
    // 10. Get SSH port
    // -----------------------------
    int ssh_port = docker_get_ssh_port(containerid);
    if (ssh_port <= 0) {
        write(client_fd, "ERR SSH_PORT", 12);
        return;
    }

    printf("got ssh port\n");
    write(client_fd, "OK", 2);

    // -----------------------------
    // 11. Send connection info to client
    // -----------------------------
    char info[128];
    snprintf(info, sizeof(info), "%d\n", ssh_port);
    write(client_fd, info, strlen(info));
    sleep(1);
}

int start_manager() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }
    int opt = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(MANAGER_PORT);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); close(s); return -1; }
    if (listen(s, 5) < 0) { perror("listen"); close(s); return -1; }

    printf("Manager running on port %d...\n", MANAGER_PORT);
    while (1) {
        int client_fd = accept(s, NULL, NULL);
        if (client_fd < 0) continue;
        handle_client(client_fd);
        close(client_fd);
    }
    close(s);
    return 0;
}

// -----------------------------------------------------------------------------
// ADMIN CLI
// -----------------------------------------------------------------------------

static void usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s serve                # run manager\n", prog);
    fprintf(stderr, "  %s add-user <id> <pubkey.pem>   # add/update user\n", prog);
    fprintf(stderr, "  %s add-container <ctr_id> <user_id> <keyfile> # add container record\n", prog);
}

int cmd_add_user(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "missing args\n"); return -1; }
    const char *user_id = argv[1];
    const char *pem_path = argv[2];
    FILE *f = fopen(pem_path, "r"); if (!f) { perror("fopen"); return -1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    char *buf = malloc(sz+1); fread(buf,1,sz,f); buf[sz]='\0'; fclose(f);
    int r = db_add_user(user_id, buf);
    free(buf);
    if (r == 0) printf("Added user %s\n", user_id); else printf("Failed to add user\n");
    return r;
}

int cmd_add_container(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "missing args\n"); return -1; }
    const char *ctr_id = argv[1];
    const char *user_id = argv[2];
    const char *keyfile = argv[3];
    FILE *f = fopen(keyfile, "rb"); if (!f) { perror("fopen"); return -1; }
    fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *buf = malloc(sz); fread(buf,1,sz,f); fclose(f);
    int r = db_add_container(ctr_id, user_id, buf, sz);
    free(buf);
    if (r == 0) printf("Added container %s for user %s\n", ctr_id, user_id); else printf("Failed to add container\n");
    return r;
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *db_pass = getenv("DB_PASSWORD");
    if (!db_pass) { fprintf(stderr, "ERROR: DB_PASSWORD env not set.\n"); return 1; }

    if (db_open(db_pass) != 0) { fprintf(stderr, "Unable to open DB.\n"); return 1; }
    if (db_init_schema() != 0) { fprintf(stderr, "DB schema init failed.\n"); return 1; }

    const char *cmd = argv[1];
    if (strcmp(cmd, "serve") == 0) {
        return start_manager();
    } else if (strcmp(cmd, "add-user") == 0) {
        return cmd_add_user(argc-1, &argv[1]);
    } else if (strcmp(cmd, "add-container") == 0) {
        return cmd_add_container(argc-1, &argv[1]);
    }

    usage(argv[0]);
    return 1;
}
