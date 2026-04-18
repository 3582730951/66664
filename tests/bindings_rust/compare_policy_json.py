#!/usr/bin/env python3
import json, re, sys
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

def norm_loc(v):
    if not isinstance(v, dict):
        return v
    out = dict(v)
    if 'file' in out:
        out['file'] = Path(out['file']).name
    out.pop('column', None)
    return out

def norm_symbol(v):
    if not isinstance(v, str):
        return v
    if v.startswith('literal::'):
        m = re.match(r'^literal::(.+?):(\d+)(?::\d+)?\|(.*)$', v)
        if m:
            return f'literal::{Path(m.group(1)).name}:{m.group(2)}|{m.group(3)}'
    return v.split('|', 1)[-1] if '|' in v else v

def norm(x):
    if isinstance(x, dict):
        out = {}
        for k in sorted(x):
            if k == 'defaults':
                continue
            if k == 'source_location':
                out[k] = norm_loc(x[k])
            elif k == 'symbol_or_region':
                out[k] = norm_symbol(x[k])
            elif k == 'annotation_tags':
                tags = sorted(tag for tag in x[k] if not str(tag).startswith('rust_kind:'))
                if tags:
                    out[k] = tags
            elif k == 'event_types' and x[k] == []:
                continue
            elif k == 'platform_caps' and x[k] == []:
                continue
            elif k == 'integrity_level' and x[k] == 'none':
                continue
            elif k == 'jit_policy' and x[k] == 'off':
                continue
            elif k == 'mobile_bridge_mode' and x[k] == 'off':
                continue
            elif k == 'profile_seed' and x[k] == 0:
                continue
            elif k == 'reaction_policy' and x[k] == 'log':
                continue
            else:
                out[k] = norm(x[k])
        return out
    if isinstance(x, list):
        xs = [norm(v) for v in x]
        if xs and isinstance(xs[0], dict) and 'symbol_or_region' in xs[0]:
            xs = sorted(xs, key=lambda i: i['symbol_or_region'])
        return xs
    return x

lhs = norm(canonicalize_root(json.loads(Path(sys.argv[1]).read_text()), str(Path(__file__).resolve().parents[2])))
rhs = norm(canonicalize_root(rewrite_root(json.loads(Path(sys.argv[2]).read_text()), str(Path(__file__).resolve().parents[2])), str(Path(__file__).resolve().parents[2])))
if lhs != rhs:
    print('JSON mismatch')
    print(json.dumps(lhs, indent=2, sort_keys=True))
    print(json.dumps(rhs, indent=2, sort_keys=True))
    sys.exit(1)
print('policy json equal')
