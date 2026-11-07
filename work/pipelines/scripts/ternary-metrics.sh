#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-lib.sh
source "$SCRIPT_DIR/ternary-lib.sh"

usage() {
	echo "usage: $0 [--report|--check]" >&2
}

MODE=check
case "${1:-}" in
	"") ;;
	--check) MODE=check ;;
	--report) MODE=report ;;
	*) usage; exit 2 ;;
esac

mkdir -p "$TERNARY_BUILD_DIR"

run_ignore() {
	local rc

	log "running: $*"
	set +e
	"$@"
	rc=$?
	set -e
	if [[ "$rc" -ne 0 ]]; then
		log "command exited with status $rc (continuing): $*"
	fi
}

log "baseline ref: $(ternary_base_ref)"
run_ignore "$SCRIPT_DIR/ternary-test.sh" baseline
run_ignore "$SCRIPT_DIR/ternary-test.sh" candidate
run_ignore "$SCRIPT_DIR/ternary-test.sh" candidate --sanitize

"$SCRIPT_DIR/ternary-bench.sh" all
"$SCRIPT_DIR/ternary-coverage.sh" all

set +e
python3 "$SCRIPT_DIR/ternary-metrics.py" compare \
	--baseline-dir "$TERNARY_BUILD_DIR" \
	--candidate-dir "$TERNARY_BUILD_DIR" \
	--out "$TERNARY_BUILD_DIR/metrics.md"
rc=$?
set -e

cat "$TERNARY_BUILD_DIR/metrics.md"
if [[ "$MODE" == report ]]; then
	exit 0
fi
exit "$rc"
