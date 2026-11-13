#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-linux-lib.sh
source "$SCRIPT_DIR/ternary-linux-lib.sh"

tl_load_env

require_tool "${CROSS_COMPILE}gcc"
require_tool "${CROSS_COMPILE}readelf"
require_tool "$QEMU_SYSTEM_RISCV32"
require_tool "$DTC"
require_file "$OPENSBI"
require_file "$TL_CONFIG"
require_file "$TL_DTS"
require_file "$TL_EMU_DTS"
require_file "$TL_USERSPACE/init.S"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

printf 'int check(void) { return 0; }\n' > "$tmp/check.c"
"${CROSS_COMPILE}gcc" -march=rv32ima -mabi=ilp32 -c "$tmp/check.c" -o "$tmp/check.o"
"${CROSS_COMPILE}readelf" -h "$tmp/check.o" | grep -q 'Class:.*ELF32'
"${CROSS_COMPILE}readelf" -h "$tmp/check.o" | grep -q 'Machine:.*RISC-V'

libgcc="$("${CROSS_COMPILE}gcc" -march=rv32ima -mabi=ilp32 -print-libgcc-file-name)"
require_file "$libgcc"

"$QEMU_SYSTEM_RISCV32" --version | grep -q '^QEMU emulator version'
"$QEMU_SYSTEM_RISCV32" -M help 2>/dev/null | grep -q '^virt '
"$DTC" --version >/dev/null
"$DTC" -I dts -O dtb -o "$tmp/check.dtb" "$TL_DTS" 2>/dev/null

for tool in make bison flex bc cpio gzip perl python3 rsync git; do
  require_tool "$tool"
done

log "toolchain: ${CROSS_COMPILE}gcc"
log "libgcc: $libgcc"
log "qemu: $QEMU_SYSTEM_RISCV32"
log "opensbi: $OPENSBI"
log "dtc: $DTC"
if [[ -x "$TL_EMU" ]]; then
  log "emulator: $TL_EMU"
else
  log "emulator: not found at $TL_EMU (optional until ternary-linux-run)"
fi
log "ternary Linux guest prerequisites OK"
