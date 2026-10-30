#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

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
  sparse
  ccache
)

FULL_PACKAGES=(
  clang
  lld
  llvm
  rustc
  cargo
  python3-sphinx
  qemu-system-x86
)

log "installing base packages"
"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y --no-install-recommends "${BASE_PACKAGES[@]}"

if [[ "${1:-}" == "--full" ]]; then
  log "installing full packages"
  "${SUDO[@]}" apt-get install -y --no-install-recommends "${FULL_PACKAGES[@]}"
  "${SUDO[@]}" apt-get install -y --no-install-recommends bindgen || log "bindgen package unavailable; install manually if CONFIG_RUST is needed"
fi

log "done"
