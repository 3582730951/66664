import json
import pathlib
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.run(args, text=True, capture_output=True, check=True)


def main():
    exe = pathlib.Path(sys.argv[1])
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        a = td / "a.json"
        b = td / "b.json"
        merged = td / "merged.json"
        a.write_text(json.dumps({
            "schema": "vp1",
            "version": 1,
            "source_seed": 1,
            "entries": [{"module_id": 1, "pc": 16, "hits": 10, "hot_class": 1, "importance": 0.5}],
            "meta": {"schema": "vp1"},
        }))
        b.write_text(json.dumps({
            "schema": "vp1",
            "version": 1,
            "source_seed": 2,
            "entries": [{"module_id": 1, "pc": 16, "hits": 5, "hot_class": 2, "importance": 0.3}],
            "meta": {"schema": "vp1"},
        }))
        run(str(exe), "validate", str(a))
        run(str(exe), "merge", str(a), str(b), "--output", str(merged))
        diff = run(str(exe), "diff", str(a), str(merged))
        assert "entries changed" in diff.stdout.lower()
        run(str(exe), "validate", str(merged))
    print("vmp_profile_tool_merge_diff_validate OK")


if __name__ == "__main__":
    main()
