#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

cd "$REPO_ROOT"
require_tool git
require_tool perl

BASE="${BASE:-origin/${GITHUB_BASE_REF:-master}}"
if ! git rev-parse --verify --quiet "$BASE^{commit}" >/dev/null; then
  if [ "$(git rev-parse --is-shallow-repository)" = "true" ]; then
    die "base $BASE is not available in a shallow clone; fetch it first"
  fi
  BASE="$(git rev-list --max-parents=0 HEAD | tail -n 1)"
fi
RANGE="$BASE..HEAD"

if git diff --quiet "$RANGE"; then
  log "no changes in $RANGE"
  exit 0
fi

log "checkpatch: $RANGE"
DIFF_FILE="$(mktemp)"
trap 'rm -f "$DIFF_FILE"' EXIT
git diff --no-color "$RANGE" > "$DIFF_FILE"
OUTPUT="$("$REPO_ROOT/scripts/checkpatch.pl" \
  --no-tree --strict --show-types - < "$DIFF_FILE" || true)"
printf '%s\n' "$OUTPUT"

if printf '%s\n' "$OUTPUT" | grep -qE '^ERROR:'; then
  die "checkpatch found errors"
fi
if printf '%s\n' "$OUTPUT" | grep -qE '^WARNING:'; then
  log "checkpatch warnings only"
fi
