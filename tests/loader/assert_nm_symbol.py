#!/usr/bin/env python3
import re
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 4:
    raise SystemExit(f"usage: {sys.argv[0]} <nm> <binary> <symbol>")

nm, binary, symbol = sys.argv[1:4]
out = subprocess.check_output([nm, "-D", binary], text=True, stderr=subprocess.STDOUT)
pat = re.compile(rf"\b[UTW]\b.*\b{re.escape(symbol)}$")
if not any(pat.search(line.strip()) for line in out.splitlines()):
    print(out)
    raise SystemExit(f"symbol {symbol} not found in {binary}")
print(f"nm symbol OK: {Path(binary).name} -> {symbol}")
