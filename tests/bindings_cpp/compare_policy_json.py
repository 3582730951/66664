#!/usr/bin/env python3
import json
import sys
from pathlib import Path


def rewrite_root(value, root):
    if isinstance(value, dict):
        return {k: rewrite_root(v, root) for k, v in value.items()}
    if isinstance(value, list):
        return [rewrite_root(v, root) for v in value]
    if isinstance(value, str):
        return value.replace("__ROOT__", root)
    return value


def canonicalize_root(value, root):
    if isinstance(value, dict):
        return {k: canonicalize_root(v, root) for k, v in value.items()}
    if isinstance(value, list):
        return [canonicalize_root(v, root) for v in value]
    if isinstance(value, str):
        return value.replace(root, "__ROOT__")
    return value


def normalize(value):
    if isinstance(value, dict):
        return {k: normalize(v) for k, v in sorted(value.items())}
    if isinstance(value, list):
        if value and isinstance(value[0], dict) and "symbol_or_region" in value[0]:
            value = sorted(value, key=lambda item: item["symbol_or_region"])
        return [normalize(v) for v in value]
    return value


def main() -> int:
    actual_path = Path(sys.argv[1]).resolve()
    expected_path = Path(sys.argv[2]).resolve()
    root = str(Path(__file__).resolve().parents[2])
    lhs = normalize(canonicalize_root(json.loads(actual_path.read_text()), root))
    rhs = normalize(canonicalize_root(rewrite_root(json.loads(expected_path.read_text()), root), root))
    if lhs != rhs:
        print("JSON mismatch")
        print(json.dumps(lhs, indent=2, sort_keys=True))
        print(json.dumps(rhs, indent=2, sort_keys=True))
        return 1
    print("policy json equal")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
