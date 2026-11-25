#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_hid.h>
#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>
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
#define BLE_KEYS_PATH EXT_PATH("apps_data/chameleon_provisioner/hid.keys")
#define MAX_LAYOUT_ENTRIES 256

typedef enum {
    TargetOsWindows = 0,
    TargetOsLinux,
    TargetOsMacOS,
    TargetOsCount
} TargetOs;

static const char* target_os_names[TargetOsCount] = {
    "Win (PS)",
    "Linux",
    "macOS",
};

typedef enum {
    TransportUsb = 0,
    TransportBle,
    TransportCount
} TransportMode;

static const char* transport_names[TransportCount] = {
    "USB",
    "BLE",
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
    TransportMode transport;
    bool ble_connected;
    FuriHalBleProfileBase* ble_profile;
    Bt* bt;
    FuriString* script_path;
    bool is_running;
    char status_text[48];
    FuriMutex* mutex;
} AppState;

static const uint16_t numpad_keys[10] = {
    HID_KEYPAD_0, HID_KEYPAD_1, HID_KEYPAD_2, HID_KEYPAD_3, HID_KEYPAD_4,
    HID_KEYPAD_5, HID_KEYPAD_6, HID_KEYPAD_7, HID_KEYPAD_8, HID_KEYPAD_9,
};

// --- Унифицированный слой отправки HID (USB или BLE) ---

static void hid_kb_press(AppState* state, uint16_t button) {
    if(state->transport == TransportBle && state->ble_profile) {
        ble_profile_hid_kb_press(state->ble_profile, button);
    } else {
        furi_hal_hid_kb_press(button);
    }
}

static void hid_kb_release(AppState* state, uint16_t button) {
    if(state->transport == TransportBle && state->ble_profile) {
        ble_profile_hid_kb_release(state->ble_profile, button);
    } else {
        furi_hal_hid_kb_release(button);
    }
}

static void hid_kb_release_all(AppState* state) {
    if(state->transport == TransportBle && state->ble_profile) {
        ble_profile_hid_kb_release_all(state->ble_profile);
    } else {
        furi_hal_hid_kb_release_all();
    }
}

static void hid_mouse_move(AppState* state, int8_t dx, int8_t dy) {
    if(state->transport == TransportBle && state->ble_profile) {
        ble_profile_hid_mouse_move(state->ble_profile, dx, dy);
    } else {
        furi_hal_hid_mouse_move(dx, dy);
    }
}

static void hid_mouse_press(AppState* state, uint8_t button) {
    if(state->transport == TransportBle && state->ble_profile) {
        ble_profile_hid_mouse_press(state->ble_profile, button);
    } else {
        furi_hal_hid_mouse_press(button);
    }
}

static void hid_mouse_release(AppState* state, uint8_t button) {
    if(state->transport == TransportBle && state->ble_profile) {
        ble_profile_hid_mouse_release(state->ble_profile, button);
    } else {
        furi_hal_hid_mouse_release(button);
    }
}

static void hid_mouse_scroll(AppState* state, int8_t delta) {
    if(state->transport == TransportBle && state->ble_profile) {
        ble_profile_hid_mouse_scroll(state->ble_profile, delta);
    } else {
        furi_hal_hid_mouse_scroll(delta);
    }
}

// --- Обработка мыши, скролла и специальных клавиш ---

static void smooth_mouse_move(AppState* state, int total_dx, int total_dy) {
    int rem_x = total_dx;
    int rem_y = total_dy;
    while(rem_x != 0 || rem_y != 0) {
        int8_t step_x = (rem_x > 120) ? 120 : ((rem_x < -120) ? -120 : (int8_t)rem_x);
        int8_t step_y = (rem_y > 120) ? 120 : ((rem_y < -120) ? -120 : (int8_t)rem_y);
        hid_mouse_move(state, step_x, step_y);
        rem_x -= step_x;
        rem_y -= step_y;
        furi_delay_ms(15);
    }
}

static void handle_mouse_click(AppState* state, const char* btn_name, bool double_click) {
    uint8_t btn = HID_MOUSE_BTN_LEFT;
    if(btn_name) {
        if(strcmp(btn_name, "RIGHT") == 0) btn = HID_MOUSE_BTN_RIGHT;
        else if(strcmp(btn_name, "MIDDLE") == 0) btn = HID_MOUSE_BTN_WHEEL;
    }

    int count = double_click ? 2 : 1;
    for(int i = 0; i < count; i++) {
        hid_mouse_press(state, btn);
        furi_delay_ms(40);
        hid_mouse_release(state, btn);
        if(double_click && i == 0) furi_delay_ms(80);
    }
}

static bool handle_special_key(AppState* state, const char* key_cmd) {
    uint16_t mod = 0;
    uint16_t key = HID_KEYBOARD_NONE;

    if(strcmp(key_cmd, "KEY_ENTER") == 0) key = HID_KEYBOARD_RETURN;
    else if(strcmp(key_cmd, "KEY_TAB") == 0) key = HID_KEYBOARD_TAB;
    else if(strcmp(key_cmd, "KEY_ESC") == 0) key = HID_KEYBOARD_ESCAPE;
    else if(strcmp(key_cmd, "KEY_SPACE") == 0) key = HID_KEYBOARD_SPACEBAR;
    else if(strcmp(key_cmd, "KEY_BACKSPACE") == 0) key = HID_KEYBOARD_DELETE;
    else if(strcmp(key_cmd, "KEY_UP") == 0) key = HID_KEYBOARD_UP_ARROW;
    else if(strcmp(key_cmd, "KEY_DOWN") == 0) key = HID_KEYBOARD_DOWN_ARROW;
    else if(strcmp(key_cmd, "KEY_LEFT") == 0) key = HID_KEYBOARD_LEFT_ARROW;
    else if(strcmp(key_cmd, "KEY_RIGHT") == 0) key = HID_KEYBOARD_RIGHT_ARROW;
    else if(strcmp(key_cmd, "KEY_ALT_F4") == 0) { mod = KEY_MOD_LEFT_ALT; key = HID_KEYBOARD_F4; }
    else if(strcmp(key_cmd, "KEY_ALT_TAB") == 0) { mod = KEY_MOD_LEFT_ALT; key = HID_KEYBOARD_TAB; }
    else if(strcmp(key_cmd, "KEY_ALT_Y") == 0) { mod = KEY_MOD_LEFT_ALT; key = HID_KEYBOARD_Y; }
    else if(strcmp(key_cmd, "KEY_ALT_SHIFT") == 0) {
        hid_kb_press(state, KEY_MOD_LEFT_ALT);
        furi_delay_ms(30);
        hid_kb_press(state, KEY_MOD_LEFT_SHIFT);
        furi_delay_ms(50);
        hid_kb_release_all(state);
        return true;
    }
    else if(strcmp(key_cmd, "KEY_GUI_D") == 0) { mod = KEY_MOD_LEFT_GUI; key = HID_KEYBOARD_D; }
    else if(strcmp(key_cmd, "KEY_GUI_R") == 0) { mod = KEY_MOD_LEFT_GUI; key = HID_KEYBOARD_R; }
    else if(strcmp(key_cmd, "KEY_GUI_L") == 0) { mod = KEY_MOD_LEFT_GUI; key = HID_KEYBOARD_L; }
    else if(strcmp(key_cmd, "KEY_GUI_SPACE") == 0) { mod = KEY_MOD_LEFT_GUI; key = HID_KEYBOARD_SPACEBAR; }
    else if(strcmp(key_cmd, "KEY_CTRL_C") == 0) { mod = KEY_MOD_LEFT_CTRL; key = HID_KEYBOARD_C; }
    else if(strcmp(key_cmd, "KEY_CTRL_V") == 0) { mod = KEY_MOD_LEFT_CTRL; key = HID_KEYBOARD_V; }
    else {
        return false;
    }

    if(mod != 0) {
        hid_kb_press(state, mod);
        furi_delay_ms(40);
    }
    if(key != HID_KEYBOARD_NONE) {
        hid_kb_press(state, key);
        furi_delay_ms(50);
    }
    hid_kb_release_all(state);
    return true;
}

// --- Работа с раскладками и UTF-8 ---

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
    } else if((c & 0xE0) == 0xC0 && str[*index + 1] != ' ') {
        uint16_t code = ((c & 0x1F) << 6) | ((uint8_t)str[*index + 1] & 0x3F);
        (*index) += 2;
        return code;
    } else if((c & 0xF0) == 0xE0 && str[*index + 1] != ' ' && str[*index + 2] != ' ') {
        uint16_t code = ((c & 0x0F) << 12) |
                        (((uint8_t)str[*index + 1] & 0x3F) << 6) |
                        ((uint8_t)str[*index + 2] & 0x3F);
        (*index) += 3;
        return code;
    }
    (*index) += 1;
    return 0;
}

static void type_char_win_altcode(AppState* state, uint8_t ascii_code) {
    char digits[8];
    snprintf(digits, sizeof(digits), "%u", ascii_code);

    hid_kb_press(state, KEY_MOD_LEFT_ALT);
    furi_delay_ms(10);

    for(size_t i = 0; digits[i] != ' '; i++) {
        uint8_t d = digits[i] - '0';
        if(d < 10) {
            hid_kb_press(state, numpad_keys[d]);
            furi_delay_ms(8);
            hid_kb_release(state, numpad_keys[d]);
            furi_delay_ms(8);
        }
    }

    hid_kb_release(state, KEY_MOD_LEFT_ALT);
    furi_delay_ms(12 + (rand() % 15));
}

static void type_smart_string(
    AppState* state,
    const char* str,
    const KeyboardLayout* layout,
    bool use_win_altcodes) {
    // По BLE используем стандартные задержки, чуть более щадящие к буферу GATT
    uint32_t base_delay = (state->transport == TransportBle) ? 22 : 12;

    size_t i = 0;
    while(str[i] != ' ') {
        uint16_t codepoint = utf8_to_utf16(str, &i);
        if(codepoint == 0) continue;

        if(use_win_altcodes && codepoint >= 32 && codepoint <= 126) {
            type_char_win_altcode(state, (uint8_t)codepoint);
            continue;
        }

        uint16_t key = lookup_layout_key(layout, codepoint);

        if(key == HID_KEYBOARD_NONE && codepoint < 128) {
            key = HID_ASCII_TO_KEY((char)codepoint);
        }

        if(key != HID_KEYBOARD_NONE) {
            hid_kb_press(state, key);
            furi_delay_ms(base_delay + (rand() % 14));
            hid_kb_release(state, key);
            furi_delay_ms(base_delay + 4 + (rand() % 24));
        }
    }
}

static void init_sd_profiles(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, CHAMELEON_DIR);
    storage_simply_mkdir(storage, EXT_PATH("apps_data/chameleon_provisioner"));

    const char* sample_path = CHAMELEON_DIR "/win_ru_safe.txt";
    if(!storage_file_exists(storage, sample_path)) {
        Stream* stream = file_stream_alloc(storage);
        if(file_stream_open(stream, sample_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            stream_write_cstring(
                stream,
                "# Работает по USB и Bluetooth HID при любой раскладке!
"
                "USB_ID 046D:C31C
"
                "MFR Logitech
"
                "PROD USB Keyboard
"
                "ALT_MODE 1
"
                "Write-Host 'Chameleon Provisioner v1.2 (USB + BLE) Ready!' -ForegroundColor Green
"
                "Get-ComputerInfo | Select-Object CsName, OsName
");
        }
        file_stream_close(stream);
        stream_free(stream);
    }

    furi_record_close(RECORD_STORAGE);
}

static void press_combo(AppState* state, uint16_t modifier, uint16_t key) {
    hid_kb_press(state, modifier);
    furi_delay_ms(40);
    hid_kb_press(state, key);
    furi_delay_ms(60);
    hid_kb_release_all(state);
}

static void press_enter(AppState* state) {
    hid_kb_press(state, HID_KEYBOARD_RETURN);
    furi_delay_ms(40);
    hid_kb_release(state, HID_KEYBOARD_RETURN);
}

static void open_target_terminal(
    AppState* state,
    TargetOs os,
    const KeyboardLayout* layout,
    bool use_altcodes) {
    if(os == TargetOsWindows) {
        press_combo(state, KEY_MOD_LEFT_GUI, HID_KEYBOARD_R);
        furi_delay_ms(650);
        type_smart_string(state, "powershell -NoExit", layout, use_altcodes);
        press_enter(state);
        furi_delay_ms(1250);
    } else if(os == TargetOsLinux) {
        hid_kb_press(state, KEY_MOD_LEFT_CTRL);
        hid_kb_press(state, KEY_MOD_LEFT_ALT);
        hid_kb_press(state, HID_KEYBOARD_T);
        furi_delay_ms(60);
        hid_kb_release_all(state);
        furi_delay_ms(1050);
    } else if(os == TargetOsMacOS) {
        press_combo(state, KEY_MOD_LEFT_GUI, HID_KEYBOARD_SPACEBAR);
        furi_delay_ms(650);
        type_smart_string(state, "Terminal", layout, false);
        furi_delay_ms(450);
        press_enter(state);
        furi_delay_ms(1250);
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
            } else if(strncmp(cmd, "MFR ", 4) == 0) {
                strncpy(hid_cfg->manuf, cmd + 4, sizeof(hid_cfg->manuf) - 1);
                hid_cfg->manuf[sizeof(hid_cfg->manuf) - 1] = ' ';
            } else if(strncmp(cmd, "PROD ", 5) == 0) {
                strncpy(hid_cfg->product, cmd + 5, sizeof(hid_cfg->product) - 1);
                hid_cfg->product[sizeof(hid_cfg->product) - 1] = ' ';
            }
        }
    }

    furi_string_free(line);
    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
}

static bool execute_sd_script(AppState* state, TargetOs os, const char* filepath) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();
    KeyboardLayout* layout = malloc(sizeof(KeyboardLayout));
    load_keyboard_layout(layout, "en-US.kl");

    // По BLE Alt-коды Numpad работают не во всех хостах (смартфоны/планшеты), поэтому по умолчанию включаем их только для USB + Windows
    bool use_win_altcodes = (os == TargetOsWindows) && (state->transport == TransportUsb);
    bool terminal_opened = false;
    bool no_term = false;
    bool success = false;

    if(file_stream_open(stream, filepath, FSAM_READ, FSOM_OPEN_EXISTING)) {
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            const char* cmd = furi_string_get_cstr(line);

            if(strlen(cmd) == 0 || cmd[0] == '#' || strncmp(cmd, "REM ", 4) == 0 ||
               strncmp(cmd, "USB_ID ", 7) == 0 || strncmp(cmd, "MFR ", 4) == 0 ||
               strncmp(cmd, "PROD ", 5) == 0) {
                continue;
            }

            if(strcmp(cmd, "NO_TERM") == 0) {
                no_term = true;
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

            if(strncmp(cmd, "MOUSE_MOVE ", 11) == 0) {
                int dx = 0, dy = 0;
                if(sscanf(cmd + 11, "%d %d", &dx, &dy) == 2) {
                    smooth_mouse_move(state, dx, dy);
                }
                continue;
            }

            if(strncmp(cmd, "MOUSE_CLICK", 11) == 0) {
                const char* arg = (strlen(cmd) > 12) ? (cmd + 12) : NULL;
                handle_mouse_click(state, arg, false);
                continue;
            }

            if(strcmp(cmd, "MOUSE_DBLCLICK") == 0) {
                handle_mouse_click(state, "LEFT", true);
                continue;
            }

            if(strncmp(cmd, "MOUSE_SCROLL ", 13) == 0) {
                int delta = atoi(cmd + 13);
                if(delta != 0) {
                    int8_t d = (delta > 127) ? 127 : ((delta < -127) ? -127 : (int8_t)delta);
                    hid_mouse_scroll(state, d);
                }
                continue;
            }

            if(strncmp(cmd, "KEY_", 4) == 0) {
                if(handle_special_key(state, cmd)) {
                    furi_delay_ms(120);
                    continue;
                }
            }

            if(!terminal_opened && !no_term) {
                open_target_terminal(state, os, layout, use_win_altcodes);
                terminal_opened = true;
            }

            type_smart_string(state, cmd, layout, use_win_altcodes);
            press_enter(state);
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

// --- Управление профилем Bluetooth HID ---

static void bt_connection_status_callback(BtStatus status, void* context) {
    AppState* state = context;
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->ble_connected = (status == BtStatusConnected);
    if(state->transport == TransportBle && !state->is_running) {
        snprintf(
            state->status_text,
            sizeof(state->status_text),
            state->ble_connected ? "BLE Connected! OK:Run" : "BLE: Pair in PC settings");
    }
    furi_mutex_release(state->mutex);
}

static void enable_ble_hid_mode(AppState* state) {
    if(!state->bt) {
        state->bt = furi_record_open(RECORD_BT);
    }
    bt_disconnect(state->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(state->bt, BLE_KEYS_PATH);

    BleProfileHidParams params = {
        .device_name_prefix = "Chameleon",
        .mac_xor = 0x0002,
    };
    state->ble_profile = bt_profile_start(state->bt, ble_profile_hid, &params);
    furi_hal_bt_start_advertising();
    bt_set_status_changed_callback(state->bt, bt_connection_status_callback, state);

    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->ble_connected = false;
    snprintf(state->status_text, sizeof(state->status_text), "BLE: Pair 'Chameleon ...'");
    furi_mutex_release(state->mutex);
}

static void disable_ble_hid_mode(AppState* state) {
    if(state->bt) {
        bt_set_status_changed_callback(state->bt, NULL, NULL);
        bt_disconnect(state->bt);
        furi_delay_ms(200);
        bt_keys_storage_set_default_path(state->bt);
        bt_profile_restore_default(state->bt);
        furi_record_close(RECORD_BT);
        state->bt = NULL;
        state->ble_profile = NULL;
        state->ble_connected = false;
    }
}

static void run_provisioning_payload(AppState* state, NotificationApp* notifications) {
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->is_running = true;
    const char* filepath = furi_string_get_cstr(state->script_path);
    TargetOs os = state->selected_os;
    TransportMode mode = state->transport;
    bool ble_ok = state->ble_connected;
    furi_mutex_release(state->mutex);

    if(mode == TransportBle) {
        if(!ble_ok) {
            furi_mutex_acquire(state->mutex, FuriWaitForever);
            snprintf(state->status_text, sizeof(state->status_text), "BLE not paired yet!");
            state->is_running = false;
            furi_mutex_release(state->mutex);
            notification_message(notifications, &sequence_error);
            return;
        }

        furi_mutex_acquire(state->mutex, FuriWaitForever);
        snprintf(state->status_text, sizeof(state->status_text), "Running over BLE...");
        furi_mutex_release(state->mutex);

        bool ok = execute_sd_script(state, os, filepath);
        notification_message(notifications, ok ? &sequence_success : &sequence_error);

        furi_mutex_acquire(state->mutex, FuriWaitForever);
        snprintf(state->status_text, sizeof(state->status_text), ok ? "BLE Done! OK:Run" : "Script read error!");
        state->is_running = false;
        furi_mutex_release(state->mutex);
        return;
    }

    // Режим USB HID
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    snprintf(state->status_text, sizeof(state->status_text), "Configuring USB HID...");
    furi_mutex_release(state->mutex);

    FuriHalUsbHidConfig hid_cfg = {
        .vid = 0x046D,
        .pid = 0xC31C,
        .manuf = "Logitech",
        .product = "USB Keyboard",
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
    snprintf(state->status_text, sizeof(state->status_text), "Running over USB...");
    furi_mutex_release(state->mutex);

    bool ok = execute_sd_script(state, os, filepath);

    furi_delay_ms(400);
    furi_hal_usb_set_config(prev_usb, NULL);

    notification_message(notifications, ok ? &sequence_success : &sequence_error);

    furi_mutex_acquire(state->mutex, FuriWaitForever);
    snprintf(state->status_text, sizeof(state->status_text), ok ? "USB Done! OK:Run" : "Read error!");
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
    canvas_draw_str(canvas, 4, 11, "Chameleon v1.2");
    canvas_draw_line(canvas, 0, 14, 128, 14);

    // Верхняя строка выбора: ОС и Транспорт (USB / BLE)
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, 25, "OS:");
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 22, 25, target_os_names[state->selected_os]);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 74, 25, "Mode:");
    canvas_set_font(canvas, FontPrimary);
    if(state->transport == TransportBle) {
        canvas_draw_str(canvas, 102, 25, state->ble_connected ? "BLE*" : "BLE");
    } else {
        canvas_draw_str(canvas, 102, 25, transport_names[state->transport]);
    }

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
    if(input_event->type == InputTypeShort || input_event->type == InputTypeLong) {
        furi_message_queue_put(event_queue, input_event, FuriWaitForever);
    }
}

int32_t chameleon_provisioner_app(void* p) {
    UNUSED(p);

    init_sd_profiles();

    AppState* state = malloc(sizeof(AppState));
    state->selected_os = TargetOsWindows;
    state->transport = TransportUsb;
    state->ble_connected = false;
    state->ble_profile = NULL;
    state->bt = NULL;
    state->script_path = furi_string_alloc_set(CHAMELEON_DIR "/win_ru_safe.txt");
    state->is_running = false;
    snprintf(state->status_text, sizeof(state->status_text), "OK:Run HoldOK:USB/BLE");
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
            if(event.key == InputKeyBack && event.type == InputTypeShort) {
                exit_loop = true;
            } else if((event.key == InputKeyLeft || event.key == InputKeyRight) &&
                      event.type == InputTypeShort && !state->is_running) {
                furi_mutex_acquire(state->mutex, FuriWaitForever);
                if(event.key == InputKeyLeft) {
                    state->selected_os = (state->selected_os == 0) ? (TargetOsCount - 1) : (state->selected_os - 1);
                } else {
                    state->selected_os = (state->selected_os + 1) % TargetOsCount;
                }
                furi_mutex_release(state->mutex);
            } else if((event.key == InputKeyUp || event.key == InputKeyDown) &&
                      event.type == InputTypeShort && !state->is_running) {
                select_script_file(state);
            } else if(event.key == InputKeyOk && event.type == InputTypeLong && !state->is_running) {
                // Долгое нажатие OK переключает транспорт между USB и Bluetooth HID
                if(state->transport == TransportUsb) {
                    furi_mutex_acquire(state->mutex, FuriWaitForever);
                    state->transport = TransportBle;
                    furi_mutex_release(state->mutex);
                    enable_ble_hid_mode(state);
                } else {
                    disable_ble_hid_mode(state);
                    furi_mutex_acquire(state->mutex, FuriWaitForever);
                    state->transport = TransportUsb;
                    snprintf(state->status_text, sizeof(state->status_text), "Mode: USB HID");
                    furi_mutex_release(state->mutex);
                }
            } else if(event.key == InputKeyOk && event.type == InputTypeShort && !state->is_running) {
                run_provisioning_payload(state, notifications);
            }
            view_port_update(view_port);
        }
    }

    disable_ble_hid_mode(state);

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
