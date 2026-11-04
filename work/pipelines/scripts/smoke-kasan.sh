#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/smoke-kasan}"
FRAGMENT="${FRAGMENT:-$SCRIPT_DIR/../configs/kasan.config}"
KERNEL_IMAGE="${KERNEL_IMAGE:-$BUILD_DIR/arch/x86/boot/bzImage}"
SMOKE_MEM="${SMOKE_MEM:-1024M}"
SMOKE_TIMEOUT="${SMOKE_TIMEOUT:-300}"

require_tool make
require_tool gcc
require_file "$FRAGMENT"
require_disk "${MIN_FREE_GB:-10}"
prepare_build_root
mkdir -p "$BUILD_DIR"

MAKE=(make -C "$REPO_ROOT" O="$BUILD_DIR")

log "configuring KASAN/KFENCE/UBSAN kernel in $BUILD_DIR"
"${MAKE[@]}" defconfig
"$REPO_ROOT/scripts/kconfig/merge_config.sh" -m -O "$BUILD_DIR" "$BUILD_DIR/.config" "$FRAGMENT"
"${MAKE[@]}" olddefconfig

log "building KASAN kernel with -j$JOBS (log: $BUILD_DIR/build.log)"
run_logged "$BUILD_DIR/build.log" "${MAKE[@]}" -j"$JOBS"

SMOKE_APPEND="kasan_multi_shot" \
KERNEL_IMAGE="$KERNEL_IMAGE" \
BUILD_DIR="$BUILD_DIR/smoke" \
SMOKE_MEM="$SMOKE_MEM" \
SMOKE_TIMEOUT="$SMOKE_TIMEOUT" \
"$SCRIPT_DIR/smoke-qemu.sh"
