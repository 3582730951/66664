#!/usr/bin/env python3
"""Validate optional external live-tool evidence reports.

The main paper does not depend on these reports.  This validator is a guardrail
for reviewers or authors who rerun real Frida/ptrace/DBI/emulator experiments on
an external runner and want to attach evidence without silently expanding the
paper's scoped claims.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "paper" / "schemas" / "external_live_matrix.schema.json"
EXPECTED_SCHEMA = "pavmp.external_live_matrix.v1"
PATH_LEAK_RE = re.compile(r"(/workspace/|/root/|File \"/)")
ABSOLUTE_PATH_RE = re.compile(r"(^|\s)/(?:home|Users|private|tmp|var|workspace|root|mnt|opt)/")
UTC_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
ID_RE = re.compile(r"^[a-z0-9_.-]+$")
ALLOWED_CATEGORIES = {"frida", "ptrace", "dbi", "emulator", "debugger", "mapping_integrity", "platform"}
ALLOWED_POLICIES = {"preserve_expected_output", "configured_reaction_path", "audit_only"}


class ValidationError(Exception):
    pass


def fail(msg: str) -> None:
    raise ValidationError(msg)


def require(condition: bool, msg: str) -> None:
    if not condition:
        fail(msg)


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        fail(f"{path}: invalid JSON at line {exc.lineno}: {exc.msg}")


def check_no_path_leak(value: Any, ctx: str, allow_absolute_paths: bool) -> None:
    if isinstance(value, str):
        if PATH_LEAK_RE.search(value):
            fail(f"{ctx}: contains container-local path metadata")
        if not allow_absolute_paths and ABSOLUTE_PATH_RE.search(value):
            fail(f"{ctx}: contains an absolute local path; use relative paths or redact")
    elif isinstance(value, list):
        for idx, item in enumerate(value):
            check_no_path_leak(item, f"{ctx}[{idx}]", allow_absolute_paths)
    elif isinstance(value, dict):
        for key, item in value.items():
            check_no_path_leak(item, f"{ctx}.{key}", allow_absolute_paths)


def require_string(obj: dict[str, Any], key: str, ctx: str, min_len: int = 1) -> str:
    value = obj.get(key)
    require(isinstance(value, str) and len(value) >= min_len, f"{ctx}.{key}: expected string length >= {min_len}")
    return value


def require_bool(obj: dict[str, Any], key: str, ctx: str) -> bool:
    value = obj.get(key)
    require(isinstance(value, bool), f"{ctx}.{key}: expected boolean")
    return value


def validate_timestamp(value: str, ctx: str) -> None:
    require(bool(UTC_RE.match(value)), f"{ctx}: expected UTC timestamp like 2026-04-24T12:34:56Z")
    try:
        dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as exc:
        fail(f"{ctx}: invalid timestamp: {exc}")


def validate_runner(runner: Any) -> None:
    require(isinstance(runner, dict), "runner: expected object")
    for key in ["platform", "arch", "kernel_or_build", "ptrace_or_debug_policy"]:
        require_string(runner, key, "runner", 2)
    tools = runner.get("tool_versions")
    require(isinstance(tools, dict) and bool(tools), "runner.tool_versions: expected non-empty object")
    for name, version in tools.items():
        require(isinstance(name, str) and name, "runner.tool_versions: tool names must be non-empty strings")
        require(isinstance(version, str) and version, f"runner.tool_versions.{name}: expected non-empty string")


def validate_target(target: Any) -> None:
    require(isinstance(target, dict), "target: expected object")
    binary = require_string(target, "binary", "target", 1)
    require(not Path(binary).is_absolute(), "target.binary: use a relative or redacted path")
    sha = require_string(target, "sha256", "target", 64)
    require(bool(SHA256_RE.match(sha)), "target.sha256: expected 64 hex characters")
    iterations = target.get("iterations")
    require(isinstance(iterations, int) and iterations > 0, "target.iterations: expected positive integer")
    require("expected_result" in target, "target.expected_result: required")
    require(isinstance(target["expected_result"], (str, int)), "target.expected_result: expected string or integer")


def validate_evidence_artifacts(check: dict[str, Any], ctx: str) -> None:
    artifacts = check.get("evidence_artifacts", [])
    require(isinstance(artifacts, list), f"{ctx}.evidence_artifacts: expected list")
    for idx, artifact in enumerate(artifacts):
        actx = f"{ctx}.evidence_artifacts[{idx}]"
        require(isinstance(artifact, dict), f"{actx}: expected object")
        path = require_string(artifact, "path", actx, 1)
        require(not Path(path).is_absolute(), f"{actx}.path: use a relative or redacted path")
        require_string(artifact, "kind", actx, 2)
        require("sha256" in artifact, f"{actx}.sha256: required for traceable evidence artifacts")
        require(isinstance(artifact["sha256"], str) and SHA256_RE.match(artifact["sha256"]), f"{actx}.sha256: expected 64 hex characters")


def validate_check(check: Any, idx: int, target_expected: Any) -> bool:
    ctx = f"checks[{idx}]"
    require(isinstance(check, dict), f"{ctx}: expected object")
    check_id = require_string(check, "id", ctx, 1)
    require(bool(ID_RE.match(check_id)), f"{ctx}.id: expected lowercase id with [a-z0-9_.-]")
    require_string(check, "display_alias", ctx, 2)
    category = require_string(check, "category", ctx, 2)
    require(category in ALLOWED_CATEGORIES, f"{ctx}.category: unsupported category {category!r}")
    require_string(check, "tool", ctx, 2)
    require_string(check, "command_summary", ctx, 4)
    ok = require_bool(check, "ok", ctx)
    oracle_triggered = require_bool(check, "oracle_triggered", ctx)
    policy = require_string(check, "output_policy", ctx, 4)
    require(policy in ALLOWED_POLICIES, f"{ctx}.output_policy: unsupported policy {policy!r}")
    audit_events = check.get("audit_events")
    require(isinstance(audit_events, list), f"{ctx}.audit_events: expected list")
    if oracle_triggered:
        require(len(audit_events) > 0, f"{ctx}.audit_events: oracle-triggered checks need at least one event")
    for event in audit_events:
        require(isinstance(event, str) and len(event) >= 2, f"{ctx}.audit_events: events must be strings")
        require(event in check.get("audit_excerpt", ""), f"{ctx}.audit_excerpt: must contain event {event!r}")
    require(isinstance(check.get("audit_excerpt"), str), f"{ctx}.audit_excerpt: expected string")
    require(len(check["audit_excerpt"]) <= 2000, f"{ctx}.audit_excerpt: keep excerpts <= 2000 chars")

    if policy == "preserve_expected_output":
        require(check.get("expected_result") == target_expected, f"{ctx}.expected_result: must match target.expected_result for preserve policy")
        require(check.get("observed_result") == target_expected, f"{ctx}.observed_result: must match target.expected_result for preserve policy")
        require(check.get("output_correct") is True, f"{ctx}.output_correct: must be true for preserve policy")
    elif policy == "configured_reaction_path":
        require(check.get("configured_reaction_observed") is True, f"{ctx}.configured_reaction_observed: must be true for reaction policy")
    else:
        require("output_correct" in check or "configured_reaction_observed" in check, f"{ctx}: audit_only checks still need an output/reaction observation field")

    require_string(check, "claim_scope", ctx, 12)
    non_claims = check.get("non_claims")
    require(isinstance(non_claims, list) and len(non_claims) > 0, f"{ctx}.non_claims: expected non-empty list")
    limitations = check.get("limitations")
    require(isinstance(limitations, list) and len(limitations) > 0, f"{ctx}.limitations: expected non-empty list")
    validate_evidence_artifacts(check, ctx)
    return ok


def validate_report(path: Path, allow_absolute_paths: bool) -> None:
    data = load_json(path)
    require(isinstance(data, dict), f"{path}: expected top-level object")
    check_no_path_leak(data, str(path), allow_absolute_paths)
    require(data.get("schema") == EXPECTED_SCHEMA, f"{path}: schema must be {EXPECTED_SCHEMA}")
    validate_timestamp(require_string(data, "generated_utc", str(path), 20), f"{path}.generated_utc")
    require(data.get("fabricated_data") is False, f"{path}.fabricated_data must be false")
    require(data.get("paper_claims_mutated") is False, f"{path}.paper_claims_mutated must be false")
    validate_runner(data.get("runner"))
    validate_target(data.get("target"))
    checks = data.get("checks")
    require(isinstance(checks, list) and len(checks) > 0, f"{path}.checks: expected non-empty list")
    check_ok = [validate_check(check, idx, data["target"]["expected_result"]) for idx, check in enumerate(checks)]
    require(data.get("ok") == all(check_ok), f"{path}.ok must equal all(check.ok)")
    limitations = data.get("limitations")
    require(isinstance(limitations, list) and len(limitations) > 0, f"{path}.limitations: expected non-empty list")


def discover_reports() -> list[Path]:
    return sorted((ROOT / "reports").glob("external_live_matrix_*.json"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate optional external live-tool evidence JSON")
    parser.add_argument("reports", nargs="*", type=Path, help="external_live_matrix_*.json files to validate")
    parser.add_argument("--require-report", action="store_true", help="fail if no report paths are supplied or discovered")
    parser.add_argument("--allow-absolute-paths", action="store_true", help="permit local absolute paths in non-submission scratch reports")
    args = parser.parse_args()

    if not SCHEMA_PATH.exists():
        print(f"FAIL: missing schema {SCHEMA_PATH.relative_to(ROOT)}", file=sys.stderr)
        return 1
    schema = load_json(SCHEMA_PATH)
    if not isinstance(schema, dict) or schema.get("title") != "PAVMP external live evidence matrix":
        print("FAIL: external live schema has unexpected title", file=sys.stderr)
        return 1

    reports = [p if p.is_absolute() else (ROOT / p) for p in args.reports]
    if not reports:
        reports = discover_reports()
    if not reports:
        msg = "no external live reports supplied or discovered; schema/template validation only"
        if args.require_report:
            print(f"FAIL: {msg}", file=sys.stderr)
            return 1
        print(msg)
        return 0

    try:
        for report in reports:
            if not report.exists():
                fail(f"{report}: report does not exist")
            validate_report(report, args.allow_absolute_paths)
    except ValidationError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"external live validation OK ({len(reports)} report(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
