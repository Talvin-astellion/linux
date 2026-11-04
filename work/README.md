# work

Инженерный пакет для проекта `/home/talvin/System/linux` (форк mainline Linux).

Содержит спецификацию, реестр проблем, план работ и пайплайны сборки/проверок.

## Быстрый старт

```bash
cd /home/talvin/System/linux

# 1. Установить локальные зависимости (Ubuntu/Debian)
work/pipelines/scripts/deps.sh          # local: base + clang/rust/sphinx/qemu/lint
work/pipelines/scripts/deps.sh ci       # CI: только сборочный base
work/pipelines/scripts/deps.sh smoke    # CI + qemu
work/pipelines/scripts/deps.sh lint     # shellcheck + actionlint
work/pipelines/scripts/deps.sh check    # git + perl для checkpatch

# 2. Локальная сборка allmodconfig
work/pipelines/scripts/build.sh

# 2b. Сборка под текущую машину (7.2.6)
work/pipelines/scripts/build-machine.sh

# 3. Проверки
work/pipelines/scripts/checkpatch.sh
work/pipelines/scripts/sparse.sh
work/pipelines/scripts/kunit.sh
work/pipelines/scripts/security-kunit.sh
work/pipelines/scripts/smoke-qemu.sh
work/pipelines/scripts/smoke-kasan.sh

# 4. Гигиена и метрики
work/pipelines/scripts/lint.sh
work/pipelines/scripts/metrics.sh --check
```

GitHub Actions уже лежат в `.github/workflows/` и являются единственным
источником истины; копирования из `work/` больше нет.

Через make:

```bash
make -C work help
make -C work deps
make -C work build
make -C work machine
make -C work checkpatch
make -C work sparse
make -C work kunit
make -C work security-kunit
make -C work smoke
make -C work smoke-kasan
make -C work lint
make -C work metrics
make -C work ci-check
```

## Структура

| Путь | Назначение |
|---|---|
| `spec/project-spec.md` | Полная спецификация проекта |
| `spec/architecture.md` | Обзор подсистем и структуры дерева |
| `spec/build-and-test.md` | Сборка, тестирование, ресурсы, артефакты |
| `spec/metrics.md` | Метрики функциональности (генерируется) |
| `problems/issues.md` | Реестр проблем P0–P2 с доказательствами |
| `plans/roadmap.md` | Фазы работ 0–5, DoD, оценки |
| `plans/runbook.md` | Операционные процедуры и troubleshooting |
| `pipelines/` | Скрипты и конфиги пайплайнов |
| `pipelines/scripts/` | Локальные скрипты (те же, что в CI) |
| `pipelines/configs/` | Фрагменты конфигурации ядра и security-конфиги |
| `.github/workflows/` | Канонические workflow-файлы |
| `.github/dependabot.yml` | Обновление GitHub Actions |

## Принципы

- Логика пайплайнов живёт в `work/pipelines/scripts/`, CI только вызывает её.
- Сборка всегда out-of-tree (`O=work/build/...`), дерево ядра не засоряется.
- Ресурсы ограничиваются автоматически: `JOBS` вычисляется по RAM, диск проверяется перед сборкой.
- Все проверки в CI blocking; `sparse` ограничен baseline 211 errors / 1252 warnings и падает при превышении.
- Actions закреплены по полному SHA; зависимости обновляет Dependabot.
- Метрики генерируются `metrics.sh` и проверяются в CI на свежесть.

## Статус

- Спецификация: draft 1.
- Baseline: тег `baseline-20261031` (`541c433`).
- Локально пройдены: defconfig, allmodconfig, KUnit, security KUnit, checkpatch, QEMU smoke, KASAN smoke — см. `plans/status.md`.
- Машинная сборка: `7.2.6-machine`, 213 модулей, QEMU smoke OK.
- AI-политика: `AGENTS.md`; partial clone описан в `plans/runbook.md`.
- CI: 7 workflow, 9 jobs (новые `meta`/`security` ещё не прогонялись);
  run `41729742211` — 5/5 green, `checks / checkpatch` проходит за ~34 с
  (run `41779398786`).
- `sparse`: baseline 211 errors / 1252 warnings, blocking on increase; системный пакет 0.6.4 не подходит, CI собирает sparse из исходников.
- Security baseline: action pinning, gitleaks, security KUnit (6 suites), KASAN/KFENCE/UBSAN smoke.
