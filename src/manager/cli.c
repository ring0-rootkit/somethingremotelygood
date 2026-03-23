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
    fprintf(stderr, "  %s list-anomalies                             # list pending anomaly reports\n", prog);
    fprintf(stderr, "  %s list-reports                               # list command reports\n", prog);
    fprintf(stderr, "  %s review-anomaly <id> <status>               # review anomaly report\n", prog);
}

int cmd_add_user(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "missing args\n"); return -1; }
    const char *user_id = argv[1];
    const char *pem_path = argv[2];
    const char *ssh_path = argv[3];

    FILE *f = fopen(pem_path, "r");
    if (!f) { 
        perror("fopen");
        return -1; 
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f,0,SEEK_SET);
    char *buf = malloc(sz+1);
    fread(buf,1,sz,f);
    buf[sz]='\0';
    fclose(f);

    f = fopen(ssh_path, "r");
    if (!f) { 
        perror("fopen");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f,0,SEEK_SET);
    char *buf_ssh = malloc(sz+1);
    fread(buf_ssh,1,sz,f);
    buf_ssh[sz]='\0';
    fclose(f);

    int r = db_add_user(user_id, buf, buf_ssh);
    free(buf);
    if (r == 0) {
        printf("Added user %s\n", user_id);
    }
    else {
        printf("Failed to add user\n");
    }

    return r;
}

int cmd_add_container(int argc, char **argv) {
    if (argc < 4) { 
        fprintf(stderr, "missing args\n");
        return -1;
    }

    const char *ctr_id = argv[1];
    const char *user_id = argv[2];
    const char *keyfile = argv[3];

    FILE *f = fopen(keyfile, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    fseek(f,0,SEEK_END);
    long sz = ftell(f);
    fseek(f,0,SEEK_SET);
    uint8_t *buf = malloc(sz);
    fread(buf,1,sz,f);
    fclose(f);

    int r = db_add_container(ctr_id, user_id, buf, sz);
    free(buf);
    if (r == 0) {
        printf("Added container %s for user %s\n", ctr_id, user_id);
    }
    else {
        printf("Failed to add container\n");
    }

    return r;
}

int cmd_list_anomalies(void) {
    const char *sql = "SELECT report_id, user_id, anomaly_type, severity, summary, "
                      "datetime(created_at, 'unixepoch') FROM anomaly_reports "
                      "WHERE status = 'pending' ORDER BY created_at DESC";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    printf("%-6s %-12s %-16s %-8s %-20s %s\n",
           "ID", "User", "Type", "Severity", "Created", "Summary");
    printf("-------------------------------------------------------------------------------------\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%-6d %-12s %-16s %-8s %-20s %s\n",
               sqlite3_column_int(stmt, 0),
               (const char *)sqlite3_column_text(stmt, 1),
               (const char *)sqlite3_column_text(stmt, 2),
               (const char *)sqlite3_column_text(stmt, 3),
               (const char *)sqlite3_column_text(stmt, 5),
               (const char *)sqlite3_column_text(stmt, 4));
    }
    sqlite3_finalize(stmt);
    return 0;
}

int cmd_list_reports(void) {
    const char *sql = "SELECT r.report_id, r.anomaly_report_id, r.user_id, r.container_id, "
                      "r.risk_level, datetime(r.created_at, 'unixepoch'), "
                      "substr(r.analysis_text, 1, 80) "
                      "FROM command_reports r ORDER BY r.created_at DESC";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    printf("%-6s %-10s %-12s %-12s %-12s %-20s %s\n",
           "ID", "Anomaly", "User", "Container", "Risk", "Created", "Analysis");
    printf("---------------------------------------------------------------------------------------------------\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%-6d %-10d %-12s %-12s %-12s %-20s %s\n",
               sqlite3_column_int(stmt, 0),
               sqlite3_column_int(stmt, 1),
               (const char *)sqlite3_column_text(stmt, 2),
               (const char *)sqlite3_column_text(stmt, 3),
               (const char *)sqlite3_column_text(stmt, 4),
               (const char *)sqlite3_column_text(stmt, 5),
               (const char *)sqlite3_column_text(stmt, 6));
    }
    sqlite3_finalize(stmt);
    return 0;
}

int cmd_review_anomaly(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: review-anomaly <report_id> <status>\n");
        fprintf(stderr, "  status: reviewed, escalated, dismissed\n");
        return -1;
    }
    int report_id = atoi(argv[1]);
    const char *status = argv[2];

    if (strcmp(status, "reviewed") != 0 && strcmp(status, "escalated") != 0 &&
        strcmp(status, "dismissed") != 0) {
        fprintf(stderr, "Invalid status. Use: reviewed, escalated, dismissed\n");
        return -1;
    }

    const char *sql = "UPDATE anomaly_reports SET status = ? WHERE report_id = ?";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, status, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, report_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        if (sqlite3_changes(db) > 0) {
            printf("Anomaly report %d marked as '%s'\n", report_id, status);
        } else {
            fprintf(stderr, "No report found with ID %d\n", report_id);
            return -1;
        }
    } else {
        fprintf(stderr, "Update failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}
