#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/smoke}"
KERNEL_IMAGE="${KERNEL_IMAGE:-$BUILD_ROOT/defconfig/arch/x86/boot/bzImage}"
QEMU="${QEMU:-qemu-system-x86_64}"
SMOKE_TIMEOUT="${SMOKE_TIMEOUT:-120}"
SMOKE_MEM="${SMOKE_MEM:-512M}"
QEMU_CPU="${QEMU_CPU:-}"

require_tool gcc
require_tool cpio
require_tool gzip
require_tool timeout
require_tool "$QEMU"
require_disk "${MIN_FREE_GB:-1}"

[[ -f "$KERNEL_IMAGE" ]] || die "kernel image not found: $KERNEL_IMAGE (build defconfig first)"
prepare_build_root
mkdir -p "$BUILD_DIR/root/dev" "$BUILD_DIR/root/proc" "$BUILD_DIR/root/sys"

cat > "$BUILD_DIR/init.c" <<'EOF'
#include <unistd.h>
#include <sys/reboot.h>

int main(void) {
    if (write(1, "SMOKE_OK\n", 9) < 0)
        return 1;
    reboot(RB_POWER_OFF);
    return 0;
}
EOF

log "building initramfs in $BUILD_DIR"
gcc -static -O2 -o "$BUILD_DIR/root/init" "$BUILD_DIR/init.c"
(
  cd "$BUILD_DIR/root"
  find . | cpio -o -H newc 2>/dev/null | gzip > "$BUILD_DIR/initramfs.cpio.gz"
)

log "booting $KERNEL_IMAGE in QEMU (timeout ${SMOKE_TIMEOUT}s)"
QEMU_ARGS=(
  -kernel "$KERNEL_IMAGE"
  -initrd "$BUILD_DIR/initramfs.cpio.gz"
  -append "console=ttyS0 rdinit=/init panic=-1"
  -nographic -no-reboot -m "$SMOKE_MEM"
)
if [[ -n "$QEMU_CPU" ]]; then
  QEMU_ARGS+=(-cpu "$QEMU_CPU")
fi

timeout "$SMOKE_TIMEOUT" "$QEMU" "${QEMU_ARGS[@]}" \
  > "$BUILD_DIR/qemu.log" 2>&1 || true

if ! grep -q 'SMOKE_OK' "$BUILD_DIR/qemu.log"; then
  tail -n 50 "$BUILD_DIR/qemu.log" >&2
  die "QEMU smoke test failed (log: $BUILD_DIR/qemu.log)"
fi

log "QEMU smoke OK (log: $BUILD_DIR/qemu.log)"
