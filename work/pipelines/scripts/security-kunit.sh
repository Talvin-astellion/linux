#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/security-kunit}"
KUNITCONFIG="${KUNITCONFIG:-$SCRIPT_DIR/../configs/security-kunit.config}"
SUITES_FILE="${SUITES_FILE:-$SCRIPT_DIR/../configs/security-kunit-suites.txt}"
KUNIT_TIMEOUT="${KUNIT_TIMEOUT:-900}"

require_tool python3
require_tool make
require_file "$KUNITCONFIG"
require_file "$SUITES_FILE"
require_disk "${MIN_FREE_GB:-5}"
prepare_build_root
mkdir -p "$BUILD_DIR"

cd "$REPO_ROOT"

log "running security KUnit with -j$JOBS, timeout ${KUNIT_TIMEOUT}s"
python3 tools/testing/kunit/kunit.py run \
  --arch=um \
  --kunitconfig="$KUNITCONFIG" \
  --jobs="$JOBS" \
  --build_dir="$BUILD_DIR" \
  --timeout="$KUNIT_TIMEOUT" \
  --json="$BUILD_DIR/kunit.json"

python3 "$SCRIPT_DIR/check-kunit-security.py" "$BUILD_DIR/kunit.json" "$SUITES_FILE"
