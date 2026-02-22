#include "docker.h"
#include "volume.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

int docker_create_basic_container(const char *container_id, const char *image, const char *user_id, const char *home_mount) {
    char cmd[1024];
    if (home_mount && strlen(home_mount) > 0) {
        snprintf(cmd, sizeof(cmd),
            "docker create -p 0:22 --name %s --hostname %s -v %s:/home/%s %s /bin/sh -c 'while true; do sleep 3600; done'",
            container_id, container_id, home_mount, user_id, image);
    } else {
        snprintf(cmd, sizeof(cmd),
            "docker create -p 0:22 --name %s --hostname %s %s /bin/sh -c 'while true; do sleep 3600; done'",
            container_id, container_id, image);
    }
    return run_cmd_shell(cmd);
}

int docker_start_container(const char *container_id) {
    char *argv[] = {"docker","start",(char *)container_id,NULL};
    return run_cmdv(argv);
}

int docker_stop_container(const char *container_id) {
    char *argv[] = {"docker","stop",(char *)container_id,NULL};
    int r = run_cmdv(argv);
    volume_close_encrypted_home(container_id);
    return r;
}

int docker_remove_container(const char *container_id) {
    char *argv[] = {"docker","rm","-f",(char *)container_id,NULL};
    int r = run_cmdv(argv);
    volume_close_encrypted_home(container_id);
    return r;
}

int docker_setup_ssh_in_volume(const char *mount_point, const char *userid, const char *pubkey_ssh) {
    char ssh_dir[512];
    char auth_keys[512];

    snprintf(ssh_dir, sizeof(ssh_dir), "%s/.ssh", mount_point);
    snprintf(auth_keys, sizeof(auth_keys), "%s/.ssh/authorized_keys", mount_point);

    struct stat st;
    int needs_setup = 1;
    if (stat(auth_keys, &st) == 0 && st.st_size > 0) {
        needs_setup = 0;
    }

    if (needs_setup) {
        if (mkdir(ssh_dir, 0700) != 0 && errno != EEXIST) {
            fprintf(stderr, "[ERR] Failed to create .ssh dir in volume: %s\n", ssh_dir);
            return -1;
        }

        FILE *f = fopen(auth_keys, "w");
        if (!f) {
            fprintf(stderr, "[ERR] Failed to create authorized_keys in volume\n");
            return -1;
        }
        if (!pubkey_ssh || strlen(pubkey_ssh) == 0) {
            fprintf(stderr, "[ERR] Empty SSH public key for user %s\n", userid);
            fclose(f);
            unlink(auth_keys);
            return -1;
        }
        fprintf(f, "%s\n", pubkey_ssh);
        fclose(f);

        chmod(auth_keys, 0600);
        chmod(ssh_dir, 0700);

        printf("[INFO] SSH keys set up in volume for %s (%zu bytes)\n", userid, strlen(pubkey_ssh));
    } else {
        printf("[INFO] SSH already set up in volume for %s\n", userid);
    }

    return 0;
}

int docker_create_user_and_inject_ssh(const char *container_id, const char *userid, const char *pubkey_pem, const char *pubkey_ssh) {
    (void)pubkey_pem;
    (void)pubkey_ssh;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "docker exec %s id -u %s >/dev/null 2>&1 || "
        "docker exec %s useradd -m -s /bin/bash %s",
        container_id, userid, container_id, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to create user %s in container %s\n", userid, container_id);
        return -1;
    }

    snprintf(cmd, sizeof(cmd),
        "docker exec %s sh -c 'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y sudo'",
        container_id);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to install sudo in container %s\n", container_id);
        return -1;
    }

    snprintf(cmd, sizeof(cmd),
        "docker exec %s sh -c 'echo \"%s ALL=(ALL) NOPASSWD:ALL\" > /etc/sudoers.d/%s && chmod 440 /etc/sudoers.d/%s'",
        container_id, userid, userid, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to add %s to sudoers in container %s\n", userid, container_id);
        return -1;
    }

    return 0;
}

int docker_fix_ssh_permissions(const char *container_id, const char *userid) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "docker exec %s chown -R %s:%s /home/%s",
        container_id, userid, userid, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[WARN] Failed to fix home permissions for %s in %s\n", userid, container_id);
        return -1;
    }
    printf("[INFO] Fixed home permissions for %s in container %s\n", userid, container_id);
    return 0;
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
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "docker port %s 22", container_id);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    char buf[128];
    if (!fgets(buf, sizeof(buf), f)) { pclose(f); return -1; }
    pclose(f);
    char *p = strrchr(buf, ':');
    if (!p) return -1;
    int port = atoi(p+1);
    return port;
}
