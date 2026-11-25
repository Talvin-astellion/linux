#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

VERIFIED=0
if [[ "${1:-}" == "verified" ]]; then
  VERIFIED=1
fi

MANIFEST="$(manifest_path)"
mkdir -p "$BUILD_DIR"
tmp="$(mktemp "$MANIFEST.tmp.XXXXXX")"
trap 'rm -f "$tmp"' EXIT

sysroot="$(cross_sysroot_path)"
libc=""
if libc="$(sysroot_libc_path "$sysroot")"; then :; else libc=""; fi
libc_sha=""
if [[ -n "$libc" ]]; then
  libc_sha="$(sha256_of "$libc")"
fi
config_sha="missing"
if [[ -f "$KERNEL_CONFIG" ]]; then
  config_sha="$(sha256_of "$KERNEL_CONFIG")"
fi
required_markers="${REQUIRED_MARKERS:-SYSTEMD_OK}"
log_sha="missing"
if (( VERIFIED )) && [[ -f "$SD_LOG" ]]; then
  log_sha="$(sha256_of "$SD_LOG")"
fi

{
  printf 'manifest_version=1\n'
  printf 'run_verified=%s\n' "$VERIFIED"
  printf 'run_mode=%s\n' "${RUN_MODE:-initrd}"
  printf 'generated_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'emulator=%s\n' "$EMULATOR"
  printf 'emulator_sha256=%s\n' "$(sha256_of "$EMULATOR")"
  printf 'repo_git=%s\n' "$(git_rev "$REPO_ROOT")"
  printf 'repo_dirty=%s\n' "$(git_dirty "$REPO_ROOT")"
  printf 'kernel_image=%s\n' "$KERNEL_IMAGE"
  printf 'kernel_image_sha256=%s\n' "$(sha256_of "$KERNEL_IMAGE")"
  printf 'kernel_release=%s\n' "$(kernel_release)"
  printf 'kernel_config=%s\n' "$KERNEL_CONFIG"
  printf 'kernel_config_sha256=%s\n' "$config_sha"
  printf 'dtb=%s\n' "$DTB"
  printf 'dtb_sha256=%s\n' "$(sha256_of "$DTB")"
  printf 'initrd=%s\n' "$SD_INITRAMFS"
  printf 'initrd_sha256=%s\n' "$(sha256_of "$SD_INITRAMFS")"
  printf 'log=%s\n' "$SD_LOG"
  printf 'required_markers=%s\n' "$required_markers"
  printf 'log_sha256=%s\n' "$log_sha"
  printf 'systemd_src=%s\n' "$SYSTEMD_SRC"
  printf 'systemd_git=%s\n' "$(git_rev "$SYSTEMD_SRC")"
  printf 'systemd_dirty=%s\n' "$(git_dirty "$SYSTEMD_SRC")"
  printf 'systemd_version=%s\n' "$(systemd_version)"
  printf 'cross_cc=%s\n' "$(cross_cc)"
  printf 'cross_cc_version=%s\n' "$(cross_cc_version)"
  printf 'cross_sysroot=%s\n' "$sysroot"
  printf 'cross_sysroot_libc=%s\n' "${libc:-missing}"
  printf 'cross_sysroot_libc_sha256=%s\n' "${libc_sha:-missing}"
  printf 'time_mode=%s\n' "${TIME_MODE:-}"
  printf 'run_timeout=%s\n' "${RUN_TIMEOUT:-}"
  printf 'max_insn=%s\n' "${MAX_INSN:-}"
} > "$tmp"

mv -f "$tmp" "$MANIFEST"
trap - EXIT
log "manifest: $MANIFEST (run_verified=$VERIFIED)"
