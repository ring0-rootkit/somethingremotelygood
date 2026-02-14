#include "config.h"
#include "database.h"
#include "server.h"
#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *db_pass = getenv("DB_PASSWORD");
    if (!db_pass) { fprintf(stderr, "ERROR: DB_PASSWORD env not set.\n"); return 1; }

    if (db_open(db_pass) != 0) { fprintf(stderr, "Unable to open DB.\n"); return 1; }
    if (db_init_schema() != 0) { fprintf(stderr, "DB schema init failed.\n"); return 1; }

    const char *cmd = argv[1];
    if (strcmp(cmd, "serve") == 0) {
        return start_manager();
    } else if (strcmp(cmd, "add-user") == 0) {
        return cmd_add_user(argc-1, &argv[1]);
    } else if (strcmp(cmd, "add-container") == 0) {
        return cmd_add_container(argc-1, &argv[1]);
    }

    usage(argv[0]);
    return 1;
}
