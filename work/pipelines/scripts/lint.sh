#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

cd "$REPO_ROOT"

fail=0
mapfile -t scripts < <(list_files 'work/pipelines/scripts/*.sh')
if (( ${#scripts[@]} == 0 )); then
  die "no pipeline scripts found"
fi

for file in "${scripts[@]}"; do
  log "bash -n $file"
  bash -n "$file" || fail=1
done

if have shellcheck; then
  log "shellcheck"
  (cd "$SCRIPT_DIR" && shellcheck -x ./*.sh) || fail=1
else
  log "shellcheck not found; run work/pipelines/scripts/deps.sh lint"
  fail=1
fi

if have actionlint; then
  log "actionlint"
  actionlint -color || fail=1
else
  log "actionlint not found; run work/pipelines/scripts/deps.sh lint"
  fail=1
fi

if ! git diff --check; then
  fail=1
fi

exit "$fail"
