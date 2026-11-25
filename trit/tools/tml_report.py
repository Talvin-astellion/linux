#!/usr/bin/env python3
"""Generate a Markdown report from a TML Always Memory JSONL log."""

import argparse
import sys
from collections import Counter

try:
    from tml_analyze import load_jsonl, verify_chain
except ImportError:
    from tools.tml_analyze import load_jsonl, verify_chain


STATE_NAMES = {
    0: "BOOT",
    1: "ACTIVE",
    2: "SACRED_PAUSE",
    3: "FREEZE",
    4: "RECOVERY",
}

VERDICT_NAMES = {
    -1: "REFUSE",
    0: "SACRED_ZERO",
    1: "PROCEED",
}


def build_report(entries):
    memory = [entry for entry in entries if "hash" in entry]
    freezes = [entry for entry in memory if entry.get("event") == "freeze"]
    verdicts = Counter(VERDICT_NAMES.get(entry.get("verdict"), "?")
                        for entry in memory)
    states = Counter(STATE_NAMES.get(entry.get("state"), "?")
                      for entry in memory)
    risks = [entry.get("risk", 0) for entry in memory]
    ok, errors = verify_chain(memory)

    lines = []
    lines.append("# TML Always Memory Report")
    lines.append("")
    lines.append("| Metric | Value |")
    lines.append("| --- | --- |")
    lines.append("| Entries | {} |".format(len(memory)))
    lines.append("| Freeze events | {} |".format(len(freezes)))
    lines.append("| Hash chain | {} |".format("valid" if ok else "BROKEN"))
    if risks:
        lines.append("| Risk min / max / avg | {} / {} / {} |".format(
            min(risks), max(risks),
            round(sum(risks) / len(risks), 2)))
    lines.append("| Final head | `{}` |".format(
        memory[-1].get("hash", "-") if memory else "-"))
    lines.append("")
    lines.append("## Verdicts")
    lines.append("")
    for name, count in sorted(verdicts.items()):
        lines.append("- {}: {}".format(name, count))
    lines.append("")
    lines.append("## States")
    lines.append("")
    for name, count in sorted(states.items()):
        lines.append("- {}: {}".format(name, count))
    lines.append("")
    lines.append("## Freeze Events")
    lines.append("")
    if freezes:
        lines.append("| Seq | Detail | Risk | Action |")
        lines.append("| --- | --- | --- | --- |")
        for entry in freezes:
            lines.append("| {} | {} | {} | {} |".format(
                entry.get("seq"), entry.get("detail"), entry.get("risk"),
                entry.get("action")))
    else:
        lines.append("No freeze events recorded.")
    lines.append("")
    lines.append("## Timeline")
    lines.append("")
    lines.append("| Seq | Event | State | Verdict | Risk | Detail |")
    lines.append("| --- | --- | --- | --- | --- | --- |")
    for entry in memory:
        lines.append("| {} | {} | {} | {} | {} | {} |".format(
            entry.get("seq"),
            entry.get("event"),
            STATE_NAMES.get(entry.get("state"), entry.get("state")),
            VERDICT_NAMES.get(entry.get("verdict"), entry.get("verdict")),
            entry.get("risk"),
            entry.get("detail")))
    lines.append("")

    if errors:
        lines.append("## Chain Errors")
        lines.append("")
        for error in errors:
            lines.append("- {}".format(error))
        lines.append("")

    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate a Markdown TML Always Memory report.")
    parser.add_argument("log", help="TML JSONL log")
    parser.add_argument("-o", "--out", help="write report to this file")
    args = parser.parse_args(argv)

    try:
        entries = load_jsonl(args.log)
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    report = build_report(entries)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(report)
    else:
        print(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
