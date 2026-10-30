# Статус выполнения

| Поле | Значение |
|---|---|
| Дата | 2026-10-31 |
| Ветка | `dev` |
| Baseline-тег | `baseline-20261031` (`541c43310e85`) |
| Окружение | 12 CPU, 5.6 ГБ RAM + 4 ГБ swap, 32 ГБ свободно на старте |

## Выполнено

| Шаг | Команда | Результат |
|---|---|---|
| CI установлен | `work/pipelines/install.sh` | 4 workflow в `.github/workflows/` |
| Baseline-тег | `git tag -a baseline-20261031 541c43310e85` | создан |
| defconfig | `make O=work/build/defconfig defconfig && make -j4` | rc=0, 559 с, 966 МБ |
| allmodconfig | `JOBS=3 work/pipelines/scripts/build.sh` | rc=0, 9467 с, 12 ГБ |
| KUnit | `JOBS=4 work/pipelines/scripts/kunit.sh` | rc=0, 303 с, 8599 passed / 39 skipped / 0 failed |
| checkpatch | `BASE=origin/master work/pipelines/scripts/checkpatch.sh` | rc=0, 0 errors, 1 warning (FILE_PATH_CHANGES) |
| QEMU smoke | `work/pipelines/scripts/smoke-qemu.sh` | rc=0, `SMOKE_OK`, ~5 с |

Артефакты:

- `work/build/defconfig/arch/x86/boot/bzImage` — 14 324 736 байт.
- `work/build/allmodconfig/arch/x86/boot/bzImage` — 43 606 016 байт.
- `work/build/allmodconfig/.config`, `work/build/allmodconfig/build.log`.
- `work/build/kunit/kunit.json` — 2 109 095 байт.
- `work/build/checkpatch.log`.
- `work/build/smoke/qemu.log` — вывод загрузки ядра.

## Не выполнено / блокеры

| Блокер | Причина | Что нужно |
|---|---|---|
| `sparse` | пакет не установлен, `sudo` требует пароль | `sudo apt-get install sparse` или `deps.sh` |
| Push CI | нет credentials для `origin`, `git push --dry-run` зависает | настроить SSH/токен и выполнить `git push origin dev` |
| ccache | пакет не установлен | `deps.sh` |
| clang/rust/sphinx | не установлены | `deps.sh --full` |

## Состояние диска после прогонов

```
/dev/nvme0n1p4  99G  75G  20G  80% /
work/build      13G
```

## Следующие шаги

1. Установить `sparse` и выполнить `work/pipelines/scripts/sparse.sh`.
2. Запушить `dev` и дождаться первого прогона CI.
3. Включить branch protection после зелёного прогона.
4. Перевести `sparse`/`kunit`/`smoke` из advisory в blocking.
