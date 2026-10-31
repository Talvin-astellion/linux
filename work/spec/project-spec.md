# Спецификация проекта: linux

| Поле | Значение |
|---|---|
| Проект | `/home/talvin/System/linux` |
| Upstream | mainline Linux (git.kernel.org) |
| Remote | `https://github.com/Talvin-astellion/linux` |
| Лицензия | GPL-2.0 (см. `COPYING`) |
| HEAD | `541c43310e85` (2026-02-12) |
| Версия в Makefile | `6.19.0` |
| Ветки | `dev`, `main` (обе на `origin/master`) |
| Документ | draft 1, 2026-10-31 |

## 1. Назначение и границы

Репозиторий — форк mainline Linux, используемый как инженерная площадка для
воспроизводимой сборки, статических проверок и автоматизированного тестирования.

Входит в область работ:

- сборка ядра и модулей;
- статические проверки (`checkpatch`, `sparse`);
- юнит-тесты KUnit;
- CI/CD на GitHub Actions;
- документация процессов и ресурсных ограничений.

Не входит:

- поддержка дистрибутива и пакетирование;
- гарантии стабильности API ядра;
- продуктовые фичи и драйверы (появятся отдельной спецификацией при необходимости).

## 2. Метаданные репозитория

- Upstream: mainline Linux, теги `v6.x` / `v7.x`.
- Текущий снимок: merge window 7.0, при этом `Makefile` содержит `6.19.0`.
  Расхождение зафиксировано в `problems/issues.md` (ISSUE-04).
- Локальных коммитов относительно `origin/master` нет.
- Теги в форке отсутствуют, baseline не зафиксирован.
- `.git` занимает 6.0 ГБ, рабочее дерево — 7.7 ГБ.

## 3. Структура репозитория

| Каталог | Назначение |
|---|---|
| `arch/` | архитектурно-зависимый код (x86, arm64, riscv, …) |
| `block/` | блочный слой |
| `certs/` | сертификаты для подписи модулей |
| `crypto/` | криптографические алгоритмы |
| `Documentation/` | документация ядра (Sphinx/RST) |
| `drivers/` | драйверы устройств |
| `fs/` | файловые системы |
| `include/` | заголовочные файлы ядра и UAPI |
| `init/` | инициализация ядра |
| `io_uring/` | подсистема io_uring |
| `ipc/` | IPC |
| `kernel/` | ядро: планировщик, время, трассировка, безопасность |
| `lib/` | библиотеки общего назначения |
| `mm/` | управление памятью |
| `net/` | сетевой стек |
| `rust/` | инфраструктура Rust for Linux |
| `samples/` | примеры |
| `scripts/` | Kbuild, Kconfig, checkpatch, kunit и др. |
| `security/` | LSM и подсистемы безопасности |
| `sound/` | звук (ALSA/ASoC) |
| `tools/` | userspace-инструменты, KUnit, kselftests, perf |
| `usr/` | initramfs |
| `virt/` | виртуализация (KVM) |

## 4. Архитектура

### 4.1 Слои

1. **Архитектурный слой** (`arch/`) — загрузка, MMU, прерывания, syscall ABI.
2. **Ядро** (`kernel/`, `mm/`, `sched`, `locking`) — планировщик, память, синхронизация.
3. **Подсистемы** (`fs/`, `net/`, `block/`, `security/`, `sound/`) — функциональность.
4. **Драйверы** (`drivers/`) — доступ к оборудованию через подсистемы.
5. **UAPI** (`include/uapi/`, `Documentation/userspace-api/`) — контракт с userspace.

### 4.2 Сборка

- Система: Kbuild + Kconfig.
- Точка входа: top-level `Makefile`.
- Конфигурация: `make <target>config` (`defconfig`, `allmodconfig`, `tinyconfig`, …).
- Out-of-tree сборка: `make O=<dir>`.
- Модули: `make modules`, `make modules_install`.
- Rust: `rust/`, требует `rustc`, `bindgen`, `CONFIG_RUST`.

### 4.3 Поток сборки

```
.config (Kconfig)
   -> syncconfig / auto.conf
   -> include/generated/*
   -> compile vmlinux + modules
   -> System.map, Module.symvers, BTF (если включён)
```

## 5. Сборка: цели и матрица toolchain

| Цель | Назначение | Приоритет |
|---|---|---|
| `allmodconfig` | максимальное покрытие компиляции | baseline (CI) |
| `defconfig` | быстрая сборка | smoke/локально |
| `machine` | сборка под текущую машину (7.2.6) | локально |
| `tinyconfig` | минимальная конфигурация | по необходимости |
| `kunitconfig` | KUnit через UML | baseline (CI) |

Toolchain:

| Инструмент | Статус | Нужен для |
|---|---|---|
| gcc 13.3 | есть | сборка ядра |
| ld 2.42 | есть | линковка |
| make 4.3, bc, flex, bison | есть | Kbuild |
| pahole 1.25 | есть | BTF (в CI отключаем) |
| python3 3.14 | есть | KUnit, scripts |
| perl | есть | checkpatch |
| openssl headers | есть | подпись/сертификаты |
| sparse | нет | статический анализ |
| clang/LLVM | нет | альтернативная сборка |
| rustc/cargo/bindgen | нет | Rust for Linux |
| sphinx | нет | `make htmldocs` |
| ccache | нет | ускорение CI |

## 6. Тестирование

| Уровень | Инструмент | Команда |
|---|---|---|
| Стиль | checkpatch | `scripts/checkpatch.pl --no-tree --strict --git <range>` |
| Статический анализ | sparse | `make C=2 CHECK=sparse` |
| Юнит-тесты | KUnit (UML) | `tools/testing/kunit/kunit.py run` |
| Интеграционные | kselftests | `make -C tools/testing/selftests` (позже) |
| Smoke загрузки | QEMU | `work/pipelines/scripts/smoke-qemu.sh` |

## 7. CI/CD

- Платформа: GitHub Actions.
- Триггеры: `push` в `dev`/`main`, `pull_request`, `workflow_dispatch`.
- Jobs: `checkpatch`, `sparse`, `allmodconfig`, `kunit`, `smoke`.
- Канонические workflows: `work/pipelines/github/`.
- Установка: `work/pipelines/install.sh` копирует их в `.github/workflows/`.
- Ограничения GitHub-hosted runner: ~14 ГБ диска, поэтому allmodconfig
  собирается с отключённым debug-info и `-j2`, плюс очистка образа раннера.

## 8. Конвенции

- Код — по `Documentation/process/coding-style.rst`.
- Патчи — по `Documentation/process/submitting-patches.rst`, обязателен DCO.
- AI-ассистенты — по `Documentation/process/coding-assistants.rst`.
- Коммиты: атомарные, с префиксом подсистемы, `Signed-off-by`.
- Ветки: `dev` — интеграция, `main` — зеркало upstream.

## 9. Ресурсные требования

| Ресурс | Локально | CI |
|---|---|---|
| CPU | 12 | 4 |
| RAM | 5.6 ГБ + 4 ГБ swap | 16 ГБ |
| Диск | 32 ГБ свободно | ~14 ГБ после очистки |
| `JOBS` | авто: RAM/2 (≈2–3) | 2 (allmodconfig), 4 (sparse/kunit) |
| Время allmodconfig | 1–3 ч (оценка) | 1–3 ч |

## 10. Риски и критерии приёмки

Риски и меры — в `problems/issues.md`. Критерии приёмки пакета:

1. Спецификация покрывает разделы 1–10.
2. Реестр проблем содержит P0–P2 с доказательствами.
3. План работ содержит фазы с DoD и оценками.
4. Локальные скрипты запускаются без CI.
5. `install.sh` разворачивает workflows.
6. Исходники ядра не изменяются, кроме `work/` и `.github/workflows/`.
