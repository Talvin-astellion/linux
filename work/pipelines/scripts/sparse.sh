#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/sparse}"
LOG_FILE="$BUILD_DIR/sparse.log"
BASELINE="${SPARSE_BASELINE:-$SCRIPT_DIR/../configs/sparse-baseline.conf}"

require_tool make
require_tool sparse
require_file "$BASELINE"
# shellcheck source=../configs/sparse-baseline.conf
source "$BASELINE"
require_disk "${MIN_FREE_GB:-5}"
prepare_build_root
mkdir -p "$BUILD_DIR"

MAKE=(make -C "$REPO_ROOT" O="$BUILD_DIR")

log "configuring defconfig in $BUILD_DIR"
"${MAKE[@]}" defconfig

log "running sparse with -j$JOBS (log: $LOG_FILE)"
"${MAKE[@]}" -j"$JOBS" C=2 CHECK=sparse 2>&1 | tee "$LOG_FILE"

errors=$(grep -c 'error:' "$LOG_FILE" || true)
warnings=$(grep -c 'warning:' "$LOG_FILE" || true)
checks=$(grep -c 'CHECK' "$LOG_FILE" || true)
max_errors="$SPARSE_MAX_ERRORS"
max_warnings="$SPARSE_MAX_WARNINGS"
if ! [[ "$max_errors" =~ ^[0-9]+$ && "$max_warnings" =~ ^[0-9]+$ ]]; then
  log "invalid sparse limits: max_errors=$max_errors max_warnings=$max_warnings"
  exit 2
fi
{
  echo "checks=$checks"
  echo "errors=$errors"
  echo "warnings=$warnings"
  echo "max_errors=$max_errors"
  echo "max_warnings=$max_warnings"
} > "$BUILD_DIR/summary.txt"
log "sparse summary: $checks checks, $errors errors, $warnings warnings"
if (( errors > max_errors || warnings > max_warnings )); then
  log "sparse regression: errors $errors/$max_errors, warnings $warnings/$max_warnings"
  exit 1
fi
log "sparse complete: $LOG_FILE"
