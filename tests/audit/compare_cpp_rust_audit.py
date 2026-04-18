#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
from pathlib import Path


def example_path(repo_root: Path, example_name: str) -> Path:
    base = Path(os.environ.get("CARGO_TARGET_DIR", repo_root / "target")) / "debug" / "examples"
    candidates = [base / example_name, base / f"{example_name}.exe"]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[-1] if os.name == "nt" else candidates[0]


def main() -> int:
    cpp_bin = Path(sys.argv[1]).resolve()
    fixture = Path(sys.argv[2]).resolve()
    repo_root = Path(sys.argv[3]).resolve()
    cargo = shutil.which("cargo")
    if cargo is None:
        print("cross language audit golden SKIPPED: cargo unavailable")
        return 0

    cpp = subprocess.check_output([str(cpp_bin), str(fixture)], text=True)
    subprocess.run(
        [cargo, "build", "-q", "-p", "rust_audit", "--example", "format_record"],
        cwd=repo_root,
        check=True,
    )
    rust = subprocess.check_output([str(example_path(repo_root, "format_record")), str(fixture)], cwd=repo_root, text=True)
    if cpp != rust:
        print("cpp:", cpp)
        print("rust:", rust)
        return 1
    print("cross language audit golden OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
