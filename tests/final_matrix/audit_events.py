#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import tempfile
from pathlib import Path

from common import run


def parse_lines(path: Path) -> list[str]:
    return [line for line in path.read_text().splitlines() if line.strip()]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--audit-helper', required=True)
    ap.add_argument('--build-dir', required=True)
    args = ap.parse_args()

    out_dir = Path(args.build_dir) / 'tests' / 'final_matrix'
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix='vmp_final_matrix_audit_') as td:
        tmp = Path(td)
        audit_log = tmp / 'events.log'
        proc = run([args.audit_helper, '--mode', 'emit', '--audit-path', str(audit_log)])
        lines = parse_lines(audit_log)
        if len(lines) != 4:
            raise SystemExit(f'expected 4 audit lines, got {len(lines)}: {lines}')
        required = ['hw_breakpoint', 'integrity_mismatch', 'env_anomaly', 'unknown']
        for kind in required:
            if not any(f'[{kind}]' in line for line in lines):
                raise SystemExit(f'missing event type {kind}: {lines}')
        if not any('[module=] [symbol=] [offset=+0x0]' in line for line in lines):
            raise SystemExit(f'unknown-symbol line missing expected empty metadata: {lines}')
        if 'emit_ok' not in proc.stdout:
            raise SystemExit(f'unexpected helper output: {proc.stdout}')

        ro_path = Path('/proc/sys/kernel/osrelease')
        env = os.environ.copy()
        env['VMP_AUDIT_PATH'] = str(ro_path)
        fallback = run([args.audit_helper, '--mode', 'readonly', '--audit-path', str(ro_path)], env=env)
        if fallback.returncode != 0:
            raise SystemExit(f'readonly fallback failed: {fallback.stdout}\n{fallback.stderr}')
        if 'readonly_fallback_ok' not in fallback.stdout:
            raise SystemExit(f'missing readonly fallback marker: {fallback.stdout}')

        (out_dir / 'audit_events.json').write_text(json.dumps({
            'line_count': len(lines),
            'events': lines,
            'readonly_stdout': fallback.stdout,
        }, indent=2, sort_keys=True))
        print('audit events OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
