#!/usr/bin/env python3
"""Run a small Linux HTTP-server case study.

The experiment compiles a single-process TCP HTTP server, protects its request
classification function, and compares baseline/protected latency and
throughput over real loopback socket requests.  It is intentionally scoped as a
server-scale sanity check rather than a production deployment benchmark.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
import math
import os
import shutil
import socket
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RAW = ROOT / "reports" / "server_case_study_20260507"
DEFAULT_OUTPUT = ROOT / "reports" / "server_case_study_20260507.json"

SOURCE = r'''
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <vmp/bindings/cpp/annotate.h>

extern void vmp_dispatch_set_binary_path(const char* path);

static uint64_t rotl64(uint64_t value, unsigned bits) {
  bits &= 63u;
  if (bits == 0u) {
    return value;
  }
  return (value << bits) | (value >> (64u - bits));
}

VMP_VM_FUNC __attribute__((noinline)) uint64_t protected_route_score(const char* path, size_t len, uint64_t salt) {
  uint64_t score = UINT64_C(0xcbf29ce484222325) ^ salt;
  for (size_t i = 0; i < len; ++i) {
    const unsigned char ch = (unsigned char)path[i];
    score ^= (uint64_t)ch + (uint64_t)(i * 131u);
    score *= UINT64_C(0x100000001b3);
    score = rotl64(score, (unsigned)((ch ^ i) & 15u));
  }
  if (len >= 4 && memcmp(path, "/api", 4) == 0) {
    score ^= UINT64_C(0x9e3779b97f4a7c15);
  } else if (len >= 7 && memcmp(path, "/health", 7) == 0) {
    score ^= UINT64_C(0x6a09e667f3bcc909);
  } else {
    score ^= UINT64_C(0xbb67ae8584caa73b);
  }
  return score;
}

static int parse_port(const char* text) {
  char* end = NULL;
  long value = strtol(text, &end, 10);
  if (end == text || *end != '\0' || value <= 0 || value > 65535) {
    return -1;
  }
  return (int)value;
}

static int parse_request_limit(const char* text) {
  char* end = NULL;
  long value = strtol(text, &end, 10);
  if (end == text || *end != '\0' || value <= 0 || value > 10000000) {
    return -1;
  }
  return (int)value;
}

static size_t extract_path(const char* request, char* out, size_t out_size) {
  const char* first_space = strchr(request, ' ');
  if (!first_space) {
    snprintf(out, out_size, "/bad");
    return strlen(out);
  }
  ++first_space;
  const char* second_space = strchr(first_space, ' ');
  if (!second_space || second_space <= first_space) {
    snprintf(out, out_size, "/bad");
    return strlen(out);
  }
  size_t len = (size_t)(second_space - first_space);
  if (len >= out_size) {
    len = out_size - 1;
  }
  memcpy(out, first_space, len);
  out[len] = '\0';
  return len;
}

static int write_all(int fd, const char* data, size_t len) {
  size_t written = 0;
  while (written < len) {
    ssize_t rc = send(fd, data + written, len - written, 0);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (rc == 0) {
      return -1;
    }
    written += (size_t)rc;
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 0 && argv && argv[0]) {
    vmp_dispatch_set_binary_path(argv[0]);
  }
  if (argc < 3) {
    fprintf(stderr, "usage: server_case <port> <request-limit>\n");
    return 2;
  }
  signal(SIGPIPE, SIG_IGN);
  const int port = parse_port(argv[1]);
  const int request_limit = parse_request_limit(argv[2]);
  if (port <= 0 || request_limit <= 0) {
    fprintf(stderr, "invalid port or request limit\n");
    return 2;
  }

  const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return 3;
  }
  int one = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t)port);
  if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    perror("bind");
    close(server_fd);
    return 4;
  }
  if (listen(server_fd, 128) != 0) {
    perror("listen");
    close(server_fd);
    return 5;
  }

  uint64_t aggregate = UINT64_C(0x123456789abcdef0);
  for (int handled = 0; handled < request_limit; ++handled) {
    const int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR) {
        --handled;
        continue;
      }
      perror("accept");
      close(server_fd);
      return 6;
    }

    char request[2048];
    ssize_t n = recv(client_fd, request, sizeof(request) - 1, 0);
    if (n < 0) {
      close(client_fd);
      continue;
    }
    request[n > 0 ? n : 0] = '\0';

    char path[512];
    const size_t path_len = extract_path(request, path, sizeof(path));
    const uint64_t score = protected_route_score(path, path_len, (uint64_t)path_len + UINT64_C(0x51ed2705));
    aggregate += score ^ (uint64_t)(path_len * 131u);

    char body[256];
    const int body_len = snprintf(body,
                                  sizeof(body),
                                  "{\"path_len\":%zu,\"score\":%llu}\n",
                                  path_len,
                                  (unsigned long long)score);
    char response[512];
    const int response_len = snprintf(response,
                                      sizeof(response),
                                      "HTTP/1.1 200 OK\r\n"
                                      "Content-Type: application/json\r\n"
                                      "Connection: close\r\n"
                                      "Content-Length: %d\r\n\r\n%s",
                                      body_len,
                                      body);
    write_all(client_fd, response, (size_t)response_len);
    close(client_fd);
  }

  close(server_fd);
  printf("server_case handled=%d aggregate=%llu\n", request_limit, (unsigned long long)aggregate);
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
  p99_idx = max(0, min(len(ordered) - 1, math.ceil(len(ordered) * 0.99) - 1))
  return {
    "median_ms": round(statistics.median(ordered), 4),
    "p95_ms": round(ordered[p95_idx], 4),
    "p99_ms": round(ordered[p99_idx], 4),
    "min_ms": round(ordered[0], 4),
    "max_ms": round(ordered[-1], 4),
  }


def number_stats(values: list[float], suffix: str) -> dict[str, float]:
  ordered = sorted(values)
  p95_idx = max(0, min(len(ordered) - 1, math.ceil(len(ordered) * 0.95) - 1))
  return {
    f"median_{suffix}": round(statistics.median(ordered), 4),
    f"p95_{suffix}": round(ordered[p95_idx], 4),
    f"min_{suffix}": round(ordered[0], 4),
    f"max_{suffix}": round(ordered[-1], 4),
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
    "profile_seed": 23,
    "mobile_bridge_mode": "off",
    "event_types": [],
  }
  entry = dict(defaults)
  entry.update({
    "symbol_or_region": "protected_route_score",
    "annotation_tags": ["vm_func", "server_route_core"],
    "protection_domain": "vm1",
  })
  return {"schema_version": 1, "defaults": defaults, "entries": [entry]}


def free_port() -> int:
  sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  sock.bind(("127.0.0.1", 0))
  port = sock.getsockname()[1]
  sock.close()
  return int(port)


def wait_for_port(port: int, timeout_sec: float = 5.0) -> None:
  deadline = time.monotonic() + timeout_sec
  last_error: OSError | None = None
  while time.monotonic() < deadline:
    try:
      with socket.create_connection(("127.0.0.1", port), timeout=0.1):
        return
    except OSError as exc:
      last_error = exc
      time.sleep(0.02)
  raise RuntimeError(f"server did not open port {port}: {last_error}")


def request_path(index: int) -> str:
  paths = [
    "/health",
    "/api/v1/item?id=17",
    "/api/v1/search?q=alpha",
    "/static/app.js",
    "/api/v2/report/weekly",
    "/metrics",
    "/api/v1/item?id=99",
    "/unknown/route",
  ]
  return paths[index % len(paths)]


def http_request(port: int, path: str) -> dict[str, Any]:
  request = f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n".encode("ascii")
  start = time.perf_counter()
  chunks: list[bytes] = []
  with socket.create_connection(("127.0.0.1", port), timeout=5.0) as sock:
    sock.sendall(request)
    while True:
      chunk = sock.recv(4096)
      if not chunk:
        break
      chunks.append(chunk)
  elapsed_ms = (time.perf_counter() - start) * 1000.0
  response = b"".join(chunks)
  header, _, body = response.partition(b"\r\n\r\n")
  status = header.splitlines()[0].decode("ascii", errors="replace") if header else ""
  return {
    "path": path,
    "status": status,
    "body": body.decode("utf-8", errors="replace").strip(),
    "latency_ms": round(elapsed_ms, 4),
  }


def run_server_sample(binary: Path, requests: int, concurrency: int, run_index: int, raw: Path, label: str) -> dict[str, Any]:
  port = free_port()
  server_log = raw / f"{label}_server_{run_index:03d}.log"
  with server_log.open("w", encoding="utf-8") as log_f:
    proc = subprocess.Popen(
      [str(binary), str(port), str(requests + 1)],
      stdout=subprocess.PIPE,
      stderr=log_f,
      text=True,
    )
    try:
      wait_for_port(port)
      paths = [request_path(i) for i in range(requests)]
      start = time.perf_counter()
      with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as pool:
        results = list(pool.map(lambda path: http_request(port, path), paths))
      elapsed_sec = time.perf_counter() - start
      stdout, _ = proc.communicate(timeout=20)
    finally:
      if proc.poll() is None:
        proc.terminate()
        try:
          proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
          proc.kill()
          proc.wait(timeout=5)

  if proc.returncode != 0:
    raise SystemExit(f"{label} server run {run_index} failed with exit {proc.returncode}; see {server_log}")
  bad = [item for item in results if item["status"] != "HTTP/1.1 200 OK"]
  if bad:
    raise SystemExit(f"{label} server run {run_index} returned non-200 response: {bad[0]}")
  body_digest = hashlib.sha256("\n".join(item["body"] for item in results).encode("utf-8")).hexdigest()
  latencies = [float(item["latency_ms"]) for item in results]
  return {
    "run": run_index,
    "stdout": stdout.strip(),
    "requests": requests,
    "concurrency": concurrency,
    "elapsed_sec": round(elapsed_sec, 6),
    "throughput_rps": round(requests / elapsed_sec, 4),
    "latency": stats(latencies),
    "body_sha256": body_digest,
    "server_log": server_log.relative_to(ROOT).as_posix(),
  }


def run_samples(binary: Path, requests: int, concurrency: int, runs_count: int, raw: Path, label: str) -> list[dict[str, Any]]:
  return [run_server_sample(binary, requests, concurrency, idx + 1, raw, label) for idx in range(runs_count)]


def aggregate(samples: list[dict[str, Any]]) -> dict[str, Any]:
  throughput = [float(item["throughput_rps"]) for item in samples]
  med_latency = [float(item["latency"]["median_ms"]) for item in samples]
  p95_latency = [float(item["latency"]["p95_ms"]) for item in samples]
  return {
    "throughput": number_stats(throughput, "rps"),
    "per_run_median_latency": number_stats(med_latency, "ms"),
    "per_run_p95_latency": number_stats(p95_latency, "ms"),
  }


def main() -> int:
  ap = argparse.ArgumentParser()
  ap.add_argument("--runs", type=int, default=20)
  ap.add_argument("--requests", type=int, default=400)
  ap.add_argument("--concurrency", type=int, default=16)
  ap.add_argument("--raw-dir", type=Path, default=DEFAULT_RAW)
  ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
  args = ap.parse_args()
  if args.runs < 5:
    raise SystemExit("--runs must be at least 5")
  if args.requests < 32:
    raise SystemExit("--requests must be at least 32")
  if args.concurrency < 1 or args.concurrency > args.requests:
    raise SystemExit("--concurrency must be between 1 and --requests")

  raw = args.raw_dir if args.raw_dir.is_absolute() else (ROOT / args.raw_dir)
  if raw.exists():
    shutil.rmtree(raw)
  raw.mkdir(parents=True, exist_ok=True)
  source = raw / "server_case.c"
  source.write_text(SOURCE, encoding="utf-8")
  pol = raw / "policy.json"
  pol.write_text(json.dumps(policy(), indent=2, sort_keys=True) + "\n", encoding="utf-8")

  baseline = raw / "server_case"
  protected = raw / "server_case.protected"
  helper = ROOT / "tests" / "integration_targets" / "trampoline_dispatch_elf.c"
  include_dir = ROOT / "bindings" / "cpp" / "include"
  vmp_protect = ROOT / "build" / "tools" / "vmp-protect"
  if not vmp_protect.exists():
    raise SystemExit("missing build/tools/vmp-protect; build the tool before running the server case study")

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
  ], log=raw / "compile.log")
  baseline.chmod(0o755)
  nm = run(["nm", "-g", str(baseline)], log=raw / "nm.log")
  if " protected_route_score" not in nm.stdout:
    raise SystemExit("compiled server harness does not expose protected_route_score")

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
  ], log=raw / "protect.log", timeout=180)
  protected.chmod(0o755)

  baseline_runs = run_samples(baseline, args.requests, args.concurrency, args.runs, raw, "baseline")
  protected_runs = run_samples(protected, args.requests, args.concurrency, args.runs, raw, "protected")
  (raw / "baseline_runs.json").write_text(json.dumps(baseline_runs, indent=2, sort_keys=True) + "\n", encoding="utf-8")
  (raw / "protected_runs.json").write_text(json.dumps(protected_runs, indent=2, sort_keys=True) + "\n", encoding="utf-8")

  baseline_digests = {item["body_sha256"] for item in baseline_runs}
  protected_digests = {item["body_sha256"] for item in protected_runs}
  baseline_stdout = {item["stdout"] for item in baseline_runs}
  protected_stdout = {item["stdout"] for item in protected_runs}
  correct = len(baseline_digests) == 1 and baseline_digests == protected_digests and len(baseline_stdout) == 1 and baseline_stdout == protected_stdout
  baseline_agg = aggregate(baseline_runs)
  protected_agg = aggregate(protected_runs)
  throughput_ratio = round(protected_agg["throughput"]["median_rps"] / baseline_agg["throughput"]["median_rps"], 4)
  latency_ratio = round(protected_agg["per_run_median_latency"]["median_ms"] / baseline_agg["per_run_median_latency"]["median_ms"], 4)

  report = {
    "schema": "pavmp.server_case_study.v1",
    "generated_utc": utc_now(),
    "fabricated_data": False,
    "system": "single-process loopback HTTP server",
    "platform": "x86_64-linux",
    "protected_symbol": "protected_route_score",
    "requests_per_run": args.requests,
    "concurrency": args.concurrency,
    "runs_per_variant": args.runs,
    "all_correct": correct,
    "baseline": baseline_agg,
    "protected": protected_agg,
    "throughput_ratio_protected_over_baseline": throughput_ratio,
    "median_latency_ratio_protected_over_baseline": latency_ratio,
    "claim_scope": "Server-scale loopback HTTP case study: a single-process server with protected request classification, measured via real TCP requests.",
    "non_claims": [
      "Not a production multi-process deployment or internet-facing service benchmark.",
      "Not a live attacker campaign against the server.",
      "Not a replacement for a mainstream protector same-workload baseline.",
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
    },
  }
  output = args.output if args.output.is_absolute() else (ROOT / args.output)
  output.parent.mkdir(parents=True, exist_ok=True)
  output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
  print(f"server case study report: {output.relative_to(ROOT)}")
  print(json.dumps({"ok": correct, "throughput_ratio": throughput_ratio, "latency_ratio": latency_ratio, "runs": args.runs}, sort_keys=True))
  return 0 if correct else 1


if __name__ == "__main__":
  raise SystemExit(main())
