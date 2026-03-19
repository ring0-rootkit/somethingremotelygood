#include "volume.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <pwd.h>

#define HOMES_DIR "./homes"
#define VOLUME_SIZE_MB 100

static VolumeInfo *mounted_volumes = NULL;
static int volume_count = 0;
static int volume_capacity = 0;
static volatile int provisioning_active = 0;

void volume_set_provisioning(int active) {
    provisioning_active = active;
}

static uid_t get_real_uid(void) {
    char *sudo_uid = getenv("SUDO_UID");
    if (sudo_uid) {
        return (uid_t)atoi(sudo_uid);
    }
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return pw->pw_uid;
    }
    return getuid();
}

static gid_t get_real_gid(void) {
    char *sudo_gid = getenv("SUDO_GID");
    if (sudo_gid) {
        return (gid_t)atoi(sudo_gid);
    }
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return pw->pw_gid;
    }
    return getgid();
}

static void ensure_homes_dir(void) {
    struct stat st;
    if (stat(HOMES_DIR, &st) != 0) {
        mkdir(HOMES_DIR, 0755);
    }
}

static void cleanup_stale_mapper(const char *mapper_name) {
    char cmd[512];
    char mapper_path[256];
    snprintf(mapper_path, sizeof(mapper_path), "/dev/mapper/%s", mapper_name);

    if (access(mapper_path, F_OK) == 0) {
        fprintf(stderr, "[WARN] Cleaning up stale mapper: %s\n", mapper_name);
        snprintf(cmd, sizeof(cmd), "umount /dev/mapper/%s 2>/dev/null; cryptsetup close %s 2>/dev/null || true", 
                 mapper_name, mapper_name);
        run_cmd_shell(cmd);
    }
}

static void get_volume_paths(const char *container_id, char *img_path, size_t img_len,
                             char *mapper_name, size_t mapper_len,
                             char *mount_point, size_t mount_len) {
    ensure_homes_dir();

    if (img_path) {
        snprintf(img_path, img_len, "%s/%s.img", HOMES_DIR, container_id);
    }
    if (mapper_name) {
        snprintf(mapper_name, mapper_len, "somethinigremotelygood_%s", container_id);
    }
    if (mount_point) {
        snprintf(mount_point, mount_len, "%s/%s_mnt", HOMES_DIR, container_id);
    }
}

int volume_create_encrypted_home(const char *container_id, const uint8_t *key, size_t key_len) {
    char img_path[512];
    char mapper_name[256];
    char mount_point[512];
    char key_path[512];

    if (getuid() != 0) {
        fprintf(stderr, "[ERR] Root privileges required for LUKS encryption. Please run with sudo.\n");
        return -1;
    }

    get_volume_paths(container_id, img_path, sizeof(img_path),
                     mapper_name, sizeof(mapper_name),
                     mount_point, sizeof(mount_point));

    cleanup_stale_mapper(mapper_name);

    if (access(img_path, F_OK) == 0) {
        return 0;
    }

    snprintf(key_path, sizeof(key_path), "%s/%s.key", HOMES_DIR, container_id);
    FILE *f = fopen(key_path, "wb");
    if (!f) return -1;
    fwrite(key, 1, key_len, f);
    fclose(f);
    chmod(key_path, 0600);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "dd if=/dev/zero of=%s bs=1M count=%d status=none",
             img_path, VOLUME_SIZE_MB);
    if (run_cmd_shell(cmd) != 0) goto cleanup;

    uid_t uid = get_real_uid();
    gid_t gid = get_real_gid();
    chown(img_path, uid, gid);

    if (strlen(key_path) > 256) {printf("path to key can not be longer than 255 symbols"); goto cleanup;}
    if (strlen(img_path) > 256) {printf("path to img can not be longer than 255 symbols"); goto cleanup;}

    snprintf(cmd, sizeof(cmd), "cryptsetup luksFormat --batch-mode --key-file %.255s %.255s",
             key_path, img_path);
    if (run_cmd_shell(cmd) != 0) goto cleanup;

    snprintf(cmd, sizeof(cmd), "cryptsetup open --key-file %.255s %.255s %s",
             key_path, img_path, mapper_name);
    if (run_cmd_shell(cmd) != 0) goto cleanup;

    snprintf(cmd, sizeof(cmd), "mkfs.ext4 -q /dev/mapper/%s", mapper_name);
    if (run_cmd_shell(cmd) != 0) {
        snprintf(cmd, sizeof(cmd), "cryptsetup close %s", mapper_name);
        run_cmd_shell(cmd);
        goto cleanup;
    }

    snprintf(cmd, sizeof(cmd), "cryptsetup close %s", mapper_name);
    run_cmd_shell(cmd);

    unlink(key_path);
    return 0;

cleanup:
    unlink(key_path);
    unlink(img_path);
    return -1;
}

int volume_open_encrypted_home(const char *container_id, const uint8_t *key, size_t key_len,
                               char *mount_point_out, size_t mount_point_len) {
    char img_path[512];
    char mapper_name[256];
    char mount_point[512];
    char key_path[512];

    if (getuid() != 0) {
        fprintf(stderr, "[ERR] Root privileges required for LUKS encryption. Please run with sudo.\n");
        return -1;
    }

    if (strlen(key_path) > 256) {printf("path to key can not be longer than 255 symbols"); return -1;}
    if (strlen(img_path) > 256) {printf("path to img can not be longer than 255 symbols"); return -1;}

    get_volume_paths(container_id, img_path, sizeof(img_path),
                     mapper_name, sizeof(mapper_name),
                     mount_point, sizeof(mount_point));

    if (access(img_path, F_OK) != 0) {
        fprintf(stderr, "[ERR] Encrypted home not found for %s\n", container_id);
        return -1;
    }

    cleanup_stale_mapper(mapper_name);

    for (int i = 0; i < volume_count; i++) {
        if (strcmp(mounted_volumes[i].container_id, container_id) == 0 &&
            mounted_volumes[i].is_mounted) {
            if (mount_point_out) {
                strncpy(mount_point_out, mounted_volumes[i].mount_point, mount_point_len);
            }
            return 0;
        }
    }

    snprintf(key_path, sizeof(key_path), "%s/%s.key", HOMES_DIR, container_id);
    FILE *f = fopen(key_path, "wb");
    if (!f) return -1;
    fwrite(key, 1, key_len, f);
    fclose(f);
    chmod(key_path, 0600);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cryptsetup open --key-file %.255s %.255s %s",
             key_path, img_path, mapper_name);
    if (run_cmd_shell(cmd) != 0) {
        unlink(key_path);
        return -1;
    }

    unlink(key_path);

    struct stat st;
    if (stat(mount_point, &st) != 0) {
        mkdir(mount_point, 0755);
        uid_t uid = get_real_uid();
        gid_t gid = get_real_gid();
        chown(mount_point, uid, gid);
    }

    snprintf(cmd, sizeof(cmd), "/dev/mapper/%s", mapper_name);
    if (mount(cmd, mount_point, "ext4", 0, NULL) != 0) {
        snprintf(cmd, sizeof(cmd), "cryptsetup close %s", mapper_name);
        run_cmd_shell(cmd);
        return -1;
    }

    if (volume_count >= volume_capacity) {
        volume_capacity = volume_capacity ? volume_capacity * 2 : 4;
        mounted_volumes = realloc(mounted_volumes, volume_capacity * sizeof(VolumeInfo));
    }

    strncpy(mounted_volumes[volume_count].container_id, container_id, sizeof(mounted_volumes[volume_count].container_id) - 1);
    strncpy(mounted_volumes[volume_count].mapper_name, mapper_name, sizeof(mounted_volumes[volume_count].mapper_name) - 1);
    strncpy(mounted_volumes[volume_count].mount_point, mount_point, sizeof(mounted_volumes[volume_count].mount_point) - 1);
    mounted_volumes[volume_count].is_mounted = 1;
    volume_count++;

    if (mount_point_out) {
        strncpy(mount_point_out, mount_point, mount_point_len);
    }

    return 0;
}

int volume_close_encrypted_home(const char *container_id) {
    for (int i = 0; i < volume_count; i++) {
        if (strcmp(mounted_volumes[i].container_id, container_id) == 0 &&
            mounted_volumes[i].is_mounted) {

            umount(mounted_volumes[i].mount_point);

            char cmd[512];
            snprintf(cmd, sizeof(cmd), "cryptsetup close %s", mounted_volumes[i].mapper_name);
            run_cmd_shell(cmd);

            rmdir(mounted_volumes[i].mount_point);

            mounted_volumes[i].is_mounted = 0;
            return 0;
        }
    }
    return -1;
}

int volume_is_home_encrypted(const char *container_id) {
    char img_path[512];
    snprintf(img_path, sizeof(img_path), "%s/%s.img", HOMES_DIR, container_id);
    return access(img_path, F_OK) == 0;
}

void volume_cleanup_all(void) {
    for (int i = 0; i < volume_count; i++) {
        if (mounted_volumes[i].is_mounted) {
            umount(mounted_volumes[i].mount_point);

            char cmd[512];
            snprintf(cmd, sizeof(cmd), "cryptsetup close %s", mounted_volumes[i].mapper_name);
            run_cmd_shell(cmd);

            rmdir(mounted_volumes[i].mount_point);
            mounted_volumes[i].is_mounted = 0;
        }
    }
    free(mounted_volumes);
    mounted_volumes = NULL;
    volume_count = 0;
    volume_capacity = 0;
}

static int is_container_running(const char *container_id) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "lxc list %s --format csv -c s 2>/dev/null", container_id);
    FILE *f = popen(cmd, "r");
    if (!f) return 0;
    char buf[32];
    int running = 0;
    if (fgets(buf, sizeof(buf), f)) {
        running = (strstr(buf, "RUNNING") != NULL);
    }
    pclose(f);
    return running;
}

void volume_cleanup_stopped_containers(void) {
    if (provisioning_active) return;
    for (int i = 0; i < volume_count; i++) {
        if (mounted_volumes[i].is_mounted) {
            if (!is_container_running(mounted_volumes[i].container_id)) {
                printf("[INFO] Container %s stopped, closing encrypted home\n", mounted_volumes[i].container_id);
                volume_close_encrypted_home(mounted_volumes[i].container_id);
            }
        }
    }
}
