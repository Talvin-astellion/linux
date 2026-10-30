# Реестр проблем

Приоритеты: **P0** — блокирует работу, **P1** — важно, **P2** — гигиена.
Статус: `open`, `mitigated`, `closed`.

## Сводка

| ID | Приоритет | Проблема | Статус |
|---|---|---|---|
| ISSUE-01 | P0 | Нет воспроизводимой сборки и baseline | closed |
| ISSUE-02 | P0 | Ресурсный дефицит (RAM/диск) для allmodconfig | mitigated |
| ISSUE-03 | P1 | Нет CI и автоматических проверок | mitigated |
| ISSUE-04 | P1 | Расхождение версии Makefile и upstream-тегов | open |
| ISSUE-05 | P1 | Несогласованная топология веток, нет тегов | mitigated |
| ISSUE-06 | P1 | Дыры в toolchain (sparse, clang, rust, sphinx) | open |
| ISSUE-07 | P1 | Ограничения GitHub-hosted runner | open |
| ISSUE-08 | P2 | Тяжёлый `.git` (6.0 ГБ) | open |
| ISSUE-09 | P2 | Нет локальной политики AI-вклада | open |
| ISSUE-10 | P2 | Нет smoke-теста загрузки и хранения артефактов | mitigated |

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

**Критерий закрытия**

Версия однозначно объяснена и зафиксирована; артефакты имеют корректный `KERNELRELEASE`.

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
необходимый job-у.

**Критерий закрытия**

Базовый набор ставится одной командой; `sparse` доступен локально и в CI.

## ISSUE-07 — Ограничения GitHub-hosted runner (P1)

**Доказательства**

Стандартный runner: ~4 CPU, 16 ГБ RAM, ~14 ГБ свободного диска.

**Влияние**

allmodconfig не помещается по диску без очистки и отключения debug-info.

**Мера**

`ci-cleanup.sh` (удаление dotnet/android/ghc/CodeQL/boost, docker prune),
CI-фрагмент без debug-info, `JOBS=2`, артефакты только логи/конфиг.
При необходимости — self-hosted runner.

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
