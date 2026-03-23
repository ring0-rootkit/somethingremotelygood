# Usage Guide

## Prerequisites

```sh
make setup
```

This installs: LXD, cryptsetup, GCC, SQLCipher, OpenSSL, Go, Python3.

## Building

```sh
make manager        # Build C manager
make client         # Build Go client
make all            # Build both
make clean          # Remove build artifacts
```

## Key Generation

### Software keys (RSA)
```sh
make keygen
```

### ESP32 hardware keys (Ed25519)
```sh
make esp32-upload       # Flash firmware
make esp32-keygen       # Generate key on device
make esp32-get-key      # Export public key
```

## User & Container Registration

### Software key registration
```sh
make register-user USER=bob CONTAINER=bob
# Optional: PEM=path SSH=path KEY=path
```

### ESP32 key registration
```sh
make esp32-register USER=bob CONTAINER=bob
```

## Running

### Start the manager
```sh
make manager-run
# Runs on port 5555, requires sudo for LUKS
```

### Connect with client
```sh
make client-run              # Software key
make client-run-agent        # ESP32 agent
```

### ESP32 agent bridge (separate terminal)
```sh
make agent-bridge
```

## Cleanup

```sh
make clean-containers    # Stop and remove all LXD containers
make cleanup-luks        # Close stale LUKS devices
```

---

## AI Behavior Analysis

User behavior monitoring with two AI stages:
1. **Anomaly Detection** (statistical) — flags unusual time, frequency, or duration patterns
2. **Command Analysis** (LLM) — admin-triggered analysis of shell history during anomalous periods

### Setup

Install Python dependencies:
```sh
pip install requests
```

The `sqlcipher` CLI tool must be available (installed via `make setup`). No Python C extensions needed.

Session events are logged automatically by the manager when users connect/disconnect.

### Generate Test Data

```sh
make generate-test-data                                  # 5 users, 60 days of sessions
make generate-test-data ARGS="--users 10"                # More users
make generate-test-data ARGS="--with-history"            # Include fake shell histories
make generate-test-data ARGS="--with-history --inject-malicious"  # Add injection attempts
make clean-test-data                                     # Remove all test data
```

### Anomaly Detection (AI 1)

Runs statistical analysis on session patterns (time-of-day, frequency, duration). No LLM needed.

```sh
make anomaly-detect                      # Analyze all users
make anomaly-detect-user USER=bob        # Specific user
```

Dry run (print without writing to DB):
```sh
sudo DB_PASSWORD=123 python3 src/ai/anomaly_detect.py --dry-run
```

### View Anomaly Reports

```sh
make list-anomalies                      # Pending anomaly reports
make list-anomalies-db                   # Same via C manager
```

### Command Analysis (AI 2)

Admin-triggered LLM analysis of shell history for a flagged anomaly. Requires Ollama running locally with `qwen2.5:3b`.

```sh
ollama pull qwen2.5:3b
make analyze-anomaly REPORT_ID=1
```

### View Command Reports

```sh
make list-reports                        # All command analysis reports
make list-reports-db                     # Same via C manager
```

### Review/Dismiss Anomalies

```sh
make review-anomaly REPORT_ID=1 STATUS=reviewed
make review-anomaly REPORT_ID=2 STATUS=escalated
make review-anomaly REPORT_ID=3 STATUS=dismissed
```

### Typical Workflow

```sh
# 1. Generate test data
make generate-test-data ARGS="--with-history"

# 2. Run anomaly detection
make anomaly-detect

# 3. Review flagged anomalies
make list-anomalies

# 4. Analyze commands for a specific anomaly
export LLM_API_KEY=sk-ant-...
make analyze-anomaly REPORT_ID=1

# 5. View results
make list-reports

# 6. Mark as reviewed
make review-anomaly REPORT_ID=1 STATUS=reviewed
```
