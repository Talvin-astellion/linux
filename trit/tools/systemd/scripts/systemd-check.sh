#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

MODE="${1:-report}"

if [[ "$MODE" == manifest ]]; then
  MANIFEST="$(manifest_path)"
  require_file "$MANIFEST"

  manifest_field() {
    sed -n "s/^$1=//p" "$MANIFEST" | head -n 1
  }

  EMULATOR="$(manifest_field emulator)"
  KERNEL_IMAGE="$(manifest_field kernel_image)"
  DTB="$(manifest_field dtb)"
  manifest_initrd="$(manifest_field initrd)"
  for path in "$EMULATOR" "$KERNEL_IMAGE" "$DTB" "$manifest_initrd"; do
    [[ -n "$path" ]] || die "manifest has an empty artifact path: $MANIFEST"
    require_file "$path"
  done

  verify_manifest_value() {
    local key="$1" current="$2" expected

    expected="$(manifest_field "$key")"
    [[ -n "$expected" ]] || die "manifest missing $key in $MANIFEST"
    if [[ "$expected" != "$current" ]]; then
      die "manifest mismatch for $key (recorded: $expected, current: $current)"
    fi
    log "OK: manifest $key"
  }

  verify_manifest_sha() {
    verify_manifest_value "$1_sha256" "$(sha256_of "$2")"
  }

  if [[ "$(manifest_field run_verified)" != 1 ]]; then
    die "manifest does not describe a successful run: $MANIFEST"
  fi

  verify_manifest_value manifest_version 1
  verify_manifest_value run_mode "$RUN_MODE"
  verify_manifest_value repo_git "$(git_rev "$REPO_ROOT")"
  verify_manifest_value repo_dirty "$(git_dirty "$REPO_ROOT")"

  verify_manifest_sha emulator "$EMULATOR"
  verify_manifest_sha kernel_image "$KERNEL_IMAGE"
  verify_manifest_sha dtb "$DTB"
  verify_manifest_sha initrd "$manifest_initrd"
  verify_manifest_value kernel_release "$(kernel_release)"

  config_sha="$(manifest_field kernel_config_sha256)"
  if [[ -n "$config_sha" && "$config_sha" != missing ]]; then
    verify_manifest_value kernel_config_sha256 "$(sha256_of "$KERNEL_CONFIG")"
  else
    log "SKIP: manifest kernel_config_sha256"
  fi

  manifest_log="$(manifest_field log)"
  [[ -n "$manifest_log" ]] || die "manifest missing log in $MANIFEST"
  require_file "$manifest_log"
  verify_manifest_sha log "$manifest_log"
  manifest_required="$(manifest_field required_markers)"
  [[ -n "$manifest_required" ]] || die "manifest missing required_markers in $MANIFEST"
  grep -q 'Linux version' "$manifest_log" ||
    die "marker not found: Linux version (log: $manifest_log)"
  read -r -a required_markers <<< "$manifest_required"
  previous_line=0
  for marker in "${required_markers[@]}"; do
    marker_line="$(awk -v marker="$marker" -v after="$previous_line" '
      NR > after { sub(/\r$/, ""); if ($0 == marker) { print NR; exit } }
    ' "$manifest_log")"
    [[ -n "$marker_line" ]] ||
      die "missing or out-of-order manifest marker: $marker (log: $manifest_log)"
    previous_line="$marker_line"
  done
  for marker in 'SYSTEMD_POWEROFF_FAILED' 'Kernel panic'; do
    if grep -q "$marker" "$manifest_log"; then
      die "unexpected marker in manifest log: $marker (log: $manifest_log)"
    fi
  done

  verify_manifest_value systemd_git "$(git_rev "$SYSTEMD_SRC")"
  verify_manifest_value systemd_dirty "$(git_dirty "$SYSTEMD_SRC")"
  verify_manifest_value systemd_version "$(systemd_version)"
  verify_manifest_value cross_cc "$(cross_cc)"
  if command -v "$(cross_cc)" >/dev/null 2>&1; then
    verify_manifest_value cross_cc_version "$(cross_cc_version)"
  else
    log "SKIP: manifest cross_cc_version (compiler not on PATH)"
  fi
  verify_manifest_value cross_sysroot "$(cross_sysroot_path)"
  manifest_libc="$(manifest_field cross_sysroot_libc)"
  if [[ -n "$manifest_libc" && "$manifest_libc" != missing ]]; then
    require_file "$manifest_libc"
    verify_manifest_value cross_sysroot_libc "$manifest_libc"
    verify_manifest_value cross_sysroot_libc_sha256 "$(sha256_of "$manifest_libc")"
  else
    log "SKIP: manifest cross_sysroot_libc_sha256"
  fi

  log "MANIFEST_OK"
  exit 0
fi

missing=0

check_path() {
  local label="$1"
  local path="$2"
  local kind="$3"

  if [[ "$kind" == dir ]]; then
    if [[ -d "$path" ]]; then
      log "OK: $label"
    else
      log "MISSING: $label ($path)"
      missing=1
    fi
  else
    if [[ -f "$path" ]]; then
      log "OK: $label"
    else
      log "MISSING: $label ($path)"
      missing=1
    fi
  fi
}

check_tool() {
  local label="$1"
  local tool="$2"

  if command -v "$tool" >/dev/null 2>&1; then
    log "OK: $label"
  else
    log "MISSING: $label ($tool)"
    missing=1
  fi
}

check_path "systemd source" "$SYSTEMD_SRC/meson.build" file
check_path "systemd PID 1 source" "$SYSTEMD_SRC/src/core/main.c" file
check_tool "meson" "$MESON"
check_tool "ninja" "$NINJA"
check_tool "cpio" cpio
check_tool "gzip" gzip
check_tool "awk" awk

for tool in gcc ar objcopy strip objdump readelf pkg-config; do
  check_tool "cross $tool" "${CROSS_COMPILE}${tool}"
done

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
tmp_c="$tmp_dir/systemd-cross.c"
printf 'int main(void) { return 0; }\n' > "$tmp_c"
if "$(cross_cc)" $(cross_flags) -c "$tmp_c" -o "$tmp_dir/systemd-cross.o" >/dev/null 2>&1; then
  log "OK: cross compiler supports rv32ima/ilp32 and 64-bit time_t"
else
  log "MISSING: cross compiler cannot build rv32ima/ilp32 with 64-bit time_t"
  missing=1
fi

check_path "kernel Image" "$KERNEL_IMAGE" file
if [[ -f "$DTB" ]]; then
  log "OK: emulator DTB"
else
  log "MISSING: emulator DTB ($DTB); run make -C tools/systemd dtb"
  missing=1
fi

if [[ -f "$KERNEL_CONFIG" ]]; then
  kernel_config_fragment="$SD_ROOT/configs/riscv32-systemd-mmu.config"
  for option in \
    CONFIG_MMU=y CONFIG_RISCV_SBI=y CONFIG_HVC_RISCV_SBI=y \
    CONFIG_SERIAL_EARLYCON_RISCV_SBI=y CONFIG_CMDLINE_FORCE=y \
    CONFIG_BLK_DEV_INITRD=y CONFIG_RD_GZIP=y CONFIG_BINFMT_ELF=y \
    CONFIG_BINFMT_SCRIPT=y CONFIG_PROC_FS=y CONFIG_SYSFS=y CONFIG_TMPFS=y \
    CONFIG_DEVTMPFS=y CONFIG_DEVTMPFS_MOUNT=y CONFIG_CGROUPS=y \
    CONFIG_CGROUP_SCHED=y CONFIG_FAIR_GROUP_SCHED=y CONFIG_CGROUP_FREEZER=y \
    CONFIG_MEMCG=y CONFIG_CPUSETS=y CONFIG_SIGNALFD=y CONFIG_TIMERFD=y \
    CONFIG_EPOLL=y CONFIG_EVENTFD=y CONFIG_INOTIFY_USER=y CONFIG_UNIX=y \
    '# CONFIG_SMP is not set' '# CONFIG_RELOCATABLE is not set' \
    '# CONFIG_RANDOMIZE_BASE is not set' '# CONFIG_RISCV_ISA_C is not set' \
    '# CONFIG_FPU is not set'; do
    if grep -Fxq "$option" "$KERNEL_CONFIG"; then
      log "OK: kernel $option"
    elif [[ -f "$kernel_config_fragment" ]] && grep -Fxq "$option" "$kernel_config_fragment"; then
      log "OK: kernel $option (fragment)"
    else
      log "MISSING: kernel $option in $KERNEL_CONFIG"
      missing=1
    fi
  done
else
  log "MISSING: kernel config ($KERNEL_CONFIG)"
  missing=1
fi

if [[ -x "$EMULATOR" ]]; then
  log "OK: emulator $EMULATOR"
else
  log "MISSING: executable emulator ($EMULATOR); run make -C emu linux"
  missing=1
fi

if (( missing )); then
  if [[ "$MODE" == require ]]; then
    die "prerequisites are missing; see tools/systemd/README.md"
  fi
  log "READY=0"
  exit 0
fi

log "READY=1"
