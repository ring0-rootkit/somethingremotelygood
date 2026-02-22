#include "server.h"
#include "config.h"
#include "database.h"
#include "crypto.h"
#include "docker.h"
#include "volume.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/rand.h>

#define CLEANUP_INTERVAL_SEC 10

static volatile int server_running = 1;
static int server_socket = -1;
static pthread_t cleanup_thread;

static void *cleanup_worker(void *arg) {
    (void)arg;
    while (server_running) {
        sleep(CLEANUP_INTERVAL_SEC);
        if (!server_running) break;
        volume_cleanup_stopped_containers();
    }
    return NULL;
}

static void signal_handler(int sig) {
    (void)sig;
    printf("\n[INFO] Shutting down manager, cleaning up encrypted volumes...\n");
    server_running = 0;
    volume_cleanup_all();
    if (server_socket >= 0) {
        close(server_socket);
    }
}

void handle_client(int client_fd) {
    char userid[128];
    char containerid[128];
    uint8_t challenge[CHALLENGE_SIZE];
    uint8_t signature[1024];
    size_t sig_len = 0;

    volume_cleanup_stopped_containers();

    ssize_t r = read(client_fd, userid, sizeof(userid)-1);
    if (r <= 0) {
        write(client_fd, "ERR NO_USER_ID", 13);
        return;
    }
    userid[r] = '\0';

    if (RAND_bytes(challenge, sizeof(challenge)) != 1) {
        write(client_fd, "ERR RAND", 8);
        return;
    }
    if (write(client_fd, challenge, sizeof(challenge)) != sizeof(challenge)) {
        write(client_fd, "ERR SEND_CHAL", 12);
        return;
    }

    r = read(client_fd, signature, sizeof(signature));
    if (r <= 0) {
        write(client_fd, "ERR NOSIG", 8);
        return;
    }
    sig_len = (size_t)r;

    char pubkey_pem[4096];
    if (db_get_user_pubkey(userid, pubkey_pem, sizeof(pubkey_pem)) != 0) {
        write(client_fd, "ERR NO_USER", 10);
        return;
    }

    char pubkey_ssh[4096];
    if (db_get_user_pubkey_ssh(userid, pubkey_ssh, sizeof(pubkey_ssh)) != 0) {
        write(client_fd, "ERR NO_USER", 10);
        return;
    }

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
    containerid[r] = '\0';

    uint8_t container_key[CONTAINER_KEY_LEN];
    if (db_get_container_key(userid, containerid, container_key, sizeof(container_key)) != 0) {
        write(client_fd, "ERR NO_CONTAINER_KEY", 20);
        return;
    }

    if (!volume_is_home_encrypted(containerid)) {
        printf("[INFO] Creating encrypted home for %s...\n", containerid);
        if (volume_create_encrypted_home(containerid, container_key, sizeof(container_key)) != 0) {
            write(client_fd, "ERR ENCRYPT_HOME", 16);
            return;
        }
    }

    char mount_point[512];
    if (volume_open_encrypted_home(containerid, container_key, sizeof(container_key), mount_point, sizeof(mount_point)) != 0) {
        write(client_fd, "ERR OPEN_HOME", 13);
        return;
    }
    printf("[INFO] Encrypted home mounted at %s\n", mount_point);

    if (docker_setup_ssh_in_volume(mount_point, userid, pubkey_ssh) != 0) {
        write(client_fd, "ERR SSH_SETUP", 13);
        volume_close_encrypted_home(containerid);
        return;
    }

    if (docker_start_container(containerid) != 0) {
        printf("[INFO] Container %s not found, creating...\n", containerid);
        int res = docker_create_basic_container(containerid, "ubuntu:22.04", userid, mount_point);
        if (res != 0) {
            printf("[INFO] Container %s setup returned error, %d\n", containerid, res);
            volume_close_encrypted_home(containerid);
            return;
        }
        docker_start_container(containerid);
    }

    if (docker_fix_ssh_permissions(containerid, userid) != 0) {
        fprintf(stderr, "[WARN] Could not fix SSH permissions, SSH may fail\n");
    }

    if (docker_ensure_sshd_running(containerid) != 0) {
        write(client_fd, "ERR SSHD_FAIL", 13);
        return;
    }

    printf("sshd is running\n");

    if (docker_create_user_and_inject_ssh(containerid, userid, pubkey_pem, pubkey_ssh) != 0) {
        write(client_fd, "ERR INJECT_SSH", 13);
        return;
    }

    printf("ssh injected\n");

    int ssh_port = docker_get_ssh_port(containerid);
    if (ssh_port <= 0) {
        write(client_fd, "ERR SSH_PORT", 12);
        return;
    }

    printf("got ssh port\n");
    write(client_fd, "OK", 2);

    char info[128];
    snprintf(info, sizeof(info), "%d\n", ssh_port);
    write(client_fd, info, strlen(info));

    printf("[INFO] Waiting for user to connect via SSH to %s...\n", containerid);

    int ssh_connected = 0;
    int connect_wait = 0;
    while (!ssh_connected && connect_wait < 60) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "docker exec %s sh -c 'netstat -tn 2>/dev/null | grep :22 | grep ESTABLISHED | wc -l' 2>/dev/null || echo 0", containerid);
        FILE *f = popen(cmd, "r");
        if (f) {
            char buf[16];
            if (fgets(buf, sizeof(buf), f)) {
                int connections = atoi(buf);
                if (connections > 0) {
                    ssh_connected = 1;
                    printf("[INFO] SSH connection detected on %s\n", containerid);
                }
            }
            pclose(f);
        }
        if (!ssh_connected) {
            sleep(1);
            connect_wait++;
        }
    }

    if (!ssh_connected) {
        printf("[INFO] No SSH connection to %s within 60s, stopping container\n", containerid);
        docker_stop_container(containerid);
        printf("[INFO] Container %s stopped and home encrypted\n", containerid);
        return;
    }

    printf("[INFO] Waiting for SSH connections to close on %s...\n", containerid);

    int ssh_active = 1;
    int check_count = 0;
    int empty_count = 0;
    while (ssh_active && check_count < 1800) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "docker exec %s sh -c 'netstat -tn 2>/dev/null | grep :22 | grep ESTABLISHED | wc -l' 2>/dev/null || echo 0", containerid);
        FILE *f = popen(cmd, "r");
        if (f) {
            char buf[16];
            if (fgets(buf, sizeof(buf), f)) {
                int connections = atoi(buf);
                if (connections == 0) {
                    empty_count++;
                    if (empty_count >= 3) {
                        ssh_active = 0;
                        printf("[INFO] No active SSH connections on %s, stopping container\n", containerid);
                    }
                } else {
                    empty_count = 0;
                }
            }
            pclose(f);
        }
        if (ssh_active) {
            sleep(1);
            check_count++;
        }
    }

    if (ssh_active) {
        printf("[INFO] Timeout reached for %s, stopping container\n", containerid);
    }

    docker_stop_container(containerid);
    printf("[INFO] Container %s stopped and home encrypted\n", containerid);
}

int start_manager() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (pthread_create(&cleanup_thread, NULL, cleanup_worker, NULL) != 0) {
        fprintf(stderr, "[ERR] Failed to create cleanup thread\n");
        return -1;
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) { perror("socket"); return -1; }
    int opt = 1; setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(MANAGER_PORT),
    };

    if (bind(server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) { 
        perror("bind"); close(server_socket); return -1; 
    }
    if (listen(server_socket, 5) < 0) { 
        perror("listen"); close(server_socket); return -1; 
    }

    printf("Manager running on port %d... (Press Ctrl+C to stop)\n", MANAGER_PORT);
    printf("[INFO] Cleanup thread started (interval: %d seconds)\n", CLEANUP_INTERVAL_SEC);
    while (server_running) {
        int client_fd = accept(server_socket, NULL, NULL);
        if (client_fd < 0) {
            if (!server_running) break;
            continue;
        }
        handle_client(client_fd);
        close(client_fd);
    }
    close(server_socket);

    pthread_join(cleanup_thread, NULL);
    return 0;
}
