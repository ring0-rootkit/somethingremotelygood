#include "cli.h"
#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s serve                # run manager\n", prog);
    fprintf(stderr, "  %s add-user <id> <pubkey.pem> <pubkey_ssh>   # add/update user\n", prog);
    fprintf(stderr, "  %s add-container <ctr_id> <user_id> <keyfile> # add container record\n", prog);
}

int cmd_add_user(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "missing args\n"); return -1; }
    const char *user_id = argv[1];
    const char *pem_path = argv[2];
    const char *ssh_path = argv[3];
    FILE *f = fopen(pem_path, "r"); if (!f) { perror("fopen"); return -1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    char *buf = malloc(sz+1); fread(buf,1,sz,f); buf[sz]='\0'; fclose(f);

    f = fopen(ssh_path, "r"); if (!f) { perror("fopen"); return -1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f,0,SEEK_SET);
    char *buf_ssh = malloc(sz+1); fread(buf_ssh,1,sz,f); buf_ssh[sz]='\0'; fclose(f);

    int r = db_add_user(user_id, buf, buf_ssh);
    free(buf);
    if (r == 0) printf("Added user %s\n", user_id); else printf("Failed to add user\n");
    return r;
}

int cmd_add_container(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "missing args\n"); return -1; }
    const char *ctr_id = argv[1];
    const char *user_id = argv[2];
    const char *keyfile = argv[3];
    FILE *f = fopen(keyfile, "rb"); if (!f) { perror("fopen"); return -1; }
    fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *buf = malloc(sz); fread(buf,1,sz,f); fclose(f);
    int r = db_add_container(ctr_id, user_id, buf, sz);
    free(buf);
    if (r == 0) printf("Added container %s for user %s\n", ctr_id, user_id); else printf("Failed to add container\n");
    return r;
}
