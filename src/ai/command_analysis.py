#!/usr/bin/env python3
"""AI 2 — LLM-based command history analysis for anomalous sessions."""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

sys.path.insert(0, "src")
from ai.config import HOMES_DIR, LLM_API_URL, LLM_MODEL
from ai.db_access import (
    get_anomaly_report,
    get_connection,
    get_container_key,
    get_sessions,
    insert_command_report,
)
from ai.sanitizer import build_analysis_prompt, sanitize_commands, validate_llm_response

try:
    import requests
except ImportError:
    requests = None

REPORTS_DIR = "./reports"


def read_shell_history(mount_point, user_id):
    """Read shell history files from a mounted volume."""
    history_files = [
        os.path.join(mount_point, user_id, ".ash_history"),
        os.path.join(mount_point, user_id, ".bash_history"),
        os.path.join(mount_point, user_id, ".zsh_history"),
    ]
    lines = []
    for path in history_files:
        if os.path.exists(path):
            with open(path, "r", errors="replace") as f:
                lines.extend(f.read().splitlines())
    return lines


def mount_volume(container_id, container_key):
    """Temporarily mount a LUKS volume to read history. Returns mount_point or None."""
    img_path = os.path.join(HOMES_DIR, f"{container_id}.img")
    if not os.path.exists(img_path):
        print(f"  No encrypted volume found: {img_path}")
        return None

    mapper_name = f"somethingremotelygood_{container_id}"
    mount_point = os.path.join(HOMES_DIR, f"{container_id}_mnt")

    if os.path.ismount(mount_point):
        return mount_point

    key_fd, key_path = tempfile.mkstemp()
    try:
        os.write(key_fd, container_key)
        os.close(key_fd)

        result = subprocess.run(
            ["losetup", "--find", "--show", img_path],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            return None
        loop_dev = result.stdout.strip()

        result = subprocess.run(
            ["cryptsetup", "open", "--type", "luks", "--key-file", key_path, loop_dev, mapper_name],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            subprocess.run(["losetup", "-d", loop_dev], capture_output=True)
            return None

        os.makedirs(mount_point, exist_ok=True)
        result = subprocess.run(
            ["mount", f"/dev/mapper/{mapper_name}", mount_point],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            subprocess.run(["cryptsetup", "close", mapper_name], capture_output=True)
            subprocess.run(["losetup", "-d", loop_dev], capture_output=True)
            return None

        return mount_point
    finally:
        os.unlink(key_path)


def unmount_volume(container_id):
    """Unmount a temporarily mounted LUKS volume."""
    mapper_name = f"somethingremotelygood_{container_id}"
    mount_point = os.path.join(HOMES_DIR, f"{container_id}_mnt")

    subprocess.run(["umount", mount_point], capture_output=True)
    subprocess.run(["cryptsetup", "close", mapper_name], capture_output=True)
    result = subprocess.run(
        ["losetup", "-j", os.path.join(HOMES_DIR, f"{container_id}.img")],
        capture_output=True, text=True
    )
    if result.stdout:
        loop_dev = result.stdout.split(":")[0]
        subprocess.run(["losetup", "-d", loop_dev], capture_output=True)


def call_llm(prompt):
    """Send prompt to Ollama API and return response text."""
    if requests is None:
        print("ERROR: 'requests' package not installed. Run: pip install requests")
        sys.exit(1)

    body = {
        "model": LLM_MODEL,
        "messages": [{"role": "user", "content": prompt}],
        "stream": False,
    }

    resp = requests.post(LLM_API_URL, json=body, timeout=600)
    resp.raise_for_status()
    data = resp.json()
    return data["message"]["content"]


def save_json_report(anomaly_id, anomaly_report, user_id, container_id, analysis_result, commands):
    """Save a detailed JSON report file to reports/ directory."""
    os.makedirs(REPORTS_DIR, exist_ok=True)

    now = datetime.now(timezone.utc)
    report = {
        "report_metadata": {
            "generated_at": now.isoformat(),
            "anomaly_report_id": anomaly_id,
            "model": LLM_MODEL,
        },
        "user": {
            "user_id": user_id,
            "container_id": container_id,
        },
        "anomaly": {
            "type": anomaly_report.get("anomaly_type", "unknown"),
            "severity": anomaly_report.get("severity", "unknown"),
            "summary": anomaly_report.get("summary", ""),
            "details": json.loads(anomaly_report.get("details_json", "[]")),
        },
        "command_analysis": analysis_result,
        "commands_analyzed": len(commands),
        "command_history": commands,
    }

    filename = f"report_anomaly{anomaly_id}_{user_id}_{now.strftime('%Y%m%d_%H%M%S')}.json"
    filepath = os.path.join(REPORTS_DIR, filename)
    with open(filepath, "w") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print(f"    → JSON report saved to {filepath}")
    return filepath


def analyze_anomaly(anomaly_id):
    """Run command analysis for a specific anomaly report."""
    conn = get_connection()
    try:
        report = get_anomaly_report(anomaly_id, conn=conn)
        if not report:
            print(f"Anomaly report #{anomaly_id} not found.")
            return 1

        user_id = report["user_id"]
        print(f"Analyzing commands for user '{user_id}' (anomaly #{anomaly_id}: {report['anomaly_type']})")

        details = json.loads(report["details_json"])
        sessions = get_sessions(user_id=user_id, conn=conn)
        container_ids = list(set(
            s["container_id"] for s in sessions
            if s["container_id"] and s["event_type"] == "ssh_connected"
        ))

        if not container_ids:
            print("  No containers found for this user.")
            return 1

        for container_id in container_ids:
            print(f"  Container: {container_id}")
            container_key = get_container_key(user_id, container_id, conn=conn)
            if not container_key:
                print(f"    No container key found, skipping.")
                continue

            # Try LUKS mount first, fall back to test directory
            mount_point = mount_volume(container_id, container_key)
            needs_unmount = mount_point is not None
            if not mount_point:
                test_dir = os.path.join(HOMES_DIR, f"test_{user_id}")
                if os.path.isdir(test_dir):
                    print(f"    Using test history from {test_dir}")
                    mount_point = test_dir
                else:
                    print(f"    No volume or test history found, skipping.")
                    continue

            try:
                commands = read_shell_history(mount_point, user_id)
                if not commands:
                    print(f"    No command history found.")
                    continue

                print(f"    Found {len(commands)} commands, sanitizing...")
                formatted, filtered = sanitize_commands(commands)

                prompt = build_analysis_prompt(formatted, user_id, report["summary"])
                print(f"    Sending to LLM for analysis...")
                response = call_llm(prompt)

                result = validate_llm_response(response)
                if not result:
                    print(f"    WARNING: LLM response failed validation, storing raw.")
                    result = {
                        "risk_level": "suspicious",
                        "summary": "LLM response failed schema validation",
                        "findings": [],
                        "recommendation": "Manual review required",
                        "raw_response": response[:500],
                    }

                db_report_id = insert_command_report(
                    anomaly_id, user_id, container_id,
                    json.dumps(result), result["risk_level"],
                    conn=conn,
                )
                print(f"    Result: {result['risk_level'].upper()} — {result['summary']}")
                print(f"    → Saved as command report #{db_report_id}")

                save_json_report(anomaly_id, report, user_id, container_id, result, filtered)
            finally:
                if needs_unmount:
                    unmount_volume(container_id)

        return 0
    finally:
        conn.close()


def list_reports():
    """Print all command analysis reports."""
    conn = get_connection()
    try:
        rows = conn.execute(
            "SELECT r.report_id, r.anomaly_report_id, r.user_id, r.container_id, "
            "r.risk_level, datetime(r.created_at, 'unixepoch'), "
            "substr(r.analysis_text, 1, 100) "
            "FROM command_reports r ORDER BY r.created_at DESC"
        )
        if not rows:
            print("No command analysis reports.")
            return
        print(f"{'ID':<6} {'Anomaly':<8} {'User':<12} {'Container':<12} {'Risk':<12} {'Created':<20} Analysis")
        print("-" * 100)
        for row in rows:
            print(f"{row[0]:<6} {row[1]:<8} {row[2]:<12} {row[3]:<12} {row[4]:<12} {row[5]:<20} {row[6]}")
    finally:
        conn.close()


def main():
    parser = argparse.ArgumentParser(description="LLM-based command history analysis")
    parser.add_argument("--anomaly-id", type=int, help="Anomaly report ID to analyze")
    parser.add_argument("--list", action="store_true", help="List all command reports")
    args = parser.parse_args()

    if args.list:
        list_reports()
    elif args.anomaly_id:
        sys.exit(analyze_anomaly(args.anomaly_id))
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
