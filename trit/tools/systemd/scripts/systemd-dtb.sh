#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SD_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib.sh"

require_tool "$DTC"
require_file "$SD_ROOT/dts/ternary-mmu.dts"

mkdir -p "$BUILD_DIR"
"$DTC" -I dts -O dtb -o "$DTB" "$SD_ROOT/dts/ternary-mmu.dts"
require_file "$DTB"
log "dtb: $DTB"
