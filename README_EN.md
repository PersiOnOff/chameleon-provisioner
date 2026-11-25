# Chameleon Provisioner for Flipper Zero

**Chameleon Provisioner** is a context-aware USB HID automation and provisioning application (`.fap`) for Flipper Zero. It allows dynamically spoofing USB device descriptors (`VID/PID`, manufacturer, and product strings) and executing multi-step provisioning scripts directly from the SD card with built-in keyboard layout protection.

> *[Читать документацию на русском языке](README.md)*

## Key Features

* **Dynamic USB Descriptor Spoofing (`FuriHalUsbHidConfig`):** Emulates legitimate USB keyboards (Logitech, Apple, Cherry, or custom hardware) by configuring `VID`, `PID`, `MFR`, and `PROD` descriptors per script.
* **SD Card Script Engine:** Reads `.txt` payload scripts directly from `/ext/chameleon/` using the native Flipper file browser (`DialogsApp`) — no firmware recompilation required.
* **Layout-Independent Windows Typing (`ALT_MODE`):** Types ASCII commands on Windows via `Alt + Numpad` codes, ensuring payloads execute accurately even if a non-English keyboard layout (e.g., Russian) or `CapsLock` is active.
* **Native `.kl` Layout Support:** Loads Flipper keyboard layout tables (`/ext/badusb/assets/layouts/*.kl`) to type localized UTF-8 characters.
* **Humanized Keystroke Timing:** Introduces randomized inter-key delays to simulate natural typing cadence.
* **Clean USB Profile Restoration:** Automatically restores the previous USB interface configuration (e.g., USB CDC/Serial for qFlipper) after script execution completes.

## Controls on Flipper Zero

| Button | Action |
| :--- | :--- |
| **Left / Right** | Switch target operating system profile (`Windows (PS)`, `Linux (Bash)`, `macOS (Zsh)`) |
| **Up / Down** | Open the SD card file browser in `/ext/chameleon` to select a `.txt` script |
| **OK** | Parse headers, spoof USB HID descriptors, open target terminal, and run the script |
| **Back** | Exit the application cleanly |

## Script Directives Reference

Place `.txt` scripts inside `SD Card/chameleon/`. On first launch, the app automatically creates this directory and generates a sample `win_ru_safe.txt` file.

| Directive | Example | Description |
| :--- | :--- | :--- |
| `USB_ID <VID>:<PID>` | `USB_ID 05AC:024F` | Sets custom USB Vendor ID and Product ID in hex format before HID enumeration. |
| `MFR <string>` | `MFR Apple Inc.` | Sets the USB Manufacturer string descriptor (up to 31 characters). |
| `PROD <string>` | `PROD Magic Keyboard` | Sets the USB Product string descriptor (up to 31 characters). |
| `ALT_MODE <1\|0>` | `ALT_MODE 1` | Enables (`1`) or disables (`0`) layout-independent `Alt + Numpad` ASCII input on Windows. |
| `LAYOUT <file.kl>` | `LAYOUT ru-RU.kl` | Loads a keymap table from `/ext/badusb/assets/layouts/` for localized UTF-8 text. |
| `SLEEP <ms>` | `SLEEP 1500` | Pauses script execution for the specified number of milliseconds. |
| `#` or `REM ` | `# Setup environment` | Comment line (ignored during execution). |

### Example Payload (`apple_spoof_demo.txt`)

```text
# Spoof as Apple Magic Keyboard and run diagnostics
USB_ID 05AC:024F
MFR Apple Inc.
PROD Magic Keyboard
ALT_MODE 1
Write-Host 'Spoofed as Apple Magic Keyboard!' -ForegroundColor Cyan
Get-PnpDevice -Class Keyboard -Status OK | Select-Object FriendlyName, InstanceId
```

## Building from Source

This project uses the official **micro Flipper Build Tool (`uFBT`)**.

1. Install `uFBT`:
   ```bash
   python -m pip install --upgrade ufbt
   ```
2. Build and launch directly on a connected Flipper Zero (close `qFlipper` first):
   ```bash
   ufbt launch
   ```
3. Alternatively, push a version tag (`v*`) to GitHub to automatically build the `.fap` binary via GitHub Actions.

## Disclaimer

This software is developed strictly for authorized system administration, endpoint policy auditing, and educational research on systems you own or have explicit written permission to test.
