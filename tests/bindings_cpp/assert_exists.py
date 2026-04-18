#!/usr/bin/env python3
import sys
from pathlib import Path

path = Path(sys.argv[1])
if not path.exists():
    raise SystemExit(f"missing expected path: {path}")
print(f"exists: {path}")
