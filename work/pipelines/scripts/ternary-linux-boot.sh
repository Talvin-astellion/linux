#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-linux-lib.sh
source "$SCRIPT_DIR/ternary-linux-lib.sh"

tl_load_env

BUILD_DIR="${TL_GUEST_BUILD:-$TL_ROOT/guest}"
BUILD_ENV="$BUILD_DIR/build.env"
LOG_FILE="${BOOT_LOG:-$BUILD_DIR/boot.log}"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-40}"
EXPECT_MARKER="${EXPECT_MARKER:-0}"
DTS="${DTS:-$TL_DTS}"

require_file "$BUILD_ENV"
initramfs_override="${INITRAMFS:-}"
dtb_override="${DTB:-}"
# shellcheck disable=SC1090
source "$BUILD_ENV"
INITRAMFS="${initramfs_override:-${INITRAMFS:-}}"
DTB="${dtb_override:-${DTB:-}}"
require_file "$KERNEL_IMAGE"
require_file "$OPENSBI"
require_tool "$QEMU_SYSTEM_RISCV32"

DTB="${DTB:-$(tl_build_dtb "$DTS" "$TL_ROOT/dtb/ternary.dtb")}"

QEMU=(
  "$QEMU_SYSTEM_RISCV32"
  -M virt
  -m 64M
  -smp 1
  -nographic
  -no-reboot
  -bios "$OPENSBI"
  -kernel "$KERNEL_IMAGE"
  -dtb "$DTB"
)
if [[ -n "${INITRAMFS:-}" && -f "$INITRAMFS" ]]; then
  QEMU+=(-initrd "$INITRAMFS")
fi
if [[ -n "${QEMU_DATA_DIR:-}" && -d "$QEMU_DATA_DIR" ]]; then
  QEMU+=(-L "$QEMU_DATA_DIR")
fi

log "booting $KERNEL_RELEASE with $DTB"
set +e
timeout "$BOOT_TIMEOUT" "${QEMU[@]}" > "$LOG_FILE" 2>&1
rc=$?
set -e

for pattern in 'Linux version' 'SBI specification' 'earlycon: sbi' 'console \[hvc0\]'; do
  grep -q "$pattern" "$LOG_FILE" || die "boot marker not found: $pattern (log: $LOG_FILE)"
done

if [[ "$EXPECT_MARKER" == 1 ]]; then
  grep -q 'TERNARY_LINUX_OK' "$LOG_FILE" ||
    die "userspace marker TERNARY_LINUX_OK not found (log: $LOG_FILE)"
fi

if [[ $rc -ne 0 && $rc -ne 124 ]]; then
  log "qemu exited with $rc after successful boot"
fi

log "boot OK: $LOG_FILE"
