# Пайплайны

Логика пайплайнов живёт в `scripts/`, CI только вызывает её. Это позволяет
запускать те же проверки локально и на GitHub.

## Workflows

Канонические workflow-файлы лежат в `.github/workflows/`; GitHub Actions не
поддерживает симлинки, поэтому `work/` больше не хранит копию. После изменения
workflow обновите документацию и `make -C work metrics`.

| Workflow | Назначение |
|---|---|
| `.github/workflows/ci.yml` | оркестратор |
| `.github/workflows/meta.yml` | lint, shellcheck, actionlint, метрики |
| `.github/workflows/checks.yml` | checkpatch, sparse |
| `.github/workflows/security.yml` | action pinning, gitleaks, security KUnit, KASAN smoke |
| `.github/workflows/build.yml` | allmodconfig |
| `.github/workflows/kunit.yml` | KUnit `all_tests` |
| `.github/workflows/smoke.yml` | QEMU smoke |

## Jobs

| Job | Workflow | Скрипт | Блокирует |
|---|---|---|---|
| `meta` | `meta.yml` | `lint.sh`, `metrics.sh --check` | да |
| `checkpatch` | `checks.yml` | `checkpatch.sh` | да |
| `sparse` | `checks.yml` | `sparse.sh` | да (baseline 211/1252)¹ |
| `allmodconfig` | `build.yml` | `build.sh` | да |
| `kunit` | `kunit.yml` | `kunit.sh` | да |
| `security-baseline` | `security.yml` | `check-actions-pinned.sh`, `check-secrets.sh` | да |
| `security-kunit` | `security.yml` | `security-kunit.sh` | да |
| `security-sanitizers` | `security.yml` | `smoke-kasan.sh` | да |
| `qemu-smoke` | `smoke.yml` | `smoke-qemu.sh` | да |

¹ CI собирает `sparse` из исходников: пакет Ubuntu 0.6.4 не проходит
`scripts/checker-valid.sh`. Локально добавьте каталог со `sparse` в `PATH`.

Оркестратор `ci.yml` вызывает шесть reusable workflow. Машинная сборка
(`scripts/build-machine.sh`) в CI не запускается: она привязана к железу и
`-march=native`.

## Локальный запуск

```bash
work/pipelines/scripts/deps.sh local
JOBS=3 work/pipelines/scripts/build.sh
BASE=origin/master work/pipelines/scripts/checkpatch.sh
JOBS=4 work/pipelines/scripts/sparse.sh
JOBS=4 work/pipelines/scripts/kunit.sh
JOBS=4 work/pipelines/scripts/security-kunit.sh
work/pipelines/scripts/smoke-qemu.sh
work/pipelines/scripts/smoke-kasan.sh

# гигиена и метрики
work/pipelines/scripts/lint.sh
work/pipelines/scripts/metrics.sh --check

# машинная сборка 7.2.6 (локально, не в CI)
JOBS=3 work/pipelines/scripts/build-machine.sh
```

Профили `deps.sh`:

| Профиль | Состав |
|---|---|
| `local` | base + clang/lld/llvm/rust/sphinx/qemu + shellcheck + actionlint |
| `ci` | сборочный base без qemu |
| `smoke` | `ci` + qemu-system-x86 |
| `lint` | git, curl, shellcheck, actionlint |
| `check` | git, perl |

Переменные окружения:

| Переменная | По умолчанию | Назначение |
|---|---|---|
| `JOBS` | `min(nproc, RAM_GB/2)` | параллелизм |
| `BUILD_ROOT` | `work/build` | корень артефактов |
| `BUILD_DIR` | зависит от скрипта | каталог сборки |
| `MIN_FREE_GB` | 5–10 | порог свободного диска |
| `BASE` | `origin/master` | база для checkpatch |
| `SPARSE_BASELINE` | `configs/sparse-baseline.conf` | пороги sparse |
| `SPARSE_MAX_ERRORS` | 211 | переопределение порога |
| `SPARSE_MAX_WARNINGS` | 1252 | переопределение порога |
| `KUNITCONFIG` | `all_tests.config` | конфиг KUnit |
| `KUNIT_TIMEOUT` | 900 | таймаут KUnit, сек |
| `KERNEL_IMAGE` | `work/build/defconfig/.../bzImage` | образ для QEMU |
| `SMOKE_TIMEOUT` | 120 | таймаут QEMU, сек |
| `SMOKE_MEM` | 512M | RAM для QEMU |
| `SMOKE_APPEND` | пусто | дополнительные параметры ядра |
| `QEMU_CPU` | пусто | `max` для native-ядра в QEMU |
| `SRC_TREE` | `/home/talvin/linux-7.2.6` | исходники для machine |
| `SRC_COPY` | `$BUILD_ROOT/src-<ver>` | чистая копия исходников |
| `BASE_CONFIG` | `/boot/config-$(uname -r)` | база для machine |
| `SYMBOLS_FILE` | `configs/machine-symbols.txt` | обязательные символы machine |
| `FRAGMENT` | `configs/machine.config` | машинный фрагмент |
| `SUITES_FILE` | `configs/security-kunit-suites.txt` | обязательные security suites |

## Артефакты

| Путь | Job |
|---|---|
| `work/build/allmodconfig/.config` | build |
| `work/build/allmodconfig/build.log` | build |
| `work/build/sparse/sparse.log` | sparse |
| `work/build/sparse/summary.txt` | sparse |
| `work/build/kunit/kunit.json` | kunit |
| `work/build/security-kunit/kunit.json` | security-kunit |
| `work/build/smoke/qemu.log` | smoke |
| `work/build/smoke/qemu-debug.log` | smoke (только при падении) |
| `work/build/smoke-kasan/build.log` | security-sanitizers |
| `work/build/smoke-kasan/smoke/qemu.log` | security-sanitizers |
| `work/build/machine-7.2.6/artifacts/` | machine |
| `work/build/machine-7.2.6/modules/` | machine |
| `work/build/machine-7.2.6/summary.txt` | machine |

## Ограничения

- GitHub-hosted runner: ~14 ГБ диска. `ci-cleanup.sh` освобождает место,
  CI-фрагмент отключает debug-info/BTF и тяжёлую инструментацию
  (KASAN/UBSAN/KFENCE/GCOV, debug-pagealloc/VM/kmemleak).
- `allmodconfig` в CI: `timeout-minutes: 360`, `JOBS=4`; ccache
  восстанавливается и сохраняется отдельными шагами
  (`actions/cache/restore` + `actions/cache/save` с `if: always()`), размер
  ограничен `CCACHE_MAXSIZE=4G`.
- `sparse` падает только при превышении baseline (`SPARSE_MAX_ERRORS`/`SPARSE_MAX_WARNINGS`).
- `checkpatch` использует `fetch-depth: 1` и отдельно подтягивает tip базовой
  ветки с `--depth=1`: полный history (~6 ГБ) не нужен, а на полном checkout
  job упирался в timeout 20 мин (ISSUE-12, closed).
- `smoke` и `security-sanitizers` закреплены на `ubuntu-24.04`: на
  `ubuntu-latest` (Ubuntu 26.04, QEMU 10.2.1) ядро падает до вывода
  консоли. См. ISSUE-11; на падении выгружаются `qemu.log`,
  `qemu-debug.log` и `bzImage`.
- `security-baseline` проверяет, что все `uses:` закреплены на 40-символьный
  SHA, и сканирует файлы `work/` и `.github/` через `tar`, включая untracked.
- `security-sanitizers` собирает отдельное ядро с KASAN/KFENCE/UBSAN и
  загружает его в QEMU с `kasan_multi_shot`; job тяжелее обычного smoke.
- При нехватке ресурсов используйте self-hosted runner с меткой `self-hosted`
  и замените `runs-on` в workflow.

## Добавление job

1. Добавьте скрипт в `scripts/` с `source lib.sh`.
2. Добавьте шаг в нужный файл `.github/workflows/`.
3. Запустите `work/pipelines/scripts/lint.sh` и `make -C work metrics`.
4. Обновите таблицы в этом файле и `spec/project-spec.md`.
