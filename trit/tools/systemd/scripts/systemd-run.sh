#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

export RUN_MODE
invalidate_manifest

require_file "$KERNEL_IMAGE"
require_file "$DTB"
require_file "$SD_INITRAMFS"
require_file "$EMULATOR"

RUN_TIMEOUT="${RUN_TIMEOUT:-900}"
MAX_INSN="${MAX_INSN:-20000000000}"
TIME_MODE="${TIME_MODE:-inst}"
STATS="${STATS:-0}"
KERNEL_ADDR="${KERNEL_ADDR:-0x80000000}"
MANIFEST="$(manifest_path)"
export MANIFEST RUN_TIMEOUT MAX_INSN TIME_MODE

EMU_ARGS=(
  --kernel "$KERNEL_IMAGE"
  --kernel-addr "$KERNEL_ADDR"
  --dtb "$DTB"
  --initrd "$SD_INITRAMFS"
  --max-insn "$MAX_INSN"
  --quiet
)
if [[ "$TIME_MODE" == inst ]]; then
  EMU_ARGS+=(--time-inst)
elif [[ "$TIME_MODE" != host ]]; then
  die "TIME_MODE must be 'host' or 'inst'"
fi
if [[ "$STATS" == 1 ]]; then
  EMU_ARGS+=(--stats)
fi

mkdir -p "$BUILD_DIR"
"$SCRIPT_DIR/systemd-manifest.sh"
log "booting systemd initrd ($RUN_MODE) under $EMULATOR"
set +e
timeout "$RUN_TIMEOUT" "$EMULATOR" "${EMU_ARGS[@]}" > "$SD_LOG" 2>&1
rc=$?
set -e

grep -q 'Linux version' "$SD_LOG" ||
  die "marker not found: Linux version (log: $SD_LOG)"
read -r -a required_markers <<< "${REQUIRED_MARKERS:-SYSTEMD_OK}"
previous_line=0
for marker in "${required_markers[@]}"; do
  marker_line="$(awk -v marker="$marker" -v after="$previous_line" '
    NR > after { sub(/\r$/, ""); if ($0 == marker) { print NR; exit } }
  ' "$SD_LOG")"
  [[ -n "$marker_line" ]] ||
    die "missing or out-of-order marker: $marker (log: $SD_LOG)"
  previous_line="$marker_line"
done

for marker in 'SYSTEMD_POWEROFF_FAILED' 'Kernel panic'; do
  if grep -q "$marker" "$SD_LOG"; then
    die "unexpected marker: $marker (log: $SD_LOG)"
  fi
done

if (( rc != 0 )); then
  if (( rc == 124 )); then
    die "emulator timed out after ${RUN_TIMEOUT}s without poweroff (log: $SD_LOG)"
  fi
  die "emulator exited with status $rc (log: $SD_LOG)"
fi

"$SCRIPT_DIR/systemd-manifest.sh" verified
log "systemd PID 1 booted and powered off cleanly: $SD_LOG"
log "artifact manifest: $MANIFEST"
