"""Environment-based configuration for AI behavior analysis."""

import os

DB_PASSWORD = os.environ.get("DB_PASSWORD")
DB_PATH = os.environ.get("DB_PATH", "encrypted_users.db")
LLM_API_KEY = os.environ.get("LLM_API_KEY", "")
LLM_API_URL = os.environ.get("LLM_API_URL", "http://localhost:11434/api/chat")
LLM_MODEL = os.environ.get("LLM_MODEL", "qwen2.5:3b")
ANOMALY_WINDOW_DAYS = int(os.environ.get("ANOMALY_WINDOW_DAYS", "30"))
ANOMALY_Z_THRESHOLD = float(os.environ.get("ANOMALY_Z_THRESHOLD", "2.5"))
HOMES_DIR = os.environ.get("HOMES_DIR", "./homes")
