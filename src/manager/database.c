#include "database.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

sqlite3 *db = NULL;

int db_open(const char *password) {
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "[DB] Failed to open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    char key_cmd[512];
    snprintf(key_cmd, sizeof(key_cmd), "PRAGMA key=\"%s\";", password);

    if (sqlite3_exec(db, key_cmd, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] Failed to apply key: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    if (sqlite3_exec(db, "SELECT count(*) FROM sqlite_master;", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] Wrong database password or corrupted DB.\n");
        return -1;
    }

    return 0;
}

int db_init_schema(void) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        " user_id TEXT PRIMARY KEY,"
        " public_key_pem TEXT NOT NULL,"
        " public_key_ssh TEXT NOT NULL"
        " );"
        "CREATE TABLE IF NOT EXISTS containers ("
        " container_id TEXT PRIMARY KEY,"
        " user_id TEXT NOT NULL,"
        " container_key BLOB NOT NULL,"
        " FOREIGN KEY(user_id) REFERENCES users(user_id)"
        " );"
        "CREATE TABLE IF NOT EXISTS sessions ("
        " session_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " user_id TEXT NOT NULL,"
        " container_id TEXT NOT NULL,"
        " event_type TEXT NOT NULL,"
        " timestamp INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " FOREIGN KEY(user_id) REFERENCES users(user_id)"
        " );"
        "CREATE TABLE IF NOT EXISTS anomaly_reports ("
        " report_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " user_id TEXT NOT NULL,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " anomaly_type TEXT NOT NULL,"
        " severity TEXT NOT NULL,"
        " summary TEXT NOT NULL,"
        " details_json TEXT NOT NULL,"
        " status TEXT DEFAULT 'pending',"
        " FOREIGN KEY(user_id) REFERENCES users(user_id)"
        " );"
        "CREATE TABLE IF NOT EXISTS command_reports ("
        " report_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " anomaly_report_id INTEGER NOT NULL,"
        " user_id TEXT NOT NULL,"
        " container_id TEXT NOT NULL,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " analysis_text TEXT NOT NULL,"
        " risk_level TEXT NOT NULL,"
        " FOREIGN KEY(anomaly_report_id) REFERENCES anomaly_reports(report_id)"
        " );";

    if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] Schema init failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

int db_add_user(const char *user_id, const char *public_key_pem, const char *public_key_ssh) {
    const char *sql = "INSERT OR REPLACE INTO users(user_id, public_key_pem, public_key_ssh) VALUES(?,?, ?)";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, public_key_pem, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, public_key_ssh, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_add_container(const char *container_id, const char *user_id, const uint8_t *key_blob, size_t key_len) {
    const char *sql = "INSERT OR REPLACE INTO containers(container_id, user_id, container_key) VALUES(?,?,?)";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, container_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 3, key_blob, (int)key_len, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_user_pubkey(const char *user_id, char *pubkey_out, size_t outlen) {
    const char *sql = "SELECT public_key_pem FROM users WHERE user_id = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *pk = sqlite3_column_text(stmt, 0);
        if (pk) {
            strncpy(pubkey_out, (const char *)pk, outlen-1);
            pubkey_out[outlen-1] = '\0';
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    sqlite3_finalize(stmt);
    return -1;
}

int db_get_user_pubkey_ssh(const char *user_id, char *pubkey_out, size_t outlen) {
    const char *sql = "SELECT public_key_ssh FROM users WHERE user_id = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *pk = sqlite3_column_text(stmt, 0);
        if (pk) {
            strncpy(pubkey_out, (const char *)pk, outlen-1);
            pubkey_out[outlen-1] = '\0';
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    sqlite3_finalize(stmt);
    return -1;
}

int db_get_container_key(const char *user_id, const char *container_id, uint8_t *key_out, size_t key_len) {
    const char *sql = "SELECT container_key FROM containers WHERE user_id = ? AND container_id = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, container_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(stmt, 0);
        int blob_len = sqlite3_column_bytes(stmt, 0);
        if ((size_t)blob_len != key_len) {
            fprintf(stderr, "[DB] Key length mismatch. Expected %zu, got %d\n", key_len, blob_len);
            sqlite3_finalize(stmt);
            return -1;
        }
        memcpy(key_out, blob, key_len);
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return -1;
}

int db_log_session_event(const char *user_id, const char *container_id, const char *event_type) {
    const char *sql = "INSERT INTO sessions(user_id, container_id, event_type) VALUES(?,?,?)";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, container_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, event_type, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}
