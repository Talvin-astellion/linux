#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

require_file "$SD_SWITCH_INITRAMFS"
export SD_INITRAMFS="$SD_SWITCH_INITRAMFS"
export REQUIRED_MARKERS="INITRD_OK SWITCH_ROOT_OK"
export RUN_MODE="switch-root"
exec bash "$SCRIPT_DIR/systemd-run.sh"
