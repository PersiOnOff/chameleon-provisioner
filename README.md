# Chameleon Provisioner for Flipper Zero

**Chameleon Provisioner** — контекстно-зависимый кроссплатформенный HID-провижинер для **Flipper Zero** с поддержкой подмены USB-дескрипторов (`VID/PID`), чтением сценариев с SD-карты, имитацией естественных задержек набора и защитой от сбоев при активной русской раскладке (`Alt + Numpad` и `.kl`-таблицы).

> **Дисклеймер:** Проект предназначен исключительно для легального администрирования, автоматизации собственных тестовых стендов и авторизованного аудита безопасности (Device Control / DLP).

---

## Возможности

- **Кроссплатформенность (Windows / Linux / macOS):** автоматическое открытие системного терминала (`PowerShell`, `Bash`, `Zsh`) под выбранную целевую ОС.
- **Чтение сценариев с SD-карты:** встроенный файловый менеджер (`DialogsApp`) для выбора любого `.txt` сценария из `/ext/chameleon/`.
- **Подмена USB-дескрипторов (`USB_ID`):** динамическая настройка `VID` и `PID` перед инициализацией USB HID прямо из шапки сценария.
- **Независимость от раскладки клавиатуры:**
  - `ALT_MODE 1` — ввод ASCII-команд на Windows через `Left Alt + Numpad` (работает корректно даже при включенной русской раскладке и `CapsLock`).
  - `LAYOUT <file.kl>` — загрузка бинарных таблиц раскладок из `/ext/badusb/assets/layouts/` (например, `ru-RU.kl`) для печати UTF-8 кириллицы.
- **Имитация живого набора:** рандомизация задержек нажатия и отпускания клавиш.

---

## Структура репозитория

```text
chameleon-provisioner/
├── application.fam               # Манифест внешнего .fap-приложения
├── chameleon_provisioner.c       # Основной исходный код на C (Furi / FreeRTOS)
├── README.md                     # Документация проекта
├── .gitignore                    # Исключения для сборки uFBT и IDE
└── payloads/                     # Примеры готовых сценариев для /ext/chameleon/
    ├── win_ru_safe.txt
    ├── usb_policy_test.txt
    ├── linux_recon_snapshot.txt
    └── win_secret_hygiene.txt
```

---

## Сборка и установка (`uFBT`)

1. Установите официальный инструментарий `uFBT` (требуется Python 3.8+):
   ```bash
   python -m pip install --upgrade ufbt
   ```
2. Клонируйте репозиторий и перейдите в папку проекта:
   ```bash
   git clone https://github.com/<your-username>/chameleon-provisioner.git
   cd chameleon-provisioner
   ```
3. Закройте `qFlipper` (чтобы освободить COM-порт), подключите Flipper Zero по USB и выполните:
   ```bash
   ufbt launch
   ```
   Приложение скомпилируется в `.fap`, установится в меню **Apps -> USB -> Chameleon Provisioner** и сразу запустится.

---

## Управление на Flipper Zero

- **Влево / Вправо (`<` / `>`):** выбор целевой ОС (`Windows (PS)`, `Linux (Bash)`, `macOS (Zsh)`).
- **Вверх / Вниз (`Up` / `Down`):** открытие проводника в папке `/ext/chameleon` для выбора `.txt` сценария.
- **ОК:** запуск выбранного сценария с подменой `USB_ID` и автоматическим возвратом исходного USB-профиля после завершения.
- **Назад (`Back`):** выход из приложения.

---

## Синтаксис сценариев (`.txt`)

Поместите свои `.txt` файлы в директорию `SD Card/chameleon/` (создается автоматически при первом запуске).

| Директива | Описание | Пример |
| :--- | :--- | :--- |
| `USB_ID <VID>:<PID>` | Подмена идентификаторов USB-устройства (hex) | `USB_ID 046D:C31C` |
| `ALT_MODE <1\|0>` | Включить/выключить ввод ASCII через `Alt + Numpad` (Windows) | `ALT_MODE 1` |
| `LAYOUT <name.kl>` | Загрузить раскладку из `/ext/badusb/assets/layouts/` | `LAYOUT ru-RU.kl` |
| `SLEEP <ms>` | Пауза в миллисекундах между командами | `SLEEP 2000` |
| `#` или `REM ` | Комментарий (игнорируется при выполнении) | `# Установка пакетов` |

### Пример сценария (`win_ru_safe.txt`)

```text
# Безопасный запуск на Windows при любой раскладке клавиатуры
USB_ID 046D:C31C
ALT_MODE 1
Write-Host 'Chameleon Provisioner Ready!' -ForegroundColor Green
Get-ComputerInfo | Select-Object CsName, OsName
```
