#!/usr/bin/env python3
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit(f"usage: {sys.argv[0]} <log-path>")
path = pathlib.Path(sys.argv[1])
if path.exists() and path.read_text(encoding="utf-8", errors="replace").strip():
    raise SystemExit(f"expected no log content at {path}")
print(f"log absent OK: {path}")
