#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-linux-lib.sh
source "$SCRIPT_DIR/ternary-linux-lib.sh"

tl_load_env

BUILD_DIR="${TL_GUEST_BUILD:-$TL_ROOT/guest}"
BUILD_ENV="$BUILD_DIR/build.env"
USERSPACE_ENV="$TL_ROOT/userspace/userspace.env"
LOG_FILE="${RUN_LOG:-$BUILD_DIR/emulator.log}"
RUN_TIMEOUT="${RUN_TIMEOUT:-120}"
MAX_INSN="${MAX_INSN:-60000000}"
TIME_MODE="${TIME_MODE:-inst}"

require_file "$BUILD_ENV"
initramfs_override="${INITRAMFS:-}"
dtb_override="${DTB:-}"
# shellcheck disable=SC1090
source "$BUILD_ENV"
INITRAMFS="${initramfs_override:-${INITRAMFS:-}}"
DTB="${dtb_override:-${EMU_DTB:-$TL_ROOT/dtb/ternary-emu.dtb}}"
require_file "$KERNEL_IMAGE"

if [[ -z "${INITRAMFS:-}" && -f "$USERSPACE_ENV" ]]; then
  # shellcheck disable=SC1090
  source "$USERSPACE_ENV"
fi
INITRAMFS="${INITRAMFS:-$TL_ROOT/userspace/initramfs.cpio.gz}"
require_file "$INITRAMFS"
require_file "$TL_EMU_DTS"

DTB="${DTB:-$TL_ROOT/dtb/ternary-emu.dtb}"
if [[ ! -f "$DTB" ]]; then
  DTB="$(tl_build_dtb "$TL_EMU_DTS" "$DTB")"
fi
require_file "$DTB"

[[ -x "$TL_EMU" ]] ||
  die "missing ternary emulator: $TL_EMU (set TL_EMU or build ~/System/ternary/emu/tern-linux)"

EMU_ARGS=(
  --kernel "$KERNEL_IMAGE"
  --dtb "$DTB"
  --initrd "$INITRAMFS"
  --max-insn "$MAX_INSN"
  --quiet
)
if [[ "$TIME_MODE" == inst ]]; then
  EMU_ARGS+=(--time-inst)
elif [[ "$TIME_MODE" != host ]]; then
  die "TIME_MODE must be 'inst' or 'host'"
fi

log "booting $KERNEL_RELEASE under $TL_EMU"
set +e
timeout "$RUN_TIMEOUT" "$TL_EMU" "${EMU_ARGS[@]}" > "$LOG_FILE" 2>&1
rc=$?
set -e

for pattern in 'Linux version' 'SBI specification' 'TERNARY_LINUX_OK'; do
  grep -q "$pattern" "$LOG_FILE" ||
    die "emulator marker not found: $pattern (log: $LOG_FILE)"
done

if [[ $rc -ne 0 && $rc -ne 2 && $rc -ne 124 ]]; then
  log "emulator exited with $rc after a successful boot"
fi

log "emulator boot OK: $LOG_FILE"
