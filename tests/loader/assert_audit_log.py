#!/usr/bin/env python3
import pathlib
import sys

if len(sys.argv) != 3:
    raise SystemExit(f"usage: {sys.argv[0]} <log-path> <needle>")
path = pathlib.Path(sys.argv[1])
needle = sys.argv[2]
if not path.exists():
    raise SystemExit(f"missing log: {path}")
text = path.read_text(encoding="utf-8", errors="replace")
if needle not in text:
    raise SystemExit(f"missing event '{needle}' in {path}\n{text}")
print(f"audit log OK: {path}")
