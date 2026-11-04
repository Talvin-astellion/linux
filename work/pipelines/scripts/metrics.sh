#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

MODE="${1:-}"
case "$MODE" in
  ""|--write|--check) ;;
  *) die "usage: $0 [--write|--check]" ;;
esac

cd "$REPO_ROOT"

METRICS_FILE="$REPO_ROOT/work/spec/metrics.md"
CONFIG_DIR="$REPO_ROOT/work/pipelines/configs"
SEC_SUITES_FILE="$CONFIG_DIR/security-kunit-suites.txt"
KASAN_CONFIG="$CONFIG_DIR/kasan.config"

mapfile -t SCRIPT_FILES < <(list_files 'work/pipelines/scripts/*.sh')
mapfile -t WORKFLOW_FILES < <(list_files '.github/workflows/*.yml')
mapfile -t CONFIG_FILES < <(list_files 'work/pipelines/configs/*')
mapfile -t DOC_FILES < <(list_files 'work/*.md' 'work/**/*.md' | grep -v metrics.md || true)
mapfile -t SEC_SUITES < <(grep -vE '^[[:space:]]*(#|$)' "$SEC_SUITES_FILE" 2>/dev/null || true)

line_count() {
  local file total=0
  for file in "$@"; do
    total=$((total + $(wc -l < "$file")))
  done
  echo "$total"
}

SCRIPT_LINES="$(line_count "${SCRIPT_FILES[@]}")"
WORKFLOW_LINES="$(line_count "${WORKFLOW_FILES[@]}")"
CONFIG_LINES="$(line_count "${CONFIG_FILES[@]}")"
DOC_LINES="$(line_count "${DOC_FILES[@]}")"
LIB_FUNCTIONS="$(grep -cE '^[a-zA-Z_][a-zA-Z0-9_]*\(\)' "$SCRIPT_DIR/lib.sh" || true)"
mapfile -t JOB_FILES < <(
  printf '%s\n' "${WORKFLOW_FILES[@]}" |
    grep -v '^\.github/workflows/ci.yml$'
)
# shellcheck disable=SC2126
WORKFLOW_JOBS="$(
  { grep -h '^    name:' "${JOB_FILES[@]}" 2>/dev/null || true; } |
    wc -l | tr -d ' '
)"
# shellcheck disable=SC2126
USES_TOTAL="$(
  { grep -RhE '^[[:space:]]*uses:' .github/workflows 2>/dev/null || true; } |
    { grep -vE 'uses:[[:space:]]*\./' || true; } |
    wc -l | tr -d ' '
)"
USES_RE='^[[:space:]]*uses:[[:space:]]*[^[:space:]]+@[0-9a-f]{40}'
# shellcheck disable=SC2126
USES_PINNED="$(
  { grep -RhE "$USES_RE" .github/workflows 2>/dev/null || true; } |
    wc -l | tr -d ' '
)"
# shellcheck disable=SC2126
APT_SITES="$(
  { grep -Rh --exclude=metrics.sh 'apt-get install' \
    .github/workflows work/pipelines/scripts 2>/dev/null || true; } |
    wc -l | tr -d ' '
)"
# shellcheck disable=SC2126
SEC_JOBS="$(
  { grep -h '^    name:' .github/workflows/security.yml 2>/dev/null || true; } |
    wc -l | tr -d ' '
)"
SANITIZERS="$(grep -cE '^CONFIG_(KASAN|KFENCE|UBSAN)' "$KASAN_CONFIG" 2>/dev/null || true)"

SPDX_SCRIPTS=0
for file in "${SCRIPT_FILES[@]}"; do
  if grep -q 'SPDX-License-Identifier' "$file"; then
    SPDX_SCRIPTS=$((SPDX_SCRIPTS + 1))
  fi
done

CI_SCRIPTS=0
for file in "${SCRIPT_FILES[@]}"; do
  base="$(basename "$file")"
  if grep -RqsF -- "scripts/$base" .github/workflows; then
    CI_SCRIPTS=$((CI_SCRIPTS + 1))
  fi
done

generate() {
  echo "# Метрики функциональности"
  echo
  echo "Сгенерировано автоматически."
  echo "Не редактируйте вручную."
  echo
  echo "## Код и конфигурация"
  echo
  echo "- Скрипты: ${#SCRIPT_FILES[@]} файлов, $SCRIPT_LINES строк"
  echo "- Функции в \`lib.sh\`: $LIB_FUNCTIONS"
  echo "- Workflow: ${#WORKFLOW_FILES[@]} файлов, $WORKFLOW_LINES строк," \
    "$WORKFLOW_JOBS job'ов"
  echo "- Конфиги: ${#CONFIG_FILES[@]} файлов, $CONFIG_LINES строк"
  echo "- Документация: ${#DOC_FILES[@]} файлов, $DOC_LINES строк"
  echo "- Покрытие скриптов CI: $CI_SCRIPTS/${#SCRIPT_FILES[@]}"
  echo "- \`apt-get install\`: $APT_SITES мест"
  echo "- Actions с pinning: $USES_PINNED/$USES_TOTAL"
  echo "- SPDX в скриптах: $SPDX_SCRIPTS/${#SCRIPT_FILES[@]}"
  echo
  echo "## Безопасность"
  echo
  echo "- Security KUnit suites: ${#SEC_SUITES[@]}"
  for suite in "${SEC_SUITES[@]}"; do
    echo "  - \`$suite\`"
  done
  echo "- Security jobs: $SEC_JOBS"
  echo "- Санитайзеры в KASAN smoke: $SANITIZERS"
}

case "$MODE" in
  --write)
    generate > "$METRICS_FILE"
    log "wrote $METRICS_FILE"
    ;;
  --check)
    tmp="$(mktemp)"
    generate > "$tmp"
    if ! diff -u "$METRICS_FILE" "$tmp"; then
      rm -f "$tmp"
      die "metrics are stale; run make -C work metrics"
    fi
    rm -f "$tmp"
    log "metrics are up to date"
    ;;
  *)
    generate
    ;;
esac
