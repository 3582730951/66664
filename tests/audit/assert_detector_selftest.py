#!/usr/bin/env python3
import os
import subprocess
import sys
from pathlib import Path

exe = Path(sys.argv[1])
log_path = Path(sys.argv[2])
env = os.environ.copy()
if log_path.exists():
    log_path.unlink()
log_path.parent.mkdir(parents=True, exist_ok=True)
env["VMP_AUDIT_LOG_PATH"] = str(log_path)
proc = subprocess.run([str(exe), "--detector-selftest"], text=True, capture_output=True, env=env)
if proc.returncode != 0:
    print(proc.stdout)
    print(proc.stderr, file=sys.stderr)
    raise SystemExit(proc.returncode)
if "audit:ok exits_triggered=3" not in proc.stdout:
    print(proc.stdout)
    raise SystemExit(1)
lines = [line for line in log_path.read_text().splitlines() if line]
if len(lines) != 3:
    print(lines)
    raise SystemExit(1)
for token in ("hw_breakpoint", "integrity_mismatch", "unknown"):
    matches = [line for line in lines if f"] [{token}] " in line]
    if len(matches) != 1:
        print(lines)
        raise SystemExit(1)
print("detector selftest OK")
