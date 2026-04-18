#!/usr/bin/env python3
import json
import os
import pathlib
import subprocess
import sys
import tempfile


def main() -> int:
    protect_tool = pathlib.Path(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="vmp_protect_strings_") as td:
        td = pathlib.Path(td)
        policy = td / "policy.json"
        policy.write_text(json.dumps({
            "schema_version": 1,
            "defaults": {
                "language_origin": "cpp",
                "annotation_origin": "attribute",
                "protection_domain": "native",
                "jit_policy": "off",
                "plaintext_budget": "transient_only",
                "reaction_policy": "log",
                "integrity_level": "basic",
                "platform_caps": ["linux", "x64"],
                "sensitivity_level": "normal",
                "profile_seed": 1,
                "mobile_bridge_mode": "off",
                "event_types": []
            },
            "entries": [
                {
                    "symbol_or_region": "sid9",
                    "annotation_tags": ["vm_string"],
                    "plaintext_budget": "transient_only",
                    "sensitivity_level": "highly_sensitive",
                    "string_id": 9,
                    "value": "protect-ok",
                    "event_types": ["string_access"]
                }
            ]
        }))
        out_json = td / "policy.out.json"
        env = os.environ.copy()
        env["VMP_STRING_MASTER_KEY"] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        proc = subprocess.run([
            str(protect_tool),
            "--policy", str(policy),
            "--emit-policy-json", str(out_json),
            "--protect-strings",
            "--string-bin", str(td / "string_pool.bin"),
            "--string-idx", str(td / "string_pool.idx.json"),
            "--string-kdf", str(td / "key_derivation.json"),
        ], env=env, check=True, capture_output=True, text=True)
        if "strings:protected=1" not in proc.stdout:
            raise SystemExit(proc.stdout)
        if not (td / "string_pool.bin").exists() or not (td / "string_pool.idx.json").exists():
            raise SystemExit("missing string protection outputs")
    print("vmp-protect strings OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
