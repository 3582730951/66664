#!/usr/bin/env python3
"""Run a SQLite component case-study sanity check.

This experiment compiles a static SQLite workload harness, protects the
SQLite library symbol sqlite3_step in the resulting ELF, and compares
baseline/protected correctness and runtime.  It is intentionally scoped as a
component case study: it exercises a real open-source database component, but
it is not a large-service deployment benchmark.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RAW = ROOT / "reports" / "sqlite_case_study_20260506"
DEFAULT_OUTPUT = ROOT / "reports" / "sqlite_case_study_20260506.json"
RESULT_RE = re.compile(r"sqlite_case n=(?P<n>\d+) sum=(?P<sum>\d+) count=(?P<count>\d+) maxlen=(?P<maxlen>\d+)")

SOURCE = r'''
#include <sqlite3.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void vmp_dispatch_set_binary_path(const char* path);

static void check_rc(int rc, sqlite3* db, const char* what) {
  if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
    fprintf(stderr, "%s: %s\n", what, sqlite3_errmsg(db));
    exit(2);
  }
}

int main(int argc, char** argv) {
  if (argc > 0 && argv && argv[0]) {
    vmp_dispatch_set_binary_path(argv[0]);
  }
  const int n = argc > 1 ? atoi(argv[1]) : 5000;
  sqlite3* db = NULL;
  check_rc(sqlite3_open(":memory:", &db), db, "open");
  check_rc(sqlite3_exec(db, "create table t(id integer primary key, v text, x integer);", NULL, NULL, NULL),
           db,
           "create");

  sqlite3_stmt* insert = NULL;
  check_rc(sqlite3_prepare_v2(db, "insert into t(v,x) values(?1,?2);", -1, &insert, NULL), db, "prepare insert");
  for (int i = 0; i < n; ++i) {
    char value[64];
    snprintf(value, sizeof(value), "row-%d", i);
    sqlite3_bind_text(insert, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(insert, 2, (i * 17) % 7919);
    check_rc(sqlite3_step(insert), db, "step insert");
    sqlite3_reset(insert);
    sqlite3_clear_bindings(insert);
  }
  sqlite3_finalize(insert);

  sqlite3_stmt* query = NULL;
  check_rc(sqlite3_prepare_v2(db, "select sum(x), count(*), max(length(v)) from t where x>=0;", -1, &query, NULL),
           db,
           "prepare query");
  check_rc(sqlite3_step(query), db, "step query");
  const long long sum = sqlite3_column_int64(query, 0);
  const long long count = sqlite3_column_int64(query, 1);
  const long long maxlen = sqlite3_column_int64(query, 2);
  sqlite3_finalize(query);
  sqlite3_close(db);

  printf("sqlite_case n=%d sum=%lld count=%lld maxlen=%lld\n", n, sum, count, maxlen);
  return 0;
}
'''


def utc_now() -> str:
  return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
  h = hashlib.sha256()
  with path.open("rb") as f:
    for chunk in iter(lambda: f.read(1024 * 1024), b""):
      h.update(chunk)
  return h.hexdigest()


def run(cmd: list[str], *, cwd: Path | None = None, log: Path | None = None, timeout: int = 120) -> subprocess.CompletedProcess[str]:
  proc = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, timeout=timeout)
  if log is not None:
    log.write_text(
      "$ " + " ".join(cmd) + "\n"
      f"[exit_code] {proc.returncode}\n--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}\n",
      encoding="utf-8",
    )
  if proc.returncode != 0:
    raise SystemExit(f"command failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
  return proc


def stats(values: list[float]) -> dict[str, float]:
  ordered = sorted(values)
  p95_idx = max(0, min(len(ordered) - 1, math.ceil(len(ordered) * 0.95) - 1))
  return {
    "median_ms": round(statistics.median(ordered), 4),
    "p95_ms": round(ordered[p95_idx], 4),
    "min_ms": round(ordered[0], 4),
    "max_ms": round(ordered[-1], 4),
  }


def policy() -> dict[str, Any]:
  defaults = {
    "language_origin": "binary",
    "annotation_origin": "external_manifest",
    "protection_domain": "native",
    "jit_policy": "off",
    "plaintext_budget": "transient_only",
    "reaction_policy": "log",
    "integrity_level": "basic",
    "platform_caps": ["linux", "x64"],
    "sensitivity_level": "normal",
    "profile_seed": 17,
    "mobile_bridge_mode": "off",
    "event_types": [],
  }
  entry = dict(defaults)
  entry.update({
    "symbol_or_region": "sqlite3_step",
    "annotation_tags": ["vm_func", "sqlite_component"],
    "protection_domain": "vm1",
  })
  return {"schema_version": 1, "defaults": defaults, "entries": [entry]}


def parse_result(stdout: str) -> dict[str, str]:
  match = RESULT_RE.search(stdout.strip())
  if not match:
    raise SystemExit(f"unexpected sqlite case output: {stdout!r}")
  return match.groupdict()


def run_samples(binary: Path, iterations: int, runs_count: int) -> list[dict[str, Any]]:
  samples: list[dict[str, Any]] = []
  for i in range(runs_count):
    proc = run([str(binary), str(iterations)], timeout=120)
    parsed = parse_result(proc.stdout)
    samples.append({
      "run": i + 1,
      "stdout": proc.stdout.strip(),
      "elapsed_ms": None,
      "parsed": parsed,
    })
  return samples


def timed_samples(binary: Path, iterations: int, runs_count: int) -> list[dict[str, Any]]:
  samples: list[dict[str, Any]] = []
  for i in range(runs_count):
    start = dt.datetime.now(dt.timezone.utc)
    mono_start = time.perf_counter()
    proc = run([str(binary), str(iterations)], timeout=120)
    elapsed_ms = (time.perf_counter() - mono_start) * 1000.0
    parsed = parse_result(proc.stdout)
    samples.append({
      "run": i + 1,
      "started_utc": start.replace(microsecond=0).isoformat().replace("+00:00", "Z"),
      "stdout": proc.stdout.strip(),
      "elapsed_ms": round(elapsed_ms, 4),
      "parsed": parsed,
    })
  return samples


def main() -> int:
  ap = argparse.ArgumentParser()
  ap.add_argument("--runs", type=int, default=40)
  ap.add_argument("--iterations", type=int, default=5000)
  ap.add_argument("--raw-dir", type=Path, default=DEFAULT_RAW)
  ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
  args = ap.parse_args()
  if args.runs < 5:
    raise SystemExit("--runs must be at least 5")
  sqlite_static = Path("/usr/lib/x86_64-linux-gnu/libsqlite3.a")
  sqlite_header = Path("/usr/include/sqlite3.h")
  if not sqlite_static.exists() or not sqlite_header.exists():
    raise SystemExit("missing libsqlite3-dev; install sqlite3 libsqlite3-dev first")

  raw = args.raw_dir if args.raw_dir.is_absolute() else (ROOT / args.raw_dir)
  if raw.exists():
    shutil.rmtree(raw)
  raw.mkdir(parents=True, exist_ok=True)
  source = raw / "sqlite_case.c"
  source.write_text(SOURCE, encoding="utf-8")
  pol = raw / "policy.json"
  pol.write_text(json.dumps(policy(), indent=2, sort_keys=True) + "\n", encoding="utf-8")

  baseline = raw / "sqlite_case"
  protected = raw / "sqlite_case.protected"
  helper = ROOT / "tests" / "integration_targets" / "trampoline_dispatch_elf.c"
  include_dir = ROOT / "bindings" / "cpp" / "include"
  vmp_protect = ROOT / "build" / "tools" / "vmp-protect"
  if not vmp_protect.exists():
    raise SystemExit("missing build/tools/vmp-protect; build the tool before running the case study")

  run([
    "gcc",
    "-O2",
    "-g",
    "-fno-pie",
    "-no-pie",
    "-rdynamic",
    "-I",
    str(include_dir),
    "-o",
    str(baseline),
    str(source),
    str(helper),
    str(sqlite_static),
    "-ldl",
    "-lpthread",
    "-lm",
  ], log=raw / "compile.log")
  baseline.chmod(0o755)

  nm = run(["nm", "-g", str(baseline)], log=raw / "nm.log")
  if " sqlite3_step" not in nm.stdout:
    raise SystemExit("compiled SQLite harness does not expose sqlite3_step")

  run([
    str(vmp_protect),
    "--policy",
    str(pol),
    "--input",
    str(baseline),
    "--output",
    str(protected),
    "--lift",
    "--strings-pool",
    str(raw / "strings_pool.bin"),
    "--strings-idx",
    str(raw / "strings_pool.idx.json"),
    "--string-kdf",
    str(raw / "strings_pool.kdf.json"),
  ], log=raw / "protect.log")
  protected.chmod(0o755)

  baseline_runs = timed_samples(baseline, args.iterations, args.runs)
  protected_runs = timed_samples(protected, args.iterations, args.runs)
  (raw / "baseline_runs.json").write_text(json.dumps(baseline_runs, indent=2, sort_keys=True) + "\n", encoding="utf-8")
  (raw / "protected_runs.json").write_text(json.dumps(protected_runs, indent=2, sort_keys=True) + "\n", encoding="utf-8")

  baseline_results = {json.dumps(item["parsed"], sort_keys=True) for item in baseline_runs}
  protected_results = {json.dumps(item["parsed"], sort_keys=True) for item in protected_runs}
  correct = len(baseline_results) == 1 and baseline_results == protected_results
  baseline_stats = stats([float(item["elapsed_ms"]) for item in baseline_runs])
  protected_stats = stats([float(item["elapsed_ms"]) for item in protected_runs])
  overhead = round(protected_stats["median_ms"] / baseline_stats["median_ms"], 4)
  sqlite_version = run(["sqlite3", "--version"]).stdout.strip()
  report = {
    "schema": "pavmp.sqlite_case_study.v1",
    "generated_utc": utc_now(),
    "fabricated_data": False,
    "component": "SQLite",
    "sqlite_version": sqlite_version,
    "platform": "x86_64-linux",
    "protected_symbol": "sqlite3_step",
    "iterations": args.iterations,
    "runs_per_variant": args.runs,
    "all_correct": correct,
    "baseline": baseline_stats,
    "protected": protected_stats,
    "overhead_ratio": overhead,
    "observed_result": json.loads(next(iter(baseline_results))) if baseline_results else None,
    "claim_scope": "Component-level SQLite case study: a static SQLite harness with sqlite3_step protected by PAVMP VM1.",
    "non_claims": [
      "Not a browser, DBMS service, or multi-process production deployment benchmark.",
      "Not a live attacker campaign against SQLite.",
      "Not a replacement for the direct Linux benchmark table.",
      "Does not compare against Tigress or a commercial protector.",
    ],
    "artifacts": {
      "raw_dir": raw.relative_to(ROOT).as_posix(),
      "source": source.relative_to(ROOT).as_posix(),
      "policy": pol.relative_to(ROOT).as_posix(),
      "baseline": baseline.relative_to(ROOT).as_posix(),
      "protected": protected.relative_to(ROOT).as_posix(),
      "baseline_runs": (raw / "baseline_runs.json").relative_to(ROOT).as_posix(),
      "protected_runs": (raw / "protected_runs.json").relative_to(ROOT).as_posix(),
    },
    "sha256": {
      "source": sha256_file(source),
      "policy": sha256_file(pol),
      "baseline": sha256_file(baseline),
      "protected": sha256_file(protected),
      "sqlite_static_library": sha256_file(sqlite_static),
    },
  }
  output = args.output if args.output.is_absolute() else (ROOT / args.output)
  output.parent.mkdir(parents=True, exist_ok=True)
  output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
  print(f"sqlite case study report: {output.relative_to(ROOT)}")
  print(json.dumps({"ok": correct, "overhead_ratio": overhead, "runs": args.runs}, sort_keys=True))
  return 0 if correct else 1


if __name__ == "__main__":
  raise SystemExit(main())
