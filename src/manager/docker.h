#ifndef DOCKER_H
#define DOCKER_H

#include <stdint.h>
#include <stddef.h>

int docker_create_basic_container(const char *container_id, const char *image, const char *user_id);
int docker_start_container(const char *container_id);
int docker_stop_container(const char *container_id);
int docker_remove_container(const char *container_id);
int docker_create_secret_from_key(const char *secret_name, const uint8_t *key, size_t key_len);
int docker_assign_secret_to_container(const char *secret_name, const char *container_id);
int docker_create_user_and_inject_ssh(const char *container_id, const char *userid, const char *pubkey_pem, const char *pubkey_ssh);
int docker_unlock_container(const char *container_id, const uint8_t *key, size_t key_len);
int docker_ensure_sshd_running(const char *container_id);
int docker_get_ssh_port(const char *container_id);

#endif
