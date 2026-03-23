"""Database access layer using sqlcipher CLI subprocess (no C extension needed)."""

import csv
import io
import subprocess
from ai.config import DB_PASSWORD, DB_PATH


class SQLCipherConnection:
    """Wrapper around the sqlcipher CLI for executing queries."""

    def __init__(self):
        self._closed = False

    def _run(self, sql, raw=False):
        """Execute SQL via sqlcipher CLI and return output."""
        if self._closed:
            raise RuntimeError("Connection is closed")
        commands = f'PRAGMA key="{DB_PASSWORD}";\n'
        if not raw:
            commands += ".mode csv\n.headers off\n"
        commands += sql.rstrip(";") + ";\n"
        result = subprocess.run(
            ["sqlcipher", DB_PATH],
            input=commands,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"sqlcipher error: {result.stderr.strip()}")
        return result.stdout

    def execute(self, sql, params=None):
        """Execute a query with optional parameter substitution.

        Uses safe quoting for parameters (single quotes escaped).
        Returns list of tuples for SELECT, or empty list for non-SELECT.
        """
        if params:
            sql = self._bind_params(sql, params)
        output = self._run(sql)
        if not output.strip():
            return []
        reader = csv.reader(io.StringIO(output))
        rows = []
        for row in reader:
            converted = []
            for val in row:
                # Try to convert to int/float where appropriate
                try:
                    converted.append(int(val))
                except ValueError:
                    try:
                        converted.append(float(val))
                    except ValueError:
                        converted.append(val)
            rows.append(tuple(converted))
        return rows

    def execute_insert(self, sql, params=None):
        """Execute an INSERT and return the last rowid."""
        if params:
            sql = self._bind_params(sql, params)
        combined = sql.rstrip(";") + ";\nSELECT last_insert_rowid();"
        output = self._run(combined)
        for line in output.strip().splitlines():
            line = line.strip()
            if line:
                try:
                    return int(line)
                except ValueError:
                    continue
        return None

    def execute_update(self, sql, params=None):
        """Execute an UPDATE/DELETE statement."""
        if params:
            sql = self._bind_params(sql, params)
        self._run(sql)

    @staticmethod
    def _bind_params(sql, params):
        """Replace ? placeholders with safely quoted parameter values."""
        result = []
        param_iter = iter(params)
        i = 0
        while i < len(sql):
            if sql[i] == "?":
                val = next(param_iter)
                if val is None:
                    result.append("NULL")
                elif isinstance(val, (int, float)):
                    result.append(str(val))
                elif isinstance(val, bytes):
                    result.append("X'" + val.hex() + "'")
                else:
                    escaped = str(val).replace("'", "''")
                    result.append(f"'{escaped}'")
                i += 1
            else:
                result.append(sql[i])
                i += 1
        return "".join(result)

    def close(self):
        self._closed = True


def get_connection():
    """Return a new SQLCipher connection wrapper."""
    conn = SQLCipherConnection()
    # Verify the password works
    rows = conn.execute("SELECT count(*) FROM sqlite_master")
    if not rows:
        raise RuntimeError("Failed to open database — wrong password?")
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
        rows = conn.execute(sql, params if params else None)
        columns = ["session_id", "user_id", "container_id", "event_type", "timestamp"]
        return [dict(zip(columns, row)) for row in rows]
    finally:
        if own_conn:
            conn.close()


def get_all_users(conn=None):
    """Get list of all user IDs."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        rows = conn.execute("SELECT user_id FROM users")
        return [row[0] for row in rows]
    finally:
        if own_conn:
            conn.close()


def insert_anomaly_report(user_id, anomaly_type, severity, summary, details_json, conn=None):
    """Insert an anomaly report and return the report_id."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        return conn.execute_insert(
            "INSERT INTO anomaly_reports(user_id, anomaly_type, severity, summary, details_json) "
            "VALUES(?,?,?,?,?)",
            (user_id, anomaly_type, severity, summary, details_json),
        )
    finally:
        if own_conn:
            conn.close()


def get_pending_anomalies(conn=None):
    """Get all pending anomaly reports."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        rows = conn.execute(
            "SELECT report_id, user_id, anomaly_type, severity, summary, details_json, "
            "datetime(created_at, 'unixepoch') as created "
            "FROM anomaly_reports WHERE status = 'pending' ORDER BY created_at DESC"
        )
        columns = ["report_id", "user_id", "anomaly_type", "severity", "summary", "details_json", "created"]
        return [dict(zip(columns, row)) for row in rows]
    finally:
        if own_conn:
            conn.close()


def insert_command_report(anomaly_id, user_id, container_id, analysis, risk_level, conn=None):
    """Insert a command analysis report."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        return conn.execute_insert(
            "INSERT INTO command_reports(anomaly_report_id, user_id, container_id, analysis_text, risk_level) "
            "VALUES(?,?,?,?,?)",
            (anomaly_id, user_id, container_id, analysis, risk_level),
        )
    finally:
        if own_conn:
            conn.close()


def update_anomaly_status(report_id, status, conn=None):
    """Update the status of an anomaly report."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        conn.execute_update(
            "UPDATE anomaly_reports SET status = ? WHERE report_id = ?",
            (status, report_id),
        )
    finally:
        if own_conn:
            conn.close()


def get_container_key(user_id, container_id, conn=None):
    """Get the container encryption key blob as bytes."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        # Use hex() to extract blob data reliably through CSV
        rows = conn.execute(
            "SELECT hex(container_key) FROM containers WHERE user_id = ? AND container_id = ?",
            (user_id, container_id),
        )
        if rows and rows[0][0]:
            return bytes.fromhex(str(rows[0][0]))
        return None
    finally:
        if own_conn:
            conn.close()


def get_anomaly_report(report_id, conn=None):
    """Get a specific anomaly report by ID."""
    own_conn = conn is None
    if own_conn:
        conn = get_connection()
    try:
        rows = conn.execute(
            "SELECT report_id, user_id, anomaly_type, severity, summary, details_json, "
            "datetime(created_at, 'unixepoch') as created, status "
            "FROM anomaly_reports WHERE report_id = ?",
            (report_id,),
        )
        columns = ["report_id", "user_id", "anomaly_type", "severity", "summary",
                    "details_json", "created", "status"]
        return dict(zip(columns, rows[0])) if rows else None
    finally:
        if own_conn:
            conn.close()
