#!/usr/bin/env bash
set -euo pipefail

SD_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SD_ROOT="$(cd -- "$SD_SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd -- "$SD_ROOT/../.." && pwd)"

SYSTEMD_SRC="${SYSTEMD_SRC:-$HOME/System/systemd}"
LINUX_SRC="${LINUX_SRC:-$HOME/System/linux}"
BUILD_DIR="${BUILD_DIR:-$SD_ROOT/build}"
CROSS_COMPILE="${CROSS_COMPILE:-riscv32-linux-gnu-}"
CROSS_SYSROOT="${CROSS_SYSROOT:-}"
EMULATOR="${EMULATOR:-$REPO_ROOT/emu/tern-linux}"
KERNEL_IMAGE="${KERNEL_IMAGE:-$LINUX_SRC/work/build/ternary-linux-mmu/arch/riscv/boot/Image}"
DTB="${DTB:-$BUILD_DIR/ternary-mmu.dtb}"
KERNEL_CONFIG="${KERNEL_CONFIG:-$LINUX_SRC/work/build/ternary-linux-mmu/.config}"
MESON="${MESON:-meson}"
NINJA="${NINJA:-ninja}"
DTC="${DTC:-dtc}"
RUN_MODE="${RUN_MODE:-initrd}"

SD_BUILD="$BUILD_DIR/systemd"
SD_SYSROOT="$BUILD_DIR/sysroot"
SD_INITRD_ROOT="$BUILD_DIR/initrd-root"
SD_INITRAMFS="${SD_INITRAMFS:-$BUILD_DIR/initramfs-systemd.cpio.gz}"
SD_SWITCH_ROOT_ROOT="$BUILD_DIR/switch-root-root"
SD_SWITCH_INITRD_ROOT="$BUILD_DIR/switch-root-initrd-root"
SD_SWITCH_INITRAMFS="$BUILD_DIR/initramfs-switch-root.cpio.gz"
SD_LOG="${SD_LOG:-$BUILD_DIR/emulator-${RUN_MODE}.log}"

log() {
  printf '[systemd-initrd] %s\n' "$*"
}

die() {
  printf '[systemd-initrd] ERROR: %s\n' "$*" >&2
  exit 1
}

case "$RUN_MODE" in
  initrd|switch-root) ;;
  *) die "RUN_MODE must be 'initrd' or 'switch-root'" ;;
esac

require_file() {
  [[ -f "$1" ]] || die "missing file: $1"
}

require_dir() {
  [[ -d "$1" ]] || die "missing directory: $1"
}

require_tool() {
  command -v "$1" >/dev/null 2>&1 || die "missing tool: $1"
}

cross_flags() {
  local flags="-march=rv32ima -mabi=ilp32 -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64"
  if [[ -n "$CROSS_SYSROOT" ]]; then
    flags+=" --sysroot=$CROSS_SYSROOT"
  fi
  printf '%s' "$flags"
}

cross_cc() {
  printf '%s' "${CROSS_COMPILE}gcc"
}

manifest_path() {
  printf '%s' "${MANIFEST:-$BUILD_DIR/manifest-${RUN_MODE}.txt}"
}

invalidate_manifest() {
  local manifest tmp

  manifest="$(manifest_path)"
  mkdir -p "$(dirname -- "$manifest")"
  tmp="$(mktemp "$manifest.tmp.XXXXXX")"
  printf 'manifest_version=1\nrun_verified=0\nrun_mode=%s\n' "$RUN_MODE" > "$tmp"
  mv -f "$tmp" "$manifest"
}

sha256_of() {
  require_file "$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    die "missing SHA-256 tool (sha256sum or shasum)"
  fi
}

git_rev() {
  if command -v git >/dev/null 2>&1 &&
     git -C "$1" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C "$1" rev-parse HEAD 2>/dev/null || printf 'unknown'
  else
    printf 'unknown'
  fi
}

git_dirty() {
  if command -v git >/dev/null 2>&1 &&
     git -C "$1" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if [[ -n "$(git -C "$1" status --porcelain --untracked-files=no 2>/dev/null)" ]]; then
      printf '1'
    else
      printf '0'
    fi
  else
    printf 'unknown'
  fi
}

kernel_release() {
  local build release uts

  build="$(dirname -- "$KERNEL_CONFIG")"
  if [[ -f "$build/include/config/kernel.release" ]]; then
    tr -d '\n' < "$build/include/config/kernel.release"
    return 0
  fi
  uts="$build/include/generated/utsrelease.h"
  if [[ -f "$uts" ]]; then
    release="$(sed -n 's/^#define UTS_RELEASE "\(.*\)"/\1/p' "$uts" | head -n 1)"
    if [[ -n "$release" ]]; then
      printf '%s' "$release"
      return 0
    fi
  fi
  if [[ -f "$KERNEL_IMAGE" ]] && command -v strings >/dev/null 2>&1; then
    release="$(strings -a "$KERNEL_IMAGE" | awk '/^Linux version / { print $3; exit }')"
    if [[ -n "$release" ]]; then
      printf '%s' "$release"
      return 0
    fi
  fi
  printf 'unknown'
}

systemd_version() {
  local version=""

  if [[ -f "$SYSTEMD_SRC/meson.version" ]]; then
    version="$(tr -d '[:space:]' < "$SYSTEMD_SRC/meson.version")"
  fi
  if [[ -z "$version" && -f "$SYSTEMD_SRC/meson.build" ]]; then
    version="$(awk '/^project\(/,/^\)/' "$SYSTEMD_SRC/meson.build" |
      sed -n "s/^[[:space:]]*version[[:space:]]*:[[:space:]]*'\([^']*\)'.*/\1/p" |
      head -n 1)"
  fi
  printf '%s' "${version:-unknown}"
}

cross_cc_version() {
  local cc version

  cc="$(cross_cc)"
  if command -v "$cc" >/dev/null 2>&1; then
    version="$("$cc" --version 2>/dev/null | head -n 1)"
    printf '%s' "${version:-unknown}"
  else
    printf 'unknown'
  fi
}

cross_sysroot_path() {
  local cc

  if [[ -n "$CROSS_SYSROOT" ]]; then
    printf '%s' "$CROSS_SYSROOT"
    return 0
  fi
  cc="$(cross_cc)"
  if command -v "$cc" >/dev/null 2>&1; then
    printf '%s' "$("$cc" -print-sysroot 2>/dev/null || printf 'unknown')"
  else
    printf 'unknown'
  fi
}

sysroot_libc_path() {
  local root="$1" path

  [[ -n "$root" && "$root" != unknown ]] || return 1
  for path in "$root/usr/lib32/ilp32/libc.so.6" \
              "$root/lib32/ilp32/libc.so.6" \
              "$root/usr/lib32/libc.so.6" \
              "$root/lib/libc.so.6"; do
    if [[ -f "$path" ]]; then
      printf '%s' "$path"
      return 0
    fi
  done
  return 1
}

sd_export_env() {
  export SYSTEMD_SRC LINUX_SRC BUILD_DIR CROSS_COMPILE CROSS_SYSROOT
  export EMULATOR KERNEL_IMAGE DTB KERNEL_CONFIG MESON NINJA DTC RUN_MODE
  export SD_BUILD SD_SYSROOT SD_INITRD_ROOT SD_INITRAMFS SD_LOG
}
