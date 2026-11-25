#!/usr/bin/env python3
"""Summarize TML Always Memory logs and emulator trace files."""

import argparse
import hashlib
import json
import sys
from collections import Counter


def json_escape(text):
    out = []
    for ch in text:
        if ch in ('"', "\\"):
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 0x20:
            out.append(" ")
        else:
            out.append(ch)
    return "".join(out)


def chain_hash(entry):
    canonical = "{}|{}|{}|{}|{}|{}|{}|{},{},{}|{}".format(
        entry.get("seq", 0),
        json_escape(str(entry.get("event", ""))),
        json_escape(str(entry.get("detail", ""))),
        entry.get("verdict", 0),
        entry.get("risk", 0),
        entry.get("state", 0),
        entry.get("action", 0),
        (entry.get("args") or [0, 0, 0])[0],
        (entry.get("args") or [0, 0, 0])[1],
        (entry.get("args") or [0, 0, 0])[2],
        entry.get("prev_hash", ""),
    )
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def load_jsonl(path):
    entries = []
    with open(path, "r", encoding="utf-8") as handle:
        for lineno, line in enumerate(handle, 1):
            if not line.endswith("\n"):
                raise ValueError("{}:{}: incomplete JSONL record".format(path, lineno))
            try:
                entry = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError("{}:{}: invalid JSON: {}".format(path, lineno, exc)) from exc
            if not isinstance(entry, dict):
                raise ValueError("{}:{}: expected a JSON object".format(path, lineno))
            entries.append(entry)
    return entries


def verify_chain(entries):
    expected_prev = "0" * 64
    expected_seq = 0
    errors = []
    for entry in entries:
        if "hash" not in entry and "prev_hash" not in entry:
            continue
        if entry.get("seq") != expected_seq:
            errors.append("seq {}: expected {}".format(entry.get("seq"), expected_seq))
        prev = entry.get("prev_hash")
        if prev != expected_prev:
            errors.append("seq {}: prev_hash mismatch".format(entry.get("seq")))
        digest = chain_hash(entry)
        if entry.get("hash") != digest:
            errors.append("seq {}: hash mismatch".format(entry.get("seq")))
        expected_prev = entry.get("hash", "")
        expected_seq += 1
    return len(errors) == 0, errors


def summarize(paths):
    all_entries = []
    by_file = {}
    for path in paths:
        entries = load_jsonl(path)
        by_file[path] = entries
        all_entries.extend(entries)

    events = Counter(entry.get("event", "?") for entry in all_entries)
    opcodes = Counter(
        entry.get("opcode") for entry in all_entries
        if entry.get("event") == "insn"
    )
    verdicts = Counter(
        entry.get("verdict") for entry in all_entries
        if "verdict" in entry and entry.get("event") != "insn"
    )
    states = Counter(
        entry.get("state") for entry in all_entries
        if "state" in entry and entry.get("event") != "insn"
    )
    risks = [entry.get("risk", 0) for entry in all_entries
             if entry.get("event") not in ("insn", "mem", "taint", "constraint")]

    chain_ok = True
    chain_errors = []
    for path, entries in by_file.items():
        ok, errors = verify_chain(entries)
        chain_ok = chain_ok and ok
        chain_errors.extend("{}: {}".format(path, err) for err in errors)

    return {
        "files": {path: len(entries) for path, entries in by_file.items()},
        "events": dict(events),
        "instructions": events.get("insn", 0),
        "opcodes": {str(k): v for k, v in opcodes.items()},
        "verdicts": {str(k): v for k, v in verdicts.items()},
        "states": {str(k): v for k, v in states.items()},
        "risks": {
            "count": len(risks),
            "min": min(risks) if risks else None,
            "max": max(risks) if risks else None,
            "avg": (sum(risks) / len(risks)) if risks else None,
        },
        "chain_ok": chain_ok,
        "chain_errors": chain_errors,
    }


def print_summary(summary):
    print("files:")
    for path, count in summary["files"].items():
        print("  {}: {} entries".format(path, count))
    print("events: {}".format(summary["events"]))
    print("instructions: {}".format(summary["instructions"]))
    if summary["opcodes"]:
        print("top opcodes:")
        for opcode, count in sorted(
                summary["opcodes"].items(), key=lambda item: -item[1])[:10]:
            print("  {}: {}".format(opcode, count))
    print("verdicts: {}".format(summary["verdicts"]))
    print("states: {}".format(summary["states"]))
    risks = summary["risks"]
    print("risk: count={} min={} max={} avg={}".format(
        risks["count"], risks["min"], risks["max"],
        None if risks["avg"] is None else round(risks["avg"], 2)))
    print("hash chain: {}".format("ok" if summary["chain_ok"] else "BROKEN"))
    for error in summary["chain_errors"]:
        print("  {}".format(error))


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Summarize TML logs, traces and symbolic constraints.")
    parser.add_argument("paths", nargs="+", help="JSONL files to analyze")
    parser.add_argument("--json", action="store_true",
                        help="emit a JSON summary instead of text")
    args = parser.parse_args(argv)

    try:
        summary = summarize(args.paths)
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print_summary(summary)
    return 0 if summary["chain_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
