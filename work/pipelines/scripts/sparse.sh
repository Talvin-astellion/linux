#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/sparse}"
LOG_FILE="$BUILD_DIR/sparse.log"

require_tool make
require_tool sparse
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
{
  echo "checks=$checks"
  echo "errors=$errors"
  echo "warnings=$warnings"
} > "$BUILD_DIR/summary.txt"
log "sparse summary: $checks checks, $errors errors, $warnings warnings"
log "sparse complete: $LOG_FILE"
