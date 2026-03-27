#include "lxd.h"
#include "volume.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

static int find_free_port(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    socklen_t len = sizeof(addr);
    getsockname(sock, (struct sockaddr *)&addr, &len);
    int port = ntohs(addr.sin_port);
    close(sock);
    return port;
}

int lxd_create_container(const char *container_id, const char *image, const char *user_id, const char *home_mount) {
    char cmd[2048];

    snprintf(cmd, sizeof(cmd), "lxc init %s %s", image, container_id);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to init container %s\n", container_id);
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "lxc config set %s security.privileged true", container_id);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to set privileged mode for %s\n", container_id);
        lxd_remove_container(container_id);
        return -1;
    }

    if (home_mount && strlen(home_mount) > 0) {
        char abs_mount[512];
        if (realpath(home_mount, abs_mount) == NULL) {
            /* realpath fails if path doesn't exist; build absolute path manually */
            char cwd[256];
            if (getcwd(cwd, sizeof(cwd)) != NULL && home_mount[0] != '/') {
                const char *rel = home_mount;
                if (rel[0] == '.' && rel[1] == '/') rel += 2;
                snprintf(abs_mount, sizeof(abs_mount), "%s/%s", cwd, rel);
            } else {
                strncpy(abs_mount, home_mount, sizeof(abs_mount) - 1);
                abs_mount[sizeof(abs_mount) - 1] = '\0';
            }
        }
        snprintf(cmd, sizeof(cmd),
            "lxc config device add %s home disk source=%s path=/home/%s",
            container_id, abs_mount, user_id);
        if (run_cmd_shell(cmd) != 0) {
            fprintf(stderr, "[ERR] Failed to add disk device to %s\n", container_id);
            lxd_remove_container(container_id);
            return -1;
        }
    }

    int port = find_free_port();
    if (port <= 0) {
        fprintf(stderr, "[ERR] Failed to find free port for SSH proxy\n");
        lxd_remove_container(container_id);
        return -1;
    }

    snprintf(cmd, sizeof(cmd),
        "lxc config device add %s ssh proxy listen=tcp:0.0.0.0:%d connect=tcp:127.0.0.1:22",
        container_id, port);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to add SSH proxy device to %s\n", container_id);
        lxd_remove_container(container_id);
        return -1;
    }

    printf("[INFO] Container %s created with SSH proxy on port %d\n", container_id, port);
    return 0;
}

int lxd_start_container(const char *container_id) {
    char *argv[] = {"lxc", "start", (char *)container_id, NULL};
    return run_cmdv(argv);
}

int lxd_stop_container(const char *container_id) {
    char *argv[] = {"lxc", "stop", (char *)container_id, NULL};
    int r = run_cmdv(argv);
    volume_close_encrypted_home(container_id);
    return r;
}

int lxd_remove_container(const char *container_id) {
    char *argv[] = {"lxc", "delete", "--force", (char *)container_id, NULL};
    int r = run_cmdv(argv);
    volume_close_encrypted_home(container_id);
    return r;
}

int lxd_setup_ssh_in_volume(const char *mount_point, const char *userid, const char *pubkey_ssh) {
    char ssh_dir[512];
    char auth_keys[512];

    snprintf(ssh_dir, sizeof(ssh_dir), "%s/.ssh", mount_point);
    snprintf(auth_keys, sizeof(auth_keys), "%s/.ssh/authorized_keys", mount_point);

    if (mkdir(ssh_dir, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ERR] Failed to create .ssh dir in volume: %s\n", ssh_dir);
        return -1;
    }

    if (!pubkey_ssh || strlen(pubkey_ssh) == 0) {
        fprintf(stderr, "[ERR] Empty SSH public key for user %s\n", userid);
        return -1;
    }

    FILE *f = fopen(auth_keys, "w");
    if (!f) {
        fprintf(stderr, "[ERR] Failed to create authorized_keys in volume\n");
        return -1;
    }
    fprintf(f, "%s\n", pubkey_ssh);
    fclose(f);

    chmod(auth_keys, 0600);
    chmod(ssh_dir, 0700);

    printf("[INFO] SSH keys set up in volume for %s (%zu bytes)\n", userid, strlen(pubkey_ssh));

    char bash_profile[512];
    snprintf(bash_profile, sizeof(bash_profile), "%s/.bash_profile", mount_point);
    if (access(bash_profile, F_OK) != 0) {
        f = fopen(bash_profile, "w");
        if (f) {
            fprintf(f,
                "export HISTFILE=~/.bash_history\n"
                "export HISTSIZE=1000\n"
                "export HISTFILESIZE=2000\n"
                "shopt -s histappend\n"
                "[ -f ~/.bashrc ] && . ~/.bashrc\n");
            fclose(f);
            chmod(bash_profile, 0644);
        }
    }

    return 0;
}

int lxd_create_user_and_setup(const char *container_id, const char *userid) {
    char cmd[1024];

    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- apk add --no-cache bash sudo",
        container_id);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to install packages in container %s\n", container_id);
        return -1;
    }

    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- sh -c 'id -u %s >/dev/null 2>&1 || adduser -D -s /bin/bash %s'",
        container_id, userid, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to create user %s in container %s\n", userid, container_id);
        return -1;
    }

    /* unlock account - adduser -D leaves it locked, sshd refuses locked accounts */
    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- passwd -u %s 2>/dev/null || "
        "lxc exec %s -- sh -c 'sed -i \"s/^%s:!:/%s:*:/\" /etc/shadow'",
        container_id, userid, container_id, userid, userid);
    run_cmd_shell(cmd);

    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- sh -c 'echo \"%s ALL=(ALL) NOPASSWD:ALL\" > /etc/sudoers.d/%s && chmod 440 /etc/sudoers.d/%s'",
        container_id, userid, userid, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to add %s to sudoers in container %s\n", userid, container_id);
        return -1;
    }

    return 0;
}

int lxd_fix_ssh_permissions(const char *container_id, const char *userid) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- chown -R %s:%s /home/%s",
        container_id, userid, userid, userid);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[WARN] Failed to fix home permissions for %s in %s\n", userid, container_id);
        return -1;
    }
    printf("[INFO] Fixed home permissions for %s in container %s\n", userid, container_id);
    return 0;
}

int lxd_ensure_sshd_running(const char *container_id) {
    printf("[sshd] Ensuring OpenSSH server is installed and configured inside %s\n", container_id);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- sh -c '"
        "if pgrep -x sshd >/dev/null 2>&1; then "
        "  echo \"[sshd] already running\"; exit 0; "
        "fi; "
        "apk add --no-cache openssh; "
        "ssh-keygen -A 2>/dev/null; "
        "sed -i \"s/^#*PasswordAuthentication.*/PasswordAuthentication no/\" /etc/ssh/sshd_config; "
        "sed -i \"s/^#*PubkeyAuthentication.*/PubkeyAuthentication yes/\" /etc/ssh/sshd_config; "
        "sed -i \"s/^#*StrictModes.*/StrictModes no/\" /etc/ssh/sshd_config; "
        "grep -q StrictModes /etc/ssh/sshd_config || echo StrictModes no >> /etc/ssh/sshd_config; "
        "sed -i \"s/^#*LogLevel.*/LogLevel DEBUG3/\" /etc/ssh/sshd_config; "
        "grep -q LogLevel /etc/ssh/sshd_config || echo LogLevel DEBUG3 >> /etc/ssh/sshd_config; "
        "mkdir -p /run/sshd; "
        "/usr/sbin/sshd; "
        "echo \"[sshd] started\"'"
        , container_id);

    return run_cmd_shell(cmd);
}

int lxd_get_ssh_port(const char *container_id) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "lxc config device get %s ssh listen 2>/dev/null", container_id);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    char buf[128];
    if (!fgets(buf, sizeof(buf), f)) { pclose(f); return -1; }
    pclose(f);
    /* output is "tcp:0.0.0.0:PORT" */
    char *p = strrchr(buf, ':');
    if (!p) return -1;
    return atoi(p + 1);
}

int lxd_setup_network(const char *container_id) {
    char cmd[512];
    char gateway[64] = "";
    int gw_a = 0, gw_b = 0, gw_c = 0, gw_d = 0;

    /* get lxdbr0 gateway IP from LXD config */
    FILE *f = popen("lxc network get lxdbr0 ipv4.address 2>/dev/null", "r");
    if (f) {
        char buf[64];
        if (fgets(buf, sizeof(buf), f)) {
            /* format: "10.162.242.1/24" */
            sscanf(buf, "%d.%d.%d.%d", &gw_a, &gw_b, &gw_c, &gw_d);
            snprintf(gateway, sizeof(gateway), "%d.%d.%d.%d", gw_a, gw_b, gw_c, gw_d);
        }
        pclose(f);
    }

    if (!gateway[0]) {
        fprintf(stderr, "[ERR] Could not determine lxdbr0 gateway\n");
        return -1;
    }

    /* derive a container IP: hash the name to get last octet (2-254) */
    unsigned int hash = 0;
    for (const char *p = container_id; *p; p++)
        hash = hash * 31 + (unsigned char)*p;
    int host = (hash % 253) + 2;

    printf("[INFO] Configuring static IP %d.%d.%d.%d for %s (gw %s)\n",
           gw_a, gw_b, gw_c, host, container_id, gateway);

    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- ip addr add %d.%d.%d.%d/24 dev eth0",
        container_id, gw_a, gw_b, gw_c, host);
    run_cmd_shell(cmd);

    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- ip route add default via %s",
        container_id, gateway);
    run_cmd_shell(cmd);

    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- sh -c 'echo nameserver 8.8.8.8 > /etc/resolv.conf'",
        container_id);
    run_cmd_shell(cmd);

    /* ensure iptables forwarding rules for lxdbr0 */
    run_cmd_shell("iptables-legacy -C FORWARD -i lxdbr0 -j ACCEPT 2>/dev/null || "
                  "iptables-legacy -I FORWARD -i lxdbr0 -j ACCEPT");
    run_cmd_shell("iptables-legacy -C FORWARD -o lxdbr0 -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || "
                  "iptables-legacy -I FORWARD -o lxdbr0 -m state --state RELATED,ESTABLISHED -j ACCEPT");

    /* ensure NAT masquerade for the bridge subnet */
    snprintf(cmd, sizeof(cmd),
        "iptables-legacy -t nat -C POSTROUTING -s %d.%d.%d.0/24 ! -d %d.%d.%d.0/24 -j MASQUERADE 2>/dev/null || "
        "iptables-legacy -t nat -A POSTROUTING -s %d.%d.%d.0/24 ! -d %d.%d.%d.0/24 -j MASQUERADE",
        gw_a, gw_b, gw_c, gw_a, gw_b, gw_c,
        gw_a, gw_b, gw_c, gw_a, gw_b, gw_c);
    run_cmd_shell(cmd);

    /* verify connectivity */
    snprintf(cmd, sizeof(cmd),
        "lxc exec %s -- ping -c1 -W2 8.8.8.8 >/dev/null 2>&1",
        container_id);
    if (run_cmd_shell(cmd) != 0) {
        fprintf(stderr, "[WARN] Container %s has no network connectivity\n", container_id);
        return -1;
    }

    return 0;
}

int lxd_is_running(const char *container_id) {
    char cmd[256];
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
