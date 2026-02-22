#ifndef DOCKER_H
#define DOCKER_H

#include <stdint.h>
#include <stddef.h>

int docker_create_basic_container(const char *container_id, const char *image, const char *user_id, const char *home_mount);
int docker_start_container(const char *container_id);
int docker_stop_container(const char *container_id);
int docker_remove_container(const char *container_id);
int docker_create_user_and_inject_ssh(const char *container_id, const char *userid, const char *pubkey_pem, const char *pubkey_ssh);
int docker_setup_ssh_in_volume(const char *mount_point, const char *userid, const char *pubkey_ssh);
int docker_fix_ssh_permissions(const char *container_id, const char *userid);
int docker_ensure_sshd_running(const char *container_id);
int docker_get_ssh_port(const char *container_id);

#endif
