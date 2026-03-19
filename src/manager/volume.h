#ifndef VOLUME_H
#define VOLUME_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    char container_id[128];
    char mapper_name[256];
    char mount_point[512];
    int is_mounted;
} VolumeInfo;

int volume_create_encrypted_home(const char *container_id, const uint8_t *key, size_t key_len);
int volume_open_encrypted_home(const char *container_id, const uint8_t *key, size_t key_len, char *mount_point, size_t mount_point_len);
int volume_close_encrypted_home(const char *container_id);
int volume_is_home_encrypted(const char *container_id);
void volume_cleanup_all(void);
void volume_cleanup_stopped_containers(void);
void volume_set_provisioning(int active);

#endif
