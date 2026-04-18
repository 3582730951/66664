#!/usr/bin/env python3
import json
import re
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


def normalize_source_location(value):
    if not isinstance(value, dict):
        return value
    out = dict(value)
    file_key = "filename" if "filename" in out else "file" if "file" in out else None
    if file_key and isinstance(out[file_key], str):
        out[file_key] = Path(out[file_key]).name
    out.pop("column", None)
    return out


def normalize_symbol(symbol):
    if not isinstance(symbol, str):
        return symbol
    if symbol.startswith("literal::"):
        match = re.match(r'^literal::(.+?):(\d+)(?::(\d+))?\|(.*)$', symbol)
        if not match:
            return symbol
        file_part, line, column, literal = match.groups()
        basename = Path(file_part).name
        return f'literal::{basename}:{line}|{literal}'
    if '|' in symbol:
        return symbol.split('|', 1)[1]
    return symbol


def normalize_entry(entry):
    out = {}
    for key, value in entry.items():
        if key == 'source_location':
            out[key] = normalize_source_location(value)
        elif key == 'symbol_or_region':
            out[key] = normalize_symbol(value)
        else:
            out[key] = normalize(value)
    return {k: out[k] for k in sorted(out)}


def normalize(value):
    if isinstance(value, dict):
        if 'symbol_or_region' in value:
            return normalize_entry(value)
        out = {}
        for key in sorted(value):
            if key == 'defaults':
                continue
            if key == 'source_location':
                out[key] = normalize_source_location(value[key])
            else:
                out[key] = normalize(value[key])
        return out
    if isinstance(value, list):
        normalized = [normalize(v) for v in value]
        if normalized and isinstance(normalized[0], dict) and 'symbol_or_region' in normalized[0]:
            normalized = sorted(normalized, key=lambda item: item['symbol_or_region'])
        return normalized
    return value


def main() -> int:
    actual_path = Path(sys.argv[1]).resolve()
    expected_path = Path(sys.argv[2]).resolve()
    root = str(Path(__file__).resolve().parents[2])
    lhs = normalize(canonicalize_root(json.loads(actual_path.read_text()), root))
    rhs = normalize(canonicalize_root(rewrite_root(json.loads(expected_path.read_text()), root), root))
    if lhs != rhs:
        print('JSON mismatch')
        print(json.dumps(lhs, indent=2, sort_keys=True))
        print(json.dumps(rhs, indent=2, sort_keys=True))
        return 1
    print('policy json equal')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
