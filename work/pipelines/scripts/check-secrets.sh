#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

CONFIG="$SCRIPT_DIR/../configs/gitleaks.toml"

require_tool git
require_tool tar
require_tool gitleaks
require_file "$CONFIG"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

log "scanning tracked work/ and .github/ files with gitleaks"
list_files work .github > "$tmp/files.txt"
tar -cf "$tmp/scan.tar" -C "$REPO_ROOT" -T "$tmp/files.txt"
mkdir -p "$tmp/tree"
tar -xf "$tmp/scan.tar" -C "$tmp/tree"
gitleaks detect \
  --source "$tmp/tree" \
  --no-git \
  --config "$CONFIG" \
  --redact \
  --verbose
log "secret scan OK"
