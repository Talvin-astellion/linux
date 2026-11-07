# План работ

Оценки в человеко-днях для одного инженера; зависят от доступности runner.

## Фаза 0 — Baseline и гигиена (1–2 дня)

| # | Задача | DoD |
|---|---|---|
| 0.1 | Проверить расхождение версии `Makefile` vs теги 7.0 | Вывод зафиксирован в `problems/issues.md` |
| 0.2 | Зафиксировать политику веток (`main` — upstream, `dev` — интеграция) | Раздел в `runbook.md` |
| 0.3 | Создать аннотированный тег `baseline-YYYYMMDD` | `git tag -n` показывает тег |
| 0.4 | Установить базовый toolchain | `deps.sh` завершается успешно |
| 0.5 | Проверить быструю сборку | `make defconfig && make -j4` проходит |

**Зависимости:** нет. **Риск:** низкий.

## Фаза 1 — Воспроизводимая локальная сборка (2–3 дня)

| # | Задача | DoD |
|---|---|---|
| 1.1 | Out-of-tree allmodconfig с CI-фрагментом | `build.sh` проходит, `.config` и лог сохранены |
| 1.2 | Авто-ограничение `JOBS` и проверка диска | Нет OOM при 5.6 ГБ RAM |
| 1.3 | Замерить время/размер сборки | Цифры внесены в `spec/build-and-test.md` |
| 1.4 | Добавить `make -C work` цели | `make -C work help` работает |

**Зависимости:** Фаза 0. **Риск:** средний (ресурсы).

## Фаза 2 — CI (2–4 дня)

| # | Задача | DoD |
|---|---|---|
| 2.1 | Workflows `checks`, `build`, `kunit`, оркестратор `ci` | YAML валиден, job стартует |
| 2.2 | Канонические workflows в `.github/workflows/` и документация | workflows запускаются, копирования из `work/` нет |
| 2.3 | Кэш ccache, артефакты, очистка диска | Повторный прогон быстрее |
| 2.4 | Branch protection | PR не мержится без зелёных checks |

**Зависимости:** Фаза 1. **Риск:** средний (лимиты runner).

## Фаза 3 — Статические проверки и тесты (3–5 дней)

| # | Задача | DoD |
|---|---|---|
| 3.1 | Стабилизировать checkpatch (ошибки блокируют) | Нет ложных срабатываний на baseline |
| 3.2 | Включить sparse как blocking или allowlist | Список известных предупреждений |
| 3.3 | Стабилизировать KUnit `all_tests` | Известные падения в allowlist |
| 3.4 | Security KUnit suites (Landlock/AppArmor/IPE) | Отдельный job, 0 failed/crashed/errors |
| 3.5 | Lint и метрики (shellcheck/actionlint/metrics) | `lint.sh` и `metrics.sh --check` в CI |
| 3.6 | Добавить kselftests smoke (опционально) | Отдельный job |

**Зависимости:** Фаза 2. **Риск:** средний (шум анализаторов).

## Фаза 4 — Hardening и поддержка (постоянно)

| # | Задача | DoD |
|---|---|---|
| 4.1 | QEMU smoke-загрузка | Job загружает `bzImage` |
| 4.2 | Матрица gcc/clang (при установке clang) | Отдельный job |
| 4.3 | Публикация артефактов/релизов | Версионированные артефакты |
| 4.4 | Периодический sync с upstream | Процедура в `runbook.md` |
| 4.5 | Политика AI-вклада (`AGENTS.md`) | Файл закоммичен |
| 4.6 | Action pinning + Dependabot | Все `uses:` на SHA, weekly updates |
| 4.7 | Gitleaks и KASAN smoke | Security baseline и sanitizers jobs |

**Зависимости:** Фазы 0–3. **Риск:** низкий.

## Сводка

| Фаза | Длительность | Результат |
|---|---|---|
| 0 | 1–2 дня | baseline, тег, toolchain |
| 1 | 2–3 дня | воспроизводимая сборка |
| 2 | 2–4 дня | CI на GitHub |
| 3 | 3–5 дней | стабильные проверки |
| 4 | постоянно | smoke, матрица, поддержка |

## Порядок первого запуска

```bash
work/pipelines/scripts/deps.sh local
JOBS=3 work/pipelines/scripts/build.sh
BASE=origin/master work/pipelines/scripts/checkpatch.sh
JOBS=4 work/pipelines/scripts/sparse.sh
JOBS=4 work/pipelines/scripts/kunit.sh
JOBS=4 work/pipelines/scripts/security-kunit.sh
work/pipelines/scripts/lint.sh
work/pipelines/scripts/metrics.sh --check
```

Первый прогон `allmodconfig` и `kunit` считать измерением baseline: зафиксировать
время, размер, список падений.

## Фактические результаты (2026-10-31 — 2026-11-01)

| Фаза | Статус | Факт |
|---|---|---|
| 0 | выполнена | baseline-тег, политика веток, toolchain проверен |
| 1 | выполнена | defconfig 559 с / 966 МБ; allmodconfig 9467 с / 12 ГБ |
| 2 | выполнена | workflows, push и branch protection готовы; allmodconfig retry success 2 ч 04 мин; checkpatch shallow fix 34 с |
| 3 | выполнена | KUnit 8599 passed; security suites: 5 в `all_tests` (54 теста) + `ipe-parser` в security job; checkpatch 0 errors; sparse baseline 211/1252; kselftests optional не начат |
| 4 | частично | QEMU smoke, AGENTS.md, partial clone, action pinning, Dependabot, gitleaks и KASAN smoke готовы (KASAN локально `SMOKE_OK` 40 с); clang/rust/sphinx локально не установлены, релизы не начаты |
| local | выполнено | machine 7.2.6: 213 модулей, ~35 мин, debug off, QEMU smoke OK |

Подробности: `work/plans/status.md`.
