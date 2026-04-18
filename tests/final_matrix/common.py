#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable


def run(cmd: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, text=True, capture_output=True)
    if check and proc.returncode != 0:
        raise SystemExit(
            f"command failed ({proc.returncode}): {' '.join(cmd)}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    return proc


def ensure_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise SystemExit(f"required tool not found: {name}")
    return path


def read_json(path: Path):
    return json.loads(path.read_text())


def write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True))


def canonicalize_root(value, root: str):
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
        file_part, line, _column, literal = match.groups()
        return f'literal::{Path(file_part).name}:{line}|{literal}'
    return symbol.split('|', 1)[-1]


def normalize_policy(value):
    if isinstance(value, dict):
        if 'symbol_or_region' in value:
            out = {}
            for key, item in value.items():
                if key == 'source_location':
                    out[key] = normalize_source_location(item)
                elif key == 'symbol_or_region':
                    out[key] = normalize_symbol(item)
                else:
                    out[key] = normalize_policy(item)
            return {k: out[k] for k in sorted(out)}
        out = {}
        for key in sorted(value):
            if key == 'defaults':
                continue
            if key == 'source_location':
                out[key] = normalize_source_location(value[key])
            else:
                out[key] = normalize_policy(value[key])
        return out
    if isinstance(value, list):
        items = [normalize_policy(v) for v in value]
        if items and isinstance(items[0], dict) and 'symbol_or_region' in items[0]:
            items = sorted(items, key=lambda x: x['symbol_or_region'])
        return items
    return value


def project_annotation_parity(policy: dict) -> list[dict[str, object]]:
    rows = []
    for entry in policy.get('entries', []):
        symbol = normalize_symbol(entry['symbol_or_region'])
        if symbol.startswith('literal::'):
            symbol = 'literal|' + symbol.split('|', 1)[1]
        else:
            symbol = symbol.split('::')[-1]
        tags = sorted(tag for tag in entry.get('annotation_tags', []) if not tag.startswith('rust_kind:'))
        if symbol.split('::')[-1] not in {'add', 'GREETING'} and not symbol.startswith('literal::'):
            continue
        if symbol.startswith('literal::') and '"secret key"' not in symbol:
            continue
        rows.append({
            'symbol_or_region': symbol,
            'protection_domain': entry.get('protection_domain', 'native'),
            'plaintext_budget': entry.get('plaintext_budget', 'transient_only'),
            'sensitivity_level': entry.get('sensitivity_level', 'normal'),
            'annotation_tags': tags,
        })
    return sorted(rows, key=lambda item: item['symbol_or_region'])


def parse_ret_int(stdout: str) -> int:
    match = re.search(r"ret_int=(\d+)", stdout)
    if not match:
        raise SystemExit(f"missing ret_int in output:\n{stdout}")
    return int(match.group(1))


def print_kv(title: str, pairs: Iterable[tuple[str, object]]) -> None:
    print(title)
    for key, value in pairs:
        print(f"  {key}: {value}")
