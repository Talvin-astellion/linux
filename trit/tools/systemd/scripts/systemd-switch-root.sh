#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

require_file "$SD_INITRAMFS"
require_file "$SD_INITRD_ROOT/usr/bin/systemctl"
require_tool cpio
require_tool gzip

rm -rf "$SD_SWITCH_ROOT_ROOT" "$SD_SWITCH_INITRD_ROOT"
mkdir -p "$SD_SWITCH_ROOT_ROOT" "$SD_SWITCH_INITRD_ROOT"
cp -al "$SD_INITRD_ROOT/." "$SD_SWITCH_ROOT_ROOT/"
cp -al "$SD_INITRD_ROOT/." "$SD_SWITCH_INITRD_ROOT/"

cat > "$SD_SWITCH_ROOT_ROOT/etc/os-release" <<'EOF'
NAME=Ternary systemd switch-root
ID=ternary-switch-root
PRETTY_NAME="Ternary systemd switch-root"
VERSION_ID=1
EOF
rm -f "$SD_SWITCH_ROOT_ROOT/etc/initrd-release"
rm -f "$SD_SWITCH_ROOT_ROOT/etc/systemd/system/default.target"
rm -f "$SD_SWITCH_ROOT_ROOT/etc/systemd/system/initrd.target.wants/systemd-initrd-smoke.service"

cat > "$SD_SWITCH_ROOT_ROOT/etc/systemd/system/ternary-switch-root-smoke.service" <<'EOF'
[Unit]
Description=Ternary systemd switch-root smoke test
DefaultDependencies=no

[Service]
Type=oneshot
ExecStart=/usr/bin/systemd-ok SWITCH_ROOT_OK
RemainAfterExit=yes
EOF

cat > "$SD_SWITCH_ROOT_ROOT/etc/systemd/system/ternary-switch-root.target" <<'EOF'
[Unit]
Description=Ternary switch-root smoke target
DefaultDependencies=no
Requires=ternary-switch-root-smoke.service
After=ternary-switch-root-smoke.service
AllowIsolate=yes
EOF
ln -sfn ternary-switch-root.target "$SD_SWITCH_ROOT_ROOT/etc/systemd/system/default.target"

smoke_dropin="$SD_SWITCH_INITRD_ROOT/etc/systemd/system/systemd-initrd-smoke.service.d"
mkdir -p "$smoke_dropin"
cat > "$smoke_dropin/switch-root.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=/usr/bin/systemd-ok INITRD_OK --no-poweroff
EOF

cat > "$SD_SWITCH_INITRD_ROOT/etc/systemd/system/ternary-switch-root.service" <<'EOF'
[Unit]
Description=Ternary switch-root transition
DefaultDependencies=no
After=systemd-initrd-smoke.service
Requires=systemd-initrd-smoke.service

[Service]
Type=oneshot
ExecStart=/usr/bin/systemctl --no-block switch-root /newroot /init
EOF

mkdir -p "$SD_SWITCH_INITRD_ROOT/etc/systemd/system/initrd.target.wants"
ln -sfn ../ternary-switch-root.service \
  "$SD_SWITCH_INITRD_ROOT/etc/systemd/system/initrd.target.wants/ternary-switch-root.service"

mkdir -p "$SD_SWITCH_INITRD_ROOT/newroot"
cp -al "$SD_SWITCH_ROOT_ROOT/." "$SD_SWITCH_INITRD_ROOT/newroot/"

( cd "$SD_SWITCH_INITRD_ROOT" && find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip -9 ) \
  > "$SD_SWITCH_INITRAMFS"

require_file "$SD_SWITCH_INITRAMFS"
printf 'INITRAMFS=%q\n' "$SD_SWITCH_INITRAMFS" > "$BUILD_DIR/systemd-switch-root.env"
log "switch-root initramfs: $SD_SWITCH_INITRAMFS"
