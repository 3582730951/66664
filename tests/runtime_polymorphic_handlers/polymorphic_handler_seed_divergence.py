#!/usr/bin/env python3
import json
import subprocess
import sys


def run_dump(path: str) -> dict:
    completed = subprocess.run([path], check=True, text=True, capture_output=True)
    return json.loads(completed.stdout)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def diff_ratio(lhs: list[int], rhs: list[int]) -> float:
    require(len(lhs) == len(rhs), "prefix hash length mismatch")
    if not lhs:
        return 0.0
    changed = sum(1 for a, b in zip(lhs, rhs) if a != b)
    return changed / len(lhs)


def main() -> None:
    seed_a, seed_b = sys.argv[1], sys.argv[2]
    a = run_dump(seed_a)
    b = run_dump(seed_b)

    for vm in ("vm1", "vm2"):
        require(a[vm]["ret_int"] == b[vm]["ret_int"], f"{vm} semantic result diverged")
        require(a[vm]["ret_int"] == 12, f"{vm} unexpected arithmetic result")
        require(a[vm]["layout_fingerprint"] != b[vm]["layout_fingerprint"], f"{vm} layout fingerprint should diverge across seeds")
        require(diff_ratio(a[vm]["prefix_hashes"], b[vm]["prefix_hashes"]) >= 0.10,
                f"{vm} expected >=10% handler prefix divergence across seeds")

    print("polymorphic_handler_seed_divergence OK")


if __name__ == "__main__":
    main()
