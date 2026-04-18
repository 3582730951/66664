#!/usr/bin/env python3
import json, re, sys
from pathlib import Path

def norm_symbol(s):
    if s.startswith('literal::'):
        m = re.match(r'^literal::(.+?):(\d+)(?::\d+)?\|(.*)$', s)
        return ('literal', m.group(3))
    return ('symbol', s.split('::')[-1])

def projection(path):
    data = json.loads(Path(path).read_text())
    out = []
    for entry in data['entries']:
        sym_kind, sym = norm_symbol(entry['symbol_or_region'])
        if sym_kind == 'symbol' and sym not in {'add', 'GREETING'}:
            continue
        if sym_kind == 'literal' and sym != '"secret key"':
            continue
        tags = sorted(tag for tag in entry.get('annotation_tags', []) if not tag.startswith('rust_kind:'))
        out.append({
            'symbol': sym,
            'kind': sym_kind,
            'protection_domain': entry.get('protection_domain', 'native'),
            'plaintext_budget': entry.get('plaintext_budget', 'transient_only'),
            'sensitivity_level': entry.get('sensitivity_level', 'normal'),
            'annotation_tags': tags,
        })
    return sorted(out, key=lambda x: (x['kind'], x['symbol']))

lhs = projection(sys.argv[1])
rhs = projection(sys.argv[2])
if lhs != rhs:
    print('parity mismatch')
    print(json.dumps(lhs, indent=2, sort_keys=True))
    print(json.dumps(rhs, indent=2, sort_keys=True))
    raise SystemExit(1)
print('rust/c parity OK')
