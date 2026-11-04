#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check that required security KUnit suites ran without failures."""

import json
import sys
from pathlib import Path


def load_suites(path: Path) -> list[str]:
    suites = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            suites.append(line)
    return suites


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} KUNIT_JSON SUITES_FILE", file=sys.stderr)
        return 2

    data = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    suites = load_suites(Path(sys.argv[2]))
    groups = {group.get("name"): group for group in data.get("sub_groups", [])}

    errors = []
    total = 0
    for name in suites:
        group = groups.get(name)
        if group is None:
            errors.append(f"missing suite: {name}")
            continue
        misc = group.get("misc") or {}
        tests = misc.get("tests", 0)
        if tests <= 0:
            errors.append(f"{name}: no tests ran")
        for key in ("failed", "crashed", "errors"):
            if misc.get(key, 0):
                errors.append(f"{name}: {key}={misc[key]}")
        total += tests

    if errors:
        for message in errors:
            print(f"FAIL: {message}", file=sys.stderr)
        return 1

    print(f"security KUnit OK: {len(suites)} suites, {total} tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
