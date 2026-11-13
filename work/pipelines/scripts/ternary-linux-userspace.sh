#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-linux-lib.sh
source "$SCRIPT_DIR/ternary-linux-lib.sh"

tl_load_env

SRC="${USERSPACE_SRC:-$TL_USERSPACE}"
OUT="${TL_USERSPACE_BUILD:-$TL_ROOT/userspace}"
ROOT="$OUT/root"
LOG_FILE="$OUT/build.log"

require_file "$SRC/init.S"
require_file "$SRC/init.ld"
require_file "$SRC/mkflat.py"
require_tool "${CROSS_COMPILE}gcc"
require_tool "${CROSS_COMPILE}objcopy"
require_tool python3
require_tool cpio
require_tool gzip

mkdir -p "$OUT"
rm -rf "$ROOT"
mkdir -p "$ROOT"

log "building flat init in $OUT"
{
  "${CROSS_COMPILE}gcc" \
    -march=rv32ima -mabi=ilp32 -nostdlib -static -mno-relax \
    -Wl,--build-id=none -Wl,-T,"$SRC/init.ld" \
    -o "$OUT/init.elf" "$SRC/init.S"

  "${CROSS_COMPILE}objcopy" -O binary "$OUT/init.elf" "$OUT/init.bin"
  python3 "$SRC/mkflat.py" "$OUT/init.bin" "$OUT/init.flat"

  install -m 0755 "$OUT/init.flat" "$ROOT/init"
  ( cd "$ROOT" && find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip -9 ) \
    > "$OUT/initramfs.cpio.gz"
} 2>&1 | tee "$LOG_FILE"

require_file "$OUT/init.flat"
require_file "$OUT/initramfs.cpio.gz"
grep -q 'TERNARY_LINUX_OK' "$OUT/init.bin" ||
  die "flat init does not contain TERNARY_LINUX_OK"

{
  printf 'INIT_FLAT=%q\n' "$OUT/init.flat"
  printf 'INITRAMFS=%q\n' "$OUT/initramfs.cpio.gz"
} > "$OUT/userspace.env"

log "flat init: $OUT/init.flat"
log "initramfs: $OUT/initramfs.cpio.gz"
