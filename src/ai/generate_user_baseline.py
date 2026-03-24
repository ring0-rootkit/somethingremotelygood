#!/usr/bin/env python3
import argparse
import random
import sys
import time

sys.path.insert(0, "src")
from ai.db_access import get_connection


def main():
    parser = argparse.ArgumentParser(
        description="Generate normal baseline data for a user (current session will be anomalous)"
    )
    parser.add_argument("user", help="User ID to generate baseline for")
    parser.add_argument("--container", help="Container ID (default: same as user)")
    parser.add_argument("--days", type=int, default=30, help="Days of baseline to generate (default: 30)")
    parser.add_argument("--sessions-per-day", type=int, default=3, help="Avg sessions per day (default: 3)")
    parser.add_argument("--clean", action="store_true", help="Remove generated sessions for this user first")
    args = parser.parse_args()

    user_id = args.user
    container_id = args.container or user_id
    now = int(time.time())

    # The "normal" window: hours from 12h ago to 6h ago
    normal_center_ts = now - (9 * 3600)  # 9h ago = midpoint of 12h..6h ago
    normal_center_hour = time.localtime(normal_center_ts).tm_hour
    # Normal window spans ~6 hours centered on that
    normal_start_hour = time.localtime(now - 12 * 3600).tm_hour
    normal_end_hour = time.localtime(now - 6 * 3600).tm_hour
    current_hour = time.localtime(now).tm_hour

    print(f"User:           {user_id}")
    print(f"Container:      {container_id}")
    print(f"Current hour:   {current_hour:02d}:00 (will be anomalous)")
    print(f"Normal window:  {normal_start_hour:02d}:00 — {normal_end_hour:02d}:00")
    print(f"Baseline days:  {args.days}")
    print()

    conn = get_connection()
    try:
        if args.clean:
            # Only remove synthetic sessions (those with exact :00 or :05 second offsets won't collide)
            conn.execute_update(
                "DELETE FROM sessions WHERE user_id = ? AND container_id = ?",
                (user_id, container_id),
            )
            conn.execute_update(
                "DELETE FROM anomaly_reports WHERE user_id = ?",
                (user_id,),
            )
            conn.execute_update(
                "DELETE FROM command_reports WHERE user_id = ?",
                (user_id,),
            )
            print("Cleaned existing session/report data for this user.")

        # Verify user exists in DB
        rows = conn.execute("SELECT user_id FROM users WHERE user_id = ?", (user_id,))
        if not rows:
            print(f"WARNING: User '{user_id}' not found in users table.")
            print("         Sessions will be created but anomaly detection needs the user registered.")
            print(f"         Run: make register-user USER={user_id} CONTAINER={container_id}")
            print()

        # Generate baseline: 30 days of sessions in the normal window
        total_sessions = 0
        for day in range(args.days, 0, -1):
            day_start = now - (day * 86400)
            n_sessions = max(1, int(random.gauss(args.sessions_per_day, 1)))

            for _ in range(n_sessions):
                # Pick hour within normal window
                if normal_start_hour <= normal_end_hour:
                    hour = random.randint(normal_start_hour, normal_end_hour)
                else:
                    # Window wraps midnight (e.g., 22:00 — 04:00)
                    hour = random.randint(0, 23)
                    while not (hour >= normal_start_hour or hour <= normal_end_hour):
                        hour = random.randint(0, 23)

                minute = random.randint(0, 59)
                ts = day_start + hour * 3600 + minute * 60

                # Normal duration: 30-90 min
                duration = int(random.gauss(50 * 60, 15 * 60))
                duration = max(10 * 60, min(90 * 60, duration))

                conn.execute_update(
                    "INSERT INTO sessions(user_id, container_id, event_type, timestamp) VALUES(?,?,?,?)",
                    (user_id, container_id, "auth_ok", ts),
                )
                conn.execute_update(
                    "INSERT INTO sessions(user_id, container_id, event_type, timestamp) VALUES(?,?,?,?)",
                    (user_id, container_id, "ssh_connected", ts + 3),
                )
                conn.execute_update(
                    "INSERT INTO sessions(user_id, container_id, event_type, timestamp) VALUES(?,?,?,?)",
                    (user_id, container_id, "ssh_disconnected", ts + 3 + duration),
                )
                total_sessions += 1

        print(f"Generated {total_sessions} sessions over {args.days} days.")
        print(f"All sessions fall within {normal_start_hour:02d}:00 — {normal_end_hour:02d}:00.")
        print()
        print("Now connect as this user and run:")
        print("  make anomaly-detect-user USER=" + user_id)
        print()
        print(f"Your current session (hour {current_hour:02d}:00) should be flagged as unusual_time.")

    finally:
        conn.close()


if __name__ == "__main__":
    main()
