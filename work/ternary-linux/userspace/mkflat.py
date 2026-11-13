#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Wrap a raw RV32 binary in a Linux bFLT header."""
import struct
import sys

raw = open(sys.argv[1], "rb").read()
hdr_size = 64
entry = hdr_size
data_start = hdr_size + len(raw)
stack = 32768
flags = 0x0001  # FLAT_FLAG_RAM
hdr = bytearray(hdr_size)
hdr[0:4] = b"bFLT"
fields = [
    4, entry, data_start, data_start, data_start,
    stack, data_start, 0, flags, 0,
]
for i, value in enumerate(fields):
    struct.pack_into(">I", hdr, 4 + i * 4, value)
open(sys.argv[2], "wb").write(hdr + raw)
