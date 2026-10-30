#!/usr/bin/env bash
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

log "sparse complete: $LOG_FILE"
