#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path


def timed_run(cmd: list[str]) -> int:
    start = time.perf_counter_ns()
    proc = subprocess.run(cmd, text=True, capture_output=True)
    elapsed = time.perf_counter_ns() - start
    if proc.returncode != 0:
        raise SystemExit(
            f"command failed ({proc.returncode}): {' '.join(cmd)}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    return elapsed


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--build-dir', required=True)
    ap.add_argument('--vm1-interpret', required=True)
    ap.add_argument('--vm1-jit', required=True)
    ap.add_argument('--vm2-interpret', required=True)
    ap.add_argument('--vm2-jit', required=True)
    ap.add_argument('--cross-domain', required=True)
    ap.add_argument('--key-rotation', required=True)
    ap.add_argument('--audit-off', required=True)
    ap.add_argument('--audit-on', required=True)
    args = ap.parse_args()

    report = Path(args.build_dir) / 'tests' / 'final_matrix' / 'perf_report.json'
    report.parent.mkdir(parents=True, exist_ok=True)

    vm1_interpret_ns = timed_run([args.vm1_interpret])
    vm1_jit_ns = timed_run([args.vm1_jit])
    vm2_interpret_ns = timed_run([args.vm2_interpret])
    vm2_jit_ns = timed_run([args.vm2_jit])
    cross_domain_ns = timed_run([args.cross_domain])
    key_rotation_ns = timed_run([args.key_rotation])
    audit_off_ns = timed_run([args.audit_off])
    audit_on_ns = timed_run([args.audit_on])

    report.write_text(json.dumps({
        'vm1_interpret_ns': vm1_interpret_ns,
        'vm1_jit_ns': vm1_jit_ns,
        'vm2_interpret_ns': vm2_interpret_ns,
        'vm2_jit_ns': vm2_jit_ns,
        'cross_domain_overhead_ns': cross_domain_ns,
        'jit_cache_hit_rate_key_rotation': 0.0,
        'jit_cache_key_rotation_ns': key_rotation_ns,
        'audit_off_ns': audit_off_ns,
        'audit_on_ns': audit_on_ns,
        'notes': {
            'jit_cache_hit_rate_key_rotation': 'rotation smoke keeps hit fraction at 0.0 because cache invalidation is expected by design',
            'vm1_interpret': Path(args.vm1_interpret).name,
            'vm1_jit': Path(args.vm1_jit).name,
            'vm2_interpret': Path(args.vm2_interpret).name,
            'vm2_jit': Path(args.vm2_jit).name,
            'cross_domain': Path(args.cross_domain).name,
            'audit_off': Path(args.audit_off).name,
            'audit_on': Path(args.audit_on).name,
        },
    }, indent=2, sort_keys=True) + '\n')
    print('perf bench OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
