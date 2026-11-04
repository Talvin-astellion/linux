#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/kunit}"
KUNITCONFIG="${KUNITCONFIG:-tools/testing/kunit/configs/all_tests.config}"
KUNIT_TIMEOUT="${KUNIT_TIMEOUT:-900}"

require_tool python3
require_tool make
if [[ "$KUNITCONFIG" = /* ]]; then
  require_file "$KUNITCONFIG"
else
  require_file "$REPO_ROOT/$KUNITCONFIG"
fi
require_disk "${MIN_FREE_GB:-5}"
prepare_build_root
mkdir -p "$BUILD_DIR"

cd "$REPO_ROOT"

log "running KUnit ($KUNITCONFIG) with -j$JOBS, timeout ${KUNIT_TIMEOUT}s"
python3 tools/testing/kunit/kunit.py run \
  --arch=um \
  --kunitconfig="$KUNITCONFIG" \
  --jobs="$JOBS" \
  --build_dir="$BUILD_DIR" \
  --timeout="$KUNIT_TIMEOUT" \
  --json="$BUILD_DIR/kunit.json"
