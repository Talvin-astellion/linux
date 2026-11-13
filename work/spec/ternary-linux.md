# Спецификация: Linux-гость на ternary-эмуляторе

| Поле | Значение |
|---|---|
| Проект | `/home/talvin/System/linux` + `~/System/ternary` |
| Ветка ядра | `trit` (инфраструктура `work/`) |
| Ветка эмулятора | `feature/linux-guest` |
| Upstream-гость | mainline Linux, RISC-V NOMMU |
| Лицензии | GPL-2.0-only (ядро и `work/`), LGPL (эмулятор) |
| Документ | draft 2, 2026-11-13 |

## 1. Назначение

Цель — загрузить немодифицированное ядро Linux для RISC-V (RV32IMA,
`ilp32`, NOMMU, UP) как гостя под интерпретатором, который исполняется на
27-тритной сбалансированной машине `tern-cpu-emu`.

Рассматривались два трека:

- **A (выбран):** RV32IMA-интерпретатор поверх ternary-ядра. Linux не знает о
  тернарной арифметике; один байт гостя отображается на один трайт.
- **B (отложен):** нативный порт (`arch/trit`, backend GCC/LLVM). Блокер —
  жёстко заданный `BITS_PER_BYTE=8` (`include/linux/bits.h:12`) и отсутствие
  ternary-модели C в toolchain.

Трек A позволяет получить загрузку и userspace без изменения ядра Linux.
Эмулятор остаётся отдельным LGPL-проектом; ядро и `work/` — GPL-2.0-only.

## 2. Контракт гостя

| Параметр | Значение |
|---|---|
| ISA | RV32IMA (`i`, `m`, `a`; без `c`, `f`, `d`, `v`, `z*`) |
| ABI | `ilp32` |
| MMU | нет (`CONFIG_MMU=n`) |
| SMP | нет (`CONFIG_SMP=n`) |
| Режим | S-mode, SBI v2.0 (`CONFIG_RISCV_M_MODE=n`) |
| RAM | `0x8000_0000..0x83FF_FFFF` (64 МиБ) |
| Ядро | `0x8040_0000`, `Image`, без релокаций |
| DTB | передаётся в `a1`, `a0=0` |
| Консоль | `console=hvc0 earlycon=sbi` |
| Initramfs | внешний `cpio.gz` по `0x8300_0000`, адреса через DTB; опционально встроенный |
| Время | `timebase-frequency = 10000000` |
| Байт | 1 трайт (значения 0..255) |

SBI-консоль использует DBCN (`sbi_debug_console_write/read`) либо legacy
v0.1; в конфигурации v0.1 отключён. Обязательные SBI-расширения:
`base`, `time`, `dbcn`, `srst` (для shutdown), опционально `ipi`.

DTS разделены: `work/ternary-linux/dts/ternary.dts` содержит `ns16550a` для
консоли OpenSBI в QEMU, `work/ternary-linux/dts/ternary-emu.dts` — без
`/soc` и serial. Свойство `mmu-type = "riscv,none"` в CPU-узле обязательно:
без него OpenSBI/QEMU не инициализирует таймер (проверено). `chosen`
содержит `linux,initrd-start/end`; эмулятор патчит их при `--initrd`.

## 3. Требования к эмулятору

Фаза 1 (платформа):

- RAM 64 МиБ; трайт как байт; little-endian.
- MMIO: SBI-консоль (без реального UART), таймер, halt/shutdown.
- Загрузка `Image` и DTB в память; установка `pc`, `a0`, `a1`.
- Пакетный режим и лимит инструкций; вывод статистики.

Фаза 2 (ISA):

- RV32I: все базовые ALU/load/store/branch/jump, `fence`, `ecall`, `ebreak`.
- M: `mul`, `mulh`, `mulhsu`, `mulhu`, `div`, `divu`, `rem`, `remu`.
- A: `lr.w`, `sc.w`, `amoswap.w`, `amoadd.w`, `amoxor.w`, `amoand.w`,
  `amoor.w`, `amomin.w`, `amomax.w`, `amominu.w`, `amomaxu.w`.
- CSR: `mstatus`, `misa`, `mie`, `mip`, `mtvec`, `mepc`, `mcause`, `mtval`,
  `mscratch`, `satp` (read-only 0), `time/timeh`, `cycle/cycleh`,
  `instret/instreth`, S-mode CSR (`sstatus`, `sie`, `sip`, `stvec`, `sepc`,
  `scause`, `stval`, `sscratch`).
- Ловушки: `ecall` из U-mode → cause 8 (syscall), из S-mode → SBI;
  невыровненный доступ; недопустимый CSR/инструкция; таймерное прерывание.
- Отслеживание привилегий: `sret` переключает S→U, trap из U ставит
  `sstatus.SPP=0`; без этого syscall уходит в SBI и init зависает.
- FDT-патчер: корректный обход node name с выравниванием на 4 байта и
  запись `linux,initrd-start/end` в big-endian.
- Без сжатых инструкций: ядро собирается с `CONFIG_RISCV_ISA_C=n`.

## 4. Требования к ядру

Фрагмент: `work/pipelines/configs/ternary-linux-guest.config`.
База: `arch/riscv/configs/nommu_virt_defconfig` + `32-bit.config`.

Ключевые отличия от базовой конфигурации:

- `CONFIG_RISCV_M_MODE=n` — включает SBI и `RISCV_TIMER`.
- `CONFIG_PHYS_RAM_BASE_FIXED=y`, `CONFIG_PHYS_RAM_BASE=0x80400000`.
- `CONFIG_RELOCATABLE=n` — bare-metal `ld` не поддерживает `-shared`.
- `CONFIG_RISCV_ISA_C/V/Z*/VENDOR_EXT=n` — только RV32IMA.
- `CONFIG_SMP=n`, `CONFIG_MMU=n`.
- `CONFIG_HVC_RISCV_SBI=y`, `CONFIG_SERIAL_EARLYCON_RISCV_SBI=y`.
- `CONFIG_BINFMT_FLAT=y` — выбранный userspace-формат для NOMMU RISC-V.
- `CONFIG_BINFMT_ELF_FDPIC=y` остаётся для будущего FDPIC-toolchain.
- `CONFIG_BLK_DEV_INITRD=y`, `CONFIG_RD_GZIP=y` — внешний `cpio.gz`.

GCC 13 `riscv64-unknown-elf` не поддерживает `-mfdpic`; `elf2flt` нет.
Поэтому `/init` собирается как плоский бинарник: ELF `-march=rv32ima
-mabi=ilp32 -nostdlib -static`, `objcopy -O binary`, затем 64-байтный
`flat_hdr` (`bFLT`, rev 4, entry 64, `FLAT_FLAG_RAM`). Код и rodata
находятся в text-сегменте; relocations отсутствуют.

## 5. Сборка и запуск

```bash
work/pipelines/scripts/ternary-linux-deps.sh
work/pipelines/scripts/ternary-linux-check.sh
work/pipelines/scripts/ternary-linux-build.sh
work/pipelines/scripts/ternary-linux-userspace.sh
work/pipelines/scripts/ternary-linux-boot.sh
work/pipelines/scripts/ternary-linux-run.sh
```

`deps.sh` сначала использует системные инструменты. Если их нет и нет
пароля `sudo`, он скачивает `.deb` через `apt-get download` и распаковывает
в `work/build/ternary-linux/toolchain` (без root). Для GCC создаются
обёртки с `GCC_EXEC_PREFIX`, чтобы host `gcc` не затрагивался.

Артефакты ядра: `work/build/ternary-linux/guest/artifacts/{Image,vmlinux,
System.map,config}`. DTB: `work/build/ternary-linux/dtb/{ternary,ternary-emu}.dtb`.
Userspace: `work/build/ternary-linux/userspace/{init.flat,initramfs.cpio.gz}`.
Логи: `guest/boot.log` (QEMU), `guest/emulator.log` (`tern-linux`).

## 6. Критерии приёмки

Фаза 0:

- `ternary-linux-check.sh` проходит.
- `ternary-linux-build.sh` создаёт `Image` (RV32IMA, ELF32).
- `ternary-linux-boot.sh` в QEMU находит строки `Linux version`,
  `SBI specification`, `earlycon: sbi`, `console [hvc0]`.

Фаза 3–4 (ternary-эмулятор):

- Гость стартует из `tern-cpu-emu`, доходит до userspace.
- В логе есть `TERNARY_LINUX_OK`.
- Скорость ≥ 100k гостевых инструкций/с.
- QEMU с `-initrd` также печатает `TERNARY_LINUX_OK` (проверено).
- Паника `Attempted to kill init` после `exit(0)` — ожидаемое завершение
  тестового `/init`, а не сбой загрузки.

## 7. Риски

| Риск | Влияние | Смягчение |
|---|---|---|
| FDPIC/flat userspace | нет init | flat-формат реализован; FDPIC — на будущее |
| Производительность интерпретатора | ниже 100k inst/s | кэш декодирования, оптимизация hot path, JIT |
| Невыровненные доступы | ложные trap | эмуляция в интерпретаторе + `CONFIG_RISCV_MISALIGNED=y` |
| Нехватка RAM/диска | OOM/сбой сборки | `JOBS` по RAM, `require_disk` |
| Изменения upstream | ломается фрагмент | проверки символов в `ternary-linux-build.sh` |
| DTS без `mmu-type` | OpenSBI не находит таймер | `mmu-type = "riscv,none"` (проверено) |
