"""Database access layer for AI analysis tools (pysqlcipher3)."""

from pysqlcipher3 import dbapi2 as sqlcipher
from ai.config import DB_PASSWORD, DB_PATH


def get_connection():
    conn = sqlcipher.connect(DB_PATH)
    conn.execute(f'PRAGMA key="{DB_PASSWORD}"')
    conn.execute("SELECT count(*) FROM sqlite_master")
    return conn


def get_sessions(user_id=None, since=None, conn=None):
    """Get session events, optionally filtered by user and time."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        sql = "SELECT session_id, user_id, container_id, event_type, timestamp FROM sessions WHERE 1=1"
        params = []
        if user_id:
            sql += " AND user_id = ?"
            params.append(user_id)
        if since:
            sql += " AND timestamp >= ?"
            params.append(int(since))
        sql += " ORDER BY timestamp ASC"
        cursor = conn.execute(sql, params)
        columns = ["session_id", "user_id", "container_id", "event_type", "timestamp"]
        return [dict(zip(columns, row)) for row in cursor.fetchall()]
    finally:
        if own_conn:
            conn.close()


def get_all_users(conn=None):
    """Get list of all user IDs."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        cursor = conn.execute("SELECT user_id FROM users")
        return [row[0] for row in cursor.fetchall()]
    finally:
        if own_conn:
            conn.close()


def insert_anomaly_report(user_id, anomaly_type, severity, summary, details_json, conn=None):
    """Insert an anomaly report and return the report_id."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        cursor = conn.execute(
            "INSERT INTO anomaly_reports(user_id, anomaly_type, severity, summary, details_json) "
            "VALUES(?,?,?,?,?)",
            (user_id, anomaly_type, severity, summary, details_json),
        )
        conn.commit()
        return cursor.lastrowid
    finally:
        if own_conn:
            conn.close()


def get_pending_anomalies(conn=None):
    """Get all pending anomaly reports."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        cursor = conn.execute(
            "SELECT report_id, user_id, anomaly_type, severity, summary, details_json, "
            "datetime(created_at, 'unixepoch') as created "
            "FROM anomaly_reports WHERE status = 'pending' ORDER BY created_at DESC"
        )
        columns = ["report_id", "user_id", "anomaly_type", "severity", "summary", "details_json", "created"]
        return [dict(zip(columns, row)) for row in cursor.fetchall()]
    finally:
        if own_conn:
            conn.close()


def insert_command_report(anomaly_id, user_id, container_id, analysis, risk_level, conn=None):
    """Insert a command analysis report."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        cursor = conn.execute(
            "INSERT INTO command_reports(anomaly_report_id, user_id, container_id, analysis_text, risk_level) "
            "VALUES(?,?,?,?,?)",
            (anomaly_id, user_id, container_id, analysis, risk_level),
        )
        conn.commit()
        return cursor.lastrowid
    finally:
        if own_conn:
            conn.close()


def update_anomaly_status(report_id, status, conn=None):
    """Update the status of an anomaly report."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        conn.execute(
            "UPDATE anomaly_reports SET status = ? WHERE report_id = ?",
            (status, report_id),
        )
        conn.commit()
    finally:
        if own_conn:
            conn.close()


def get_container_key(user_id, container_id, conn=None):
    """Get the container encryption key blob."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        cursor = conn.execute(
            "SELECT container_key FROM containers WHERE user_id = ? AND container_id = ?",
            (user_id, container_id),
        )
        row = cursor.fetchone()
        return row[0] if row else None
    finally:
        if own_conn:
            conn.close()


def get_anomaly_report(report_id, conn=None):
    """Get a specific anomaly report by ID."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        cursor = conn.execute(
            "SELECT report_id, user_id, anomaly_type, severity, summary, details_json, "
            "datetime(created_at, 'unixepoch') as created, status "
            "FROM anomaly_reports WHERE report_id = ?",
            (report_id,),
        )
        columns = ["report_id", "user_id", "anomaly_type", "severity", "summary",
                    "details_json", "created", "status"]
        row = cursor.fetchone()
        return dict(zip(columns, row)) if row else None
    finally:
        if own_conn:
            conn.close()
