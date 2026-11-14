# План: Linux-гость на ternary-эмуляторе

Оценки — для одного инженера; зависят от доступности toolchain и QEMU.
Спецификация: `work/spec/ternary-linux.md`.

## Фаза 0 — Baseline (выполнена)

| # | Задача | DoD |
|---|---|---|
| 0.1 | Локальный RV32-toolchain без root | `ternary-linux-deps.sh` создаёт `toolchain.env` |
| 0.2 | Конфигурация гостя | `ternary-linux-guest.config` применяется, символы проверены |
| 0.3 | DTS тернарной машины | `dtc` собирает `ternary.dtb`, QEMU загружает |
| 0.4 | Сборка ядра | `Image` 32-bit RISC-V, `phys_base=0x80400000` |
| 0.5 | QEMU-baseline | лог содержит `SBI specification`, `earlycon: sbi`, `hvc0` |

Факт (2026-11-13):

- Ядро `6.19.0-g5a3449c1fea8` собрано GCC 13.2.0 + binutils 2.42.
- `Image` ≈ 2.3 МиБ; на момент Фазы 0 загрузка до `VFS: Unable to
  mount root fs` (initramfs ещё нет).
- Локально распакованы `gcc-riscv64-unknown-elf`, `qemu-system-misc`,
  `opensbi` 1.7; `sudo` не требуется.
- Выяснено: `CONFIG_RELOCATABLE` надо выключить (bare-metal `ld` не умеет
  `-shared`); `PHYS_RAM_BASE=0x80400000` соответствует `fw_dynamic`;
  отсутствие `mmu-type` в CPU-узле ломает инициализацию таймера в QEMU.

## Фаза 1 — Платформа эмулятора (2–3 дня)

| # | Задача | DoD |
|---|---|---|
| 1.1 | RAM 64 МиБ, байт=трайт | загрузка `Image`/DTB по адресам |
| 1.2 | Загрузчик образов | `--load`, `--load-binary`, `--pc`, `--dtb` |
| 1.3 | MMIO: консоль, таймер, shutdown | SBI DBCN пишет в stdout |
| 1.4 | Пакетный режим и лимит инструкций | `--batch`, `--max-insn`, статистика |
| 1.5 | Ввод для интерактивного userspace | DBCN read/stdin без блокировки эмулятора |

**Зависимости:** Фаза 0. **Риск:** средний.

Факт (2026-11-13): платформа реализована в `emu/rv32.c`; загрузка `Image`,
DTB и initrd, SBI DBCN, `--time-inst`, `--max-insn`, трассировка.

## Фаза 2 — Интерпретатор RV32IMA (3–5 дней)

| # | Задача | DoD |
|---|---|---|
| 2.1 | RV32I + M + A | unit-тесты на каждую инструкцию |
| 2.2 | CSR и привилегированные переходы | `ecall`, `mret`/`sret`, `wfi` |
| 2.3 | Ловушки | невыровненные, illegal, breakpoint |
| 2.4 | Таймер | `time/timeh`, сравнение, прерывание |
| 2.5 | Дизассемблер и трассировка | `--trace` для первых 10k инструкций |

**Зависимости:** Фаза 1. **Риск:** высокий (точность семантики).

Факт (2026-11-13): RV32IMA, CSR, U-mode ecall (cause 8), timer и
невыровненные доступы работают; Linux доходит до userspace. Unit-тесты ISA
перенесены в Фазу 5.

## Фаза 3 — Загрузка Linux (3–5 дней)

| # | Задача | DoD |
|---|---|---|
| 3.1 | SBI base/time/dbcn/srst | гость печатает `Linux version` |
| 3.2 | Старт ядра из эмулятора | `SBI specification`, `hvc0` |
| 3.3 | Initramfs и userspace | `TERNARY_LINUX_OK` |
| 3.4 | Производительность | ≥ 100k гостевых инструкций/с |

**Зависимости:** Фаза 2. **Риск:** высокий (SBI, таймер, FDPIC).

Факт (2026-11-13): гость печатает `Linux version`, `SBI specification v2.0`,
`earlycon: sbi`, `console [hvc0]`; исправлены SBI probe и FDT patch.
Замеренная скорость интерпретатора — ~36M гостевых инструкций/с.

## Фаза 4 — Userspace (2–4 дня)

| # | Задача | DoD |
|---|---|---|
| 4.1 | FDPIC-toolchain или flat | статический `/init` запускается |
| 4.2 | Initramfs | cpio с `/init`, встроен в ядро |
| 4.3 | Маркер и smoke | `TERNARY_LINUX_OK` в логе |
| 4.4 | Минимальные syscalls | `write`, `exit`, `reboot` |

**Зависимости:** Фаза 3. **Риск:** средний (toolchain).

Факт (2026-11-13): выбран flat-формат; минимальный `/init` (121 байт,
64-байтный `flat_hdr`) печатает `TERNARY_LINUX_OK` через `write(2)` и
завершается `exit(0)`. Initramfs — `cpio.gz` ≈200 байт. Оба запуска
(`tern-linux` и QEMU с `-initrd`) находят маркер. Паника
`Attempted to kill init` после `exit` ожидаема.

## Фаза 5 — Hardening (постоянно)

| # | Задача | DoD |
|---|---|---|
| 5.1 | Регрессионные тесты ISA/SBI | тесты в `work/ternary-linux/tests/` |
| 5.2 | CI-job | сборка и boot в QEMU на каждом PR |
| 5.3 | Документация | runbook и troubleshooting |
| 5.4 | Опционально: MMU/IRQ | отдельная ветка, не блокирует baseline |

Факт (2026-11-14):

- 5.1 выполнена: `test_rv32i`, `test_rv32m`, `test_rv32a`, `test_sbi`
  (`work/ternary-linux/tests/`) собираются `--march=rv32ima_zicsr_zifencei`
  и исполняются на `tern-linux`; PASS/FAIL определяется по test-finisher
  (`0x0010_0000`) и маркеру `test finisher PASS`.
- 5.2 выполнена: `ternary-linux-isa.sh` + reusable workflow
  `.github/workflows/ternary-linux.yml` (`ternary-linux / isa`); job
  собирает `tern-linux` из ветки эмулятора `feature/linux-guest` и гоняет
  весь набор тестов.
- 5.3 выполнена: команды и troubleshooting добавлены в `plans/runbook.md`,
  контракт тестов — в `spec/ternary-linux.md`.
- 5.4 остаётся опциональной.

## Команды

```bash
make -C work ternary-linux-deps
make -C work ternary-linux-check
make -C work ternary-linux-build
make -C work ternary-linux-userspace
make -C work ternary-linux-boot
make -C work ternary-linux-run
make -C work ternary-linux-isa
```

## Текущий статус

| Фаза | Статус |
|---|---|
| 0 | выполнена |
| 1 | выполнена функционально |
| 2 | выполнена функционально; unit-тесты — Фаза 5 |
| 3 | выполнена (`TERNARY_LINUX_OK`) |
| 4 | выполнена (flat + initramfs) |
| 5 | выполнена (ISA/SBI-тесты, CI-job, документация); 5.4 опционально |
