# Сборка и тестирование

Практическое руководство: команды, ресурсы, артефакты, ограничения.

## 1. Зависимости

Установка:

```bash
work/pipelines/scripts/deps.sh          # базовый набор
work/pipelines/scripts/deps.sh --full   # + clang, rust, sphinx, qemu
```

Базовый набор (Ubuntu/Debian):

```
build-essential bc bison flex libssl-dev libelf-dev libncurses-dev
dwarves cpio kmod rsync git perl python3 sparse ccache
```

Полный набор дополнительно: `clang lld llvm rustc cargo python3-sphinx
qemu-system-x86 bindgen`.

Проверка:

```bash
for t in gcc make bc flex bison pahole python3 perl sparse ccache; do
  command -v "$t" >/dev/null && echo "OK $t" || echo "MISSING $t"
done
```

## 2. Конфигурации

| Конфигурация | Команда | Назначение |
|---|---|---|
| `defconfig` | `make O=work/build/defconfig defconfig` | быстрая сборка |
| `allmodconfig` | `make O=work/build/allmodconfig allmodconfig` | максимальное покрытие |
| CI-фрагмент | `work/pipelines/configs/allmodconfig-ci.config` | отключает debug-info |
| KUnit | `tools/testing/kunit/configs/all_tests.config` | юнит-тесты |

CI-фрагмент:

```
CONFIG_DEBUG_INFO_NONE=y
# CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT is not set
# CONFIG_DEBUG_INFO_DWARF4 is not set
# CONFIG_DEBUG_INFO_DWARF5 is not set
# CONFIG_DEBUG_INFO_BTF is not set
# CONFIG_DEBUG_INFO_BTF_MODULES is not set

# CONFIG_KASAN is not set
# CONFIG_UBSAN is not set
# CONFIG_KFENCE is not set
# CONFIG_GCOV_KERNEL is not set
# CONFIG_DEBUG_ATOMIC_SLEEP is not set
# CONFIG_DEBUG_VM is not set
# CONFIG_DEBUG_PAGEALLOC is not set
# CONFIG_DEBUG_KMEMLEAK is not set
```

Фрагмент уменьшает время hosted-runner сборки: debug-info и тяжёлая
инструментация не нужны для проверки компиляции всех модулей.

## 3. Локальная сборка

```bash
# allmodconfig (основной baseline)
JOBS=3 work/pipelines/scripts/build.sh

# переопределить каталог
BUILD_DIR=/mnt/fast/build/allmodconfig work/pipelines/scripts/build.sh
```

Скрипт выполняет:

1. `make O=$BUILD_DIR allmodconfig`;
2. `scripts/kconfig/merge_config.sh -m -O $BUILD_DIR` с CI-фрагментом;
3. `make O=$BUILD_DIR olddefconfig`;
4. `make O=$BUILD_DIR -j$JOBS` с логом в `$BUILD_DIR/build.log`.

### 3.1 Сборка под текущую машину

```bash
JOBS=3 work/pipelines/scripts/build-machine.sh
# или
make -C work machine
```

Скрипт:

1. копирует `/home/talvin/linux-7.2.6` в `work/build/src-7.2.6` и делает
   `mrproper` только в копии (оригинал не меняется);
2. берёт `/boot/config-7.2.6`, применяет `machine.config`
   (`-machine`, `X86_NATIVE_CPU`, без debug/BTF);
3. собирает out-of-tree в `work/build/machine-7.2.6` с `-j3`;
4. ставит модули в staging `work/build/machine-7.2.6/modules`;
5. складывает `bzImage`, `vmlinux`, `System.map`, `config`, `Module.symvers`
   в `artifacts/`.

Результат baseline: `7.2.6-machine`, 213 модулей, `bzImage` 14 639 616 байт,
`vmlinux` 70 962 872 байт, ~35 мин при `-j3`, debug-info/BTF отключены.
QEMU smoke для native-ядра: `QEMU_CPU=max`.

Оригинальное дерево `/home/talvin/linux-7.2.6` и `/lib/modules/7.2.6` не
затрагиваются; установка в `/boot` не выполняется.

## 4. Проверки

### 4.1 checkpatch

```bash
BASE=origin/master work/pipelines/scripts/checkpatch.sh
```

Проверяет `git diff $BASE..HEAD`. Ошибки (`ERROR:`) валят job, предупреждения
(`WARNING:`) выводятся, но не блокируют.

### 4.2 sparse

```bash
JOBS=4 work/pipelines/scripts/sparse.sh
```

`make defconfig` + `make C=2 CHECK=sparse`. Лог: `work/build/sparse/sparse.log`.
Baseline: 211 errors / 1252 warnings. Скрипт падает при превышении
`SPARSE_MAX_ERRORS`/`SPARSE_MAX_WARNINGS`; это защита от новых регрессий.
Основная масса ошибок — 182 `incompatible types in comparison expression
(different address spaces)`; далее 13 `too long token expansion` и единичные
случаи. Это известный шум sparse, а не новые regressions.

### 4.3 QEMU smoke

```bash
KERNEL_IMAGE=work/build/defconfig/arch/x86/boot/bzImage \
  work/pipelines/scripts/smoke-qemu.sh
```

Собирает минимальный initramfs, загружает ядро в QEMU и ждёт строку
`SMOKE_OK`. Лог: `work/build/smoke/qemu.log`. Скрипт печатает версии
QEMU/gcc/ld, фиксирует код возврата QEMU, добавляет
`earlycon`/`earlyprintk` и на падении снимает трассу
`work/build/smoke/qemu-debug.log` (`-d int,cpu_reset`).

Smoke-job закреплён на `runs-on: ubuntu-24.04`: на Ubuntu 26.04
(QEMU 10.2.1) defconfig `bzImage` падает до вывода консоли (ISSUE-11).
На падении CI также выгружает `arch/x86/boot/bzImage`.

### 4.4 KUnit

```bash
JOBS=4 work/pipelines/scripts/kunit.sh
```

UML-сборка и запуск `all_tests.config`. Результаты: `work/build/kunit/kunit.json`.
Job blocking: локальный baseline 0 failed, CI-прогон зелёный.

## 5. Ресурсы

| Сценарий | JOBS | RAM | Диск (факт) | Время (факт) |
|---|---|---|---|---|
| defconfig | 4 | 5.6 ГБ | 966 МБ | 559 с (~9 мин) |
| allmodconfig | 3 | 5.6 ГБ + swap | 12 ГБ | 9467 с (~2ч38м) |
| allmodconfig (CI) | 4 | 16 ГБ | ~12 ГБ | timeout 360 мин; retry после 180-мин отмены |
| machine 7.2.6 | 3 | 5.6 ГБ | 1.9 ГБ + 1.8 ГБ src | ~35 мин (cold, debug off) |
| KUnit (UML) | 4 | 5.6 ГБ | 260 МБ | 303 с (~5 мин) |
| QEMU smoke | — | 512 МБ | ~1 МБ | ~5 с |
| sparse (defconfig) | 4 | 5.6 ГБ | 967 МБ | 2957 checks, 211 errors, 1252 warnings, ~5 мин (warm) |

Фактические замеры baseline (2026-10-31): defconfig rc=0, allmodconfig rc=0,
KUnit 8599 passed / 39 skipped / 0 failed, checkpatch 0 errors / 1 warning,
sparse rc=0 (211 errors / 1252 warnings, baseline; blocking on increase).

`sparse` из Ubuntu 0.6.4 не проходит `scripts/checker-valid.sh`
(`__typeof_unqual__`); используйте сборку из
`https://git.kernel.org/pub/scm/devel/sparse/sparse.git`.

Ограничители в скриптах:

- `JOBS` по умолчанию = `min(nproc, MemTotal_GB / 2)`.
- `MIN_FREE_GB` проверяется до сборки (по умолчанию 5–10 ГБ).
- `BUILD_DIR` можно вынести на отдельный диск.

## 6. Артефакты

| Путь | Содержимое |
|---|---|
| `work/build/allmodconfig/.config` | итоговый конфиг |
| `work/build/allmodconfig/build.log` | полный лог сборки |
| `work/build/sparse/sparse.log` | вывод sparse |
| `work/build/kunit/kunit.json` | результаты KUnit |
| `work/build/kunit/*.log` | логи UML |
| `work/build/smoke/qemu.log` | вывод QEMU smoke |
| `work/build/sparse/sparse.log` | полный лог sparse |
| `work/build/sparse/summary.txt` | счётчики checks/errors/warnings |
| `work/build/machine-7.2.6/artifacts/` | bzImage/vmlinux/System.map/config |
| `work/build/machine-7.2.6/modules/` | staging-модули 7.2.6-machine |
| `work/build/machine-7.2.6/summary.txt` | итоги машинной сборки |
| `work/build/machine-7.2.6/config.diff` | отличия от `/boot/config-7.2.6` |
| `work/build/src-7.2.6/` | чистая копия исходников 7.2.6 |

`vmlinux` и модули в CI не публикуются: слишком большой размер.

## 7. Ограничения

- GitHub-hosted runner: ~14 ГБ диска, 4 CPU, 16 ГБ RAM. allmodconfig требует
  очистки образа и отключения debug-info; при нехватке — self-hosted runner.
- `pahole` 1.25 и отключённый BTF: не влияет на baseline.
- KUnit `all_tests` может содержать известные падения; список ведётся в
  `problems/issues.md` после первого прогона.
- `make htmldocs` недоступен без `sphinx`.
- Машинная сборка (`7.2.6-machine`) — только локально: конфиг привязан к
  железу и `-march=native`; в CI не запускается.
- QEMU smoke для native-ядра требует `QEMU_CPU=max` (или `-cpu host` с KVM).

## 8. Очистка

```bash
make -C work clean            # rm -rf work/build/
make O=work/build/allmodconfig clean
make O=work/build/allmodconfig mrproper
```
