#!/usr/bin/env python3
import json
import os
import pathlib
import subprocess
import sys
import tempfile


def main() -> int:
    tool, asm_tool, run_tool = sys.argv[1:4]
    with tempfile.TemporaryDirectory(prefix="vmp_string_roundtrip_") as td:
        td = pathlib.Path(td)
        policy = td / "policy.json"
        policy.write_text(json.dumps({
            "schema_version": 1,
            "defaults": {"language_origin": "cpp", "annotation_origin": "attribute"},
            "entries": [
                {
                    "symbol_or_region": "sid42",
                    "annotation_tags": ["vm_string"],
                    "plaintext_budget": "transient_only",
                    "sensitivity_level": "highly_sensitive",
                    "string_id": 42,
                    "value": "roundtrip-ok"
                }
            ]
        }))
        pool = td / "string_pool.bin"
        idx = td / "string_pool.idx.json"
        meta = td / "key_derivation.json"
        env = os.environ.copy()
        env["VMP_STRING_MASTER_KEY"] = "1234123412341234123412341234123412341234123412341234123412341234"
        subprocess.run([tool, "--policy", str(policy), "--out-bin", str(pool), "--out-idx", str(idx), "--out-kdf", str(meta)],
                       env=env, check=True, capture_output=True, text=True)

        asm = td / "mod.vm1s"
        asm.write_text("""entry:\n  load_tstr vr5, &sid42\n  mov vr0, vr5\n  domain_call native, 701, 1\n  release_tstr vr5\n  ret\n""")
        mod = td / "mod.vm1"
        subprocess.run([asm_tool, str(asm), str(mod)], check=True, capture_output=True, text=True)
        proc = subprocess.run([
            run_tool,
            "--string-pool", str(pool),
            "--string-idx", str(idx),
            "--key-env", "VMP_STRING_MASTER_KEY",
            "--native-print-string", "701",
            str(mod),
        ], env=env, check=True, capture_output=True, text=True)
        out = proc.stdout
        if "native_string=roundtrip-ok" not in out:
            raise SystemExit(f"unexpected output: {out}")
    print("vmp-string-protect round-trip OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
