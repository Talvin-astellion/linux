#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-lib.sh
source "$SCRIPT_DIR/ternary-lib.sh"

MODE="${1:-all}"
shift || true
SANITIZE=0
for arg in "$@"; do
	case "$arg" in
		--sanitize)
			SANITIZE=1
			;;
		*)
			die "unknown argument: $arg"
			;;
	esac
done

case "$MODE" in
	all|baseline|candidate) ;;
	*) die "usage: $0 [all|baseline|candidate] [--sanitize]" ;;
esac

run_mode() {
	local mode="$1"
	local src obj binary log warnlog json suffix
	local summary checks failures warnings rc
	local source name object
	local -a cflags
	local -a sources
	local -a objects

	src="$(ternary_source_for "$mode")"
	suffix="$mode"
	[[ "$SANITIZE" == 1 ]] && suffix="$mode-sanitize"
	obj="$TERNARY_BUILD_DIR/obj-$suffix"
	binary="$TERNARY_BUILD_DIR/test-$suffix"
	log="$TERNARY_BUILD_DIR/test-$suffix.log"
	warnlog="$TERNARY_BUILD_DIR/warnings-$suffix.txt"
	json="$TERNARY_BUILD_DIR/tests-$suffix.json"

	mkdir -p "$obj"
	: > "$warnlog"

	cflags=(-std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter
		-I"$src/emu" -I"$TERNARY_TESTS_DIR")
	if [[ "$SANITIZE" == 1 ]]; then
		cflags+=(-O1 -fsanitize=address,undefined
			-fno-omit-frame-pointer)
	fi

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
		if ! gcc "${cflags[@]}" -c "$source" -o "$object" \
			2>>"$warnlog"; then
			die "failed to compile $source"
		fi
	done

	if ! gcc "${cflags[@]}" "${objects[@]}" -o "$binary" \
		2>>"$warnlog"; then
		die "failed to link $binary"
	fi

	log "running $suffix tests"
	set +e
	if have timeout; then
		ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
			timeout 300 "$binary" >"$log" 2>&1
		rc=$?
	else
		ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
			"$binary" >"$log" 2>&1
		rc=$?
	fi
	set -e

	cat "$log"
	warnings="$(grep -c 'warning:' "$warnlog" 2>/dev/null || true)"
	[[ -n "$warnings" ]] || warnings=0

	summary="$(grep -E '^RESULT ' "$log" | tail -n 1 || true)"
	checks=0
	failures=1
	if [[ -n "$summary" ]]; then
		checks="$(sed -E 's/.*checks=([0-9]+).*/\1/' <<<"$summary")"
		failures="$(sed -E 's/.*failures=([0-9]+).*/\1/' <<<"$summary")"
	fi

	cat >"$json" <<EOF
{
  "mode": "$suffix",
  "checks": $checks,
  "failures": $failures,
  "warnings": $warnings,
  "rc": $rc
}
EOF

	log "$suffix: checks=$checks failures=$failures warnings=$warnings rc=$rc"
	return "$rc"
}

if [[ "$MODE" == all ]]; then
	for mode in $(ternary_mode_list); do
		run_mode "$mode"
	done
else
	run_mode "$MODE"
fi
