#!/usr/bin/env python3
"""AI 1 — Statistical anomaly detection on user session patterns."""

import argparse
import json
import math
import sys
import time
from collections import defaultdict

sys.path.insert(0, "src")
from ai.config import ANOMALY_WINDOW_DAYS, ANOMALY_Z_THRESHOLD
from ai.db_access import (
    get_all_users,
    get_connection,
    get_pending_anomalies,
    get_sessions,
    insert_anomaly_report,
)


def compute_mean_stddev(values):
    """Compute mean and standard deviation."""
    if not values:
        return 0.0, 0.0
    n = len(values)
    mean = sum(values) / n
    if n < 2:
        return mean, 0.0
    variance = sum((x - mean) ** 2 for x in values) / (n - 1)
    return mean, math.sqrt(variance)


def bucket_by_hour(sessions):
    """Count sessions per hour-of-day bucket."""
    buckets = defaultdict(int)
    for s in sessions:
        hour = time.gmtime(s["timestamp"]).tm_hour
        buckets[hour] += 1
    return buckets


def sessions_per_day(sessions):
    """Count sessions per calendar day."""
    days = defaultdict(int)
    for s in sessions:
        day = time.strftime("%Y-%m-%d", time.gmtime(s["timestamp"]))
        days[day] += 1
    return list(days.values())


def compute_durations(sessions):
    """Pair ssh_connected/ssh_disconnected events to compute session durations."""
    durations = []
    pending = {}
    for s in sessions:
        key = (s["user_id"], s["container_id"])
        if s["event_type"] == "ssh_connected":
            pending[key] = s["timestamp"]
        elif s["event_type"] in ("ssh_disconnected", "timeout"):
            if key in pending:
                duration = s["timestamp"] - pending.pop(key)
                if duration > 0:
                    durations.append(duration)
    return durations


def analyze_user(user_id, conn):
    """Analyze a single user's behavior for anomalies."""
    now = int(time.time())
    baseline_start = now - (ANOMALY_WINDOW_DAYS * 86400)
    recent_start = now - 86400  # last 24h

    baseline_sessions = get_sessions(user_id=user_id, since=baseline_start, conn=conn)
    recent_sessions = get_sessions(user_id=user_id, since=recent_start, conn=conn)

    if len(baseline_sessions) < 5:
        return None  # Not enough data for baseline

    baseline_connects = [s for s in baseline_sessions if s["event_type"] == "ssh_connected"]
    recent_connects = [s for s in recent_sessions if s["event_type"] == "ssh_connected"]

    anomalies = []

    # 1. Time-of-day analysis
    baseline_hours = bucket_by_hour(baseline_connects)
    if recent_connects and baseline_hours:
        total_baseline = sum(baseline_hours.values())
        hour_fractions = {h: c / total_baseline for h, c in baseline_hours.items()}

        for s in recent_connects:
            hour = time.gmtime(s["timestamp"]).tm_hour
            fraction = hour_fractions.get(hour, 0.0)
            if fraction < 0.02:  # Less than 2% of historical activity in this hour
                anomalies.append({
                    "type": "unusual_time",
                    "detail": f"Connection at hour {hour:02d}:00 UTC (historical rate: {fraction:.1%})",
                    "z_score": 4.0 if fraction == 0 else 1.0 / max(fraction, 0.001),
                })

    # 2. Session frequency analysis
    baseline_daily = sessions_per_day(baseline_connects)
    recent_daily = sessions_per_day(recent_connects)
    if baseline_daily and recent_daily:
        mean, std = compute_mean_stddev(baseline_daily)
        if std > 0:
            for day_count in recent_daily:
                z = (day_count - mean) / std
                if z > ANOMALY_Z_THRESHOLD:
                    anomalies.append({
                        "type": "high_frequency",
                        "detail": f"{day_count} sessions today vs baseline {mean:.1f}±{std:.1f}/day (z={z:.1f})",
                        "z_score": z,
                    })

    # 3. Session duration analysis
    baseline_durations = compute_durations(baseline_sessions)
    recent_durations = compute_durations(recent_sessions)
    if baseline_durations and recent_durations:
        mean, std = compute_mean_stddev(baseline_durations)
        if std > 0:
            for dur in recent_durations:
                z = (dur - mean) / std
                if z > ANOMALY_Z_THRESHOLD:
                    anomalies.append({
                        "type": "long_duration",
                        "detail": f"Session {dur/60:.0f}min vs baseline {mean/60:.0f}±{std/60:.0f}min (z={z:.1f})",
                        "z_score": z,
                    })

    if not anomalies:
        return None

    # Determine severity from max z-score
    max_z = max(a["z_score"] for a in anomalies)
    if max_z > 4.5:
        severity = "high"
    elif max_z > 3.5:
        severity = "medium"
    else:
        severity = "low"

    # Composite escalation: multiple anomaly types escalate severity
    types_found = set(a["type"] for a in anomalies)
    if len(types_found) > 1:
        anomaly_type = "composite"
        if severity == "low":
            severity = "medium"
        elif severity == "medium":
            severity = "high"
    else:
        anomaly_type = types_found.pop()

    summary = "; ".join(a["detail"] for a in anomalies[:3])
    if len(anomalies) > 3:
        summary += f" (+{len(anomalies) - 3} more)"

    return {
        "user_id": user_id,
        "anomaly_type": anomaly_type,
        "severity": severity,
        "summary": summary,
        "details": anomalies,
    }


def list_pending():
    """Print pending anomaly reports."""
    reports = get_pending_anomalies()
    if not reports:
        print("No pending anomaly reports.")
        return
    print(f"{'ID':<6} {'User':<12} {'Type':<16} {'Severity':<8} {'Created':<20} Summary")
    print("-" * 90)
    for r in reports:
        print(f"{r['report_id']:<6} {r['user_id']:<12} {r['anomaly_type']:<16} "
              f"{r['severity']:<8} {r['created']:<20} {r['summary'][:60]}")


def main():
    parser = argparse.ArgumentParser(description="Detect anomalous user behavior patterns")
    parser.add_argument("--user", help="Analyze specific user only")
    parser.add_argument("--dry-run", action="store_true", help="Print results without writing to DB")
    parser.add_argument("--list-pending", action="store_true", help="List pending anomaly reports")
    args = parser.parse_args()

    if args.list_pending:
        list_pending()
        return

    conn = get_connection()
    try:
        if args.user:
            users = [args.user]
        else:
            users = get_all_users(conn=conn)

        if not users:
            print("No users found.")
            return

        found = 0
        for user_id in users:
            result = analyze_user(user_id, conn)
            if result:
                found += 1
                print(f"[ANOMALY] {result['severity'].upper()}: {result['user_id']} — "
                      f"{result['anomaly_type']}: {result['summary']}")
                if not args.dry_run:
                    report_id = insert_anomaly_report(
                        result["user_id"],
                        result["anomaly_type"],
                        result["severity"],
                        result["summary"],
                        json.dumps(result["details"]),
                        conn=conn,
                    )
                    print(f"  → Saved as anomaly report #{report_id}")
            else:
                print(f"[OK] {user_id}: no anomalies detected")

        print(f"\nAnalyzed {len(users)} user(s), found {found} anomaly report(s).")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
