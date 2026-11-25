#!/usr/bin/env python3
"""Assembler encoding and emulator smoke tests."""

import os
import re
import shutil
import subprocess
import sys
import tempfile

ASMDIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ASMDIR)
EMU = os.path.join(REPO, "emu", "tern-cpu-emu")
CPU_H = os.path.join(REPO, "emu", "cpu.h")
SERIAL_LOAD = os.path.join(ASMDIR, "serial_load.py")
OUTDIR = os.path.join(ASMDIR, "out")

TWO_REG = [
    "LD1", "LD2", "LD3", "ST1", "ST2", "ST3", "POP", "PSH",
    "ADCI", "ADDI", "MULI", "RTLI", "TANDI", "TORI", "TMAJI",
    "TADDI", "TMULI", "TMINI", "TMAXI",
]
THREE_REG = [
    "ADC", "ADD", "ADS", "MUL", "MULU", "RTL", "TAND", "TOR",
    "TMAJ", "TADD", "TMUL", "TMIN", "TMAX",
]

CASES = [(name, name, "P01, P02, _NOP, 0") for name in TWO_REG]
CASES += [(name, name, "P01, P02, _NOP, P03, _NOP") for name in THREE_REG]
CASES += [("ADSI", "ADSI", "P01, 0, 0")]
CASES += [("NOP", "NOP", None), ("HALT", "HALT", None)]
CASES += [
    ("MV", "ADDI", "P01, P02"),
    ("MVI", "ADDI", "P01, 0"),
    ("JMP", "ADDI", "P01"),
    ("JMPI", "ADDI", "0"),
    ("NEG", "ADDI", "P01, P02"),
    ("SWP", "RTLI", "P01, P02"),
    ("BRA", "ADDI", "0"),
]


def trit_string(value):
    """Render the low 12-bit opcode field as six trits."""
    symbols = {0: "0", 1: "+", 3: "-"}
    out = []
    for shift in range(10, -1, -2):
        out.append(symbols[(value >> shift) & 3])
    return "".join(out)


def balanced_literal(value):
    if value == 0:
        return "0"
    digits = []
    while value:
        value, remainder = divmod(value, 3)
        if remainder == 2:
            remainder = -1
            value += 1
        digits.append({-1: "n", 0: "0", 1: "1"}[remainder])
    return "".join(reversed(digits))


def opcode_table():
    text = open(CPU_H, "r", encoding="utf-8").read()
    table = {}
    for match in re.finditer(r"OPCODE_(\w+)\s*=\s*0b([01]+)", text):
        table[match.group(1)] = trit_string(int(match.group(2), 2))
    return table


def preprocess(source_path):
    cmd = [
        "gcc", "-E", "-xc",
        "-I", ASMDIR,
        "--include", "assembler.inc",
        "--include", "aliases.ymp",
        source_path,
    ]
    result = subprocess.run(cmd, text=True, capture_output=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError("preprocessor failed")
    return result.stdout


def instruction_trytes(preprocessed):
    """Return the first tryte of each assembled instruction."""
    instructions = []
    for line in preprocessed.splitlines():
        if line.startswith("#") or ";" not in line:
            continue
        parts = [part.strip() for part in line.split(";") if part.strip()]
        if len(parts) not in (1, 3):
            raise AssertionError(
                "expected 1 or 3 trytes per instruction, got %d: %r"
                % (len(parts), line)
            )
        instructions.append("".join(parts[0].split()))
    return instructions


def test_encodings():
    expected = opcode_table()
    covered = {opcode for _name, opcode, _args in CASES}
    if covered != set(expected):
        missing = sorted(set(expected) - covered)
        extra = sorted(covered - set(expected))
        raise AssertionError(
            "opcode coverage mismatch; missing=%r extra=%r" % (missing, extra)
        )
    source = "\n".join(
        ("%s;" % name) if args is None else "%s(%s);" % (name, args)
        for name, _op, args in CASES
    )
    with tempfile.NamedTemporaryFile("w", suffix=".tas", delete=False) as tmp:
        tmp.write(source + "\n")
        path = tmp.name
    try:
        text = preprocess(path)
    finally:
        os.unlink(path)

    instructions = instruction_trytes(text)
    if len(instructions) != len(CASES):
        raise AssertionError(
            "expected %d instructions, got %d" % (len(CASES), len(instructions))
        )

    failures = []
    for (name, opcode, _args), tryte in zip(CASES, instructions):
        if len(tryte) != 9 or any(ch not in "-0+" for ch in tryte):
            failures.append("%s: malformed tryte %r" % (name, tryte))
            continue
        actual = tryte[-6:]
        if actual != expected[opcode]:
            failures.append(
                "%s: opcode %s, expected %s" % (name, actual, expected[opcode])
            )

    if failures:
        raise AssertionError("\n".join(failures))


def run_emulator_bytes(program, data):
    result = subprocess.run(
        [EMU, program],
        input=data,
        capture_output=True,
        timeout=20,
    )
    output = (result.stdout + result.stderr).decode("utf-8", "replace")
    if result.returncode != 0:
        raise AssertionError("%s exited %d\n%s" % (program, result.returncode, output))
    if "ERROR" in output or "Unexpected" in output:
        raise AssertionError("%s reported a load/run error\n%s" % (program, output))
    return output


def run_emulator(program, commands):
    return run_emulator_bytes(program, commands.encode("utf-8"))


def assemble_source(source):
    tmpdir = tempfile.mkdtemp(prefix="tern-asm-")
    src = os.path.join(tmpdir, "test.tas")
    out = os.path.join(tmpdir, "test.trt")
    with open(src, "w", encoding="utf-8") as handle:
        handle.write(source + "\n")
    text = preprocess(src)
    with open(out, "w", encoding="utf-8") as handle:
        handle.write(text)
    return tmpdir, out


def assemble_with_tas(source):
    tmpdir = tempfile.mkdtemp(prefix="tern-tas-data-")
    src = os.path.join(tmpdir, "data.tas")
    out = os.path.join(tmpdir, "data.trt")
    try:
        with open(src, "w", encoding="utf-8") as handle:
            handle.write(source + "\n")
        result = subprocess.run(
            [
                sys.executable, os.path.join(ASMDIR, "tas.py"),
                "--include", "assembler.inc",
                "--include", "aliases.ymp",
                src, "-o", out,
            ],
            text=True,
            capture_output=True,
            timeout=20,
        )
        output = result.stdout + result.stderr
        if result.returncode != 0:
            raise AssertionError("tas.py failed\n%s" % output)
        with open(out, "r", encoding="utf-8") as handle:
            return handle.read()
    finally:
        shutil.rmtree(tmpdir)


def assemble_math_program(source):
    tmpdir = tempfile.mkdtemp(prefix="tern-math-")
    src = os.path.join(tmpdir, "math.tas")
    out = os.path.join(tmpdir, "math.trt")
    try:
        with open(src, "w", encoding="utf-8") as handle:
            handle.write(source + "\n")
        result = subprocess.run(
            [
                sys.executable, os.path.join(ASMDIR, "tas.py"),
                "--include", "assembler.inc",
                "--include", "aliases.ymp",
                src, "-o", out,
            ],
            cwd=ASMDIR,
            text=True,
            capture_output=True,
            timeout=20,
        )
        output = result.stdout + result.stderr
        if result.returncode != 0:
            raise AssertionError("tas.py failed\n%s" % output)
        return tmpdir, out
    except Exception:
        shutil.rmtree(tmpdir)
        raise


def test_runtime_macros():
    source = """--: 10-;
10-:
ADDI( P01, ZERO, _NOP, 10);
ADDI( P02, ZERO, _NOP, 1);
SWP( P01, P02);
MUL( P04, P01, _NOP, P02, _NOP);
MULI( P05, P04, _NOP, 10);
NEG( P06, P02);
MV( P07, P01);
MVI( P08, 10);
ADDI( PC, PC, _NOP, 0);
"""
    tmpdir, program = assemble_source(source)
    try:
        output = run_emulator(program, "g\nq\n")
    finally:
        shutil.rmtree(tmpdir)
    actual = {}
    for register in ("GP01", "GP02", "GP04", "GP05", "GP06", "GP07", "GP08"):
        values = register_values(output, register)
        if not values:
            raise AssertionError("unable to read %s\n%s" % (register, output))
        actual[register] = values[-1]
    expected = {
        "GP01": 1,
        "GP02": 3,
        "GP04": 3,
        "GP05": 9,
        "GP06": -3,
        "GP07": 1,
        "GP08": 3,
    }
    if actual != expected:
        raise AssertionError("runtime macro result %r, expected %r" % (actual, expected))


def register_values(output, register):
    pattern = r"%s\s+\d+\s*:\s*[^(\n]*\((-?\d+)\)" % register
    return [int(value) for value in re.findall(pattern, output)]


def test_data_directives():
    text = assemble_with_tas(""".equ A, 1
.tryte A
.tryte n
.triword 0
.triword 1, 1, 1
.string "A\\n"
""")
    trytes = []
    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue
        trytes.extend(token for token in line.replace(";", " ").split() if token)
    expected = [
        "000000001", "00000000n",
        "000000000", "000000000", "000000000",
        "000000001", "000000001", "000000001",
        "0000011n1", "00000n1n0", "000000000",
    ]
    if trytes != expected:
        raise AssertionError("data directive trytes %r, expected %r" %
                             (trytes, expected))


def test_examples():
    examples = sorted(
        name[:-4] for name in os.listdir(os.path.join(ASMDIR, "examples"))
        if name.endswith(".tas")
    )
    for name in examples:
        if name == "loader":
            continue
        program = os.path.join(OUTDIR, name + ".trt")
        if not os.path.exists(program):
            raise AssertionError("missing assembled program %s" % program)
        run_emulator(program, "g\nq\n")

    fib = os.path.join(OUTDIR, "fib.trt")
    output = run_emulator(fib, "g\nq\n")
    values = register_values(output, "GP01")
    values2 = register_values(output, "GP02")
    if not values or not values2:
        raise AssertionError("unable to read fib registers\n%s" % output)
    expected = (1548008755920, 2504730781961)  # F60, F61
    actual = (values[-1], values2[-1])
    if actual != expected:
        raise AssertionError("fib result %r, expected %r" % (actual, expected))

    labels = os.path.join(OUTDIR, "labels.trt")
    output = run_emulator(labels, "g\nq\n")
    expected = {"GP01": 1, "GP02": 4, "GP03": 6, "GP04": 5, "GP05": 9}
    actual = {}
    for register in expected:
        values = register_values(output, register)
        if not values:
            raise AssertionError(
                "unable to read labels register %s\n%s" % (register, output)
            )
        actual[register] = values[-1]
    if actual != expected:
        raise AssertionError(
            "labels result %r, expected %r" % (actual, expected)
        )


def test_hello():
    program = os.path.join(OUTDIR, "hello.trt")
    if not os.path.exists(program):
        raise AssertionError("missing assembled program %s" % program)
    output = run_emulator(program, "g\nq\n")
    if "Hello, ternary!\n" not in output:
        raise AssertionError("hello output not found\n%s" % output)
    if "CPU halted" not in output:
        raise AssertionError("hello program did not halt cleanly\n%s" % output)


def test_forth():
    program = os.path.join(OUTDIR, "forth.trt")
    if not os.path.exists(program):
        raise AssertionError("missing assembled program %s" % program)
    source = (
        b"2 3 + . 7 2 - . 5 DUP + . 9 DROP 4 . "
        b": INC 1 + . ; 4 INC : SUM + . ; 2 3 SUM 11 4 SUM BYE\n"
    )
    output = run_emulator_bytes(program, b"g\n" + source)
    if "5 5 10 4 5 5 15 " not in output:
        raise AssertionError("forth output not found\n%s" % output)
    if "CPU halted" not in output:
        raise AssertionError("forth program did not halt cleanly\n%s" % output)
    if "?" in output:
        raise AssertionError("forth interpreter reported an unknown token\n%s" % output)


def test_serial_loader():
    hello = os.path.join(OUTDIR, "hello.trt")
    loader = os.path.join(OUTDIR, "loader.trt")
    for program in (hello, loader):
        if not os.path.exists(program):
            raise AssertionError("missing assembled program %s" % program)

    payload = subprocess.run(
        [sys.executable, SERIAL_LOAD, hello],
        capture_output=True,
        timeout=20,
    )
    if payload.returncode != 0:
        raise AssertionError(
            "serial_load.py failed\n%s"
            % (payload.stdout + payload.stderr).decode("utf-8", "replace")
        )
    output = run_emulator_bytes(loader, b"g\n" + payload.stdout)
    if "Hello, ternary!\n" not in output:
        raise AssertionError("serial loader hello output not found\n%s" % output)
    if "CPU halted" not in output:
        raise AssertionError("serial loader program did not halt cleanly\n%s" % output)


def test_math_library():
    cases = [
        ("MATH_MOD", 1071, 462, None, 147),
        ("MATH_MOD", 49, 11, None, 5),
        ("MATH_MOD", 21, 21, None, 0),
        ("MATH_MOD", 7, 11, None, 7),
        ("MATH_MOD", 0, 11, None, 0),
        ("MATH_GCD", 1071, 462, None, 21),
        ("MATH_GCD", 21, 0, None, 21),
        ("MATH_GCD", 0, 21, None, 21),
        ("MATH_GCD", 13, 17, None, 1),
        ("MATH_MODPOW", 7, 0, 11, 1),
        ("MATH_MODPOW", 7, 1, 11, 7),
        ("MATH_MODPOW", 7, 2, 11, 5),
        ("MATH_MODPOW", 7, 13, 11, 2),
        ("MATH_MODPOW", 2, 10, 13, 10),
    ]
    for routine, a, b, c, expected in cases:
        setup = [
            "ADDI( P01, ZERO, _NOP, @(%s) );" % balanced_literal(a),
            "ADDI( P02, ZERO, _NOP, @(%s) );" % balanced_literal(b),
        ]
        if c is not None:
            setup.append("ADDI( P03, ZERO, _NOP, @(%s) );" % balanced_literal(c))
        source = """--: @(start);
10-:
#include "lib/math.tas"
start:
IO_INIT
%s
CALL( %s );
HALT;
""" % ("\n".join(setup), routine)
        tmpdir, program = assemble_math_program(source)
        try:
            output = run_emulator(program, "g\nq\n")
        finally:
            shutil.rmtree(tmpdir)
        values = register_values(output, "GP01")
        if not values or values[-1] != expected:
            raise AssertionError(
                "%s(%d, %d, %d) = %r, expected %d\n%s"
                % (routine, a, b, c if c is not None else 0,
                   values[-1] if values else None, expected, output)
            )


def check_math_helper(routine, operands, expected):
    setup = [
        "ADDI( %s, ZERO, _NOP, @(%s) );" % (register, balanced_literal(value))
        for register, value in zip(("P01", "P02", "P03"), operands)
    ]
    source = """--: @(start);
10-:
#include "lib/math.tas"
start:
IO_INIT
%s
CALL( %s );
HALT;
""" % ("\n".join(setup), routine)
    tmpdir, program = assemble_math_program(source)
    try:
        output = run_emulator(program, "g\nq\n")
    finally:
        shutil.rmtree(tmpdir)
    for register, expected_value in expected.items():
        values = register_values(output, register)
        if not values or values[-1] != expected_value:
            raise AssertionError(
                "%s%r: %s = %r, expected %d\n%s"
                % (routine, operands, register,
                   values[-1] if values else None, expected_value, output)
            )


def test_math_helpers():
    check_math_helper("MATH_ABS", [-13], {"GP01": 13})
    check_math_helper("MATH_ABS", [13], {"GP01": 13})
    check_math_helper("MATH_NEG", [-13], {"GP01": 13})
    check_math_helper("MATH_NEG", [13], {"GP01": -13})
    check_math_helper("MATH_SGN", [-13], {"GP01": -1})
    check_math_helper("MATH_SGN", [0], {"GP01": 0})
    check_math_helper("MATH_SGN", [13], {"GP01": 1})
    check_math_helper("MATH_MIN", [-13, 7], {"GP01": -13})
    check_math_helper("MATH_MIN", [7, 7], {"GP01": 7})
    check_math_helper("MATH_MAX", [-13, 7], {"GP01": 7})
    check_math_helper("MATH_MAX", [7, 7], {"GP01": 7})
    check_math_helper("MATH_DIV", [1071, 462], {"GP01": 2, "GP02": 147})
    check_math_helper("MATH_DIV", [49, 11], {"GP01": 4, "GP02": 5})
    check_math_helper("MATH_DIV", [21, 21], {"GP01": 1, "GP02": 0})
    check_math_helper("MATH_DIV", [7, 11], {"GP01": 0, "GP02": 7})
    check_math_helper("MATH_DIV", [0, 11], {"GP01": 0, "GP02": 0})


def test_numeric_io():
    source = """--: @(start);
10-:
#include "lib/io.tas"
start:
IO_INIT
CALL( IO_GET_WORD );
CALL( IO_PUT_WORD );
HALT;
"""
    tmpdir, program = assemble_math_program(source)
    try:
        for value in (0, 13, 1071, -13, 2187):
            text = balanced_literal(value).rjust(27, "0")
            output = run_emulator_bytes(
                program, b"g\n" + text.encode("ascii") + b"\n"
            )
            if text not in output:
                raise AssertionError(
                    "numeric I/O round trip for %d did not print %r\n%s"
                    % (value, text, output)
                )
            if "CPU trap 1" in output:
                raise AssertionError(
                    "numeric I/O round trip for %d trapped\n%s" % (value, output)
                )
    finally:
        shutil.rmtree(tmpdir)


def expect_tas_error(source, expected):
    tmpdir = tempfile.mkdtemp(prefix="tern-tas-error-")
    src = os.path.join(tmpdir, "error.tas")
    out = os.path.join(tmpdir, "error.trt")
    try:
        with open(src, "w", encoding="utf-8") as handle:
            handle.write(source + "\n")
        result = subprocess.run(
            [
                sys.executable, os.path.join(ASMDIR, "tas.py"),
                "--include", "assembler.inc",
                "--include", "aliases.ymp",
                src, "-o", out,
            ],
            text=True,
            capture_output=True,
            timeout=20,
        )
        output = result.stdout + result.stderr
        if result.returncode == 0:
            raise AssertionError("expected tas.py to reject: %s" % source)
        if "Traceback" in output:
            raise AssertionError("tas.py crashed instead of reporting an error\n%s" % output)
        if expected not in output:
            raise AssertionError("expected %r in tas.py output\n%s" % (expected, output))
    finally:
        shutil.rmtree(tmpdir)


def test_tas_errors():
    expect_tas_error(
        """.equ A, MISSING + 1
ADDI( P01, ZERO, _NOP, @(A));
ADDI( PC, PC, _NOP, 0);
""",
        "undefined symbol 'MISSING'",
    )
    expect_tas_error(
        """.equ A, B + 1
.equ B, A + 1
ADDI( P01, ZERO, _NOP, @(A));
ADDI( PC, PC, _NOP, 0);
""",
        "constant cycle involving",
    )
    expect_tas_error(
        """BRANCH(missing);
ADDI( PC, PC, _NOP, 0);
""",
        "undefined branch target 'missing'",
    )
    expect_tas_error(
        """x:
x:
ADDI( PC, PC, _NOP, 0);
""",
        "duplicate label 'x'",
    )


def main():
    test_encodings()
    test_runtime_macros()
    test_data_directives()
    test_examples()
    test_hello()
    test_forth()
    test_serial_loader()
    test_math_library()
    test_math_helpers()
    test_numeric_io()
    test_tas_errors()
    print("assembler encoding and emulator smoke tests passed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        sys.exit(1)
