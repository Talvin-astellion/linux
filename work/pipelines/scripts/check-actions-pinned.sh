#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

cd "$REPO_ROOT"

fail=0
while IFS= read -r line; do
  uses="${line#*uses:}"
  uses="${uses%%#*}"
  uses="${uses#"${uses%%[![:space:]]*}"}"
  uses="${uses%"${uses##*[![:space:]]}"}"
  [[ -z "$uses" ]] && continue
  case "$uses" in
    ./*) continue ;;
  esac
  if ! [[ "$uses" =~ @[0-9a-f]{40}$ ]]; then
    echo "unpinned action: $uses" >&2
    fail=1
  fi
done < <(grep -RhE '^[[:space:]]*uses:' .github/workflows)

if (( fail )); then
  die "pin every action to a full commit SHA"
fi

log "all workflow actions are pinned"
