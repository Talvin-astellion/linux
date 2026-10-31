#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

SRC_TREE="${SRC_TREE:-/home/talvin/linux-7.2.6}"
BASE_CONFIG="${BASE_CONFIG:-/boot/config-$(uname -r)}"
FRAGMENT="${FRAGMENT:-$SCRIPT_DIR/../configs/machine.config}"

KVER="$(make -s -C "$SRC_TREE" kernelversion 2>/dev/null || true)"
[[ -n "$KVER" ]] || die "cannot determine kernel version from $SRC_TREE/Makefile"

SRC_COPY="${SRC_COPY:-$BUILD_ROOT/src-$KVER}"
BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/machine-$KVER}"
LOG_FILE="$BUILD_DIR/build.log"
ARTIFACT_DIR="$BUILD_DIR/artifacts"
MODULES_DIR="$BUILD_DIR/modules"

require_tool make
require_tool gcc
[[ -d "$SRC_TREE" ]] || die "source tree not found: $SRC_TREE"
[[ -r "$BASE_CONFIG" ]] || die "base config not found: $BASE_CONFIG"
[[ -r "$FRAGMENT" ]] || die "fragment not found: $FRAGMENT"

prepare_build_root

if [[ ! -d "$SRC_COPY" ]]; then
  require_disk "${MIN_FREE_GB:-14}"
  log "copying $SRC_TREE to $SRC_COPY"
  cp -a "$SRC_TREE" "$SRC_COPY"
fi

if [[ -e "$SRC_COPY/.config" || -e "$SRC_COPY/include/config/auto.conf" ]]; then
  log "cleaning source copy $SRC_COPY"
  make -C "$SRC_COPY" mrproper >/dev/null
fi

require_disk "${MIN_FREE_GB:-8}"
mkdir -p "$BUILD_DIR" "$ARTIFACT_DIR"

MAKE=(make -C "$SRC_COPY" O="$BUILD_DIR")

log "seeding $BUILD_DIR/.config from $BASE_CONFIG"
cp "$BASE_CONFIG" "$BUILD_DIR/.config"

log "applying machine fragment $FRAGMENT"
"${MAKE[@]}" olddefconfig
"$SRC_COPY/scripts/kconfig/merge_config.sh" -m -O "$BUILD_DIR" \
  "$BUILD_DIR/.config" "$FRAGMENT"
"${MAKE[@]}" olddefconfig

REQUIRED_SYMS=(
  CONFIG_X86_NATIVE_CPU
  CONFIG_DRM_AMDGPU
  CONFIG_RTW88
  CONFIG_RTW88_8822CE
  CONFIG_R8169
  CONFIG_BLK_DEV_NVME
  CONFIG_SND_HDA_INTEL
  CONFIG_SND_SOC_SOF_AMD_RENOIR
  CONFIG_USB_XHCI_HCD
  CONFIG_BT_RTL
  CONFIG_KVM_AMD
)
missing=()
for sym in "${REQUIRED_SYMS[@]}"; do
  grep -q "^${sym}=[ym]$" "$BUILD_DIR/.config" || missing+=("$sym")
done
if (( ${#missing[@]} )); then
  die "machine config lost required symbols: ${missing[*]}"
fi
grep -q '^CONFIG_DEBUG_INFO_NONE=y$' "$BUILD_DIR/.config" || \
  die "CONFIG_DEBUG_INFO_NONE is not enabled"
grep -q '^CONFIG_LOCALVERSION="-machine"$' "$BUILD_DIR/.config" || \
  die "CONFIG_LOCALVERSION is not -machine"

"$SRC_COPY/scripts/diffconfig" -m "$BASE_CONFIG" "$BUILD_DIR/.config" \
  > "$BUILD_DIR/config.diff" || true

log "building $KVER with -j$JOBS (log: $LOG_FILE)"
set -o pipefail
"${MAKE[@]}" -j"$JOBS" 2>&1 | tee "$LOG_FILE"

KERNELRELEASE="$("${MAKE[@]}" -s kernelrelease)"
log "kernelrelease: $KERNELRELEASE"

log "installing modules into staging $MODULES_DIR"
"${MAKE[@]}" modules_install INSTALL_MOD_PATH="$MODULES_DIR" \
  > "$BUILD_DIR/modules-install.log" 2>&1

cp "$BUILD_DIR/arch/x86/boot/bzImage" "$ARTIFACT_DIR/bzImage-$KERNELRELEASE"
cp "$BUILD_DIR/vmlinux" "$ARTIFACT_DIR/vmlinux-$KERNELRELEASE"
cp "$BUILD_DIR/System.map" "$ARTIFACT_DIR/System.map-$KERNELRELEASE"
cp "$BUILD_DIR/.config" "$ARTIFACT_DIR/config-$KERNELRELEASE"
cp "$BUILD_DIR/Module.symvers" "$ARTIFACT_DIR/Module.symvers-$KERNELRELEASE" 2>/dev/null || true

MODULE_COUNT=0
if [[ -d "$MODULES_DIR/lib/modules/$KERNELRELEASE" ]]; then
  MODULE_COUNT="$(find "$MODULES_DIR/lib/modules/$KERNELRELEASE" -name '*.ko*' | wc -l)"
fi

{
  echo "kernelrelease=$KERNELRELEASE"
  echo "source_tree=$SRC_TREE"
  echo "source_copy=$SRC_COPY"
  echo "base_config=$BASE_CONFIG"
  echo "build_dir=$BUILD_DIR"
  echo "jobs=$JOBS"
  echo "native_cpu=$(grep -c '^CONFIG_X86_NATIVE_CPU=y$' "$BUILD_DIR/.config")"
  echo "debug_info_none=$(grep -c '^CONFIG_DEBUG_INFO_NONE=y$' "$BUILD_DIR/.config")"
  echo "modules=$MODULE_COUNT"
  echo "bzImage_bytes=$(stat -c %s "$ARTIFACT_DIR/bzImage-$KERNELRELEASE")"
  echo "vmlinux_bytes=$(stat -c %s "$ARTIFACT_DIR/vmlinux-$KERNELRELEASE")"
} > "$BUILD_DIR/summary.txt"

log "artifacts: $ARTIFACT_DIR"
log "summary: $BUILD_DIR/summary.txt"
