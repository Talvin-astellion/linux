#!/usr/bin/env python3
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tas import AsmError, MAX_TRYTE, MIN_TRYTE, balanced_to_int, int_to_trytes

TRIT_VALUE = {"n": -1, "-": -1, "0": 0, "1": 1, "+": 1}
START_ADDRESSES = (-4, -3, -2)


def parse_trt(text, path):
    values = {}
    address = 0
    accum = ""
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        for char in raw_line.split("#", 1)[0]:
            if char.isspace():
                continue
            if char == ":":
                try:
                    address = balanced_to_int(accum)
                except ValueError:
                    raise AsmError(path, line_number,
                                   "invalid address '%s'" % accum)
                accum = ""
                continue
            if char == ";":
                try:
                    value = balanced_to_int(accum)
                except ValueError:
                    raise AsmError(path, line_number,
                                   "invalid tryte '%s'" % accum)
                if value < MIN_TRYTE or value > MAX_TRYTE:
                    raise AsmError(path, line_number,
                                   "tryte value out of range")
                values[address] = int_to_trytes(value, path, line_number)[0]
                address += 1
                accum = ""
                continue
            if char not in TRIT_VALUE:
                raise AsmError(path, line_number,
                               "invalid character '%s' in .trt file" % char)
            accum += char
        if accum:
            raise AsmError(path, line_number,
                           "line ends with incomplete value '%s'" % accum)
    return values


def read_start(values, path):
    if START_ADDRESSES[0] not in values:
        raise AsmError(path, 0, "missing PC start vector")
    parts = [balanced_to_int(values.get(address, "000000000"))
             for address in START_ADDRESSES]
    return parts[0] + parts[1] * (3 ** 9) + parts[2] * (3 ** 18)


def encode_tryte(token):
    return bytes(TRIT_VALUE[char] + 0x1F for char in token)


def build_payload(text, path):
    values = parse_trt(text, path)
    start = read_start(values, path)
    load_addresses = [address for address in values if address >= 0]
    if not load_addresses:
        raise AsmError(path, 0, "no loadable trytes in image")
    unexpected = [address for address in values
                  if address < 0 and address not in START_ADDRESSES]
    if unexpected:
        raise AsmError(path, 0, "unsupported negative address in image")
    base = min(load_addresses)
    end = max(load_addresses)
    payload = []
    for word in (start, base):
        payload.extend(encode_tryte(token)
                       for token in int_to_trytes(word, path, 0))
    for address in range(base, end + 1):
        token = values.get(address, "000000000")
        payload.append(encode_tryte(token))
    return b"".join(payload)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Serialize a .trt image")
    parser.add_argument("input", help="input .trt file")
    parser.add_argument("-o", "--output", default="-",
                        help="output binary file (default: stdout)")
    args = parser.parse_args(argv)
    try:
        with open(args.input, "r", encoding="utf-8") as handle:
            payload = build_payload(handle.read(), args.input)
        if args.output == "-":
            sys.stdout.buffer.write(payload)
        else:
            with open(args.output, "wb") as handle:
                handle.write(payload)
    except (OSError, AsmError, ValueError) as error:
        print("serial_load.py: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
