# Готовый комплект файлов для репозитория `chameleon-provisioner`

Ниже собраны все файлы проекта для загрузки на GitHub, плюс готовые `.txt` сценарии для папки `SD Card/chameleon/` на Flipper Zero.

---

## 1. Файл `application.fam`

```python
App(
    appid="chameleon_provisioner",
    name="Chameleon Provisioner",
    apptype=FlipperAppType.EXTERNAL,
    entry_point="chameleon_provisioner_app",
    stack_size=2 * 1024,
    fap_category="USB",
    fap_version=(1, 0),
    fap_author="Developer",
    fap_description="Context-aware cross-platform HID provisioner with layout support",
)
```

---

## 2. Файл `chameleon_provisioner.c`

```c
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_hid.h>
#include <gui/gui.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stream/file_stream.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CHAMELEON_DIR "/ext/chameleon"
#define LAYOUTS_DIR "/ext/badusb/assets/layouts"
#define MAX_LAYOUT_ENTRIES 256

typedef enum {
    TargetOsWindows = 0,
    TargetOsLinux,
    TargetOsMacOS,
    TargetOsCount
} TargetOs;

static const char* target_os_names[TargetOsCount] = {
    "Windows (PS)",
    "Linux (Bash)",
    "macOS (Zsh)",
};

typedef struct __attribute__((packed)) {
    uint16_t unicode;
    uint16_t hid_key;
} KeyLayoutEntry;

typedef struct {
    KeyLayoutEntry entries[MAX_LAYOUT_ENTRIES];
    size_t count;
    bool loaded;
} KeyboardLayout;

typedef struct {
    TargetOs selected_os;
    FuriString* script_path;
    bool is_running;
    char status_text[48];
    FuriMutex* mutex;
} AppState;

static const uint16_t numpad_keys[10] = {
    HID_KEYPAD_0, HID_KEYPAD_1, HID_KEYPAD_2, HID_KEYPAD_3, HID_KEYPAD_4,
    HID_KEYPAD_5, HID_KEYPAD_6, HID_KEYPAD_7, HID_KEYPAD_8, HID_KEYPAD_9,
};

static bool load_keyboard_layout(KeyboardLayout* layout, const char* layout_name) {
    layout->count = 0;
    layout->loaded = false;

    FuriString* path = furi_string_alloc_printf("%s/%s", LAYOUTS_DIR, layout_name);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t bytes_read = storage_file_read(file, layout->entries, sizeof(layout->entries));
        layout->count = bytes_read / sizeof(KeyLayoutEntry);
        layout->loaded = (layout->count > 0);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(path);

    return layout->loaded;
}

static uint16_t lookup_layout_key(const KeyboardLayout* layout, uint16_t unicode_char) {
    if(!layout->loaded) return HID_KEYBOARD_NONE;
    for(size_t i = 0; i < layout->count; i++) {
        if(layout->entries[i].unicode == unicode_char) {
            return layout->entries[i].hid_key;
        }
    }
    return HID_KEYBOARD_NONE;
}

static uint16_t utf8_to_utf16(const char* str, size_t* index) {
    uint8_t c = (uint8_t)str[*index];
    if(c < 0x80) {
        (*index) += 1;
        return c;
    } else if((c & 0xE0) == 0xC0 && str[*index + 1] != '\0') {
        uint16_t code = ((c & 0x1F) << 6) | ((uint8_t)str[*index + 1] & 0x3F);
        (*index) += 2;
        return code;
    } else if((c & 0xF0) == 0xE0 && str[*index + 1] != '\0' && str[*index + 2] != '\0') {
        uint16_t code = ((c & 0x0F) << 12) |
                        (((uint8_t)str[*index + 1] & 0x3F) << 6) |
                        ((uint8_t)str[*index + 2] & 0x3F);
        (*index) += 3;
        return code;
    }
    (*index) += 1;
    return 0;
}

static void type_char_win_altcode(uint8_t ascii_code) {
    char digits[8];
    snprintf(digits, sizeof(digits), "%u", ascii_code);

    furi_hal_hid_kb_press(KEY_MOD_LEFT_ALT);
    furi_delay_ms(10);

    for(size_t i = 0; digits[i] != '\0'; i++) {
        uint8_t d = digits[i] - '0';
        if(d < 10) {
            furi_hal_hid_kb_press(numpad_keys[d]);
            furi_delay_ms(8);
            furi_hal_hid_kb_release(numpad_keys[d]);
            furi_delay_ms(8);
        }
    }

    furi_hal_hid_kb_release(KEY_MOD_LEFT_ALT);
    furi_delay_ms(12 + (rand() % 15));
}

static void type_smart_string(
    const char* str,
    const KeyboardLayout* layout,
    bool use_win_altcodes) {
    size_t i = 0;
    while(str[i] != '\0') {
        uint16_t codepoint = utf8_to_utf16(str, &i);
        if(codepoint == 0) continue;

        if(use_win_altcodes && codepoint >= 32 && codepoint <= 126) {
            type_char_win_altcode((uint8_t)codepoint);
            continue;
        }

        uint16_t key = lookup_layout_key(layout, codepoint);

        if(key == HID_KEYBOARD_NONE && codepoint < 128) {
            key = HID_ASCII_TO_KEY((char)codepoint);
        }

        if(key != HID_KEYBOARD_NONE) {
            furi_hal_hid_kb_press(key);
            furi_delay_ms(12 + (rand() % 16));
            furi_hal_hid_kb_release(key);
            furi_delay_ms(16 + (rand() % 28));
        }
    }
}

static void init_sd_profiles(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, CHAMELEON_DIR);

    const char* sample_path = CHAMELEON_DIR "/win_ru_safe.txt";
    if(!storage_file_exists(storage, sample_path)) {
        Stream* stream = file_stream_alloc(storage);
        if(file_stream_open(stream, sample_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            stream_write_cstring(
                stream,
                "# Работает даже при включенной русской раскладке на ПК!\n"
                "USB_ID 046D:C31C\n"
                "ALT_MODE 1\n"
                "Write-Host 'Chameleon Provisioner Ready!' -ForegroundColor Green\n"
                "Get-ComputerInfo | Select-Object CsName, OsName\n");
        }
        file_stream_close(stream);
        stream_free(stream);
    }

    furi_record_close(RECORD_STORAGE);
}

static void press_combo(uint16_t modifier, uint16_t key) {
    furi_hal_hid_kb_press(modifier);
    furi_delay_ms(40);
    furi_hal_hid_kb_press(key);
    furi_delay_ms(60);
    furi_hal_hid_kb_release_all();
}

static void press_enter(void) {
    furi_hal_hid_kb_press(HID_KEYBOARD_RETURN);
    furi_delay_ms(40);
    furi_hal_hid_kb_release(HID_KEYBOARD_RETURN);
}

static void open_target_terminal(TargetOs os, const KeyboardLayout* layout, bool use_altcodes) {
    if(os == TargetOsWindows) {
        press_combo(KEY_MOD_LEFT_GUI, HID_KEYBOARD_R);
        furi_delay_ms(600);
        type_smart_string("powershell -NoExit", layout, use_altcodes);
        press_enter();
        furi_delay_ms(1200);
    } else if(os == TargetOsLinux) {
        furi_hal_hid_kb_press(KEY_MOD_LEFT_CTRL);
        furi_hal_hid_kb_press(KEY_MOD_LEFT_ALT);
        furi_hal_hid_kb_press(HID_KEYBOARD_T);
        furi_delay_ms(60);
        furi_hal_hid_kb_release_all();
        furi_delay_ms(1000);
    } else if(os == TargetOsMacOS) {
        press_combo(KEY_MOD_LEFT_GUI, HID_KEYBOARD_SPACEBAR);
        furi_delay_ms(600);
        type_smart_string("Terminal", layout, false);
        furi_delay_ms(400);
        press_enter();
        furi_delay_ms(1200);
    }
}

static void parse_usb_descriptors(const char* filepath, FuriHalUsbHidConfig* hid_cfg) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();

    if(file_stream_open(stream, filepath, FSAM_READ, FSOM_OPEN_EXISTING)) {
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            const char* cmd = furi_string_get_cstr(line);
            if(strncmp(cmd, "USB_ID ", 7) == 0) {
                unsigned int vid = 0, pid = 0;
                if(sscanf(cmd + 7, "%x:%x", &vid, &pid) == 2) {
                    hid_cfg->vid = (uint16_t)vid;
                    hid_cfg->pid = (uint16_t)pid;
                }
                break;
            }
        }
    }

    furi_string_free(line);
    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
}

static bool execute_sd_script(TargetOs os, const char* filepath) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();
    KeyboardLayout* layout = malloc(sizeof(KeyboardLayout));
    load_keyboard_layout(layout, "en-US.kl");

    bool use_win_altcodes = (os == TargetOsWindows);
    bool terminal_opened = false;
    bool success = false;

    if(file_stream_open(stream, filepath, FSAM_READ, FSOM_OPEN_EXISTING)) {
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            const char* cmd = furi_string_get_cstr(line);

            if(strlen(cmd) == 0 || cmd[0] == '#' || strncmp(cmd, "REM ", 4) == 0 ||
               strncmp(cmd, "USB_ID ", 7) == 0) {
                continue;
            }

            if(strncmp(cmd, "LAYOUT ", 7) == 0) {
                load_keyboard_layout(layout, cmd + 7);
                continue;
            }

            if(strncmp(cmd, "ALT_MODE ", 9) == 0) {
                use_win_altcodes = (atoi(cmd + 9) != 0) && (os == TargetOsWindows);
                continue;
            }

            if(strncmp(cmd, "SLEEP ", 6) == 0) {
                uint32_t ms = (uint32_t)atoi(cmd + 6);
                if(ms > 0) furi_delay_ms(ms);
                continue;
            }

            if(!terminal_opened) {
                open_target_terminal(os, layout, use_win_altcodes);
                terminal_opened = true;
            }

            type_smart_string(cmd, layout, use_win_altcodes);
            press_enter();
            furi_delay_ms(300);
        }
        success = true;
    }

    free(layout);
    furi_string_free(line);
    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);

    return success;
}

static void run_provisioning_payload(AppState* state, NotificationApp* notifications) {
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->is_running = true;
    snprintf(state->status_text, sizeof(state->status_text), "Configuring USB HID...");
    const char* filepath = furi_string_get_cstr(state->script_path);
    TargetOs os = state->selected_os;
    furi_mutex_release(state->mutex);

    FuriHalUsbHidConfig hid_cfg = {
        .vid = 0x046D,
        .pid = 0xC31C,
        .mfr_name = "Logitech",
        .prod_name = "USB Keyboard",
    };
    parse_usb_descriptors(filepath, &hid_cfg);

    FuriHalUsbInterface* prev_usb = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_hid, &hid_cfg) == true);

    uint32_t timeout = 50;
    while(!furi_hal_hid_is_connected() && timeout > 0) {
        furi_delay_ms(100);
        timeout--;
    }

    if(!furi_hal_hid_is_connected()) {
        furi_mutex_acquire(state->mutex, FuriWaitForever);
        snprintf(state->status_text, sizeof(state->status_text), "USB Host timeout!");
        state->is_running = false;
        furi_mutex_release(state->mutex);
        furi_hal_usb_set_config(prev_usb, NULL);
        return;
    }

    furi_delay_ms(700);

    furi_mutex_acquire(state->mutex, FuriWaitForever);
    snprintf(state->status_text, sizeof(state->status_text), "Running script...");
    furi_mutex_release(state->mutex);

    bool ok = execute_sd_script(os, filepath);

    furi_delay_ms(400);
    furi_hal_usb_set_config(prev_usb, NULL);

    notification_message(notifications, ok ? &sequence_success : &sequence_error);

    furi_mutex_acquire(state->mutex, FuriWaitForever);
    snprintf(state->status_text, sizeof(state->status_text), ok ? "Done! OK: Run again" : "Read error!");
    state->is_running = false;
    furi_mutex_release(state->mutex);
}

static void select_script_file(AppState* state) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".txt", NULL);
    browser_options.base_path = CHAMELEON_DIR;
    browser_options.hide_ext = false;

    FuriString* selected_path = furi_string_alloc_set(state->script_path);
    if(dialog_file_browser_show(dialogs, selected_path, selected_path, &browser_options)) {
        furi_mutex_acquire(state->mutex, FuriWaitForever);
        furi_string_set(state->script_path, selected_path);
        snprintf(state->status_text, sizeof(state->status_text), "File loaded. Press OK");
        furi_mutex_release(state->mutex);
    }

    furi_string_free(selected_path);
    furi_record_close(RECORD_DIALOGS);
}

static void app_draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 11, "Chameleon Provisioner");
    canvas_draw_line(canvas, 0, 14, 128, 14);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, 25, "OS (< / >):");
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 55, 25, target_os_names[state->selected_os]);

    const char* full_path = furi_string_get_cstr(state->script_path);
    const char* filename = strrchr(full_path, '/');
    filename = filename ? (filename + 1) : full_path;

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, 38, "File (Up/Dn):");
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 49, filename);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, 61, state->status_text);

    furi_mutex_release(state->mutex);
}

static void app_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    if(input_event->type == InputTypeShort) {
        furi_message_queue_put(event_queue, input_event, FuriWaitForever);
    }
}

int32_t chameleon_provisioner_app(void* p) {
    UNUSED(p);

    init_sd_profiles();

    AppState* state = malloc(sizeof(AppState));
    state->selected_os = TargetOsWindows;
    state->script_path = furi_string_alloc_set(CHAMELEON_DIR "/win_ru_safe.txt");
    state->is_running = false;
    snprintf(state->status_text, sizeof(state->status_text), "OK: Run | Up/Dn: File");
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, app_draw_callback, state);
    view_port_input_callback_set(view_port, app_input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool exit_loop = false;

    while(!exit_loop) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.key == InputKeyBack) {
                exit_loop = true;
            } else if((event.key == InputKeyLeft || event.key == InputKeyRight) && !state->is_running) {
                furi_mutex_acquire(state->mutex, FuriWaitForever);
                if(event.key == InputKeyLeft) {
                    state->selected_os = (state->selected_os == 0) ? (TargetOsCount - 1) : (state->selected_os - 1);
                } else {
                    state->selected_os = (state->selected_os + 1) % TargetOsCount;
                }
                furi_mutex_release(state->mutex);
            } else if((event.key == InputKeyUp || event.key == InputKeyDown) && !state->is_running) {
                select_script_file(state);
            } else if(event.key == InputKeyOk && !state->is_running) {
                run_provisioning_payload(state, notifications);
            }
            view_port_update(view_port);
        }
    }

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_string_free(state->script_path);
    furi_mutex_free(state->mutex);
    free(state);

    return 0;
}
```

---

## 3. Файл `README.md`

```markdown
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
```

---

## 4. Файл `.gitignore`

```text
# uFBT build artifacts and SDK cache
.ufbt/
build/
dist/
*.fap
*.elf
*.bin
*.dfu

# IDE and OS files
.vscode/
.idea/
.DS_Store
Thumbs.db
```

---

## 5. Готовые сценарии для `payloads/` (и `SD Card/chameleon/`)

### `payloads/win_ru_safe.txt`
```text
# Работает даже при включенной русской раскладке на ПК!
USB_ID 046D:C31C
ALT_MODE 1
Write-Host 'Chameleon Provisioner Ready!' -ForegroundColor Green
Get-ComputerInfo | Select-Object CsName, OsName
```

### `payloads/usb_policy_test.txt`
```text
# Тест политик Device Control: нестандартный VID/PID
USB_ID 1234:5678
ALT_MODE 1
Write-Host "[!] Warning: Unregistered USB HID device was permitted by OS policy!" -ForegroundColor Red
Get-Date | Out-File -FilePath "$env:TEMP\usb_audit_marker.txt"
```

### `payloads/linux_recon_snapshot.txt`
```text
# Снимок сетевого периметра и активных служб
USB_ID 046D:C31C
OUT=~/host_audit_$(hostname)_$(date +%H%M).txt
{ echo "=== SYSTEM ==="; uname -a; echo "=== PORTS ==="; ss -tulpn; echo "=== DOCKER ==="; docker ps 2>/dev/null; } > $OUT
echo "Audit saved to $OUT"
```

### `payloads/win_secret_hygiene.txt`
```text
# Аудит гигиены секретов на рабочей станции
USB_ID 413C:2113
ALT_MODE 1
Get-ChildItem Env: | Where-Object { $_.Name -match "TOKEN|KEY|SECRET|PASS" } | Format-Table Name
Get-Content (Get-PSReadlineOption).HistorySavePath -Tail 15 -ErrorAction SilentlyContinue
```
