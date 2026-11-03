# Пайплайны

Логика пайплайнов живёт в `scripts/`, CI только вызывает её. Это позволяет
запускать те же проверки локально и на GitHub.

## Установка CI

```bash
work/pipelines/install.sh
```

Скрипт копирует `github/*.yml` в `.github/workflows/`. GitHub Actions не
поддерживает симлинки для workflow-файлов, поэтому используется копирование.
После изменения `github/*.yml` повторите установку.

## Jobs

| Job | Файл | Скрипт | Блокирует |
|---|---|---|---|
| `checkpatch` | `github/checks.yml` | `scripts/checkpatch.sh` | да |
| `sparse` | `github/checks.yml` | `scripts/sparse.sh` | да (baseline 211/1252)¹ |
| `allmodconfig` | `github/build.yml` | `scripts/build.sh` | да |
| `kunit` | `github/kunit.yml` | `scripts/kunit.sh` | да |
| `smoke` | `github/smoke.yml` | `scripts/smoke-qemu.sh` | да |

¹ CI собирает `sparse` из исходников: пакет Ubuntu 0.6.4 не проходит
`scripts/checker-valid.sh`. Локально добавьте каталог со `sparse` в `PATH`.

Оркестратор — `github/ci.yml`, вызывает четыре reusable workflow. Машинная
сборка (`scripts/build-machine.sh`) в CI не запускается: она привязана к
железу и `-march=native`.

## Локальный запуск

```bash
work/pipelines/scripts/deps.sh
JOBS=3 work/pipelines/scripts/build.sh
BASE=origin/master work/pipelines/scripts/checkpatch.sh
JOBS=4 work/pipelines/scripts/sparse.sh
JOBS=4 work/pipelines/scripts/kunit.sh
work/pipelines/scripts/smoke-qemu.sh

# машинная сборка 7.2.6 (локально, не в CI)
JOBS=3 work/pipelines/scripts/build-machine.sh
```

Переменные окружения:

| Переменная | По умолчанию | Назначение |
|---|---|---|
| `JOBS` | `min(nproc, RAM_GB/2)` | параллелизм |
| `BUILD_ROOT` | `$REPO_ROOT/build` | корень артефактов |
| `BUILD_DIR` | зависит от скрипта | каталог сборки |
| `MIN_FREE_GB` | 5–10 | порог свободного диска |
| `BASE` | `origin/master` | база для checkpatch |
| `KUNITCONFIG` | `all_tests.config` | конфиг KUnit |
| `KUNIT_TIMEOUT` | 900 | таймаут KUnit, сек |
| `KERNEL_IMAGE` | `work/build/defconfig/.../bzImage` | образ для QEMU |
| `SMOKE_TIMEOUT` | 120 | таймаут QEMU, сек |
| `QEMU_CPU` | пусто | `max` для native-ядра в QEMU |
| `SRC_TREE` | `/home/talvin/linux-7.2.6` | исходники для machine |
| `SRC_COPY` | `$BUILD_ROOT/src-<ver>` | чистая копия исходников |
| `BASE_CONFIG` | `/boot/config-$(uname -r)` | база для machine |
| `FRAGMENT` | `configs/machine.config` | машинный фрагмент |

## Артефакты

| Путь | Job |
|---|---|
| `work/build/allmodconfig/.config` | build |
| `work/build/allmodconfig/build.log` | build |
| `work/build/sparse/sparse.log` | sparse |
| `work/build/kunit/kunit.json` | kunit |
| `work/build/smoke/qemu.log` | smoke |
| `work/build/smoke/qemu-debug.log` | smoke (только при падении) |
| `work/build/machine-7.2.6/artifacts/` | machine |
| `work/build/machine-7.2.6/modules/` | machine |
| `work/build/machine-7.2.6/summary.txt` | machine |

## Ограничения

- GitHub-hosted runner: ~14 ГБ диска. `ci-cleanup.sh` освобождает место,
  CI-фрагмент отключает debug-info/BTF и тяжёлую инструментацию
  (KASAN/UBSAN/KFENCE/GCOV, debug-pagealloc/VM/kmemleak).
- `allmodconfig` в CI: `timeout-minutes: 360`, `JOBS=4`; первый прогон с
  `JOBS=2` и timeout 180 не уложился. ccache восстанавливается и сохраняется
  отдельными шагами (`actions/cache/restore` + `actions/cache/save` с
  `if: always()`), поэтому частичный кэш переживает отмену job; размер ограничен
  `CCACHE_MAXSIZE=4G`.
- `sparse` падает только при превышении baseline (`SPARSE_MAX_ERRORS`/`SPARSE_MAX_WARNINGS`).
- `smoke` закреплён на `ubuntu-24.04`: на `ubuntu-latest` (Ubuntu 26.04,
  QEMU 10.2.1) ядро падает до вывода консоли. См. ISSUE-11; на падении
  выгружаются `qemu.log`, `qemu-debug.log` и `bzImage`.
- При нехватке ресурсов используйте self-hosted runner с меткой `self-hosted`
  и замените `runs-on` в workflow.

## Добавление job

1. Добавьте скрипт в `scripts/` с `source lib.sh`.
2. Добавьте шаг в соответствующий workflow в `github/`.
3. Запустите `install.sh`.
4. Обновите таблицу в этом файле и `spec/project-spec.md`.
