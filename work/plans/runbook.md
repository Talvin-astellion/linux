# Runbook

Операционные процедуры для проекта.

## 1. Первичная настройка

```bash
cd /home/talvin/System/linux
work/pipelines/scripts/deps.sh local
git tag -a "baseline-$(date +%Y%m%d)" -m "baseline before CI work"
```

Проверка окружения:

```bash
nproc
free -h
df -h .
for t in gcc make bc flex bison pahole python3 perl shellcheck actionlint; do
  command -v "$t" >/dev/null && echo "OK $t" || echo "MISSING $t"
done
```

## 2. Политика веток и тегов

| Ветка | Роль | Правило |
|---|---|---|
| `main` | зеркало upstream | только fast-forward от `origin/master` |
| `dev` | интеграция | PR, зелёный CI |
| `feature/*` | задачи | ответвление от `dev` |

- Теги: `baseline-YYYYMMDD` (состояние до работ), `ci-YYYYMMDD.N` (прогоны).
- Force-push в `main`/`dev` запрещён.
- `dev` мержится в `main` только после стабилизации.

## 3. Локальная сборка

```bash
# базовый вариант
JOBS=3 work/pipelines/scripts/build.sh

# свой каталог и запас диска
BUILD_DIR=/mnt/fast/build/allmodconfig MIN_FREE_GB=15 \
  work/pipelines/scripts/build.sh
```

### Машинная сборка 7.2.6

```bash
JOBS=3 work/pipelines/scripts/build-machine.sh
```

Скрипт не меняет `/home/talvin/linux-7.2.6`: создаёт копию
`work/build/src-7.2.6`, чистит её через `mrproper` и собирает out-of-tree в
`work/build/machine-7.2.6`. Артефакты — в `artifacts/`, модули — в `modules/`.

Проверка:

```bash
make -s -C work/build/src-7.2.6 O="$PWD/work/build/machine-7.2.6" kernelrelease
QEMU_CPU=max KERNEL_IMAGE=work/build/machine-7.2.6/artifacts/bzImage-7.2.6-machine \
  work/pipelines/scripts/smoke-qemu.sh
```

`O=` должен быть абсолютным: `make -C` меняет текущий каталог.

### Установка машинного ядра (опционально)

Выполнять только после проверки артефактов и с пониманием риска:

```bash
sudo make -C work/build/src-7.2.6 O="$PWD/work/build/machine-7.2.6" modules_install
sudo install -m 644 work/build/machine-7.2.6/artifacts/bzImage-7.2.6-machine /boot/vmlinuz-7.2.6-machine
sudo install -m 644 work/build/machine-7.2.6/artifacts/System.map-7.2.6-machine /boot/System.map-7.2.6-machine
sudo install -m 644 work/build/machine-7.2.6/artifacts/config-7.2.6-machine /boot/config-7.2.6-machine
sudo update-initramfs -c -k 7.2.6-machine
sudo update-grub
```

Откат: выбрать предыдущее ядро в GRUB; затем удалить
`/boot/*7.2.6-machine*` и `/lib/modules/7.2.6-machine`, выполнить
`sudo update-grub`.

Если OOM:

1. Уменьшить `JOBS` (например, `JOBS=2`).
2. Увеличить swap или добавить RAM.
3. Отключить debug-info (уже в CI-фрагменте).

Если мало диска:

1. `make -C work clean`.
2. `make O=work/build/allmodconfig mrproper`.
3. Вынести `BUILD_DIR` на другой диск.

## 4. Проверки

```bash
# diff против upstream
BASE=origin/master work/pipelines/scripts/checkpatch.sh

# sparse на defconfig
JOBS=4 work/pipelines/scripts/sparse.sh

# KUnit
JOBS=4 KUNIT_TIMEOUT=900 work/pipelines/scripts/kunit.sh

# security KUnit (Landlock/AppArmor/IPE)
JOBS=4 work/pipelines/scripts/security-kunit.sh

# KASAN/KFENCE/UBSAN smoke
JOBS=4 work/pipelines/scripts/smoke-kasan.sh

# lint, метрики, security baseline
work/pipelines/scripts/lint.sh
work/pipelines/scripts/metrics.sh --check
work/pipelines/scripts/check-actions-pinned.sh
work/pipelines/scripts/check-secrets.sh
```

Разбор результатов:

- checkpatch: блокируют только `ERROR:`; `WARNING:` — на ревью.
- sparse: исторические предупреждения; новые — в отчёт.
- KUnit: падения смотреть в `work/build/kunit/kunit.json` и логах UML.
- security KUnit: `check-kunit-security.py` проверяет suites из
  `configs/security-kunit-suites.txt` и нулевые failed/crashed/errors.
- KASAN smoke: смотреть `work/build/smoke-kasan/smoke/qemu.log`.

## 5. CI и branch protection

Канонические workflows лежат в `.github/workflows/`; `install.sh` больше нет.
После изменения:

```bash
git add .github/workflows .github/dependabot.yml work
git commit -m "ci: ..."
git push origin dev
```

После первого прогона:

1. Включить branch protection для `dev`; текущие required checks —
   `checks / checkpatch`, `checks / sparse`, `build / allmodconfig`,
   `kunit / kunit`, `smoke / qemu-smoke`.
2. После зелёного прогона добавить `meta / meta`, `security / security-baseline`,
   `security / security-kunit`, `security / security-sanitizers`.
3. `sparse` ограничен baseline 211/1252; `kunit`, `smoke` и security jobs blocking.
4. `allmodconfig` в CI: `timeout-minutes: 360`, `JOBS=4`, CI-фрагмент без
   debug-info/KASAN/UBSAN/KFENCE/GCOV; иначе hosted runner упирается в 3 часа.
5. Для `main` protection включать только вместе с политикой sync с upstream.

## 6. Обновление из upstream

```bash
git fetch origin
git checkout main
git merge --ff-only origin/master
git checkout dev
git merge main
```

Конфликты в `work/` решать вручную; workflows и скрипты не зависят от версии ядра.

### Partial clone и размер `.git`

`.git` занимает ~6 ГБ. Для быстрого клонирования:

```bash
git clone --filter=blob:none https://github.com/Talvin-astellion/linux
git gc --aggressive --prune=now
```

CI использует `actions/checkout` с `fetch-depth: 1`; `checkpatch` дополнительно
подтягивает только tip базовой ветки (`git fetch --depth=1`), полный history не
нужен.

## 7. Troubleshooting

| Симптом | Причина | Действие |
|---|---|---|
| `Killed` при сборке | OOM | `JOBS=2`, увеличить swap |
| `No space left on device` | диск | очистка, вынести `BUILD_DIR` |
| `missing required tool` | нет пакета | `deps.sh` |
| checkpatch падает на baseline | шум/ложные | сузить `BASE`, добавить allowlist |
| KUnit timeout | медленный runner | увеличить `KUNIT_TIMEOUT` |
| sparse не найден | пакет Ubuntu 0.6.4 непригоден | собрать sparse из исходников и добавить в `PATH` |
| QEMU smoke не стартует native-ядро | QEMU CPU не поддерживает `-march=native` | `QEMU_CPU=max` |
| QEMU smoke: `Booting from ROM..` и мгновенный выход | Ubuntu 26.04 / QEMU 10.2.1 (ISSUE-11) | использовать `ubuntu-24.04`; смотреть `qemu-debug.log` |
| `source tree is not clean` | сборка `O=` из грязного дерева | использовать `work/build/src-*` |
| workflow не запускается | YAML не в `.github/workflows` | проверить путь и имя файла |
| reusable workflow not found | отсутствует файл в `.github/workflows` | добавить все 7 workflow |
| `shellcheck`/`actionlint` not found | не установлен lint-профиль | `deps.sh lint` |
| `gitleaks` not found | нет бинарника (CI ставит сам) | установить gitleaks в `PATH` |
| metrics are stale | изменились скрипты/workflow/доки | `make -C work metrics` |
| `allmodconfig` отменён через 3 ч | timeout 180 и медленный runner | timeout 360, `JOBS=4`, облегчённый CI-фрагмент |
| `checkpatch` отменён на Checkout | полный fetch 6 ГБ упирается в timeout 20 мин | `fetch-depth: 1` + shallow base fetch (ISSUE-12, closed) |

## 8. Очистка

```bash
make -C work clean
rm -rf work/build/src-7.2.6 work/build/machine-7.2.6
git clean -xdf work/pipelines   # осторожно: удалит неотслеживаемое
```

## 9. Контакты и ссылки

- `AGENTS.md` — локальная политика AI-вклада (DCO, раскрытие, review).
- `Documentation/process/coding-assistants.rst` — правила AI-вклада.
- `Documentation/process/submitting-patches.rst` — формат патчей.
- `Documentation/dev-tools/testing-overview.rst` — тестирование.
- `Documentation/admin-guide/quickly-build-trimmed-linux.rst` — быстрая сборка.
