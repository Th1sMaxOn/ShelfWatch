# Перевірка ЛР1 ShelfWatch

Цей файл містить результати перевірок.

## Підтверджено

- Каталог із `.git` перенесено у `Lab01/Work/ShelfWatch`; локальний Git config збережено побайтно. Не було інших файлів чи наявного призначення. Панель tmux People %8 показує новий шлях; інші панелі не змінювалися.
- 100 файлів шаблону скопійовано без `.git` і `firmware/build`, перевірено побайтно. Firmware, `.ioc`, `diagram.json`, Dockerfile, Makefile, wokwi.toml і ліцензії не змінено. README шаблону збережено окремо.
- UART/LED підтверджено переглядом коду: PA2/PA3 USART2 115200 8N1, PA5 LED, стартовий рядок і `tick`, HAL_Delay(1000).
- Дві чисті збірки з однаковим образом успішні. Для першої перед build виконано `make clean`, для другої clean-log збережено окремо. Після clean в обох логах видно створення `build` і повну компіляцію.
- Хост macOS 27.0.1 arm64; Docker Desktop, context desktop-linux, Engine 29.6.1 Linux arm64. GCC 10.3.1. Точний image ID збережено у [image.txt](evidence/image.txt), параметри в [environment.json](evidence/environment.json). Це не запуск Ubuntu 24.04 VM.
- VS Code знайдено у `/Applications/Visual Studio Code.app`, `code --list-extensions` показав `wokwi.wokwi-vscode`. VirtualBox встановлено, але його `list vms` і `list runningvms` не показали VM. Це не доводить відсутності VM на іншому комп'ютері.

Логи [build-1](evidence/build-1/build.log), [build-2](evidence/build-2/build.log), [clean-2](evidence/build-2/clean.log), [Docker](evidence/docker-version.txt), [компілятор](evidence/compiler.txt). Компілятор повідомив text=13464, data=120, bss=1808 байтів. Це розмір секцій, не вимір максимального використання стеку під час виконання.

| Артефакт | SHA256 обох збірок |
|---|---|
| firmware.elf | `0f7b87f63097adfc290c57ea018a65e5a05da061aaa48fd520c14fbfe05f85da` |
| firmware.hex | `20c68952fd3c7ab6640f8ca53935729d6ca2c54d55dbba98597a92c2e0248415` |
| firmware.bin | `c21e312881deefbec24a06e9808d3651367530ac8ea930d3b37cc011daa7f7df` |

Повні списки [SHA256 першої](evidence/build-1/SHA256SUMS) та [другої](evidence/build-2/SHA256SUMS) збірки збігаються. Доказ обмежений одним шляхом, платформою й образом. Dockerfile не фіксує digest базового образу або версії пакетів; майбутня збірка з оновленими пакетами не гарантовано дасть ці хеші. Інша архітектура VM й абсолютний шлях debug-інформації також можуть змінити ELF. У VM спочатку порівнювати дві її власні чисті збірки.

## Очікуване за кодом

Після запуску UART повинен показати `STM32C031C6 ready`, далі tick. Перший tick може бути 0, бо цикл друкує до HAL_Delay. LED перемикається з інтервалом близько 1000 мс, повний цикл займає близько 2 с. UART-друк додає невелику затримку. Це висновок із коду, ще не спостереження ShelfWatch у симуляторі.

## Не виконано

Ubuntu VM не запущено, її Docker і build не перевірено. ShelfWatch у Wokwi не запущено: під час перевірки VS Code інтерфейс переключився на інший студентський проєкт, дії припинено, щоб не працювати в чужій копії. Trial не активовано; наявність ліцензії відома зі слів користувача, її дію не перевірено. Симуляційного UART-логу, вимірювання LED, скриншотів CubeMX і датчиків немає.

PRD описує майбутні сценарії. Немає реалізації режимів, сенсорних драйверів, threshold, ack, обробки NACK чи тестів ЛР2. PDF-звіт і п'ять фактичних складнощів студента ще не підготовлено. Немає віддаленого репозиторію, доступу викладача чи PR.

## Повторна перевірка

На Ubuntu з кореня цієї копії:

```sh
mkdir -p /tmp/shelfwatch-check
uname -sm > /tmp/shelfwatch-check/platform.txt
cat /etc/os-release >> /tmp/shelfwatch-check/platform.txt
docker version > /tmp/shelfwatch-check/docker-version.txt
make clean > /tmp/shelfwatch-check/clean-1.log 2>&1
make build > /tmp/shelfwatch-check/build-1.log 2>&1
sha256sum firmware/build/firmware.{elf,hex,bin} > /tmp/shelfwatch-check/SHA256SUMS-1
make clean > /tmp/shelfwatch-check/clean-2.log 2>&1
make build > /tmp/shelfwatch-check/build-2.log 2>&1
sha256sum firmware/build/firmware.{elf,hex,bin} > /tmp/shelfwatch-check/SHA256SUMS-2
diff -u /tmp/shelfwatch-check/SHA256SUMS-1 /tmp/shelfwatch-check/SHA256SUMS-2
```

Виконувати команди послідовно й перевіряти код виходу кожної. У разі помилки build зупинитися й читати log, не продовжувати до заяви про успіх. Зберегти ОС, Docker image ID і gcc --version для VM також. Реальні результати перенести спочатку в окрему VM-Inbox, порівняти й лише потім доповнити основну копію.
