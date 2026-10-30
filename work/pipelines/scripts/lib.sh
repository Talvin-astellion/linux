#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
REPO_ROOT="$(cd -- "$WORK_DIR/.." && pwd)"
BUILD_ROOT="${BUILD_ROOT:-$WORK_DIR/build}"

log() {
  printf '[%s] %s\n' "$(date -u +%H:%M:%S)" "$*" >&2
}

die() {
  log "ERROR: $*"
  exit 1
}

have() {
  command -v "$1" >/dev/null 2>&1
}

require_tool() {
  have "$1" || die "missing required tool: $1 (run work/pipelines/scripts/deps.sh)"
}

detect_jobs() {
  local cpus mem_gb by_mem
  cpus="$(nproc)"
  mem_gb="$(awk '/^MemTotal:/ {printf "%d", $2 / 1024 / 1024}' /proc/meminfo)"
  by_mem=$(( mem_gb / 2 ))
  (( by_mem < 1 )) && by_mem=1
  (( by_mem > cpus )) && by_mem="$cpus"
  printf '%s' "$by_mem"
}

JOBS="${JOBS:-$(detect_jobs)}"

free_gb() {
  df -Pk "$REPO_ROOT" | awk 'NR == 2 {printf "%d", $4 / 1024 / 1024}'
}

require_disk() {
  local need="${1:-5}" avail
  avail="$(free_gb)"
  (( avail >= need )) || die "need ${need}G free on $REPO_ROOT, have ${avail}G"
}

prepare_build_root() {
  mkdir -p "$BUILD_ROOT"
}

export REPO_ROOT WORK_DIR BUILD_ROOT JOBS
