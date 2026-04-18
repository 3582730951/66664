#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path

from common import read_json, run

MODULE_TEXT = '''
entry:
  ldi_u64 vr0, 0
  ldi_u64 vr1, 1
  ldi_u64 vr2, 128
loop:
  add vr0, vr0, vr1
  jlt vr0, vr2, @loop
  ret
'''


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--source-dir', required=True)
    ap.add_argument('--build-dir', required=True)
    ap.add_argument('--vm1-asm', required=True)
    ap.add_argument('--vm1-run', required=True)
    ap.add_argument('--fusion-test', required=True)
    args = ap.parse_args()

    build_dir = Path(args.build_dir)
    out_dir = build_dir / 'tests' / 'final_matrix'
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix='vmp_final_matrix_jit_') as td:
        tmp = Path(td)
        module_src = tmp / 'hot.vm1s'
        module_bin = tmp / 'hot.vm1'
        warm_profile = tmp / 'warm_profile.json'
        offline_profile = tmp / 'offline_profile.json'
        audit_log = tmp / 'scheduler.log'

        module_src.write_text(MODULE_TEXT)
        run([args.vm1_asm, str(module_src), str(module_bin)])
        run([args.vm1_run, '--jit=off', '--profile-out', str(warm_profile), str(module_bin)])
        warm = read_json(warm_profile)
        if not warm.get('entries'):
            raise SystemExit('warm profile was empty')
        hot = max(warm['entries'], key=lambda entry: entry.get('hits', 0))
        crafted = {
            'schema': 'vp1',
            'version': 1,
            'source_seed': hot['module_id'],
            'meta': {'schema': 'vp1', 'origin': 'final_matrix_hotspot'},
            'entries': [{
                'module_id': hot['module_id'],
                'pc': hot['pc'],
                'hits': max(int(hot.get('hits', 0)), 512),
                'hot_class': 3,
                'importance': 1.0,
            }],
        }
        offline_profile.write_text(json.dumps(crafted, indent=2, sort_keys=True))
        proc = run([
            args.vm1_run,
            '--jit=c',
            '--profile', str(offline_profile),
            '--audit-path', str(audit_log),
            str(module_bin),
        ])
        log = audit_log.read_text()
        if 'scheduler_decision' not in log or 'kind=jit_compile_now' not in log:
            raise SystemExit(f'missing jit scheduler decision in audit log:\n{log}')
        run([args.fusion_test])
        (out_dir / 'jit_hotspot.json').write_text(json.dumps({
            'ret_stdout': proc.stdout,
            'module_id': hot['module_id'],
            'pc': hot['pc'],
            'audit_log': log,
        }, indent=2, sort_keys=True))
        print('jit hotspot OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
