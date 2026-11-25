#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SD_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

require_file "$SD_SYSROOT/usr/lib/systemd/systemd"
require_file "$SD_SYSROOT/usr/bin/systemctl"
require_tool awk
require_tool cpio
require_tool gzip
require_tool "$(cross_cc)"
require_tool "${CROSS_COMPILE}objdump"
require_tool "${CROSS_COMPILE}readelf"

rm -rf "$SD_INITRD_ROOT"
mkdir -p "$SD_INITRD_ROOT"/{bin,dev,etc/systemd/system/sysinit.target.wants,lib,proc,run,sys,usr/bin,usr/lib/systemd/system,var}

install -m 0755 "$SD_SYSROOT/usr/lib/systemd/systemd" \
  "$SD_INITRD_ROOT/usr/lib/systemd/systemd"
ln -sfn usr/lib/systemd/systemd "$SD_INITRD_ROOT/init"

if [[ -f "$SD_SYSROOT/usr/lib/systemd/systemd-executor" ]]; then
  install -m 0755 "$SD_SYSROOT/usr/lib/systemd/systemd-executor" \
    "$SD_INITRD_ROOT/usr/lib/systemd/systemd-executor"
fi
install -m 0755 "$SD_SYSROOT/usr/bin/systemctl" \
  "$SD_INITRD_ROOT/usr/bin/systemctl"

"$(cross_cc)" $(cross_flags) -static -o "$SD_INITRD_ROOT/usr/bin/systemd-ok" \
  "$SD_ROOT/systemd-ok.c"

CATALOG_DIRS=(
  "$SD_SYSROOT/usr/lib"
  "$SD_SYSROOT/usr/lib/systemd"
  "$SD_SYSROOT/lib"
  "$SD_SYSROOT/usr/lib/riscv32-linux-gnu"
  "$SD_SYSROOT/lib/riscv32-linux-gnu"
)
if [[ -n "$CROSS_SYSROOT" ]]; then
  CATALOG_DIRS+=(
    "$CROSS_SYSROOT/lib"
    "$CROSS_SYSROOT/usr/lib"
    "$CROSS_SYSROOT/lib32/ilp32"
    "$CROSS_SYSROOT/usr/lib32/ilp32"
  )
fi

declare -a queue=("$SD_INITRD_ROOT/usr/lib/systemd/systemd"
                  "$SD_INITRD_ROOT/usr/bin/systemctl"
                  "$SD_INITRD_ROOT/usr/bin/systemd-ok")
if [[ -f "$SD_INITRD_ROOT/usr/lib/systemd/systemd-executor" ]]; then
  queue+=("$SD_INITRD_ROOT/usr/lib/systemd/systemd-executor")
fi
declare -A seen=()

find_lib() {
  local name="$1"
  local dir
  for dir in "${CATALOG_DIRS[@]}"; do
    if [[ -f "$dir/$name" ]]; then
      printf '%s\n' "$dir/$name"
      return 0
    fi
  done
  return 1
}

for dlopen_lib in libmount.so.1 libblkid.so.1 libuuid.so.1; do
  dlopen_source="$(find_lib "$dlopen_lib")" || continue
  install -m 0755 "$dlopen_source" "$SD_INITRD_ROOT/usr/lib/$dlopen_lib"
  install -m 0755 "$dlopen_source" "$SD_INITRD_ROOT/usr/lib/systemd/$dlopen_lib"
  queue+=("$SD_INITRD_ROOT/usr/lib/$dlopen_lib")
done

while (( ${#queue[@]} > 0 )); do
  binary="${queue[0]}"
  queue=("${queue[@]:1}")

  while read -r lib; do
    [[ -n "$lib" ]] || continue
    [[ -n "${seen[$lib]:-}" ]] && continue
    source_lib="$(find_lib "$lib")" || die "shared library not found in sysroot: $lib"
    seen[$lib]=1
    install -m 0755 "$source_lib" "$SD_INITRD_ROOT/usr/lib/$lib"
    install -m 0755 "$source_lib" "$SD_INITRD_ROOT/usr/lib/systemd/$lib"
    queue+=("$SD_INITRD_ROOT/usr/lib/$lib")
  done < <("${CROSS_COMPILE}objdump" -p "$binary" | awk '/NEEDED/{print $2}')

  interpreter="$("${CROSS_COMPILE}readelf" -l "$binary" | awk '/interpreter:/{sub(/\]$/, "", $4); print $4; exit}')"
  if [[ -n "$interpreter" && -z "${seen[$interpreter]:-}" ]]; then
    source_lib="$(find_lib "$(basename "$interpreter")")" ||
      die "ELF interpreter not found in sysroot: $interpreter"
    seen[$interpreter]=1
    install -m 0755 "$source_lib" "$SD_INITRD_ROOT${interpreter}"
  fi
done

cat > "$SD_INITRD_ROOT/etc/initrd-release" <<'EOF'
NAME=Ternary systemd initrd
ID=ternary-systemd-initrd
PRETTY_NAME="Ternary systemd initrd"
VERSION_ID=1
EOF

cat > "$SD_INITRD_ROOT/etc/systemd/system/systemd-initrd-smoke.service" <<'EOF'
[Unit]
Description=Ternary systemd initrd smoke test
DefaultDependencies=no
After=initrd.target

[Service]
Type=oneshot
ExecStart=/usr/bin/systemd-ok
RemainAfterExit=yes

[Install]
WantedBy=initrd.target
EOF

mkdir -p "$SD_INITRD_ROOT/etc/systemd/system/initrd.target.wants"
ln -sfn ../systemd-initrd-smoke.service \
  "$SD_INITRD_ROOT/etc/systemd/system/initrd.target.wants/systemd-initrd-smoke.service"

for unit in initrd.target initrd-root-fs.target initrd-usr-fs.target initrd-switch-root.target \
            sysinit.target basic.target local-fs.target local-fs-pre.target \
            swap.target slices.target paths.target timers.target sockets.target \
            rescue.target emergency.target; do
  if [[ -f "$SD_SYSROOT/usr/lib/systemd/system/$unit" ]]; then
    install -m 0644 "$SD_SYSROOT/usr/lib/systemd/system/$unit" \
      "$SD_INITRD_ROOT/usr/lib/systemd/system/$unit"
  fi
done

ln -sfn /usr/lib/systemd/system/initrd.target \
  "$SD_INITRD_ROOT/etc/systemd/system/default.target"

( cd "$SD_INITRD_ROOT" && find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip -9 ) \
  > "$SD_INITRAMFS"

require_file "$SD_INITRAMFS"
printf 'INITRAMFS=%q\n' "$SD_INITRAMFS" > "$BUILD_DIR/systemd.env"
log "initramfs: $SD_INITRAMFS"
