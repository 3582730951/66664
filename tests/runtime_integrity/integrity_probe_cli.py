#!/usr/bin/env python3
import subprocess
import sys
import tempfile
from pathlib import Path


def require(cond: bool, msg: str) -> None:
    if not cond:
        raise SystemExit(msg)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: integrity_probe_cli.py <vmp-vm1-asm> <vmp-integrity-probe>")

    vm1_asm = Path(sys.argv[1])
    integrity_probe = Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="vmp_integrity_probe_") as td:
        td_path = Path(td)
        asm_path = td_path / "sample.vm1s"
        module_path = td_path / "sample.vm1"
        asm_path.write_text("ldi_u64 vr0, 42\nret\n", encoding="utf-8")

        assemble = subprocess.run([str(vm1_asm), str(asm_path), str(module_path)], capture_output=True, text=True)
        require(assemble.returncode == 0, f"assembly failed: {assemble.stdout}\n{assemble.stderr}")

        probe_ok = subprocess.run([str(integrity_probe), str(module_path)], capture_output=True, text=True)
        require(probe_ok.returncode == 0, f"probe on clean module failed: {probe_ok.stdout}\n{probe_ok.stderr}")
        require("crc32=0x" in probe_ok.stdout, "missing CRC32 line for clean probe")
        require("sha256=" in probe_ok.stdout, "missing SHA-256 line for clean probe")

        probe_tamper = subprocess.run([str(integrity_probe), "--tamper", "8:FF", str(module_path)], capture_output=True, text=True)
        require(probe_tamper.returncode != 0, "tampered probe must exit non-zero")
        require("tampered_crc32=0x" in probe_tamper.stdout, "missing tampered CRC32 output")
        require("tampered_sha256=" in probe_tamper.stdout, "missing tampered SHA-256 output")
        require("mismatch=1" in probe_tamper.stdout, "tampered probe must report mismatch")

    print("integrity_probe_cli OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
