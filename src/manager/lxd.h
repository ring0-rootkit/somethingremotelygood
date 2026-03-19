#ifndef LXD_H
#define LXD_H

int lxd_create_container(const char *container_id, const char *image, const char *user_id, const char *home_mount);
int lxd_start_container(const char *container_id);
int lxd_stop_container(const char *container_id);
int lxd_remove_container(const char *container_id);
int lxd_setup_ssh_in_volume(const char *mount_point, const char *userid, const char *pubkey_ssh);
int lxd_create_user_and_setup(const char *container_id, const char *userid);
int lxd_fix_ssh_permissions(const char *container_id, const char *userid);
int lxd_ensure_sshd_running(const char *container_id);
int lxd_get_ssh_port(const char *container_id);
int lxd_is_running(const char *container_id);
int lxd_setup_network(const char *container_id);

#endif
