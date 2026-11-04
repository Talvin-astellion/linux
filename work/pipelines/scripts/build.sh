#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/allmodconfig}"
FRAGMENT="${FRAGMENT:-$SCRIPT_DIR/../configs/allmodconfig-ci.config}"
LOG_FILE="$BUILD_DIR/build.log"

require_tool make
require_tool gcc
require_file "$FRAGMENT"
require_disk "${MIN_FREE_GB:-10}"
prepare_build_root
mkdir -p "$BUILD_DIR"

MAKE=(make -C "$REPO_ROOT" O="$BUILD_DIR")
MAKE_CC=()
if have ccache; then
  MAKE_CC=(CC="ccache gcc")
fi

log "configuring allmodconfig in $BUILD_DIR"
"${MAKE[@]}" allmodconfig

log "applying CI fragment $FRAGMENT"
"$REPO_ROOT/scripts/kconfig/merge_config.sh" -m -O "$BUILD_DIR" \
  "$BUILD_DIR/.config" "$FRAGMENT"
"${MAKE[@]}" olddefconfig

log "building with -j$JOBS (log: $LOG_FILE)"
run_logged "$LOG_FILE" "${MAKE[@]}" -j"$JOBS" "${MAKE_CC[@]}"

log "build complete: $BUILD_DIR"
