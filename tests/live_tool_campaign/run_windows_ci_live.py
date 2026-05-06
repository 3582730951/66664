#!/usr/bin/env python3
"""Collect Windows GitHub Actions live evidence in external-live schema.

The collector is intentionally conservative: it records only checks that really
ran on the Windows runner.  Detector/audit events are included only when the
target audit log contains them; a successful debugger or Frida attach is not
reported as detector coverage by itself.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REPORT_DIR = ROOT / "reports"
EVENT_CANDIDATES = [
    "debugger_detected",
    "hardware_breakpoint_detected",
    "frida_injection_detected",
    "oracle_divergence",
    "mapping_integrity_event",
]

sys.path.insert(0, str(ROOT / "tests" / "integration_targets"))
from run_integration_ci import expected_target_c, expected_target_cpp, expected_target_rust  # noqa: E402


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def stamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return path.name


def bundle_rel(path: Path, bundle_root: Path) -> str:
    try:
        return path.resolve().relative_to(bundle_root.resolve()).as_posix()
    except ValueError:
        return rel(path)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def redact(value: object) -> str:
    text = str(value)
    replacements = {
        str(ROOT.resolve()): "<repo>",
        str(ROOT.resolve()).replace("\\", "/"): "<repo>",
        str(Path.home()): "<home>",
        str(Path.home()).replace("\\", "/"): "<home>",
        "/workspace/": "<repo>/",
        "/root/": "<home>/",
    }
    for needle, repl in replacements.items():
        text = text.replace(needle, repl)
    return text[:400]


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", errors="replace")


def ensure_text_file(path: Path) -> None:
    if not path.exists():
        write_text(path, "")


def evidence_artifact(path: Path, kind: str, bundle_root: Path) -> dict[str, str]:
    if not path.exists():
        raise FileNotFoundError(f"missing evidence artifact: {rel(path)}")
    item = {"path": bundle_rel(path, bundle_root), "kind": kind}
    item["sha256"] = sha256_file(path)
    return item


def target_expected(binary: Path, iterations: int) -> str:
    test_name = binary.parent.name
    if test_name == "target_c":
        return expected_target_c(iterations)
    if test_name == "target_cpp":
        return expected_target_cpp(iterations)
    if test_name == "target_rust":
        return expected_target_rust(iterations)
    raise SystemExit(f"cannot infer expected output for target directory: {test_name}")


def discover_default_binary(report_dir: Path, integration_date: str | None) -> Path:
    if integration_date:
        candidate = report_dir / f"integration_artifacts_{integration_date}" / "x86_64-windows" / "target_c" / "target_c.protected.exe"
        if candidate.exists():
            return candidate
        raise SystemExit(f"missing Windows protected target: {rel(candidate)}")
    candidates = sorted(
        report_dir.glob("integration_artifacts_*/x86_64-windows/target_c/target_c.protected.exe"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        raise SystemExit("missing Windows protected target under reports/integration_artifacts_*/x86_64-windows/target_c")
    return candidates[0]


def run_target(binary: Path, iterations: int, audit_path: Path, timeout_sec: int = 60) -> dict[str, Any]:
    env = os.environ.copy()
    env["VMP_AUDIT_PATH"] = str(audit_path)
    started = time.perf_counter()
    proc = subprocess.run(
        [str(binary), str(iterations)],
        cwd=str(binary.parent),
        env=env,
        capture_output=True,
        text=True,
        timeout=timeout_sec,
    )
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    audit_text = audit_path.read_text(encoding="utf-8", errors="ignore") if audit_path.exists() else ""
    return {
        "exit_code": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "observed": proc.stdout.strip().replace("\r\n", "\n").replace("\r", ""),
        "elapsed_ms": elapsed_ms,
        "audit": audit_text,
    }


def events_from_audit(audit_text: str) -> list[str]:
    return [event for event in EVENT_CANDIDATES if event in audit_text]


def make_check(
    *,
    check_id: str,
    alias: str,
    category: str,
    tool: str,
    command_summary: str,
    ok: bool,
    expected: str,
    observed: str | None,
    audit_text: str,
    claim_scope: str,
    non_claims: list[str],
    limitations: list[str],
    artifacts: list[dict[str, str]],
) -> dict[str, Any]:
    events = events_from_audit(audit_text)
    return {
        "id": check_id,
        "display_alias": alias,
        "category": category,
        "tool": tool,
        "command_summary": command_summary,
        "ok": bool(ok),
        "oracle_triggered": bool(events),
        "output_policy": "preserve_expected_output",
        "expected_result": expected,
        "observed_result": observed,
        "output_correct": observed == expected,
        "audit_events": events,
        "audit_excerpt": audit_text[:2000],
        "claim_scope": claim_scope,
        "non_claims": non_claims,
        "limitations": limitations,
        "evidence_artifacts": artifacts,
    }


def collect_native_check(binary: Path, iterations: int, expected: str, raw_dir: Path, bundle_root: Path) -> dict[str, Any]:
    audit_path = raw_dir / "windows_native_audit.log"
    result = run_target(binary, iterations, audit_path)
    stdout_log = raw_dir / "windows_native_stdout.log"
    stderr_log = raw_dir / "windows_native_stderr.log"
    write_text(stdout_log, result["stdout"])
    write_text(stderr_log, result["stderr"])
    ensure_text_file(audit_path)
    ok = result["exit_code"] == 0 and result["observed"] == expected
    return make_check(
        check_id="windows_native_execution",
        alias="Windows-native",
        category="platform",
        tool="windows-runner",
        command_summary="<protected-pe> <iterations> on windows-latest",
        ok=ok,
        expected=expected,
        observed=result["observed"],
        audit_text=result["audit"],
        claim_scope="The protected PE executed natively on the Windows GitHub runner and matched the deterministic oracle output.",
        non_claims=[
            "This check is platform correctness evidence, not debugger, Frida, or emulator detector coverage.",
            "No detector event is claimed unless it appears in audit_events.",
        ],
        limitations=["One GitHub-hosted Windows runner image and one protected PE sample."],
        artifacts=[
            evidence_artifact(stdout_log, "stdout", bundle_root),
            evidence_artifact(stderr_log, "stderr", bundle_root),
            evidence_artifact(audit_path, "audit-log", bundle_root),
        ],
    )


def run_cmd_under_windows_debugger(command_line: str, audit_path: Path, timeout_sec: int) -> dict[str, Any]:
    if not sys.platform.startswith("win"):
        raise RuntimeError("Windows Debug API is available only on Windows")

    from ctypes import wintypes

    DWORD = wintypes.DWORD
    WORD = wintypes.WORD
    BOOL = wintypes.BOOL
    HANDLE = wintypes.HANDLE
    LPCWSTR = wintypes.LPCWSTR
    LPWSTR = wintypes.LPWSTR
    LPVOID = wintypes.LPVOID

    class STARTUPINFOW(ctypes.Structure):
        _fields_ = [
            ("cb", DWORD),
            ("lpReserved", LPWSTR),
            ("lpDesktop", LPWSTR),
            ("lpTitle", LPWSTR),
            ("dwX", DWORD),
            ("dwY", DWORD),
            ("dwXSize", DWORD),
            ("dwYSize", DWORD),
            ("dwXCountChars", DWORD),
            ("dwYCountChars", DWORD),
            ("dwFillAttribute", DWORD),
            ("dwFlags", DWORD),
            ("wShowWindow", WORD),
            ("cbReserved2", WORD),
            ("lpReserved2", ctypes.POINTER(wintypes.BYTE)),
            ("hStdInput", HANDLE),
            ("hStdOutput", HANDLE),
            ("hStdError", HANDLE),
        ]

    class PROCESS_INFORMATION(ctypes.Structure):
        _fields_ = [
            ("hProcess", HANDLE),
            ("hThread", HANDLE),
            ("dwProcessId", DWORD),
            ("dwThreadId", DWORD),
        ]

    class DEBUG_EVENT(ctypes.Structure):
        _fields_ = [
            ("dwDebugEventCode", DWORD),
            ("dwProcessId", DWORD),
            ("dwThreadId", DWORD),
            ("_alignment", DWORD),
            ("u", wintypes.BYTE * 176),
        ]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateProcessW.argtypes = [
        LPCWSTR,
        LPWSTR,
        LPVOID,
        LPVOID,
        BOOL,
        DWORD,
        LPVOID,
        LPCWSTR,
        ctypes.POINTER(STARTUPINFOW),
        ctypes.POINTER(PROCESS_INFORMATION),
    ]
    kernel32.CreateProcessW.restype = BOOL
    kernel32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), DWORD]
    kernel32.WaitForDebugEvent.restype = BOOL
    kernel32.ContinueDebugEvent.argtypes = [DWORD, DWORD, DWORD]
    kernel32.ContinueDebugEvent.restype = BOOL
    kernel32.GetExitCodeProcess.argtypes = [HANDLE, ctypes.POINTER(DWORD)]
    kernel32.GetExitCodeProcess.restype = BOOL
    kernel32.CloseHandle.argtypes = [HANDLE]
    kernel32.CloseHandle.restype = BOOL

    DEBUG_PROCESS = 0x00000001
    CREATE_NO_WINDOW = 0x08000000
    DBG_CONTINUE = 0x00010002
    EXIT_PROCESS_DEBUG_EVENT = 5
    STILL_ACTIVE = 259

    startup = STARTUPINFOW()
    startup.cb = ctypes.sizeof(STARTUPINFOW)
    proc_info = PROCESS_INFORMATION()
    cmd_buffer = ctypes.create_unicode_buffer(command_line)

    old_audit = os.environ.get("VMP_AUDIT_PATH")
    os.environ["VMP_AUDIT_PATH"] = str(audit_path)
    try:
        ok = kernel32.CreateProcessW(
            None,
            cmd_buffer,
            None,
            None,
            False,
            DEBUG_PROCESS | CREATE_NO_WINDOW,
            None,
            None,
            ctypes.byref(startup),
            ctypes.byref(proc_info),
        )
    finally:
        if old_audit is None:
            os.environ.pop("VMP_AUDIT_PATH", None)
        else:
            os.environ["VMP_AUDIT_PATH"] = old_audit

    if not ok:
        raise OSError(ctypes.get_last_error(), "CreateProcessW(DEBUG_PROCESS) failed")

    debug_events = 0
    root_exited = False
    deadline = time.monotonic() + timeout_sec
    try:
        while time.monotonic() < deadline:
            event = DEBUG_EVENT()
            if not kernel32.WaitForDebugEvent(ctypes.byref(event), 1000):
                exit_code_probe = DWORD(STILL_ACTIVE)
                kernel32.GetExitCodeProcess(proc_info.hProcess, ctypes.byref(exit_code_probe))
                if exit_code_probe.value != STILL_ACTIVE:
                    root_exited = True
                    break
                continue
            debug_events += 1
            kernel32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE)
            if event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT and event.dwProcessId == proc_info.dwProcessId:
                root_exited = True
                break
        if not root_exited:
            raise TimeoutError("debug loop timed out before root process exit")
        exit_code = DWORD(STILL_ACTIVE)
        kernel32.GetExitCodeProcess(proc_info.hProcess, ctypes.byref(exit_code))
        return {"exit_code": int(exit_code.value), "debug_events": debug_events}
    finally:
        kernel32.CloseHandle(proc_info.hThread)
        kernel32.CloseHandle(proc_info.hProcess)


def collect_debugger_check(binary: Path, iterations: int, expected: str, raw_dir: Path, bundle_root: Path) -> dict[str, Any]:
    audit_path = raw_dir / "windows_debugapi_audit.log"
    stdout_log = raw_dir / "windows_debugapi_stdout.log"
    stderr_log = raw_dir / "windows_debugapi_stderr.log"
    quoted_binary = subprocess.list2cmdline([str(binary), str(iterations)])
    cmd_line = f'cmd.exe /d /s /c "{quoted_binary} > {subprocess.list2cmdline([str(stdout_log)])} 2> {subprocess.list2cmdline([str(stderr_log)])}"'
    result = run_cmd_under_windows_debugger(cmd_line, audit_path, timeout_sec=90)
    stdout_text = stdout_log.read_text(encoding="utf-8", errors="ignore") if stdout_log.exists() else ""
    stderr_text = stderr_log.read_text(encoding="utf-8", errors="ignore") if stderr_log.exists() else ""
    audit_text = audit_path.read_text(encoding="utf-8", errors="ignore") if audit_path.exists() else ""
    observed = stdout_text.strip().replace("\r\n", "\n").replace("\r", "")
    ok = result["exit_code"] == 0 and result["debug_events"] > 0 and observed == expected
    meta_log = raw_dir / "windows_debugapi_meta.json"
    write_text(meta_log, json.dumps(result, indent=2, sort_keys=True) + "\n")
    ensure_text_file(stdout_log)
    ensure_text_file(stderr_log)
    ensure_text_file(audit_path)
    return make_check(
        check_id="windows_debugapi_launch",
        alias="Windows-DebugAPI",
        category="debugger",
        tool="kernel32-debug-api",
        command_summary="CreateProcessW(DEBUG_PROCESS) around <protected-pe> <iterations>",
        ok=ok,
        expected=expected,
        observed=observed,
        audit_text=audit_text,
        claim_scope="The protected PE completed with correct output while launched under the Windows Debug API on the CI runner.",
        non_claims=[
            "A successful Debug API launch is not reported as debugger detector coverage unless audit_events includes debugger_detected.",
            "This does not cover hardware breakpoint register manipulation or external GUI debuggers.",
        ],
        limitations=["GitHub-hosted Windows runner only; Debug API launch, not manual WinDbg/cdb interaction."],
        artifacts=[
            evidence_artifact(stdout_log, "stdout", bundle_root),
            evidence_artifact(stderr_log, "stderr", bundle_root),
            evidence_artifact(audit_path, "audit-log", bundle_root),
            evidence_artifact(meta_log, "debugger-metadata", bundle_root),
        ],
    )


def collect_frida_check(binary: Path, iterations: int, expected: str, raw_dir: Path, bundle_root: Path) -> dict[str, Any]:
    try:
        import frida  # type: ignore
    except Exception as exc:
        raise RuntimeError(f"frida import failed: {redact(exc)}") from exc

    audit_path = raw_dir / "windows_frida_audit.log"
    stdout_log = raw_dir / "windows_frida_stdout.log"
    stderr_log = raw_dir / "windows_frida_stderr.log"
    env = os.environ.copy()
    env["VMP_AUDIT_PATH"] = str(audit_path)
    proc = subprocess.Popen(
        [str(binary), str(iterations)],
        cwd=str(binary.parent),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    attached = False
    script_loaded = False
    try:
        device = frida.get_local_device()
        deadline = time.monotonic() + 10.0
        session = None
        last_error: Exception | None = None
        while time.monotonic() < deadline and proc.poll() is None:
            try:
                session = device.attach(proc.pid)
                attached = True
                break
            except Exception as exc:  # pragma: no cover - external tool dependent
                last_error = exc
                time.sleep(0.05)
        if session is None:
            raise RuntimeError(f"frida attach failed before process exit: {redact(last_error)}")
        try:
            script = session.create_script("send({probe: 'attached'});")
            script.load()
            script_loaded = True
        finally:
            try:
                session.detach()
            except Exception:
                pass
        stdout_text, stderr_text = proc.communicate(timeout=60)
    finally:
        if proc.poll() is None:
            proc.kill()
            stdout_text, stderr_text = proc.communicate(timeout=10)

    write_text(stdout_log, stdout_text)
    write_text(stderr_log, stderr_text)
    ensure_text_file(audit_path)
    audit_text = audit_path.read_text(encoding="utf-8", errors="ignore") if audit_path.exists() else ""
    observed = stdout_text.strip().replace("\r\n", "\n").replace("\r", "")
    ok = attached and script_loaded and proc.returncode == 0 and observed == expected
    meta_log = raw_dir / "windows_frida_meta.json"
    write_text(meta_log, json.dumps({"attached": attached, "script_loaded": script_loaded, "exit_code": proc.returncode}, indent=2, sort_keys=True) + "\n")
    return make_check(
        check_id="windows_frida_attach",
        alias="Windows-Frida",
        category="frida",
        tool="frida-python",
        command_summary="frida.get_local_device().attach(<protected-pe-pid>) with a no-op script",
        ok=ok,
        expected=expected,
        observed=observed,
        audit_text=audit_text,
        claim_scope="Frida attached to the protected PE process on the Windows CI runner while the output remained correct.",
        non_claims=[
            "No Frida detector event is claimed unless audit_events includes frida_injection_detected.",
            "The script is a no-op attach probe, not a full tampering campaign.",
        ],
        limitations=["Depends on Frida availability and timing on GitHub-hosted Windows runners."],
        artifacts=[
            evidence_artifact(stdout_log, "stdout", bundle_root),
            evidence_artifact(stderr_log, "stderr", bundle_root),
            evidence_artifact(audit_path, "audit-log", bundle_root),
            evidence_artifact(meta_log, "frida-metadata", bundle_root),
        ],
    )


def command_version(cmd: list[str]) -> str | None:
    try:
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=10)
    except Exception:
        return None
    text = (proc.stdout or proc.stderr or "").strip().splitlines()
    return text[0][:160] if text else None


def runner_metadata(include_frida: bool) -> dict[str, Any]:
    versions = {
        "collector": "tests/live_tool_campaign/run_windows_ci_live.py",
        "python": platform.python_version(),
        "windows-debug-api": "kernel32 CreateProcessW(DEBUG_PROCESS)",
    }
    if include_frida:
        frida_version = command_version([sys.executable, "-m", "frida_tools.repl", "--version"])
        if frida_version:
            versions["frida-tools"] = frida_version
    return {
        "platform": sys.platform,
        "arch": platform.machine() or "unknown-arch",
        "kernel_or_build": platform.platform(),
        "ptrace_or_debug_policy": "Windows same-user Debug API on GitHub-hosted runner",
        "tool_versions": versions,
        "notes": "Generated in GitHub Actions Windows CI; detector events are recorded only when present in the audit log.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect Windows CI live evidence JSON")
    parser.add_argument("--report-dir", type=Path, default=DEFAULT_REPORT_DIR)
    parser.add_argument("--integration-date", help="YYYYMMDD integration artifact date; defaults to newest Windows artifact")
    parser.add_argument("--binary", type=Path, help="protected PE to run; defaults to target_c.protected.exe from integration artifacts")
    parser.add_argument("--iterations", type=int, default=100_000)
    parser.add_argument("--output", type=Path, default=DEFAULT_REPORT_DIR / f"external_live_matrix_windows_{stamp()}.json")
    parser.add_argument("--frida", action="store_true", help="attempt optional Frida attach evidence if frida is installed")
    parser.add_argument("--keep-failed", action="store_true", help="include failed optional checks instead of moving them to skipped_checks")
    args = parser.parse_args()

    report_dir = args.report_dir if args.report_dir.is_absolute() else (ROOT / args.report_dir)
    binary = args.binary if args.binary else discover_default_binary(report_dir, args.integration_date)
    if not binary.is_absolute():
        binary = ROOT / binary
    binary = binary.resolve()
    if not binary.exists():
        raise SystemExit(f"missing protected PE: {rel(binary)}")
    if args.iterations <= 0:
        raise SystemExit("--iterations must be positive")

    expected = target_expected(binary, args.iterations)
    raw_dir = report_dir / f"windows_live_{stamp()}"
    bundle_root = report_dir.resolve()
    checks: list[dict[str, Any]] = []
    skipped: list[dict[str, str]] = []

    native = collect_native_check(binary, args.iterations, expected, raw_dir, bundle_root)
    if not native["ok"]:
        raise SystemExit("Windows native execution check failed; not writing claim-bearing evidence report")
    checks.append(native)

    optional_collectors = [("windows_debugapi_launch", collect_debugger_check)]
    if args.frida:
        optional_collectors.append(("windows_frida_attach", collect_frida_check))
    for check_id, collector in optional_collectors:
        try:
            check = collector(binary, args.iterations, expected, raw_dir, bundle_root)
            if check.get("ok") is True or args.keep_failed:
                checks.append(check)
            else:
                skipped.append({"id": check_id, "reason": "check ran but did not meet positive-evidence criteria"})
        except Exception as exc:
            skipped.append({"id": check_id, "reason": redact(exc)})

    report: dict[str, Any] = {
        "schema": "pavmp.external_live_matrix.v1",
        "generated_utc": utc_now(),
        "fabricated_data": False,
        "paper_claims_mutated": False,
        "runner": runner_metadata(args.frida),
        "target": {
            "binary": bundle_rel(binary, bundle_root),
            "sha256": sha256_file(binary),
            "iterations": args.iterations,
            "expected_result": expected,
        },
        "checks": checks,
        "ok": all(bool(check.get("ok")) for check in checks),
        "limitations": [
            "Supplemental Windows CI evidence; it does not mutate manuscript claims unless explicitly incorporated.",
            "Detector coverage is claimed only for audit events present in each check's audit_events field.",
        ],
    }
    if skipped:
        report["skipped_checks"] = skipped

    output = args.output if args.output.is_absolute() else (ROOT / args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"windows external live matrix report: {rel(output)}")
    print(json.dumps({"ok": report["ok"], "checks": [c["id"] for c in checks], "skipped": skipped}, sort_keys=True))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
