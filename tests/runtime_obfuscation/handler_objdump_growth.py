#!/usr/bin/env python3
import pathlib
import re
import subprocess
import sys


def disasm(path: pathlib.Path) -> str:
    return subprocess.run(
        ["objdump", "-Cd", str(path)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout


def count_lines(text: str, needle: str) -> int:
    count = 0
    current = False
    for line in text.splitlines():
        if re.match(r"^[0-9a-fA-F]+ <", line):
            current = needle in line
        elif current and line.strip():
            count += 1
    return count


def main() -> int:
    base = pathlib.Path(sys.argv[1])
    obf = pathlib.Path(sys.argv[2])
    base_text = disasm(base)
    obf_text = disasm(obf)

    base_lines = count_lines(base_text, "emit_vm1_polymorphic_junk")
    obf_lines = count_lines(obf_text, "emit_vm1_polymorphic_junk")
    if base_lines <= 0 or obf_lines <= 0:
        raise SystemExit("unable to locate emit_vm1_polymorphic_junk in objdump output")
    if obf_lines < base_lines * 2:
        raise SystemExit(f"expected handler growth >=2x, got base={base_lines} obf={obf_lines}")

    print("runtime_obfuscation_handler_objdump_growth OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
