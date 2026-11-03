# Статус выполнения

| Поле | Значение |
|---|---|
| Дата | 2026-11-01 |
| Ветка | `dev` |
| Baseline-тег | `baseline-20261031` (`541c43310e85`) |
| Окружение | 12 CPU, 5.6 ГБ RAM + 4 ГБ swap, 32 ГБ свободно на старте |

## Выполнено

| Шаг | Команда | Результат |
|---|---|---|
| CI установлен | `work/pipelines/install.sh` | 5 workflow в `.github/workflows/` |
| Baseline-тег | `git tag -a baseline-20261031 541c43310e85` | создан |
| defconfig | `make O=work/build/defconfig defconfig && make -j4` | rc=0, 559 с, 966 МБ |
| allmodconfig | `JOBS=3 work/pipelines/scripts/build.sh` | rc=0, 9467 с, 12 ГБ |
| KUnit | `JOBS=4 work/pipelines/scripts/kunit.sh` | rc=0, 303 с, 8599 passed / 39 skipped / 0 failed |
| checkpatch | `BASE=origin/master work/pipelines/scripts/checkpatch.sh` | rc=0, 0 errors, 1 warning (FILE_PATH_CHANGES) |
| sparse | `PATH=/tmp/opencode/sparse-src:$PATH JOBS=4 work/pipelines/scripts/sparse.sh` | rc=0, 2957 checks, 211 errors, 1252 warnings (baseline; blocking on increase) |
| QEMU smoke | `work/pipelines/scripts/smoke-qemu.sh` | rc=0, `SMOKE_OK`, ~5 с |
| machine 7.2.6 | `JOBS=3 work/pipelines/scripts/build-machine.sh` | rc=0, `7.2.6-machine`, 213 модулей, ~35 мин, debug off |
| QEMU smoke (machine) | `QEMU_CPU=max KERNEL_IMAGE=work/build/machine-7.2.6/artifacts/bzImage-7.2.6-machine work/pipelines/scripts/smoke-qemu.sh` | rc=0, `SMOKE_OK`, ~5 с |
| Push CI | `git push origin dev` | выполнен; `origin/dev` обновлён |
| Branch protection | GitHub API PUT | `dev`: 5 required checks |
| CI blocking | workflows | `sparse`/`kunit`/`smoke` без `continue-on-error` |
| CI first run | GitHub Actions `41185886775` | checkpatch/sparse/kunit/smoke success; allmodconfig отменён по timeout 180 мин |
| CI retry config | `build.yml`, `allmodconfig-ci.config` | timeout 360 мин, `JOBS=4`, KASAN/UBSAN/KFENCE/GCOV/debug off |
| CI runner migration | `smoke.yml` | smoke закреплён на `ubuntu-24.04` (QEMU 10.2.1 ломает boot, ISSUE-11) |
| Smoke diagnostics | `smoke-qemu.sh` | версии QEMU/gcc/ld, код возврата, `earlycon`, `qemu-debug.log` |

Артефакты:

- `work/build/defconfig/arch/x86/boot/bzImage` — 14 324 736 байт.
- `work/build/allmodconfig/arch/x86/boot/bzImage` — 43 606 016 байт.
- `work/build/allmodconfig/.config`, `work/build/allmodconfig/build.log`.
- `work/build/kunit/kunit.json` — 2 109 095 байт.
- `work/build/checkpatch.log`.
- `work/build/sparse/sparse.log`, `work/build/sparse/summary.txt` — baseline sparse.
- `work/build/smoke/qemu.log` — вывод загрузки ядра.
- `work/build/machine-7.2.6/artifacts/bzImage-7.2.6-machine` — 14 639 616 байт.
- `work/build/machine-7.2.6/summary.txt`, `config.diff`.
- `work/build/machine-7.2.6/modules/lib/modules/7.2.6-machine` — staging.

## Не выполнено / блокеры

| Блокер | Причина | Что нужно |
|---|---|---|
| Системный `sparse` | пакет 0.6.4 не проходит `scripts/checker-valid.sh` | собрать sparse из исходников (в CI уже так) |
| CI allmodconfig | первый прогон отменён по `timeout-minutes: 180`; retry с 360 мин ещё не завершён | дождаться результата retry |
| ccache | пакет не установлен | `deps.sh` |
| clang/rust/sphinx | не установлены | `deps.sh --full` |

## Состояние диска после прогонов

```
/dev/nvme0n1p4  99G  73G  22G  77% /
work/build      17G
  allmodconfig  12G
  src-7.2.6     1.8G
  machine-7.2.6 1.9G
```

## Следующие шаги

1. Дождаться retry CI-прогона (`timeout 360`, `JOBS=4`) и зафиксировать
   результат `allmodconfig`.
2. Разобрать baseline sparse (211 errors): основная масса — address-space
   `__rcu` сравнения; новые regressions блокируются порогом.
3. Включить `enforce_admins`/PR-only для `dev` и защиту `main` после финального
   push документации.
4. `sparse`/`kunit`/`smoke` переведены в blocking.
5. При необходимости установить `7.2.6-machine` в `/boot` по `plans/runbook.md`
   (сейчас не установлено; нужен sudo).
6. ISSUE-11: прогнать матрицу 24.04/26.04 × QEMU 8.2.2/10.2.1 на failing
   `bzImage` и снять pin после выяснения причины.
