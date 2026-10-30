# Архитектура дерева ядра

Документ описывает структуру и потоки управления проекта без углубления в
отдельные драйверы.

## 1. Карта каталогов

| Каталог | Размер | Роль |
|---|---:|---|
| `arch/` | 164 МБ | архитектуры: загрузка, MMU, IRQ, syscall ABI |
| `drivers/` | 1.2 ГБ | драйверы оборудования |
| `fs/` | 49 МБ | VFS и файловые системы |
| `kernel/` | 16 МБ | планировщик, время, трассировка, RCU, LSM-хуки |
| `mm/` | 6.2 МБ | аллокаторы, page cache, NUMA, swap |
| `net/` | 38 МБ | сокеты, TCP/IP, netfilter, netdev |
| `tools/` | 98 МБ | KUnit, kselftests, perf, bpftool |
| `Documentation/` | 82 МБ | RST-документация |
| `rust/` | — | инфраструктура Rust for Linux |

## 2. Слои и зависимости

```
+----------------------------------------------------------+
| userspace: syscalls, UAPI (include/uapi, fs, net, io_uring)|
+----------------------------------------------------------+
| подсистемы: VFS, net, block, security, sound, virt          |
+----------------------------------------------------------+
| ядро: sched, mm, locking, time, RCU, tracing                |
+----------------------------------------------------------+
| arch: x86 / arm64 / riscv / ...                             |
+----------------------------------------------------------+
| hardware                                                    |
+----------------------------------------------------------+
```

Правила зависимостей:

- `arch/` может зависеть от `kernel/`, `mm/`, `include/`; обратное — только через
  архитектурные хуки.
- Драйверы используют API подсистем (`driver-api`), а не внутренности друг друга.
- UAPI-заголовки стабильны и не включают внутренние заголовки ядра.

## 3. Подсистемы

### 3.1 Планировщик и время

`kernel/sched/`, `kernel/time/`. Планировщик EEVDF, cgroup CPU-контроллер,
таймеры, clocksource, NOHZ.

### 3.2 Память

`mm/`. Buddy allocator, SLUB, page cache, reclaim, swap, NUMA, THP, KASAN/KFENCE.

### 3.3 VFS и ФС

`fs/`. VFS-слой, dentry/inode, page cache, журналирование (ext4, xfs, btrfs),
сетевые ФС (nfs, cifs).

### 3.4 Сеть

`net/`. socket layer, TCP/UDP, netfilter, routing, netdev, XDP, TLS.

### 3.5 Безопасность

`security/`. LSM (SELinux, AppArmor, Landlock), capabilities, seccomp, IMA/EVM.

### 3.6 Виртуализация

`virt/kvm/`, `arch/*/kvm/`. KVM, vhost, guest support.

### 3.7 Rust

`rust/`. Абстракции ядра на Rust, `CONFIG_RUST`, требует `rustc` + `bindgen`.

## 4. Система сборки

- `Kconfig` — декларация опций и зависимостей.
- `Kbuild`/`Makefile` — правила сборки.
- `scripts/kconfig/` — `conf`, `merge_config.sh`, `merge_config`.
- `scripts/` — `checkpatch.pl`, `get_maintainer.pl`, `config`.

Ключевые артефакты:

| Артефакт | Назначение |
|---|---|
| `.config` | итоговая конфигурация |
| `include/generated/autoconf.h` | C-макросы конфигурации |
| `vmlinux` | несжатое ядро с символами |
| `System.map` | карта символов |
| `Module.symvers` | экспортируемые символы модулей |
| `*.ko` | модули |
| `vmlinux.o`, BTF | промежуточные/отладочные артефакты |

## 5. Потоки данных (упрощённо)

1. Загрузка: `arch/*/boot` → `start_kernel()` (`init/main.c`).
2. Инициализация подсистем: `initcall`-уровни.
3. Runtime: syscall → VFS/net/block → драйвер → hardware.
4. Прерывания: `arch` → IRQ-контроллер → подсистема → драйвер.
5. Трассировка: tracepoints → ring buffer → `tracefs`.

## 6. Документация

| Раздел | Путь |
|---|---|
| Процесс разработки | `Documentation/process/` |
| API ядра | `Documentation/core-api/` |
| Драйверы | `Documentation/driver-api/` |
| Подсистемы | `Documentation/subsystem-apis.rst` |
| Тестирование | `Documentation/dev-tools/testing-overview.rst` |
| Сборка | `Documentation/kbuild/` |
| AI-ассистенты | `Documentation/process/coding-assistants.rst` |
