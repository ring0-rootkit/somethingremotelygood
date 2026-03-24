# somethingremotelygood

**Secure remote access to containerized environments with hardware key storage and AI-powered behavior monitoring.**

An ESP32-C3 microcontroller serves as a hardware security module — Ed25519 private keys are generated and stored on-device, never leaving the chip. Users authenticate via challenge-response through an SSH agent bridge, gaining access to isolated LXD containers with LUKS-encrypted home volumes. A two-level AI subsystem monitors user sessions: statistical anomaly detection flags unusual patterns, and a local LLM (Qwen2.5 via Ollama) analyzes command history for threats.

## Architecture

```
ESP32-C3 ──serial──▶ Python Bridge ──SSH agent──▶ Go Client ──TCP 5555──▶ C Manager
  Ed25519 keys          Unix socket             challenge-response       SQLCipher DB
  NVS + TRNG            /tmp/esp32-agent.sock                            LXD + LUKS

                                                                            │
                                                                            ▼
                                                                     Python AI Layer
                                                                     anomaly detection
                                                                     command analysis
```

| Component | Language | Description |
|-----------|----------|-------------|
| **ESP32 Firmware** | C++ | Ed25519 key generation/signing, password-protected NVS storage, serial protocol |
| **Agent Bridge** | Python | Translates serial protocol to SSH agent (Unix socket), handles lock/unlock |
| **Client** | Go | Challenge-response auth with manager, SSH session to container |
| **Manager** | C | TCP server, SQLCipher user DB, LXD container lifecycle, LUKS volume management |
| **AI Analysis** | Python | Statistical anomaly detection (AI 1) + LLM command analysis (AI 2) |

## Quick Start

```bash
# Install dependencies (Ubuntu/Debian)
make setup

# Generate keys (software RSA — or use ESP32 below)
make keygen

# Build manager and client
make all

# Register a user
make register-user USER=bob CONTAINER=bob_container

# Run manager
make manager-run

# Connect (in another terminal)
make client-run
```

### ESP32 Hardware Key Flow

```bash
# Flash firmware
make esp32-upload

# Generate key on device (with optional password)
make esp32-keygen

# Start SSH agent bridge
make agent-bridge

# Register ESP32 key with manager
make esp32-register USER=bob CONTAINER=bob_container

# Connect using hardware key
make client-run-agent
```

## AI Behavior Analysis

Session events (`auth_ok`, `auth_fail`, `ssh_connected`, `ssh_disconnected`, `timeout`) are logged by the C manager into the encrypted database.

### AI 1 — Statistical Anomaly Detection

Computes per-user baselines (30-day window) across three metrics:
- **Time-of-day** — hourly connection distribution (flags < 2% activity hours)
- **Session frequency** — connections per day (z-score)
- **Session duration** — connect/disconnect pair durations (z-score)

Severity: z > 2.5 → low, z > 3.5 → medium, z > 4.5 → high. Multiple anomaly types escalate to `composite`.

```bash
make anomaly-detect              # all users
make anomaly-detect-user USER=bob
make list-anomalies              # pending reports
```

### AI 2 — LLM Command Analysis

Admin-triggered deep analysis of shell history from LUKS-encrypted volumes. Commands pass through 5-layer sanitization (numbered format, regex injection filter, length limits, data boundary markers, JSON schema validation) before being sent to a local Qwen2.5:3b model via Ollama.

```bash
make analyze-anomaly REPORT_ID=1
make list-reports
make generate-pdf JSON=reports/report_file.json
```

### Testing with Synthetic Data

```bash
make generate-test-data ARGS="--users 5 --with-history --inject-malicious"
make anomaly-detect
make analyze-anomaly REPORT_ID=1
make clean-test-data
```

## Project Structure

```
src/
├── manager/          C server — auth, containers, LUKS, session logging
│   ├── server.c        TCP handler, challenge-response protocol
│   ├── database.c      SQLCipher schema (users, containers, sessions, reports)
│   ├── crypto.c        Ed25519/RSA signature verification
│   ├── lxd.c           LXD REST API integration
│   ├── volume.c        LUKS volume create/mount/unmount
│   └── cli.c           Admin commands (list-anomalies, review-anomaly)
├── client/           Go client — SSH agent integration, auth
│   ├── main.go         CLI entry point
│   ├── agent.go        SSH agent protocol
│   └── ssh.go          SSH session management
└── ai/               Python behavior analysis
    ├── anomaly_detect.py      AI 1: z-score anomaly detection
    ├── command_analysis.py    AI 2: LLM command history analysis
    ├── sanitizer.py           5-layer prompt injection defense
    ├── db_access.py           SQLCipher CLI wrapper
    ├── generate_report_pdf.py PDF report generator
    ├── generate_test_data.py  Synthetic data generator
    └── generate_user_baseline.py  Baseline generator for real users

esp32/ssh_agent/      ESP32-C3 firmware (PlatformIO + Arduino)
wrapper/              Python serial tools (keytool, agent bridge)
conference/           CTDA 2024 article generators (docx)
```

## Security Properties

- Private keys never leave ESP32 (optional password encryption via PBKDF2 + AES-256-CBC)
- Container data encrypted at rest (LUKS)
- User database encrypted (SQLCipher)
- Command history analyzed locally (Ollama) — no data sent to external services
- 5-layer prompt injection defense for LLM analysis
- All inter-component communication via local interfaces (serial, Unix socket, loopback)

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `DB_PASSWORD` | — | SQLCipher database encryption key |
| `LLM_API_URL` | `http://localhost:11434/api/chat` | Ollama API endpoint |
| `LLM_MODEL` | `qwen2.5:3b` | LLM model for command analysis |
| `ANOMALY_WINDOW_DAYS` | `30` | Baseline window for anomaly detection |
| `ANOMALY_Z_THRESHOLD` | `2.5` | Z-score threshold for anomaly flagging |

## Dependencies

**System:** lxd, cryptsetup, gcc, sqlcipher, openssl, go, python3

**Python:** fpdf2, requests

**ESP32:** PlatformIO (pio)
