# Реестр проблем

Приоритеты: **P0** — блокирует работу, **P1** — важно, **P2** — гигиена.
Статус: `open`, `mitigated`, `closed`.

## Сводка

| ID | Приоритет | Проблема | Статус |
|---|---|---|---|
| ISSUE-01 | P0 | Нет воспроизводимой сборки и baseline | closed |
| ISSUE-02 | P0 | Ресурсный дефицит (RAM/диск) для allmodconfig | mitigated |
| ISSUE-03 | P1 | Нет CI и автоматических проверок | mitigated |
| ISSUE-04 | P1 | Расхождение версии Makefile и upstream-тегов | closed |
| ISSUE-05 | P1 | Несогласованная топология веток, нет тегов | mitigated |
| ISSUE-06 | P1 | Дыры в toolchain (sparse, clang, rust, sphinx) | mitigated |
| ISSUE-07 | P1 | Ограничения GitHub-hosted runner | open |
| ISSUE-08 | P2 | Тяжёлый `.git` (6.0 ГБ) | closed |
| ISSUE-09 | P2 | Нет локальной политики AI-вклада | closed |
| ISSUE-10 | P2 | Нет smoke-теста загрузки и хранения артефактов | mitigated |
| ISSUE-11 | P1 | `ubuntu-latest` 26.04 ломает QEMU smoke | mitigated |

## ISSUE-01 — Нет воспроизводимой сборки и baseline (P0)

**Доказательства**

- `.config` отсутствует в корне дерева.
- Нет `vmlinux`, `System.map`, `Module.symvers`.
- Нет тегов (`git tag` пуст).

**Влияние**

Невозможно отличить регрессию от исходного состояния, нет точки сравнения для
CI и bisect.

**Мера**

Фаза 0–1: зафиксировать baseline-тег, добавить CI-конфиг и out-of-tree сборку,
публиковать `.config` и логи как артефакты.

**Критерий закрытия**

`make O=work/build/allmodconfig allmodconfig && make -j2` воспроизводится по
документированной команде; создан тег `baseline-YYYYMMDD`.

## ISSUE-02 — Ресурсный дефицит (P0)

**Доказательства**

- `free -h`: 5.6 ГБ RAM, 4 ГБ swap.
- `df -h`: 32 ГБ свободно.
- Референсная сборка `/home/talvin/linux-7.2.6` занимает 12 ГБ (defconfig-уровень).

**Влияние**

`allmodconfig -j12` приводит к OOM; полная сборка может исчерпать диск.

**Мера**

- `JOBS` = `min(nproc, RAM_GB/2)` (≈2–3).
- Отключение debug-info/BTF в CI-фрагменте.
- Проверка `MIN_FREE_GB` перед сборкой.
- Возможность вынести `BUILD_DIR` на другой диск.
- Очистка `make clean/mrproper` после прогонов.

**Критерий закрытия**

allmodconfig стабильно собирается локально и в CI без OOM; зафиксированы
фактические время и размер.

## ISSUE-03 — Нет CI и автоматических проверок (P1)

**Доказательства**

- Каталог `.github/` отсутствует.
- Нет workflow-файлов.

**Влияние**

Изменения не проверяются автоматически; регрессии попадают в `dev`.

**Мера**

Фаза 2–3: `work/pipelines/github/*.yml`, `install.sh`, branch protection.

**Критерий закрытия**

PR запускает `checkpatch`, `sparse`, `allmodconfig`, `kunit`; статус виден в PR.

**Проверка (2026-10-31):** CI run `41185886775` — `checkpatch`, `sparse`,
`kunit`, `smoke` успешны; `allmodconfig` отменён по `timeout-minutes: 180`.

## ISSUE-04 — Расхождение версии (P1)

**Доказательства**

- `Makefile`: `VERSION=6 PATCHLEVEL=19 SUBLEVEL=0`.
- Merge-коммиты: `v6.20-rc1`, `v7.0`, `net-next-7.0`, `ext4_for_linus-7.0-rc1`.

**Влияние**

Неверный `uname -r`, путаница в артефактах и packaging, сложность сверки с
upstream.

**Мера**

Проверить, является ли это состоянием merge window до bump, либо локальной
правкой. Задокументировать в `spec/project-spec.md`, при необходимости
синхронизировать `Makefile` с upstream и создать baseline-тег.

**Проверка (2026-10-31)**

- `git log --all --grep='Linux 7\.0'` не находит коммита с bump версии.
- В истории есть merge-коммиты подсистем с тегами 7.0, но `Makefile` остаётся
  `6.19.0`.
- Следовательно, артефакты этой ветки должны иметь
  `KERNELRELEASE=6.19.0`; это не ошибка.
- Для 7.2.6 используется `/home/talvin/linux-7.2.6` (machine build).

**Критерий закрытия**

Версия однозначно объяснена и зафиксирована; артефакты имеют корректный `KERNELRELEASE`.

**Закрыто:** 2026-10-31, версия объяснена; machine-сборка 7.2.6 выполняется
из отдельного дерева.

## ISSUE-05 — Топология веток и теги (P1)

**Доказательства**

- `git branch -vv`: `dev` и `main` на одном коммите.
- `origin/HEAD -> origin/master`.
- Локальной ветки `master` нет, `main` трекает `origin/master`.

**Влияние**

Непонятно, куда мержить и от чего строить CI; нет baseline для bisect.

**Мера**

Политика: `main` — зеркало upstream, `dev` — интеграция; защита веток;
аннотированные теги `baseline-*` и `ci-*`.

**Критерий закрытия**

Политика описана в `runbook.md`, тег создан, branch protection включён.

## ISSUE-06 — Дыры в toolchain (P1)

**Доказательства**

`command -v`: `sparse`, `clang`, `rustc`, `cargo`, `bindgen`, `sphinx`, `ccache`
отсутствуют.

**Влияние**

Нельзя запускать выбранные проверки (sparse), альтернативную сборку (clang),
Rust и docs.

**Мера**

`work/pipelines/scripts/deps.sh` (базовый и `--full`); в CI ставится минимум,
необходимый job-у. `sparse` 0.6.4 из Ubuntu не проходит
`scripts/checker-valid.sh`; в CI он собирается из исходников. Локальный
baseline: 211 errors, 1252 warnings.

**Критерий закрытия**

Базовый набор ставится одной командой; `sparse` доступен локально и в CI.
Осталось: `clang`, `rust`, `sphinx` для полного набора.

## ISSUE-07 — Ограничения GitHub-hosted runner (P1)

**Доказательства**

Стандартный runner: ~4 CPU, 16 ГБ RAM, ~14 ГБ свободного диска.
Прогон `41185886775`: `allmodconfig` отменён ровно через 180 мин.

**Влияние**

allmodconfig не помещается по диску без очистки и отключения debug-info;
при `JOBS=2` сборка не укладывается в 3 часа.

**Мера**

`ci-cleanup.sh` (удаление dotnet/android/ghc/CodeQL/boost, docker prune),
CI-фрагмент без debug-info и тяжёлой инструментации (KASAN/UBSAN/KFENCE/GCOV,
debug-pagealloc/VM/kmemleak), `JOBS=4`, timeout 360 мин, артефакты только
логи/конфиг. При необходимости — self-hosted runner.

**Критерий закрытия**

allmodconfig job проходит на hosted runner либо обоснован переход на self-hosted.

## ISSUE-08 — Тяжёлый `.git` (P2)

**Доказательства**

`du -sh .git` = 6.0 ГБ.

**Влияние**

Медленные clone/fetch, нагрузка на CI.

**Мера**

Документировать partial clone (`--filter=blob:none`) и `git gc`; для CI —
`fetch-depth` по необходимости.

**Критерий закрытия**

Рекомендации в `runbook.md`; время fetch в CI приемлемо.

**Закрыто:** 2026-10-31, добавлены partial clone/`git gc`; шаг Checkout в CI
завершается успешно.

## ISSUE-09 — Нет локальной политики AI-вклада (P2)

**Доказательства**

Есть upstream `Documentation/process/coding-assistants.rst`, но нет локального
`AGENTS.md`/политики.

**Влияние**

Риск нарушения DCO/лицензирования при AI-ассистенте.

**Мера**

Добавить `AGENTS.md` в корень (отдельной задачей) со ссылкой на
`coding-assistants.rst` и требованием DCO.

**Критерий закрытия**

Политика закоммичена и упомянута в `spec/project-spec.md`.

**Закрыто:** 2026-10-31, добавлен `AGENTS.md`; ссылка есть в
`spec/project-spec.md` и `plans/runbook.md`.

## ISSUE-10 — Нет smoke-теста загрузки и хранения артефактов (P2)

**Доказательства**

Нет QEMU-прогона, нет публикации артефактов.

**Влияние**

Сборка может быть зелёной, но ядро не загружается.

**Мера**

Фаза 4: QEMU smoke (x86_64 defconfig), публикация `bzImage`/логов в artifacts
или release. Реализовано: `work/pipelines/scripts/smoke-qemu.sh`, локальный
прогон успешен (`SMOKE_OK`).

**Критерий закрытия**

Smoke-job стабильно загружает ядро в QEMU.

## ISSUE-11 — `ubuntu-latest` 26.04 ломает QEMU smoke (P1)

**Доказательства**

- Прогон `41589323199` (2026-11-03) выполнен на `ubuntu-26.04`
  (`ubuntu26/20261025.179`), QEMU `10.2.1`, SeaBIOS `1.17.0`.
- `qemu.log` — 303 байта: баннер SeaBIOS/iPXE, `Booting from ROM..`, ESC-
  последовательности очистки экрана, затем QEMU завершается примерно через
  0.2 с. Вывода ядра нет.
- Зелёные прогоны `41185886775` и `41182917469` (2026-11-01) выполнены на
  `ubuntu-24.04`, QEMU `8.2.2`, SeaBIOS `1.16.3`; `SMOKE_OK` примерно за 2.5 с.
- Локально (Ubuntu 24.04, QEMU 8.2.2) тот же defconfig `bzImage` грузится,
  в том числе с `-cpu max` (LA57).
- `dev` содержит только коммиты в `work/`/CI поверх baseline
  `541c43310e85`; исходники ядра не менялись.
- Аннотация GitHub: миграция `ubuntu-latest` на Ubuntu 26 начинается
  2026-10-19.

**Влияние**

Smoke-job блокирующий, поэтому каждый push в `dev`/PR красный, хотя ядро
собирается без предупреждений.

**Мера**

- Smoke-job закреплён на `runs-on: ubuntu-24.04`
  (`work/pipelines/github/smoke.yml`).
- `smoke-qemu.sh` теперь пишет версии QEMU/gcc/ld, фиксирует код возврата,
  добавляет `earlycon`/`earlyprintk` и на падении снимает
  `qemu-debug.log` (`-d int,cpu_reset`).
- На падении workflow выгружает `bzImage` для локального анализа.
- `ubuntu-latest` в остальных jobs пока оставлен: они не запускают QEMU.

**Критерий закрытия**

Причина несовместимости 26.04 (QEMU 10.2.1 vs GCC 15/binutils) определена;
pin снят или заменён документированным workaround.

**Follow-up**

Матрица в контейнерах: сборка 24.04 / 26.04 × QEMU 8.2.2 / 10.2.1 с
загруженным failing `bzImage`, чтобы разделить toolchain и QEMU.
