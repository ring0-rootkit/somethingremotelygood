#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <stdint.h>
#include <stddef.h>

extern sqlite3 *db;

int db_open(const char *password);
int db_init_schema(void);
int db_add_user(const char *user_id, const char *public_key_pem, const char *public_key_ssh);
int db_add_container(const char *container_id, const char *user_id, const uint8_t *key_blob, size_t key_len);
int db_get_user_pubkey(const char *user_id, char *pubkey_out, size_t outlen);
int db_get_user_pubkey_ssh(const char *user_id, char *pubkey_out, size_t outlen);
int db_get_container_key(const char *user_id, const char *container_id, uint8_t *key_out, size_t key_len);

#endif
