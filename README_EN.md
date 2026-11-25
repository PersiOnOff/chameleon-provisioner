# Chameleon Provisioner (v1.2.0)

[Читать документацию на русском](README.md)

**Chameleon Provisioner** is a context-aware hybrid HID provisioner for **Flipper Zero** supporting both wired **USB HID** and wireless **Bluetooth LE HID** transports, dynamic USB descriptor spoofing (`VID`, `PID`, `MFR`, `PROD`), mouse/scroll emulation, and layout-independent execution.

## Features (v1.2.0)

* **Dual Transport (`USB` & `BLE`):** Switch seamlessly between wired USB HID and wireless Bluetooth LE HID by long-pressing the **OK** button.
* **Isolated BLE Bonding Keys:** Stores HID pairing keys separately in `/ext/apps_data/chameleon_provisioner/hid.keys` without breaking your primary Flipper mobile app connection.
* **Dynamic USB Descriptor Spoofing:** Override `VID`, `PID`, Manufacturer (`MFR`), and Product (`PROD`) strings directly from the `.txt` script header (USB mode).
* **Layout-Independent Windows Input (`ALT_MODE 1`):** Types ASCII commands via `Alt + Numpad` on Windows, preventing garbled text when non-US keyboard layouts are active.
* **`.kl` Keymap Support:** Load standard Flipper keymaps from `/ext/badusb/assets/layouts/` (e.g., `LAYOUT ru-RU.kl`) for localized text input.
* **Mouse & Scroll Emulation:** Smooth relative cursor movement, single/double clicks, and scroll wheel control.
* **Special Keys (`KEY_*`):** Built-in shortcuts (`KEY_ALT_Y`, `KEY_ALT_SHIFT`, `KEY_GUI_D`, `KEY_GUI_L`, etc.) for UI and dialog navigation without a mouse.
* **Built-in File Browser:** Pick any `.txt` payload from `/ext/chameleon/` using Flipper's native file dialog.

## Controls on Flipper Zero

| Button | Action |
| :--- | :--- |
| **Left / Right** | Select target OS (`Windows (PS)`, `Linux (Bash)`, `macOS (Zsh)`) |
| **Up / Down** | Open file browser to select a `.txt` script from `/ext/chameleon/` |
| **OK (Short Press)** | Execute the selected payload |
| **OK (Long Press)** | Toggle transport mode between `USB` and `BLE` |
| **Back** | Exit application and restore default USB/BLE profile |

## Bluetooth HID Pairing Guide (v1.2.0)

1. Launch **Chameleon Provisioner** on your Flipper Zero.
2. **Hold the OK button** (~1 second) until the top-right badge switches from `USB` to **`BLE`**.
3. On your target host (PC, laptop, Android phone, or iPad/iPhone), open **Bluetooth Settings**.
4. Pair with **`Chameleon <Flipper_Name>`**.
5. Once connected, the badge changes to **`BLE*`** — you can now trigger payloads wirelessly with a short press of **OK**.

> **Note:** In `BLE` mode, `ALT_MODE` (Alt+Numpad) is automatically disabled for compatibility with mobile OSes, macOS, and Linux, and keystroke timings are adjusted for Bluetooth throughput.

## Script Directives Reference

| Directive | Description | Example |
| :--- | :--- | :--- |
| `USB_ID <VID>:<PID>` | Spoof Vendor ID and Product ID (USB only) | `USB_ID 046D:C52B` |
| `MFR <string>` | Spoof Manufacturer string (up to 31 chars) | `MFR Apple Inc.` |
| `PROD <string>` | Spoof Product string (up to 31 chars) | `PROD Magic Keyboard` |
| `ALT_MODE <0\|1>` | Type ASCII via Alt+Numpad on Windows | `ALT_MODE 1` |
| `LAYOUT <file.kl>` | Load keymap layout file from SD card | `LAYOUT ru-RU.kl` |
| `NO_TERM` | Skip automatic terminal launch | `NO_TERM` |
| `SLEEP <ms>` | Pause execution in milliseconds | `SLEEP 1500` |
| `MOUSE_MOVE <dx> <dy>` | Smooth relative cursor movement | `MOUSE_MOVE 120 -40` |
| `MOUSE_CLICK <btn>` | Mouse button click (`LEFT`, `RIGHT`, `MIDDLE`) | `MOUSE_CLICK LEFT` |
| `MOUSE_DBLCLICK` | Double left-click | `MOUSE_DBLCLICK` |
| `MOUSE_SCROLL <delta>` | Scroll wheel (`+` up, `-` down) | `MOUSE_SCROLL -5` |
| `KEY_<name>` | Press special key or system shortcut | `KEY_ALT_Y`, `KEY_GUI_L` |

## Building from Source

```bash
pip install --upgrade ufbt
ufbt launch
```

## Disclaimer

This tool is intended strictly for authorized system administration, environment provisioning, and security testing on systems you own or have explicit permission to test.
