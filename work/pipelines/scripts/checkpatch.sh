#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

cd "$REPO_ROOT"
require_tool git
require_tool perl

BASE="${BASE:-origin/${GITHUB_BASE_REF:-master}}"
if ! git rev-parse --verify --quiet "$BASE^{commit}" >/dev/null; then
  BASE="$(git rev-list --max-parents=0 HEAD | tail -n 1)"
fi
RANGE="$BASE..HEAD"

if ! git rev-parse --verify --quiet HEAD^ >/dev/null; then
  log "no parent commit; nothing to check"
  exit 0
fi

if git diff --quiet "$RANGE"; then
  log "no changes in $RANGE"
  exit 0
fi

log "checkpatch: $RANGE"
OUTPUT="$(git diff --no-color "$RANGE" | \
  "$REPO_ROOT/scripts/checkpatch.pl" --no-tree --strict --show-types - || true)"
printf '%s\n' "$OUTPUT"

if printf '%s\n' "$OUTPUT" | grep -qE '^ERROR:'; then
  die "checkpatch found errors"
fi
if printf '%s\n' "$OUTPUT" | grep -qE '^WARNING:'; then
  log "checkpatch warnings only"
fi
