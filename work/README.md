# work

Инженерный пакет для проекта `/home/talvin/System/linux` (форк mainline Linux).

Содержит спецификацию, реестр проблем, план работ и пайплайны сборки/проверок.

## Быстрый старт

```bash
cd /home/talvin/System/linux

# 1. Установить зависимости (Ubuntu/Debian)
work/pipelines/scripts/deps.sh

# 2. Локальная сборка allmodconfig
work/pipelines/scripts/build.sh

# 3. Проверки
work/pipelines/scripts/checkpatch.sh
work/pipelines/scripts/sparse.sh
work/pipelines/scripts/kunit.sh
work/pipelines/scripts/smoke-qemu.sh

# 4. Установить GitHub Actions
work/pipelines/install.sh
```

Через make:

```bash
make -C work help
make -C work deps
make -C work build
make -C work checkpatch
make -C work sparse
make -C work kunit
make -C work smoke
make -C work ci-install
```

## Структура

| Путь | Назначение |
|---|---|
| `spec/project-spec.md` | Полная спецификация проекта |
| `spec/architecture.md` | Обзор подсистем и структуры дерева |
| `spec/build-and-test.md` | Сборка, тестирование, ресурсы, артефакты |
| `problems/issues.md` | Реестр проблем P0–P2 с доказательствами |
| `plans/roadmap.md` | Фазы работ 0–4, DoD, оценки |
| `plans/runbook.md` | Операционные процедуры и troubleshooting |
| `pipelines/` | Скрипты и GitHub Actions workflows |
| `pipelines/github/` | Канонические workflow-файлы |
| `pipelines/scripts/` | Локальные скрипты (те же, что в CI) |
| `pipelines/configs/` | Фрагменты конфигурации ядра |

## Принципы

- Логика пайплайнов живёт в `work/pipelines/scripts/`, CI только вызывает её.
- Сборка всегда out-of-tree (`O=work/build/...`), дерево ядра не засоряется.
- Ресурсы ограничиваются автоматически: `JOBS` вычисляется по RAM, диск проверяется перед сборкой.
- Тяжёлые проверки (`sparse`, `kunit`) на старте advisory (`continue-on-error`), после стабилизации становятся blocking.

## Статус

- Спецификация: draft 1.
- Baseline: тег `baseline-20261031` (`541c433`).
- Локально пройдены: defconfig, allmodconfig, KUnit, checkpatch, QEMU smoke — см. `plans/status.md`.
- CI: workflows установлены в `.github/workflows/`, нужен push и первый прогон.
- `sparse`: baseline 211 errors / 1252 warnings (advisory); системный пакет 0.6.4 не подходит, CI собирает sparse из исходников.
