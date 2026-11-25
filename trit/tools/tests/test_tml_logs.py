"""Cross-check emulator log recovery with the independent Python verifier."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from tml_analyze import chain_hash, load_jsonl, summarize, verify_chain
from tml_report import build_report


class LogRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="ternary-log-")
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "memory.jsonl"

    def emulator(self, expected_code=0):
        result = subprocess.run(
            [str(ROOT / "emu" / "tern-cpu-emu"), "--tml-log", str(self.path)],
            input="q\n", text=True, capture_output=True, timeout=10,
        )
        self.assertEqual(result.returncode, expected_code, result.stdout + result.stderr)
        return result

    def write_entries(self, entries):
        self.path.write_text(
            "".join(json.dumps(entry, ensure_ascii=False, separators=(",", ":")) + "\n"
                    for entry in entries),
            encoding="utf-8",
        )

    def test_process_restarts_continue_one_chain(self):
        self.emulator()
        first = load_jsonl(self.path)
        self.assertEqual(len(first), 3)
        self.emulator()
        entries = load_jsonl(self.path)
        self.assertEqual([entry["seq"] for entry in entries], list(range(6)))
        self.assertEqual(entries[:3], first)
        self.assertEqual(entries[3]["prev_hash"], first[-1]["hash"])
        self.assertEqual(verify_chain(entries), (True, []))
        self.assertTrue(summarize([self.path])["chain_ok"])
        self.assertIn("| Hash chain | valid |", build_report(entries))

    def test_existing_canonical_format_and_escaped_strings(self):
        # Match the original emitter's format using an independently computed digest.
        entry = {
            "seq": 0, "ts": "2026-01-01T00:00:00Z",
            "event": 'quoted "event"\\\n', "detail": "журнал",
            "state": 1, "verdict": -1, "risk": 7,
            "action": -(2**63), "args": [-1, 0, 42], "prev_hash": "0" * 64,
        }
        entry["hash"] = chain_hash(entry)
        self.write_entries([entry])
        self.emulator()
        entries = load_jsonl(self.path)
        self.assertEqual(entries[0], entry)
        self.assertEqual(entries[1]["seq"], 1)
        self.assertEqual(entries[1]["prev_hash"], entry["hash"])
        self.assertEqual(verify_chain(entries), (True, []))

    def test_modified_record_is_rejected_without_appending(self):
        self.emulator()
        entries = load_jsonl(self.path)
        entries[0]["risk"] += 1
        self.write_entries(entries)
        before = self.path.read_bytes()
        self.assertFalse(verify_chain(entries)[0])
        self.assertIn("verify", self.emulator(2).stderr)
        self.assertEqual(self.path.read_bytes(), before)

    def test_restarted_sequence_is_not_a_new_chain(self):
        self.emulator()
        entries = load_jsonl(self.path)
        entries.append(entries[0].copy())
        self.write_entries(entries)
        before = self.path.read_bytes()
        ok, errors = verify_chain(entries)
        self.assertFalse(ok)
        self.assertTrue(any("expected 3" in error for error in errors))
        self.assertIn("| Hash chain | BROKEN |", build_report(entries))
        self.emulator(2)
        self.assertEqual(self.path.read_bytes(), before)

    def test_sequence_gap_with_valid_hash_is_rejected(self):
        self.emulator()
        entries = load_jsonl(self.path)
        entries[-1]["seq"] += 1
        entries[-1]["hash"] = chain_hash(entries[-1])
        self.write_entries(entries)
        ok, errors = verify_chain(entries)
        self.assertFalse(ok)
        self.assertEqual(errors, ["seq 3: expected 2"])
        self.emulator(2)

    def test_incomplete_and_malformed_records_fail_in_all_readers(self):
        self.emulator()
        original = self.path.read_bytes()
        for contents in (original[:-1], original + b'{"seq":', original + b"{}oops\n",
                         original + b"\n", original + b"[]\n"):
            with self.subTest(contents=contents[-20:]):
                self.path.write_bytes(contents)
                with self.assertRaises(ValueError):
                    load_jsonl(self.path)
                self.emulator(2)
                self.assertEqual(self.path.read_bytes(), contents)
                for tool in ("tml_analyze.py", "tml_report.py"):
                    result = subprocess.run(
                        [sys.executable, str(ROOT / "tools" / tool), str(self.path)],
                        text=True, capture_output=True, timeout=10,
                    )
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
