#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

TL_ROOT="${TL_ROOT:-$BUILD_ROOT/ternary-linux}"
TL_ENV="${TL_ENV:-$TL_ROOT/toolchain.env}"
TL_CONFIG="$REPO_ROOT/work/pipelines/configs/ternary-linux-guest.config"
TL_DTS="$REPO_ROOT/work/ternary-linux/dts/ternary.dts"
TL_EMU_DTS="$REPO_ROOT/work/ternary-linux/dts/ternary-emu.dts"
TL_USERSPACE="$REPO_ROOT/work/ternary-linux/userspace"
TL_EMU="${TL_EMU:-${TERNARY_REPO:-$HOME/System/ternary}/emu/tern-linux}"

tl_load_env() {
  [[ -f "$TL_ENV" ]] || die "missing $TL_ENV (run work/pipelines/scripts/ternary-linux-deps.sh)"
  # shellcheck disable=SC1090
  source "$TL_ENV"
  if [[ -n "${TL_PATH_PREFIX:-}" ]]; then
    export PATH="$TL_PATH_PREFIX:$PATH"
  fi
  : "${CROSS_COMPILE:=riscv64-unknown-elf-}"
  : "${QEMU_SYSTEM_RISCV32:=qemu-system-riscv32}"
  : "${DTC:=dtc}"
  export CROSS_COMPILE QEMU_SYSTEM_RISCV32 DTC
}

tl_require_guest() {
  require_file "$1"
  require_file "$TL_CONFIG"
  require_file "$TL_DTS"
}

tl_build_dtb() {
  local dts="${1:-$TL_DTS}"
  local dtb="${2:-$TL_ROOT/dtb/$(basename "$dts" .dts).dtb}"

  require_tool "$DTC"
  mkdir -p "$(dirname "$dtb")"
  "$DTC" -I dts -O dtb -o "$dtb" "$dts"
  printf '%s\n' "$dtb"
}

tl_write_env() {
  local path_prefix="$1"
  local cross="$2"
  local qemu="$3"
  local dtc="$4"
  local opensbi="$5"
  local qemu_data="${6:-}"

  mkdir -p "$TL_ROOT"
  {
    printf 'TL_PATH_PREFIX=%q\n' "$path_prefix"
    printf 'CROSS_COMPILE=%q\n' "$cross"
    printf 'QEMU_SYSTEM_RISCV32=%q\n' "$qemu"
    printf 'DTC=%q\n' "$dtc"
    printf 'OPENSBI=%q\n' "$opensbi"
    printf 'QEMU_DATA_DIR=%q\n' "$qemu_data"
  } > "$TL_ENV"
}
