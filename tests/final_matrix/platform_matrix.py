#!/usr/bin/env python3
from __future__ import annotations

import argparse


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--platform', required=True)
    ap.add_argument('--arch', required=True)
    args = ap.parse_args()
    print(f'SKIP_REASON: plan 17.5 requires dedicated per-platform runners; current configured build is {args.platform}/{args.arch} only')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
