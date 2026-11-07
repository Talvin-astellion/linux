#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-lib.sh
source "$SCRIPT_DIR/ternary-lib.sh"

MODE="${1:-all}"
case "$MODE" in
	all|baseline|candidate) ;;
	*) die "usage: $0 [all|baseline|candidate]" ;;
esac

run_mode() {
	local mode="$1"
	local src obj binary source name object
	local -a cflags
	local -a sources
	local -a objects

	src="$(ternary_source_for "$mode")"
	obj="$TERNARY_BUILD_DIR/cov-$mode"
	binary="$TERNARY_BUILD_DIR/cov-test-$mode"

	mkdir -p "$obj"
	cflags=(-std=gnu11 -O0 -g --coverage -Wall -Wextra
		-Wno-unused-parameter -I"$src/emu" -I"$TERNARY_TESTS_DIR")

	sources=(
		"$src/emu/ternary.c"
		"$src/emu/disassem.c"
		"$TERNARY_TESTS_DIR/test_main.c"
		"$TERNARY_TESTS_DIR/test_trit.c"
		"$TERNARY_TESTS_DIR/test_arithmetic.c"
		"$TERNARY_TESTS_DIR/test_disasm.c"
	)

	objects=()
	for source in "${sources[@]}"; do
		name="$(basename "$source" .c)"
		object="$obj/$name.o"
		objects+=("$object")
		if ! gcc "${cflags[@]}" -c "$source" -o "$object"; then
			die "failed to compile $source for coverage"
		fi
	done

	if ! gcc "${cflags[@]}" "${objects[@]}" -o "$binary"; then
		die "failed to link coverage binary for $mode"
	fi

	log "running $mode coverage tests"
	set +e
	(cd "$obj" && "$binary" >test.log 2>&1)
	set -e

	set +e
	(cd "$obj" && gcov -b -c -o "$obj" "$src/emu/ternary.c" \
		>gcov.log 2>&1)
	set -e

	python3 "$SCRIPT_DIR/ternary-metrics.py" coverage \
		--gcov-dir "$obj" \
		--out "$TERNARY_BUILD_DIR/coverage-$mode.json"
}

if [[ "$MODE" == all ]]; then
	for mode in $(ternary_mode_list); do
		run_mode "$mode"
	done
else
	run_mode "$MODE"
fi
