#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

PROFILE="${1:-local}"
case "$PROFILE" in
  local|ci|smoke|lint|check) ;;
  --full) PROFILE=local ;;
  *) die "unknown profile: $PROFILE (use local, ci, smoke, lint, or check)" ;;
esac

if ! have apt-get; then
  die "apt-get not found; install dependencies manually (see work/spec/build-and-test.md)"
fi

SUDO=()
if (( EUID != 0 )); then
  require_tool sudo
  SUDO=(sudo)
fi

BASE_PACKAGES=(
  build-essential
  bc
  bison
  flex
  libssl-dev
  libelf-dev
  libncurses-dev
  dwarves
  cpio
  kmod
  rsync
  git
  perl
  python3
  ccache
  curl
  ca-certificates
)

FULL_PACKAGES=(
  clang
  lld
  llvm
  rustc
  cargo
  python3-sphinx
  qemu-system-x86
  shellcheck
)

LINT_PACKAGES=(
  git
  curl
  ca-certificates
  tar
  gzip
  shellcheck
)

CHECK_PACKAGES=(
  git
  perl
  curl
  ca-certificates
  tar
  gzip
)

install_apt() {
  log "installing: $*"
  "${SUDO[@]}" apt-get install -y --no-install-recommends "$@"
}

install_actionlint() {
  local version="1.7.12"
  local sha256="8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8"
  local tmp

  if have actionlint; then
    log "actionlint already installed"
    return
  fi

  require_tool curl
  require_tool tar
  tmp="$(mktemp -d)"
  log "installing actionlint $version"
  curl -fsSL "https://github.com/rhysd/actionlint/releases/download/v${version}/actionlint_${version}_linux_amd64.tar.gz" \
    -o "$tmp/actionlint.tar.gz"
  echo "${sha256}  $tmp/actionlint.tar.gz" | sha256sum -c -
  tar -xzf "$tmp/actionlint.tar.gz" -C "$tmp" actionlint
  "${SUDO[@]}" install -m 0755 "$tmp/actionlint" /usr/local/bin/actionlint
  rm -rf "$tmp"
}

log "installing dependencies for profile: $PROFILE"
"${SUDO[@]}" apt-get update

case "$PROFILE" in
  local)
    install_apt "${BASE_PACKAGES[@]}"
    install_apt "${FULL_PACKAGES[@]}"
    install_apt bindgen || log "bindgen unavailable; install manually if CONFIG_RUST is needed"
    install_actionlint
    ;;
  ci)
    install_apt "${BASE_PACKAGES[@]}"
    ;;
  smoke)
    install_apt "${BASE_PACKAGES[@]}"
    install_apt qemu-system-x86
    ;;
  lint)
    install_apt "${LINT_PACKAGES[@]}"
    install_actionlint
    ;;
  check)
    install_apt "${CHECK_PACKAGES[@]}"
    ;;
esac

log "done"
