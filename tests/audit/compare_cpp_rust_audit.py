#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

cpp_bin = Path(sys.argv[1])
fixture = Path(sys.argv[2])
cpp = subprocess.check_output([str(cpp_bin), str(fixture)], text=True)
rust = subprocess.check_output([
    "cargo", "run", "-q", "-p", "rust_audit", "--example", "format_record", "--", str(fixture)
], cwd="/workspace/vmp", text=True)
if cpp != rust:
    print("cpp:", cpp)
    print("rust:", rust)
    raise SystemExit(1)
print("cross language audit golden OK")
