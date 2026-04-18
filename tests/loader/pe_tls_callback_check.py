#!/usr/bin/env python3
import pathlib
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit(f"usage: {sys.argv[0]} <pe-file>")
path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
if data[:2] != b'MZ':
    raise SystemExit('not a PE file (missing MZ)')
pe_offset = struct.unpack_from('<I', data, 0x3C)[0]
if data[pe_offset:pe_offset+4] != b'PE\0\0':
    raise SystemExit('not a PE file (missing PE signature)')
num_sections = struct.unpack_from('<H', data, pe_offset + 6)[0]
opt_size = struct.unpack_from('<H', data, pe_offset + 20)[0]
section_off = pe_offset + 24 + opt_size
found = False
for i in range(num_sections):
    off = section_off + i * 40
    name = data[off:off+8].rstrip(b'\0').decode('ascii', errors='ignore')
    raw_size = struct.unpack_from('<I', data, off + 16)[0]
    if name == '.CRT' and raw_size >= struct.calcsize('P'):
        found = True
        break
if not found:
    raise SystemExit('missing non-empty .CRT section for TLS callback payload')
print(f'PE TLS callback section OK: {path}')
