"""Boot image layout and raw loader regression tests."""

import struct
import subprocess
import sys
import tempfile
from pathlib import Path


with tempfile.TemporaryDirectory() as tmp:
    kernel, data, initrd = (Path(tmp) / name for name in ("kernel", "data", "initrd"))
    # Load the word immediately after this image and write it to the test finisher.
    kernel.write_bytes(struct.pack("<4I", 0x00000297, 0x0102A303, 0x001002B7, 0x0062A023))
    data.write_bytes(struct.pack("<I", 0x5555))
    with initrd.open("wb") as stream:
        stream.truncate(0xE00001)  # First byte crossing into the default DTB address.

    def run(*args, error=None):
        result = subprocess.run(
            [sys.argv[1], "--kernel", kernel, "--max-insn", "8", *args],
            capture_output=True, text=True, timeout=5,
        )
        if error is None:
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode == 2 and error in result.stderr, result.stderr

    run("--load", "0x80400010", data)
    run("--load", "0x80400010", data, "--dtb", data, "--dtb-addr", "0x85000000", "--initrd", initrd)
    run("--load", "0x8040000f", data, error="overlaps")
    run("--load", "0x80400010", data, "--load", "0x80400011", data, error="overlaps")
    run("--load", "0x7fffffff", data, error="does not fit in RAM")
    run("--load", "0x87fffffd", data, error="does not fit in RAM")
    run("--load", "0x180400010", data, error="invalid address")
    run("--dtb", data, "--initrd", initrd, error="overlaps")
