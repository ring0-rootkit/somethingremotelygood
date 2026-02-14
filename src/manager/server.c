#include "server.h"
#include "config.h"
#include "database.h"
#include "crypto.h"
#include "docker.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/rand.h>

void handle_client(int client_fd) {
    char userid[128];
    char containerid[128];
    uint8_t challenge[CHALLENGE_SIZE];
    uint8_t signature[1024];
    size_t sig_len = 0;

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

    if (docker_start_container(containerid) != 0) {
        printf("[INFO] Container %s not found, creating...\n", containerid);
        int res = docker_create_basic_container(containerid, "ubuntu:22.04", userid);
        if (res != 0) {
            printf("[INFO] Container %s setup returned error, %d\n", containerid, res);
            return;
        }
        docker_start_container(containerid);
    }
    if (docker_unlock_container(containerid, container_key, sizeof(container_key)) != 0) {
        write(client_fd, "ERR UNLOCK_FAIL", 15);
        return;
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
    sleep(1);
}

int start_manager() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }
    int opt = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(MANAGER_PORT);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); close(s); return -1; }
    if (listen(s, 5) < 0) { perror("listen"); close(s); return -1; }

    printf("Manager running on port %d...\n", MANAGER_PORT);
    while (1) {
        int client_fd = accept(s, NULL, NULL);
        if (client_fd < 0) continue;
        handle_client(client_fd);
        close(client_fd);
    }
    close(s);
    return 0;
}
