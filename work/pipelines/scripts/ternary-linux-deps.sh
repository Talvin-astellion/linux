#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ternary-linux-lib.sh
source "$SCRIPT_DIR/ternary-linux-lib.sh"

TL_ROOT="${TL_ROOT:-$BUILD_ROOT/ternary-linux}"
TL_ENV="${TL_ENV:-$TL_ROOT/toolchain.env}"
TC_ROOT="$TL_ROOT/toolchain"
TC_USR="$TC_ROOT/usr"
TC_WRAP="$TC_ROOT/wrap"
APT_CACHE="$TL_ROOT/apt"
PACKAGES=(
  gcc-riscv64-unknown-elf
  binutils-riscv64-unknown-elf
  qemu-system-misc
  qemu-system-common
  qemu-system-data
  device-tree-compiler
  opensbi
)

find_opensbi() {
  local candidate
  for candidate in \
    "${OPENSBI:-}" \
    /usr/lib/riscv32-linux-gnu/opensbi/generic/fw_dynamic.bin \
    "$TC_USR/lib/riscv32-linux-gnu/opensbi/generic/fw_dynamic.bin" \
    /usr/share/qemu/opensbi-riscv32-generic-fw_dynamic.bin; do
    [[ -n "$candidate" && -f "$candidate" ]] && { printf '%s' "$candidate"; return 0; }
  done
  return 1
}

cross_candidate() {
  local prefix="$1"
  local gcc="${prefix}gcc"
  local libgcc

  have "$gcc" || return 1
  libgcc="$("$gcc" -march=rv32ima -mabi=ilp32 -print-libgcc-file-name 2>/dev/null)" || return 1
  [[ -f "$libgcc" ]] || return 1
  printf '%s' "$prefix"
}

system_cross() {
  local prefix
  for prefix in riscv64-unknown-elf- riscv64-linux-gnu-; do
    if prefix="$(cross_candidate "$prefix")"; then
      printf '%s' "$prefix"
      return 0
    fi
  done
  return 1
}

system_ready() {
  local cross="$1"
  have qemu-system-riscv32 &&
    have dtc &&
    find_opensbi >/dev/null &&
    [[ -n "$cross" ]]
}

install_with_apt() {
  local sudo_cmd=()
  have apt-get || die "apt-get not found; install dependencies manually"
  if (( EUID != 0 )); then
    have sudo || die "sudo not found; install dependencies manually"
    sudo -n true 2>/dev/null || die "sudo requires a password; rerun without root to use local extraction"
    sudo_cmd=(sudo)
  fi
  "${sudo_cmd[@]}" apt-get update
  "${sudo_cmd[@]}" apt-get install -y --no-install-recommends "${PACKAGES[@]}"
}

download_packages() {
  local pkg deb

  mkdir -p "$APT_CACHE"
  for pkg in "${PACKAGES[@]}"; do
    compgen -G "$APT_CACHE/${pkg}_*.deb" >/dev/null && continue
    log "downloading $pkg"
    ( cd "$APT_CACHE" && apt-get download "$pkg" )
  done
  for pkg in "${PACKAGES[@]}"; do
    compgen -G "$APT_CACHE/${pkg}_*.deb" >/dev/null || die "download failed for $pkg"
    for deb in "$APT_CACHE/${pkg}_"*.deb; do
      dpkg-deb -x "$deb" "$TC_ROOT"
    done
  done
}

write_wrapper() {
  local tool="$1"
  local real="$TC_USR/bin/$tool"

  cat > "$TC_WRAP/$tool" <<EOF
#!/bin/sh
export GCC_EXEC_PREFIX="$TC_USR/lib/gcc/"
exec "$real" "\$@"
EOF
  chmod +x "$TC_WRAP/$tool"
}

prepare_local_toolchain() {
  local libgcc

  require_tool apt-get
  require_tool dpkg-deb
  mkdir -p "$TC_ROOT" "$TC_USR" "$TC_WRAP"
  download_packages
  write_wrapper riscv64-unknown-elf-gcc
  write_wrapper riscv64-unknown-elf-cpp

  libgcc="$("$TC_WRAP/riscv64-unknown-elf-gcc" -march=rv32ima -mabi=ilp32 \
    -print-libgcc-file-name)"
  [[ -f "$libgcc" ]] || die "missing rv32ima/ilp32 libgcc: $libgcc"
  "$TC_USR/bin/qemu-system-riscv32" --version >/dev/null 2>&1 || {
    ldd "$TC_USR/bin/qemu-system-riscv32" >&2 || true
    die "local qemu-system-riscv32 cannot run; install its runtime libraries"
  }
  tl_write_env "$TC_WRAP:$TC_USR/bin" "riscv64-unknown-elf-" \
    "$TC_USR/bin/qemu-system-riscv32" "$TC_USR/bin/dtc" "$(find_opensbi)" \
    "$TC_USR/share/qemu"
}

main() {
  local cross opensbi

  require_tool make
  require_tool bash
  require_tool gzip
  require_tool cpio

  cross="$(system_cross || true)"
  if system_ready "$cross"; then
    opensbi="$(find_opensbi)"
    tl_write_env "" "$cross" "$(command -v qemu-system-riscv32)" \
      "$(command -v dtc)" "$opensbi" ""
    log "using system toolchain: ${cross}gcc"
  else
    if have apt-get && (( EUID == 0 )); then
      install_with_apt
      cross="$(system_cross)" || die "toolchain still missing after apt install"
      opensbi="$(find_opensbi)" || die "riscv32 OpenSBI firmware not found"
      tl_write_env "" "$cross" "$(command -v qemu-system-riscv32)" \
        "$(command -v dtc)" "$opensbi" ""
    else
      log "system toolchain incomplete; extracting packages into $TC_ROOT"
      prepare_local_toolchain
    fi
  fi

  # shellcheck disable=SC1090
  source "$TL_ENV"
  log "toolchain: ${CROSS_COMPILE}gcc"
  log "qemu: $QEMU_SYSTEM_RISCV32"
  log "opensbi: $OPENSBI"
  log "env: $TL_ENV"
}

main "$@"
