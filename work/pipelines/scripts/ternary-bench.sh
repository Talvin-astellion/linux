#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-lib.sh
source "$SCRIPT_DIR/ternary-lib.sh"

MODE="${1:-all}"
if [[ -z "${BENCH_CPU:-}" ]] && have taskset &&
	taskset -c 0 true 2>/dev/null; then
	BENCH_CPU=0
	export BENCH_CPU
fi
case "$MODE" in
	all|baseline|candidate) ;;
	*) die "usage: $0 [all|baseline|candidate]" ;;
esac

run_mode() {
	local mode="$1"
	local src binary samples run
	local -a cflags

	src="$(ternary_source_for "$mode")"
	binary="$TERNARY_BUILD_DIR/bench-$mode"
	samples="$TERNARY_BUILD_DIR/bench-$mode.samples"

	mkdir -p "$TERNARY_BUILD_DIR"
	cflags=(-std=gnu11 -O2 -g -I"$src/emu" -I"$TERNARY_TESTS_DIR")

	log "building $mode benchmark"
	gcc "${cflags[@]}" "$src/emu/ternary.c" \
		"$TERNARY_BENCH_DIR/bench_ternary.c" -o "$binary"

	: >"$samples"
	for run in 1 2 3 4 5 6 7; do
		log "benchmark $mode run $run"
		if [[ -n "${BENCH_CPU:-}" ]] && have taskset; then
			if have timeout; then
				timeout 300 taskset -c "$BENCH_CPU" \
					"$binary" >>"$samples"
			else
				taskset -c "$BENCH_CPU" "$binary" >>"$samples"
			fi
		else
			if have timeout; then
				timeout 300 "$binary" >>"$samples"
			else
				"$binary" >>"$samples"
			fi
		fi
	done

	python3 "$SCRIPT_DIR/ternary-metrics.py" bench \
		--samples "$samples" \
		--out "$TERNARY_BUILD_DIR/bench-$mode.json"
}

if [[ "$MODE" == all ]]; then
	for mode in $(ternary_mode_list); do
		run_mode "$mode"
	done
else
	run_mode "$MODE"
fi
