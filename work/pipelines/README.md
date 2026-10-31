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
| `sparse` | `github/checks.yml` | `scripts/sparse.sh` | нет (advisory)¹ |
| `allmodconfig` | `github/build.yml` | `scripts/build.sh` | да |
| `kunit` | `github/kunit.yml` | `scripts/kunit.sh` | нет (advisory) |
| `smoke` | `github/smoke.yml` | `scripts/smoke-qemu.sh` | нет (advisory) |

¹ CI собирает `sparse` из исходников: пакет Ubuntu 0.6.4 не проходит
`scripts/checker-valid.sh`. Локально добавьте каталог со `sparse` в `PATH`.

Оркестратор — `github/ci.yml`, вызывает три reusable workflow.

## Локальный запуск

```bash
work/pipelines/scripts/deps.sh
JOBS=3 work/pipelines/scripts/build.sh
BASE=origin/master work/pipelines/scripts/checkpatch.sh
JOBS=4 work/pipelines/scripts/sparse.sh
JOBS=4 work/pipelines/scripts/kunit.sh
work/pipelines/scripts/smoke-qemu.sh
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

## Артефакты

| Путь | Job |
|---|---|
| `work/build/allmodconfig/.config` | build |
| `work/build/allmodconfig/build.log` | build |
| `work/build/sparse/sparse.log` | sparse |
| `work/build/kunit/kunit.json` | kunit |
| `work/build/smoke/qemu.log` | smoke |

## Ограничения

- GitHub-hosted runner: ~14 ГБ диска. `ci-cleanup.sh` освобождает место,
  CI-фрагмент отключает debug-info/BTF.
- `sparse` и `kunit` помечены `continue-on-error` до стабилизации baseline.
- При нехватке ресурсов используйте self-hosted runner с меткой `self-hosted`
  и замените `runs-on` в workflow.

## Добавление job

1. Добавьте скрипт в `scripts/` с `source lib.sh`.
2. Добавьте шаг в соответствующий workflow в `github/`.
3. Запустите `install.sh`.
4. Обновите таблицу в этом файле и `spec/project-spec.md`.
