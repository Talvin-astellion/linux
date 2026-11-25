# systemd как PID 1 в initramfs ternary-эмулятора

Пайплайн собирает `~/System/systemd` кросс-компилятором для RV32, упаковывает
`systemd` в `cpio.gz` и загружает этот initramfs через `emu/tern-linux`.

## Почему потребовалось расширение эмулятора

Существующий гость из `~/System/linux/work` — RV32IMA + `CONFIG_MMU=n` + плоский
`/init`. systemd как PID 1 требует MMU, динамического загрузчика, `/proc`,
`/sys`, `/dev` и cgroups, поэтому на старом профиле запуск невозможен.

В `emu/rv32.c` добавлена поддержка **Sv32**:

* `satp` режимы `Bare` и `Sv32`, запись `PPN` и игнорирование неизвестных режимов;
* two-level page table walk, 4 KiB и 4 MiB страницы, биты `V/R/W/X/U/A/D`;
* проверки прав для U/S-режима, `mstatus.SUM` и `mstatus.MXR`;
* page-fault cause 12/13/15 с `stval` = виртуальный адрес;
* `sfence.vma` и сброс 128-set 2-way TLB при записи `satp`;
* SBI DBCN использует физические адреса и не зависит от `satp`.

Старый NOMMU-профиль продолжает работать: при `satp=0` трансляции нет.
Тесты: `make -C emu test-rv32` (host) и
`make -C emu test-rv32-guest` (bare-metal гость, нужен
`riscv64-unknown-elf-gcc`).

## Требования

| Компонент | Требование |
| --- | --- |
| systemd | checkout в `~/System/systemd` (переопределяется `SYSTEMD_SRC`) |
| Ядро | RV32IMA, `CONFIG_MMU=y`, `defconfig + 32-bit.config + tools/systemd/configs/riscv32-systemd-mmu.config` |
| Toolchain | RV32 glibc: `riscv32-linux-gnu-gcc` с `-march=rv32ima -mabi=ilp32` и 64-bit `time_t` |
| Хост | `meson`, `ninja`, `cpio`, `gzip`, `awk` |
| Эмулятор | собранный `emu/tern-linux` |

`riscv64-unknown-elf-gcc` из NOMMU-пайплайна **не подходит** для systemd:
в нём нет glibc, динамического загрузчика и 64-bit `time_t`. Он нужен только
для bare-metal теста Sv32.

Проверка готовности (не падает при отсутствии зависимостей):

```bash
make -C tools/systemd check
make -C tools/systemd verify   # жёсткая проверка, exit 1 при отсутствии зависимостей
```

Переменные окружения: `SYSTEMD_SRC`, `LINUX_SRC`, `BUILD_DIR`,
`CROSS_COMPILE`, `CROSS_SYSROOT`, `KERNEL_IMAGE`, `DTB`, `KERNEL_CONFIG`,
`DTC`, `EMULATOR`, `KERNEL_ADDR`, `RUN_TIMEOUT`, `MAX_INSN`, `TIME_MODE`,
`MANIFEST`, `RUN_MODE`, `STATS`. `RUN_MODE` равен `initrd` или `switch-root` и
выбирает отдельные файлы лога и манифеста. `STATS=1` добавляет `--stats` в
аргументы эмулятора, и счётчики инструкций/Sv32 попадают в лог выбранного режима.

`DTB` по умолчанию указывает на `build/ternary-mmu.dtb`; цель `dtb` собирает его
из `tools/systemd/dts/ternary-mmu.dts` с `mmu-type = "riscv,sv32"`.

## Пайплайн

```bash
make -C emu linux

make -C /home/talvin/System/linux O=/path/to/build-rv32-mmu \
  ARCH=riscv CROSS_COMPILE=riscv32-linux-gnu- defconfig
scripts/kconfig/merge_config.sh -m /path/to/build-rv32-mmu/.config \
  arch/riscv/configs/32-bit.config \
  /home/talvin/System/ternary/tools/systemd/configs/riscv32-systemd-mmu.config
make -C /home/talvin/System/linux O=/path/to/build-rv32-mmu \
  ARCH=riscv CROSS_COMPILE=riscv32-linux-gnu- -j4 Image

KERNEL_IMAGE=/path/to/build-rv32-mmu/arch/riscv/boot/Image \
KERNEL_CONFIG=/path/to/build-rv32-mmu/.config \
make -C tools/systemd all
```

Отдельные шаги:

```bash
make -C tools/systemd dtb     # Sv32 DTB в build/ternary-mmu.dtb
make -C tools/systemd build   # meson cross build + install в build/sysroot
make -C tools/systemd initrd  # systemd + libs + smoke unit → build/initramfs-systemd.cpio.gz
make -C tools/systemd run     # загрузка под tern-linux, проверка SYSTEMD_OK
make -C tools/systemd switch-root-initrd  # initramfs с /newroot → build/initramfs-switch-root.cpio.gz
make -C tools/systemd run-switch-root     # INITRD_OK → switch-root → SWITCH_ROOT_OK
make -C tools/systemd manifest                    # build/manifest-initrd.txt
make -C tools/systemd verify-manifest             # сверка успешного initrd-запуска
make -C tools/systemd verify-manifest-switch-root # сверка успешного switch-root
```

Артефакты находятся в `tools/systemd/build/`: `ternary-mmu.dtb`,
`meson-cross-rv32.txt`, `systemd/`, `sysroot/`, `initrd-root/`,
`initramfs-systemd.cpio.gz`, `switch-root-root/`, `switch-root-initrd-root/`,
`initramfs-switch-root.cpio.gz`, `emulator-initrd.log`,
`emulator-switch-root.log`, `systemd.env`, `systemd-switch-root.env`,
`manifest-initrd.txt`, `manifest-switch-root.txt`.

Initramfs содержит:

* `/init` → `/usr/lib/systemd/systemd`;
* `/etc/initrd-release`, `default.target` → `initrd.target`;
* `systemd-initrd-smoke.service` в `initrd.target.wants`, который печатает
  `SYSTEMD_OK` в `/dev/console` и инициирует `reboot(RB_POWER_OFF)`;
* `/proc`, `/sys`, `/dev`, `/run`, `/etc`, `/usr/lib`, необходимые `.so` и
  ELF-интерпретатор из sysroot.

Вариант `initramfs-switch-root.cpio.gz` дополнительно содержит `/newroot` с
полным OS tree (`/etc/os-release`, `/init`, собственный `default.target`) и
`ternary-switch-root.service`, который вызывает
`systemctl switch-root /newroot /init`. Initrd-сервис в этом варианте печатает
`INITRD_OK` без выключения; сервис нового root печатает `SWITCH_ROOT_OK` и
выключает машину. Файлы `/newroot` собраны жёсткими ссылками, чтобы образ не
дублировал `.so` и не перекрывал DTB в RAM.

## Проверка результата

`systemd-run.sh` требует в логе `Linux version` и `SYSTEMD_OK`, отсутствие
`Kernel panic`/`SYSTEMD_POWEROFF_FAILED` и нулевой код выхода эмулятора.
`run-switch-root` проверяет отдельные строки `INITRD_OK` и `SWITCH_ROOT_OK`
именно в этом порядке. Таймаут или незавершённое выключение считаются ошибкой.
Логи разделены по режимам: `build/emulator-initrd.log` и
`build/emulator-switch-root.log`.

В начале каждого запуска соответствующий манифест сбрасывается в
`run_verified=0`, поэтому старый успешный результат не переживёт неудачный
повтор. После успешного запуска пишется `build/manifest-initrd.txt` или
`build/manifest-switch-root.txt` с SHA-256 эмулятора, ядра, DTB, initramfs и
лога, версией ядра/компилятора, путём sysroot и ожидаемыми marker-строками.
`verify-manifest` и `verify-manifest-switch-root` пересчитывают хэши текущих
артефактов, проверяют git-ревизии репозитория и systemd, порядок marker-строк
в логе и падают при расхождении или если последний запуск не был успешным.
Это фиксирует рабочую комбинацию ядра, systemd, sysroot и эмулятора; путь можно
переопределить через `MANIFEST`.

Если загрузка не доходит до systemd:

```bash
emu/tern-linux --kernel "$KERNEL_IMAGE" --dtb "$DTB" \
  --initrd tools/systemd/build/initramfs-systemd.cpio.gz \
  --debug-mmu --max-insn 600000000
```

`--debug-mmu` печатает адрес, тип доступа, `satp`, PTE и cause при page fault.

## Ограничения

* Эмулятор остаётся RV32IMA: только Sv32; Sv39/Sv57 и сжатые инструкции
  не поддерживаются.
* Нет host filesystem mount: всё, что нужно systemd, должно быть в initramfs;
  консоль — SBI DBCN.
* Внешнего блочного rootfs нет; `switch-root` проверяется на `/newroot`,
  встроенном в initramfs. В обычном режиме smoke-сервис выключает машину
  напрямую через `reboot(RB_POWER_OFF)`; в switch-root режиме это делает сервис
  нового root. Корректное завершение проверяется по коду выхода эмулятора.
* Сборка systemd с RV32 glibc требует готового кросс-sysroot; этот репозиторий
  не собирает glibc и не гарантирует, что все опции systemd доступны в конкретном
  checkout. Проверка `verify` показывает точную причину, если зависимостей нет.
* Старый NOMMU-пайплайн `make -C work ternary-linux-run` не изменён и по-прежнему
  проверяет `TERNARY_LINUX_OK`.
