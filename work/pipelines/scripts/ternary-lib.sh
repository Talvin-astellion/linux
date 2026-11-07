#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

TERNARY_REPO="${TERNARY_REPO:-$HOME/System/ternary}"
TERNARY_BASE_REF="${TERNARY_BASE_REF:-dev}"
TERNARY_BUILD_DIR="${TERNARY_BUILD_DIR:-$BUILD_ROOT/ternary}"
TERNARY_TESTS_DIR="$WORK_DIR/ternary/tests"
TERNARY_BENCH_DIR="$WORK_DIR/ternary/bench"
TERNARY_SRC_BASELINE="$TERNARY_BUILD_DIR/src/baseline"

ternary_base_ref() {
	local ref="$TERNARY_BASE_REF"

	if ! git -C "$TERNARY_REPO" rev-parse --verify --quiet \
		"${ref}^{commit}" >/dev/null; then
		log "baseline ref '$ref' not found; falling back to master"
		ref=master
	fi
	printf '%s' "$ref"
}

ternary_prepare_baseline() {
	local ref

	require_dir "$TERNARY_REPO"
	ref="$(ternary_base_ref)"

	if [[ -f "$TERNARY_SRC_BASELINE/emu/ternary.c" ]]; then
		printf '%s' "$TERNARY_SRC_BASELINE"
		return
	fi

	mkdir -p "$(dirname "$TERNARY_SRC_BASELINE")"
	git -C "$TERNARY_REPO" worktree prune >/dev/null 2>&1 || true
	git -C "$TERNARY_REPO" worktree remove --force \
		"$TERNARY_SRC_BASELINE" >/dev/null 2>&1 || true
	rm -rf "$TERNARY_SRC_BASELINE"
	git -C "$TERNARY_REPO" worktree add --detach \
		"$TERNARY_SRC_BASELINE" "$ref" >/dev/null
	printf '%s' "$TERNARY_SRC_BASELINE"
}

ternary_candidate_src() {
	require_dir "$TERNARY_REPO"
	printf '%s' "$TERNARY_REPO"
}

ternary_source_for() {
	case "$1" in
		baseline)
			ternary_prepare_baseline
			;;
		candidate)
			ternary_candidate_src
			;;
		*)
			die "unknown source mode: $1"
			;;
	esac
}

ternary_mode_list() {
	printf '%s\n' baseline candidate
}

export TERNARY_REPO TERNARY_BASE_REF TERNARY_BUILD_DIR
export TERNARY_TESTS_DIR TERNARY_BENCH_DIR TERNARY_SRC_BASELINE
