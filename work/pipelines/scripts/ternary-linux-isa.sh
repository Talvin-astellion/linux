#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build and run the RV32IMA/SBI regression tests that execute as bare-metal
# guests of the tern-linux ternary interpreter.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-linux-lib.sh
source "$SCRIPT_DIR/ternary-linux-lib.sh"

TERNARY_REPO="${TERNARY_REPO:-$HOME/System/ternary}"

# Load the RV32 toolchain environment when deps.sh has prepared one.  CI may
# provide CROSS_COMPILE/TL_EMU directly instead.
if [[ -f "$TL_ENV" ]]; then
	set +u
	# shellcheck disable=SC1090
	source "$TL_ENV"
	set -u
	if [[ -n "${TL_PATH_PREFIX:-}" ]]; then
		export PATH="$TL_PATH_PREFIX:$PATH"
	fi
fi

: "${CROSS_COMPILE:=riscv64-unknown-elf-}"
TL_EMU="${TL_EMU:-$TERNARY_REPO/emu/tern-linux}"
TEST_SRC="${TL_TESTS_DIR:-$REPO_ROOT/work/ternary-linux/tests}"
OUT="${TL_ISA_BUILD:-$TL_ROOT/isa}"
MAX_INSN="${MAX_INSN:-5000000}"
TEST_TIMEOUT="${TEST_TIMEOUT:-120}"
SUMMARY="$OUT/summary.txt"
TESTS=(test_rv32i test_rv32m test_rv32a test_sbi)

require_tool "${CROSS_COMPILE}gcc"
require_tool "${CROSS_COMPILE}objcopy"
require_file "$TEST_SRC/link.ld"
require_file "$TEST_SRC/start.S"
require_file "$TEST_SRC/test.h"
require_file "$TL_EMU"
[[ -x "$TL_EMU" ]] || die "emulator is not executable: $TL_EMU"

rm -rf "$OUT"
mkdir -p "$OUT"
: > "$SUMMARY"

CFLAGS=(
	-march=rv32ima_zicsr_zifencei -mabi=ilp32 -O2 -g
	-ffreestanding -fno-builtin -fno-stack-protector
	-nostdlib -nostartfiles -mno-relax -mcmodel=medany
	-Wl,--build-id=none -Wl,-T,"$TEST_SRC/link.ld"
)

build_test() {
	local test="$1"
	local -a sources=("$TEST_SRC/start.S" "$TEST_SRC/$test.c")

	if [[ "$test" == test_sbi ]]; then
		sources+=("$TEST_SRC/trap.S")
	fi

	log "building $test"
	"${CROSS_COMPILE}gcc" "${CFLAGS[@]}" "${sources[@]}" \
		-o "$OUT/$test.elf"
	"${CROSS_COMPILE}objcopy" -O binary "$OUT/$test.elf" "$OUT/$test.bin"
}

run_test() {
	local test="$1"
	local status
	local log_file="$OUT/$test.log"

	log "running $test"
	set +e
	if have timeout; then
		timeout "$TEST_TIMEOUT" "$TL_EMU" \
			--kernel "$OUT/$test.bin" \
			--max-insn "$MAX_INSN" --time-inst \
			>"$log_file" 2>&1
		status=$?
	else
		"$TL_EMU" --kernel "$OUT/$test.bin" \
			--max-insn "$MAX_INSN" --time-inst \
			>"$log_file" 2>&1
		status=$?
	fi
	set -e

	if [[ "$status" -eq 0 ]] &&
		grep -q 'test finisher PASS' "$log_file"; then
		printf 'PASS %s\n' "$test" | tee -a "$SUMMARY"
		return 0
	fi

	printf 'FAIL %s (rc=%s, log=%s)\n' "$test" "$status" "$log_file" |
		tee -a "$SUMMARY"
	tail -n 20 "$log_file" >&2 || true
	return 1
}

fail=0
for test in "${TESTS[@]}"; do
	build_test "$test"
	if ! run_test "$test"; then
		fail=1
	fi
done

if (( fail != 0 )); then
	die "ISA regression tests failed (see $SUMMARY)"
fi

log "ISA regression tests passed: $SUMMARY"
