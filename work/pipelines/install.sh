#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DEST="$REPO_ROOT/.github/workflows"

mkdir -p "$DEST"
shopt -s nullglob
files=("$SCRIPT_DIR"/github/*.yml)
(( ${#files[@]} > 0 )) || { echo "no workflows found in $SCRIPT_DIR/github" >&2; exit 1; }

for f in "${files[@]}"; do
  cp -v "$f" "$DEST/$(basename "$f")"
done

echo "Installed ${#files[@]} workflow(s) into $DEST"
echo "Next: git add .github/workflows && git commit && git push"
