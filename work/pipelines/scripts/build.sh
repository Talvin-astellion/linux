#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/allmodconfig}"
FRAGMENT="${FRAGMENT:-$SCRIPT_DIR/../configs/allmodconfig-ci.config}"
LOG_FILE="$BUILD_DIR/build.log"

require_tool make
require_tool gcc
require_disk "${MIN_FREE_GB:-10}"
prepare_build_root
mkdir -p "$BUILD_DIR"

MAKE=(make -C "$REPO_ROOT" O="$BUILD_DIR")
CC_ENV=()
if have ccache; then
  CC_ENV=(CC="ccache gcc")
fi

log "configuring allmodconfig in $BUILD_DIR"
"${MAKE[@]}" allmodconfig

log "applying CI fragment $FRAGMENT"
"$REPO_ROOT/scripts/kconfig/merge_config.sh" -m -O "$BUILD_DIR" \
  "$BUILD_DIR/.config" "$FRAGMENT"
"${MAKE[@]}" olddefconfig

log "building with -j$JOBS (log: $LOG_FILE)"
set -o pipefail
"${MAKE[@]}" -j"$JOBS" ${CC_ENV[@]+"${CC_ENV[@]}"} 2>&1 | tee "$LOG_FILE"

log "build complete: $BUILD_DIR"
