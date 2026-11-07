# Статус выполнения

| Поле | Значение |
|---|---|
| Дата | 2026-11-07 |
| Ветка | `trit` (поверх `dev`) |
| Baseline-тег | `baseline-20261031` (`541c43310e85`) |
| Окружение | 12 CPU, 5.6 ГБ RAM + 4 ГБ swap, 32 ГБ свободно на старте |

## Выполнено

| Шаг | Команда | Результат |
|---|---|---|
| CI установлен | `.github/workflows/` | 7 workflow, 9 jobs; `work/pipelines/github/` и `install.sh` удалены |
| Baseline-тег | `git tag -a baseline-20261031 541c43310e85` | создан |
| defconfig | `make O=work/build/defconfig defconfig && make -j4` | rc=0, 559 с, 966 МБ |
| allmodconfig | `JOBS=3 work/pipelines/scripts/build.sh` | rc=0, 9467 с, 12 ГБ |
| KUnit | `JOBS=4 work/pipelines/scripts/kunit.sh` | rc=0, 303 с, 8599 passed / 39 skipped / 0 failed |
| Security KUnit | `JOBS=4 work/pipelines/scripts/security-kunit.sh` | 5 suites/54 теста в `all_tests`; `ipe-parser` добавлен в security config, полный прогон в CI |
| checkpatch | `BASE=origin/master work/pipelines/scripts/checkpatch.sh` | rc=0, 0 errors, 1 warning (FILE_PATH_CHANGES) |
| sparse | `PATH=/tmp/opencode/sparse-src:$PATH JOBS=4 work/pipelines/scripts/sparse.sh` | rc=0, 2957 checks, 211 errors, 1252 warnings (baseline; blocking on increase) |
| QEMU smoke | `work/pipelines/scripts/smoke-qemu.sh` | rc=0, `SMOKE_OK`, ~5 с |
| machine 7.2.6 | `JOBS=3 work/pipelines/scripts/build-machine.sh` | rc=0, `7.2.6-machine`, 213 модулей, ~35 мин, debug off |
| QEMU smoke (machine) | `QEMU_CPU=max KERNEL_IMAGE=work/build/machine-7.2.6/artifacts/bzImage-7.2.6-machine work/pipelines/scripts/smoke-qemu.sh` | rc=0, `SMOKE_OK`, ~5 с |
| KASAN smoke (local) | `SMOKE_APPEND=kasan_multi_shot KERNEL_IMAGE=.../smoke-kasan/arch/x86/boot/bzImage work/pipelines/scripts/smoke-qemu.sh` | rc=0, `SMOKE_OK`, 40 с, QEMU 8.2.2 |
| smoke-qemu path fix | `BUILD_DIR=work/build/smoke-relative ... smoke-qemu.sh` | rc=0, относительный `BUILD_DIR` работает |
| Lint (local) | `PATH=/tmp/opencode/bin:$PATH work/pipelines/scripts/lint.sh` | rc=0; shellcheck 0.11.0, actionlint 1.7.12; исправлен SC2054 в `ternary-test.sh` |
| CI check (local) | `PATH=/tmp/opencode/bin:$PATH make -C work ci-check` | rc=0; lint + `metrics.sh --check` |
| Action pinning | `work/pipelines/scripts/check-actions-pinned.sh` | rc=0, все `uses:` на SHA |
| Secret scan (local) | `PATH=/tmp/opencode/bin:$PATH work/pipelines/scripts/check-secrets.sh` | rc=0, gitleaks 8.30.1, no leaks |
| Push CI | `git push origin dev` | выполнен; `origin/dev` обновлён |
| Push trit | `git push origin trit` | `e4a117ee2e7d`; включая KASAN, sparse и lint fixes |
| Branch protection | GitHub API PUT | `dev`: 5 required checks |
| CI blocking | workflows | `sparse`/`kunit`/`smoke` без `continue-on-error` |
| CI first run | GitHub Actions `41185886775` | checkpatch/sparse/kunit/smoke success; allmodconfig отменён по timeout 180 мин |
| CI retry config | `build.yml`, `allmodconfig-ci.config` | timeout 360 мин, `JOBS=4`, KASAN/UBSAN/KFENCE/GCOV/debug off |
| CI runner migration | `smoke.yml` | smoke закреплён на `ubuntu-24.04` (QEMU 10.2.1 ломает boot, ISSUE-11) |
| Smoke diagnostics | `smoke-qemu.sh` | версии QEMU/gcc/ld, код возврата, `earlycon`, `qemu-debug.log` |
| CI allmodconfig retry | GitHub Actions `41698069267` | success, 2 ч 04 мин |
| CI green run | GitHub Actions `41729742211` | 5/5 jobs success |
| CI canonical green | GitHub Actions `41941909988` | 9/9 jobs success, включая `meta` и `security` |
| CI checkpatch shallow | GitHub Actions `41779398786` | `checks / checkpatch` success за 34 с; ISSUE-12 closed |
| QEMU append fix | `smoke-qemu.sh`, `0341fbf13974` | длинная строка разбита, checkpatch 0 errors |
| Canonical workflows | `.github/workflows/*.yml` | actions закреплены по SHA, добавлены `meta` и `security` |
| Dependency profiles | `deps.sh` | `local`, `ci`, `smoke`, `lint`, `check`; apt-списки убраны из workflows |
| Hygiene tooling | `lint.sh`, `metrics.sh`, `check-actions-pinned.sh` | shellcheck/actionlint, метрики, SHA-pinning |
| Secret scan | `check-secrets.sh`, gitleaks 8.30.1 | сканирование `work/` и `.github/` через `tar`, включая untracked |
| Dependabot | `.github/dependabot.yml` | weekly github-actions updates |

Артефакты:

- `work/build/defconfig/arch/x86/boot/bzImage` — 14 324 736 байт.
- `work/build/allmodconfig/arch/x86/boot/bzImage` — 43 606 016 байт.
- `work/build/allmodconfig/.config`, `work/build/allmodconfig/build.log`.
- `work/build/kunit/kunit.json` — 2 109 095 байт.
- `work/build/security-kunit/kunit.json` — security suites.
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
| clang/rust/sphinx | не установлены локально | `deps.sh local` |
| shellcheck/actionlint/gitleaks | системно не установлены; lint проверен бинарями из `/tmp/opencode/bin` | `deps.sh lint` (нужен sudo) |
| Required checks | branch protection всё ещё 5 jobs; токен без admin | выдать admin/branch-protection и добавить `meta`/`security` |

## Состояние диска после прогонов

```
/dev/nvme0n1p4  99G  75G  19G  80% /
work/build      19G
  allmodconfig  12G
  machine-7.2.6 1.9G
  src-7.2.6     1.8G
  smoke-kasan   1.7G
```

## Следующие шаги

1. Выдать GitHub-токен с admin/branch-protection: добавить `meta` и `security`
   jobs в required checks, включить `enforce_admins`/PR-only для `dev` и
   защиту `main`.
2. Установить `clang`, `rust`, `sphinx`, `shellcheck`, `actionlint`, `gitleaks`
   через `deps.sh` (нужен sudo), затем прогнать clang-матрицу, `lint.sh` и
   `check-secrets.sh` штатно.
3. ISSUE-11: прогнать матрицу 24.04/26.04 × QEMU 8.2.2/10.2.1 на failing
   `bzImage` и снять pin после выяснения причины.
4. При необходимости установить `7.2.6-machine` в `/boot` по `plans/runbook.md`
   (нужен sudo) и интегрировать `trit` в `dev`.
