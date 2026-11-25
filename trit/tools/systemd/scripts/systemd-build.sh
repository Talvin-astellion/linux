#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

"$SCRIPT_DIR/systemd-check.sh" require >/dev/null

require_dir "$SYSTEMD_SRC"
require_tool "$MESON"
require_tool "$NINJA"

mkdir -p "$BUILD_DIR" "$SD_BUILD"

CC_BIN="$(cross_cc)"
AR_BIN="${CROSS_COMPILE}ar"
STRIP_BIN="${CROSS_COMPILE}strip"
OBJCOPY_BIN="${CROSS_COMPILE}objcopy"
PKGCONFIG_BIN="${CROSS_COMPILE}pkg-config"
if ! command -v "$PKGCONFIG_BIN" >/dev/null 2>&1; then
  PKGCONFIG_BIN=pkg-config
fi

{
  printf '[binaries]\n'
  printf "c = '%s'\n" "$CC_BIN"
  printf "ar = '%s'\n" "$AR_BIN"
  printf "strip = '%s'\n" "$STRIP_BIN"
  printf "objcopy = '%s'\n" "$OBJCOPY_BIN"
  printf "pkg-config = '%s'\n" "$PKGCONFIG_BIN"
  printf '\n[properties]\n'
  printf 'needs_exe_wrapper = true\n'
  printf '\n[host_machine]\n'
  printf "system = 'linux'\n"
  printf "cpu_family = 'riscv32'\n"
  printf "cpu = 'riscv32'\n"
  printf "endian = 'little'\n"
  printf '\n[built-in options]\n'
  printf 'c_args = %s\n' "$(python3 -c 'import sys; print("[" + ", ".join(repr(x) for x in sys.argv[1].split()) + "]")' "$(cross_flags)")"
  printf 'c_link_args = %s\n' "$(python3 -c 'import sys; print("[" + ", ".join(repr(x) for x in sys.argv[1].split()) + "]")' "$(cross_flags)")"
} > "$BUILD_DIR/meson-cross-rv32.txt"

MESON_OPTIONS=(
  --prefix=/usr
  --buildtype=plain
  -Dtests=false
  -Dfuzz-tests=false
  -Dinstall-tests=false
  -Dman=disabled
  -Dhtml=disabled
  -Dtranslations=false
  -Dbootloader=disabled
  -Dukify=disabled
  -Dfirstboot=false
  -Dlocaled=false
  -Dlogind=false
  -Dmachined=false
  -Dnetworkd=false
  -Dresolve=false
  -Dtimedated=false
  -Dportabled=false
  -Dhomed=disabled
  -Dimportd=disabled
  -Dremote=disabled
  -Dpam=disabled
  -Daudit=disabled
  -Dselinux=disabled
  -Dseccomp=disabled
  -Dpolkit=disabled
  -Dblkid=disabled
  -Dkmod=disabled
  -Dtpm2=disabled
  -Dqrencode=disabled
  -Dmicrohttpd=disabled
  -Delfutils=disabled
)

log "configuring systemd cross build in $SD_BUILD"
"$MESON" setup "$SD_BUILD" "$SYSTEMD_SRC" \
  --cross-file "$BUILD_DIR/meson-cross-rv32.txt" "${MESON_OPTIONS[@]}"

log "compiling systemd"
"$MESON" compile -C "$SD_BUILD"

rm -rf "$SD_SYSROOT"
mkdir -p "$SD_SYSROOT"
log "installing systemd into $SD_SYSROOT"
DESTDIR="$SD_SYSROOT" "$MESON" install -C "$SD_BUILD" --no-rebuild

require_file "$SD_SYSROOT/usr/lib/systemd/systemd"
log "systemd binary: $SD_SYSROOT/usr/lib/systemd/systemd"
