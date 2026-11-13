#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-linux-lib.sh
source "$SCRIPT_DIR/ternary-linux-lib.sh"

tl_load_env

SRC="${SRC:-$REPO_ROOT}"
BUILD_DIR="${TL_GUEST_BUILD:-$TL_ROOT/guest}"
ARTIFACT_DIR="$BUILD_DIR/artifacts"
BASE_DEFCONFIG="${BASE_DEFCONFIG:-nommu_virt_defconfig}"
FRAGMENT="${FRAGMENT:-$TL_CONFIG}"
INITRAMFS="${INITRAMFS:-}"
LOG_FILE="$BUILD_DIR/build.log"

require_tool make
require_tool "${CROSS_COMPILE}gcc"
require_file "$SRC/Makefile"
require_file "$SRC/arch/riscv/configs/32-bit.config"
require_file "$FRAGMENT"
require_disk "${MIN_FREE_GB:-4}"
prepare_build_root
mkdir -p "$BUILD_DIR" "$ARTIFACT_DIR"

MAKE=(make -C "$SRC" --no-print-directory O="$BUILD_DIR" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE")

log "configuring $BASE_DEFCONFIG in $BUILD_DIR"
"${MAKE[@]}" "$BASE_DEFCONFIG"

log "merging 32-bit.config and $FRAGMENT"
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$BUILD_DIR" \
  "$BUILD_DIR/.config" \
  "$SRC/arch/riscv/configs/32-bit.config" "$FRAGMENT"
"${MAKE[@]}" olddefconfig

if [[ -n "$INITRAMFS" ]]; then
  require_dir "$INITRAMFS"
  log "embedding initramfs $INITRAMFS"
  "$SRC/scripts/config" --file "$BUILD_DIR/.config" \
    --set-str INITRAMFS_SOURCE "$INITRAMFS"
  "${MAKE[@]}" olddefconfig
fi

check_enabled() {
  grep -qx "$1" "$BUILD_DIR/.config" || die "missing kernel symbol: $1"
}

check_disabled() {
  grep -qx "# $1 is not set" "$BUILD_DIR/.config" || die "unexpected kernel symbol: $1"
}

check_absent() {
  ! grep -q "^$1=" "$BUILD_DIR/.config" || die "unexpected kernel symbol: $1"
}

check_enabled CONFIG_ARCH_RV32I=y
check_enabled CONFIG_32BIT=y
check_enabled CONFIG_NONPORTABLE=y
check_enabled CONFIG_PHYS_RAM_BASE_FIXED=y
check_enabled CONFIG_PHYS_RAM_BASE=0x80400000
check_enabled CONFIG_RISCV_SBI=y
check_enabled CONFIG_BLK_DEV_INITRD=y
check_enabled CONFIG_RD_GZIP=y
check_enabled CONFIG_BINFMT_FLAT=y
check_enabled CONFIG_HVC_RISCV_SBI=y
check_enabled CONFIG_SERIAL_EARLYCON_RISCV_SBI=y
check_enabled CONFIG_CMDLINE_FORCE=y
check_absent CONFIG_64BIT
check_disabled CONFIG_MMU
check_disabled CONFIG_RELOCATABLE
check_disabled CONFIG_RISCV_M_MODE
check_disabled CONFIG_SMP
check_disabled CONFIG_RISCV_ISA_C
check_disabled CONFIG_RISCV_ISA_V
check_disabled CONFIG_RISCV_ISA_VENDOR_EXT_ANDES
check_disabled CONFIG_RISCV_ISA_VENDOR_EXT_MIPS
check_disabled CONFIG_RISCV_ISA_VENDOR_EXT_SIFIVE
check_disabled CONFIG_RISCV_ISA_VENDOR_EXT_THEAD

log "building Image with -j$JOBS (log: $LOG_FILE)"
run_logged "$LOG_FILE" "${MAKE[@]}" -j"$JOBS" Image

KERNEL_IMAGE="$BUILD_DIR/arch/riscv/boot/Image"
KERNEL_VMLINUX="$BUILD_DIR/vmlinux"
KERNEL_SYSMAP="$BUILD_DIR/System.map"
KERNEL_RELEASE="$("${MAKE[@]}" -s kernelrelease | tail -n1)"

require_file "$KERNEL_IMAGE"
require_file "$KERNEL_VMLINUX"
require_file "$KERNEL_SYSMAP"

install -m 0644 "$KERNEL_IMAGE" "$ARTIFACT_DIR/Image"
install -m 0644 "$KERNEL_VMLINUX" "$ARTIFACT_DIR/vmlinux"
install -m 0644 "$KERNEL_SYSMAP" "$ARTIFACT_DIR/System.map"
install -m 0644 "$BUILD_DIR/.config" "$ARTIFACT_DIR/config"

DTB="$(tl_build_dtb "$TL_DTS" "$TL_ROOT/dtb/ternary.dtb")"
EMU_DTB="$(tl_build_dtb "$TL_EMU_DTS" "$TL_ROOT/dtb/ternary-emu.dtb")"

{
  printf 'KERNEL_IMAGE=%q\n' "$ARTIFACT_DIR/Image"
  printf 'KERNEL_VMLINUX=%q\n' "$ARTIFACT_DIR/vmlinux"
  printf 'KERNEL_SYSMAP=%q\n' "$ARTIFACT_DIR/System.map"
  printf 'GUEST_CONFIG=%q\n' "$ARTIFACT_DIR/config"
  printf 'KERNEL_RELEASE=%q\n' "$KERNEL_RELEASE"
  printf 'PHYS_RAM_BASE=%q\n' "0x80400000"
  printf 'DTB=%q\n' "$DTB"
  printf 'EMU_DTB=%q\n' "$EMU_DTB"
  printf 'INITRAMFS=%q\n' "$INITRAMFS"
} > "$BUILD_DIR/build.env"

log "kernel: $KERNEL_RELEASE"
log "image: $ARTIFACT_DIR/Image"
log "config: $ARTIFACT_DIR/config"
log "dtb: $DTB"
log "emu dtb: $EMU_DTB"
