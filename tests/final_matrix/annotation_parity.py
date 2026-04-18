#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

from common import project_annotation_parity, read_json, run, write_json


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--source-dir', required=True)
    ap.add_argument('--build-dir', required=True)
    ap.add_argument('--vmp-clang', required=True)
    ap.add_argument('--rust-policy-out', required=True)
    args = ap.parse_args()

    source_dir = Path(args.source_dir)
    build_dir = Path(args.build_dir)
    out_dir = build_dir / 'tests' / 'final_matrix'
    out_dir.mkdir(parents=True, exist_ok=True)

    cpp_src = source_dir / 'tests' / 'bindings_rust' / 'parity_c' / 'sample.c'
    cpp_json = out_dir / 'annotation_cpp.policy.json'
    cpp_obj = out_dir / 'annotation_cpp.o'

    run([
        args.vmp_clang,
        '-c',
        str(cpp_src),
        '-I',
        str(source_dir / 'bindings' / 'cpp' / 'include'),
        f'--vmp-collect={cpp_json}',
        '-o',
        str(cpp_obj),
    ], cwd=source_dir)

    rust_json = Path(args.rust_policy_out)
    if not rust_json.exists():
        run(['cargo', 'build', '-p', 'demo_sample'], cwd=source_dir)
        run([
            'cargo',
            'run',
            '-p',
            'vmp-rust-collect',
            '--',
            '--target-dir',
            str(source_dir / 'target'),
            '--policy-out',
            str(rust_json),
        ], cwd=source_dir)

    cpp_policy = read_json(cpp_json)
    rust_policy = read_json(rust_json)
    cpp_projection = project_annotation_parity(cpp_policy)
    rust_projection = project_annotation_parity(rust_policy)
    write_json(out_dir / 'annotation_cpp.normalized.json', cpp_projection)
    write_json(out_dir / 'annotation_rust.normalized.json', rust_projection)
    if cpp_projection != rust_projection:
        raise SystemExit(
            'annotation parity mismatch\n'
            f'cpp={json.dumps(cpp_projection, indent=2, sort_keys=True)}\n'
            f'rust={json.dumps(rust_projection, indent=2, sort_keys=True)}'
        )
    print('annotation parity OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
