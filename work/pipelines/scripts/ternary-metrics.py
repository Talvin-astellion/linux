#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Collect and compare ternary metrics for the pipeline."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import statistics
import sys
from pathlib import Path


def parse_gcov(gcov_file: Path) -> dict:
    lines_total = 0
    lines_executed = 0
    branches_total = 0
    branches_taken = 0

    for line in gcov_file.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.lstrip()
        if stripped.startswith("branch "):
            branches_total += 1
            if "never executed" not in line:
                match = re.search(r"taken\s+(\d+)", line)
                if match and int(match.group(1)) > 0:
                    branches_taken += 1
            continue

        match = re.match(r"^\s*([^:]+?):\s*(\d+):", line)
        if not match:
            continue
        count = match.group(1).strip()
        if count == "-":
            continue
        lines_total += 1
        if count in ("#####", "====="):
            continue
        try:
            if int(count) > 0:
                lines_executed += 1
        except ValueError:
            pass

    return {
        "lines": {
            "executed": lines_executed,
            "total": lines_total,
            "percent": (100.0 * lines_executed / lines_total) if lines_total else 0.0,
        },
        "branches": {
            "taken": branches_taken,
            "total": branches_total,
            "percent": (100.0 * branches_taken / branches_total) if branches_total else 100.0,
        },
    }


def find_gcov(directory: Path) -> Path:
    candidates = sorted(directory.rglob("ternary.c.gcov"))
    if not candidates:
        raise SystemExit(f"no ternary.c.gcov found under {directory}")
    return candidates[0]


def command_coverage(args: argparse.Namespace) -> int:
    result = parse_gcov(find_gcov(Path(args.gcov_dir)))
    Path(args.out).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0


def command_bench(args: argparse.Namespace) -> int:
    samples: dict[str, list[float]] = {}
    for line in Path(args.samples).read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) < 3 or parts[0] != "BENCH":
            continue
        try:
            value = float(parts[2])
        except ValueError:
            continue
        if not math.isfinite(value) or value <= 0:
            continue
        samples.setdefault(parts[1], []).append(value)

    if not samples:
        raise SystemExit(f"no benchmark samples in {args.samples}")

    result = {name: statistics.median(values) for name, values in samples.items()}
    Path(args.out).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0


def load_json(directory: Path, name: str) -> dict:
    path = directory / name
    if not path.exists():
        raise SystemExit(f"missing metrics file: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def command_compare(args: argparse.Namespace) -> int:
    baseline_dir = Path(args.baseline_dir)
    candidate_dir = Path(args.candidate_dir)

    baseline_tests = load_json(baseline_dir, "tests-baseline.json")
    candidate_tests = load_json(candidate_dir, "tests-candidate.json")
    baseline_bench = load_json(baseline_dir, "bench-baseline.json")
    candidate_bench = load_json(candidate_dir, "bench-candidate.json")
    baseline_cov = load_json(baseline_dir, "coverage-baseline.json")
    candidate_cov = load_json(candidate_dir, "coverage-candidate.json")
    sanitizer = load_json(candidate_dir, "tests-candidate-sanitize.json")

    speedup_min = float(os.environ.get("SPEEDUP_MIN", "0.05"))
    regression_max = float(os.environ.get("REGRESSION_MAX", "0.05"))
    coverage_slack = float(os.environ.get("COVERAGE_SLACK", "5.0"))

    failures: list[str] = []
    lines: list[str] = []

    lines.append("# Ternary metrics")
    lines.append("")
    lines.append("| Metric | Baseline | Candidate | Gate |")
    lines.append("|---|---:|---:|---|")

    lines.append(
        f"| Tests passed | {baseline_tests['checks'] - baseline_tests['failures']} "
        f"/ {baseline_tests['checks']} | "
        f"{candidate_tests['checks'] - candidate_tests['failures']} "
        f"/ {candidate_tests['checks']} | candidate failures = 0 |"
    )
    if candidate_tests["failures"] != 0:
        failures.append(f"candidate has {candidate_tests['failures']} failing tests")
    if candidate_tests["checks"] < baseline_tests["checks"]:
        failures.append("candidate ran fewer checks than baseline")

    lines.append(
        f"| Line coverage | {baseline_cov['lines']['percent']:.2f}% "
        f"({baseline_cov['lines']['executed']}/{baseline_cov['lines']['total']}) | "
        f"{candidate_cov['lines']['percent']:.2f}% "
        f"({candidate_cov['lines']['executed']}/{candidate_cov['lines']['total']}) | "
        f"executed >= baseline and percent >= baseline - {coverage_slack:.1f}pp |"
    )
    if candidate_cov["lines"]["executed"] < baseline_cov["lines"]["executed"]:
        failures.append("candidate covers fewer lines than baseline")
    if (
        candidate_cov["lines"]["percent"] + coverage_slack
        < baseline_cov["lines"]["percent"]
    ):
        failures.append("candidate line coverage regressed")

    lines.append(
        f"| Branch coverage | {baseline_cov['branches']['percent']:.2f}% "
        f"({baseline_cov['branches']['taken']}/{baseline_cov['branches']['total']}) | "
        f"{candidate_cov['branches']['percent']:.2f}% "
        f"({candidate_cov['branches']['taken']}/{candidate_cov['branches']['total']}) | "
        f"taken >= baseline and percent >= baseline - {coverage_slack:.1f}pp |"
    )
    if candidate_cov["branches"]["taken"] < baseline_cov["branches"]["taken"]:
        failures.append("candidate covers fewer branches than baseline")
    if (
        candidate_cov["branches"]["percent"] + coverage_slack
        < baseline_cov["branches"]["percent"]
    ):
        failures.append("candidate branch coverage regressed")

    for operation in ("ADD", "ADC", "MUL"):
        base_value = baseline_bench.get(operation, 0.0)
        cand_value = candidate_bench.get(operation, 0.0)
        ratio = (cand_value / base_value) if base_value else 0.0
        lines.append(
            f"| {operation} throughput | {base_value:,.0f} ops/s | "
            f"{cand_value:,.0f} ops/s ({ratio:.2f}x) | "
            f">= {1.0 + speedup_min:.2f}x |"
        )
        if base_value and cand_value < base_value * (1.0 + speedup_min):
            failures.append(
                f"{operation} throughput is below the required "
                f"{(1.0 + speedup_min):.2f}x speedup"
            )
        if not base_value:
            failures.append(f"missing baseline throughput for {operation}")

    for operation, base_value in sorted(baseline_bench.items()):
        cand_value = candidate_bench.get(operation, 0.0)
        if operation in ("ADD", "ADC", "MUL"):
            continue
        ratio = (cand_value / base_value) if base_value else 0.0
        lines.append(
            f"| {operation} throughput | {base_value:,.0f} ops/s | "
            f"{cand_value:,.0f} ops/s ({ratio:.2f}x) | "
            f">= {1.0 - regression_max:.2f}x |"
        )
        if base_value and cand_value < base_value * (1.0 - regression_max):
            failures.append(f"{operation} throughput regressed")

    lines.append(
        f"| Warnings | {baseline_tests['warnings']} | "
        f"{candidate_tests['warnings']} | candidate <= baseline |"
    )
    if candidate_tests["warnings"] > baseline_tests["warnings"]:
        failures.append("candidate has more compiler warnings than baseline")

    sanitizer_ok = sanitizer.get("failures", 1) == 0 and sanitizer.get("rc", 1) == 0
    lines.append(f"| ASan/UBSan | n/a | {'ok' if sanitizer_ok else 'failed'} | ok |")
    if not sanitizer_ok:
        failures.append("candidate sanitizer run failed")

    lines.append("")
    if failures:
        lines.append("## Verdict: FAIL")
        lines.append("")
        for failure in failures:
            lines.append(f"- {failure}")
    else:
        lines.append("## Verdict: PASS")

    report = "\n".join(lines) + "\n"
    Path(args.out).write_text(report, encoding="utf-8")
    print(report, end="")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    coverage = subparsers.add_parser("coverage")
    coverage.add_argument("--gcov-dir", required=True)
    coverage.add_argument("--out", required=True)
    coverage.set_defaults(func=command_coverage)

    bench = subparsers.add_parser("bench")
    bench.add_argument("--samples", required=True)
    bench.add_argument("--out", required=True)
    bench.set_defaults(func=command_bench)

    compare = subparsers.add_parser("compare")
    compare.add_argument("--baseline-dir", required=True)
    compare.add_argument("--candidate-dir", required=True)
    compare.add_argument("--out", required=True)
    compare.set_defaults(func=command_compare)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
