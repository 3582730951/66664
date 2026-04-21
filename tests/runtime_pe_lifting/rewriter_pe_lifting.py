#!/usr/bin/env python3
import json
import pathlib
import re
import shutil
import struct
import subprocess
import sys


def fail(msg: str) -> None:
    raise SystemExit(msg)


def sh(cmd: list[str | pathlib.Path], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    text_cmd = [str(part) for part in cmd]
    res = subprocess.run(text_cmd, text=True, capture_output=True)
    if check and res.returncode != 0:
        raise SystemExit(
            f"command failed ({res.returncode}): {' '.join(text_cmd)}\nstdout:\n{res.stdout}\nstderr:\n{res.stderr}"
        )
    return res


def parse_pe(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    if data[:2] != b'MZ':
        fail('not mz')
    pe_off = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b'PE\0\0':
        fail('not pe')
    file_hdr_off = pe_off + 4
    machine, section_count, _, ptr_sym, nsyms, opt_size, _ = struct.unpack_from('<HHIIIHH', data, file_hdr_off)
    sec_off = file_hdr_off + 20 + opt_size
    string_table_off = ptr_sym + nsyms * 18
    string_table_size = 0
    if ptr_sym and string_table_off + 4 <= len(data):
        string_table_size = struct.unpack_from('<I', data, string_table_off)[0]

    def resolve_section_name(raw_field: bytes) -> str:
        short = raw_field.split(b'\0', 1)[0].decode(errors='replace')
        if not short.startswith('/') or len(short) == 1 or not short[1:].isdigit() or string_table_size < 4:
            return short
        offset = int(short[1:])
        if offset >= string_table_size:
            return short
        start = string_table_off + offset
        end = data.find(b'\0', start, string_table_off + string_table_size)
        if end == -1:
            end = string_table_off + string_table_size
        return data[start:end].decode(errors='replace')

    sections = []
    for idx in range(section_count):
        off = sec_off + idx * 40
        name = resolve_section_name(data[off:off + 8])
        virtual_size, virtual_address, raw_size, raw_ptr = struct.unpack_from('<IIII', data, off + 8)
        characteristics = struct.unpack_from('<I', data, off + 36)[0]
        sections.append({
            'index': idx + 1,
            'name': name,
            'virtual_size': virtual_size,
            'virtual_address': virtual_address,
            'raw_size': raw_size,
            'raw_ptr': raw_ptr,
            'characteristics': characteristics,
        })
    return {
        'data': data,
        'machine': machine,
        'sections': sections,
    }


def section_bytes(pe: dict, section: dict) -> bytes:
    start = section['raw_ptr']
    end = start + section['raw_size']
    return pe['data'][start:end]


def parse_vmpcode_section(blob: bytes) -> list[dict[str, object]]:
    if blob[:4] != b'VMPC':
        fail('bad vmpcode magic')
    offset = 4
    count = struct.unpack_from('<I', blob, offset)[0]
    offset += 4
    records = []
    for _ in range(count):
        bundle_id = struct.unpack_from('<Q', blob, offset)[0]
        offset += 8
        domain = blob[offset]
        offset += 1
        symbol_len = struct.unpack_from('<I', blob, offset)[0]
        offset += 4
        payload_len = struct.unpack_from('<I', blob, offset)[0]
        offset += 4
        symbol = blob[offset:offset + symbol_len].decode(errors='replace')
        offset += symbol_len
        payload = blob[offset:offset + payload_len]
        offset += payload_len
        records.append({
            'bundle_id': bundle_id,
            'domain': domain,
            'symbol': symbol,
            'payload': payload,
        })
    return records


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit('usage: rewriter_pe_lifting.py <vmp-protect> <vmp-trampoline-inject> <binary-dir>')

    protect_tool = pathlib.Path(sys.argv[1])
    trampoline_tool = pathlib.Path(sys.argv[2])
    binary_dir = pathlib.Path(sys.argv[3])
    source_root = pathlib.Path(__file__).resolve().parents[2]
    work = binary_dir / 'tests' / 'rewriter_pe_lifting'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    compiler = shutil.which('x86_64-w64-mingw32-gcc') or shutil.which('x86_64-w64-mingw32-clang')
    if not compiler:
        print('SKIP_REASON: mingw compiler unavailable')
        return

    src = source_root / 'tests' / 'integration_targets' / 'target_c.c'
    include_dir = source_root / 'bindings' / 'cpp' / 'include'
    baseline = work / 'target_c.exe'
    stage1 = work / 'target_c.stage1.exe'
    protected = work / 'target_c.protected.exe'
    sh([compiler, str(src), '-I', str(include_dir), '-O2', '-o', str(baseline)])

    policy = work / 'policy.json'
    policy.write_text(json.dumps({
        'schema_version': 1,
        'defaults': {
            'language_origin': 'binary',
            'annotation_origin': 'external_manifest',
            'protection_domain': 'native',
            'jit_policy': 'off',
            'plaintext_budget': 'transient_only',
            'reaction_policy': 'log',
            'integrity_level': 'basic',
            'platform_caps': ['windows', 'x64'],
            'sensitivity_level': 'normal',
            'profile_seed': 1,
            'mobile_bridge_mode': 'off',
            'event_types': [],
        },
        'entries': [{
            'symbol_or_region': 'protected_mix_c',
            'language_origin': 'binary',
            'annotation_origin': 'external_manifest',
            'annotation_tags': ['vm_func'],
            'protection_domain': 'vm1',
            'jit_policy': 'off',
            'plaintext_budget': 'transient_only',
            'reaction_policy': 'log',
            'integrity_level': 'basic',
            'platform_caps': ['windows', 'x64'],
            'sensitivity_level': 'normal',
            'profile_seed': 1,
            'mobile_bridge_mode': 'off',
            'event_types': [],
        }],
    }, indent=2))

    sh([protect_tool, '--policy', policy, '--input', baseline, '--output', stage1])
    sh([trampoline_tool, '--policy', policy, '--input', stage1, '--output', protected])

    stage1_pe = parse_pe(stage1)
    protected_pe = parse_pe(protected)
    stage1_names = {sec['name'] for sec in stage1_pe['sections']}
    new_randomized = [
        sec for sec in protected_pe['sections']
        if sec['name'] not in stage1_names and re.fullmatch(r'\.[a-z0-9]{7}', sec['name'])
    ]
    vmpcode_sections = [sec for sec in new_randomized if section_bytes(protected_pe, sec).startswith(b'VMPC')]
    if not vmpcode_sections:
        fail(f'missing serialized VMPC section in new randomized sections: {[sec["name"] for sec in new_randomized]}')

    records = parse_vmpcode_section(section_bytes(protected_pe, vmpcode_sections[0]))
    if len(records) != 1:
        fail(f'expected exactly one VMPC record, got {records}')
    match = records[0]
    if match['domain'] != 1:
        fail(f'expected vm1 domain=1, got {match["domain"]}')
    if not re.fullmatch(r'[0-9a-f]{16}', str(match['symbol'])):
        fail(f'expected HMAC symbol id, got {match["symbol"]!r}')
    if not bytes(match['payload']).startswith(b'VM1B'):
        fail(f'expected VM1 payload magic, got {bytes(match["payload"])[:4]!r}')

    print('rewriter_pe_lifting OK')


if __name__ == '__main__':
    main()
