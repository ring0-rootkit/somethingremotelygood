#include "docker.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    (void)secret_name; (void)container_id;
    return 0;
}

int docker_create_user_and_inject_ssh(const char *container_id, const char *userid, const char *pubkey_pem, const char *pubkey_ssh) {
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
        "docker exec %s mkdir -p /home/%s/.ssh && "
        "docker exec %s chmod 700 /home/%s/.ssh && "
        "docker exec %s chown %s:%s /home/%s/.ssh",
        container_id, userid, container_id, userid, container_id, userid, userid, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to create .ssh dir for %s in container %s\n", userid, container_id);
        return -1;
    }

    char tmp[256];
    if (write_temp("/tmp/sshkey", pubkey_ssh, strlen(pubkey_ssh), tmp, sizeof(tmp)) != 0) {
        fprintf(stderr, "[ERR] Failed to write temp SSH key\n");
        return -1;
    }

    snprintf(cmd, sizeof(cmd),
        "docker cp %s %s:/home/%s/.ssh/authorized_keys",
        tmp, container_id, userid);
    int r = run_cmd_shell(cmd);
    unlink(tmp);
    if (r != 0) return -1;

    snprintf(cmd, sizeof(cmd),
        "docker exec %s chmod 600 /home/%s/.ssh/authorized_keys && "
        "docker exec %s chown %s:%s /home/%s/.ssh/authorized_keys",
        container_id, userid, container_id, userid, userid, userid);
    if (run_cmd_shell(cmd) != 0) return -1;

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

int docker_unlock_container(const char *container_id, const uint8_t *key, size_t key_len) {
    char tmp[256];
    if (write_temp("/tmp/ctrkey", key, key_len, tmp, sizeof(tmp)) != 0) return -1;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "docker cp %s %s:/root/container_key.bin", tmp, container_id);
    int r = run_cmd_shell(cmd);
    unlink(tmp);
    if (r != 0) return -1;

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
