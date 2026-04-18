#!/usr/bin/env python3
import json
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
    policy_file = Path(sys.argv[2]).resolve()
    repo_root = Path(sys.argv[3]).resolve()
    cargo = shutil.which("cargo")
    if cargo is None:
        print("compare_cpp_rust SKIPPED: cargo unavailable")
        return 0

    cpp = json.loads(subprocess.check_output([str(cpp_bin), str(policy_file)], text=True))
    subprocess.run(
        [cargo, "build", "-q", "-p", "vmp-policy", "--example", "summary"],
        cwd=repo_root,
        check=True,
    )
    rust = json.loads(
        subprocess.check_output([str(example_path(repo_root, "summary")), str(policy_file)], text=True, cwd=repo_root)
    )
    assert cpp == rust, f"cpp != rust\ncpp={cpp}\nrust={rust}"
    print("compare_cpp_rust OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
