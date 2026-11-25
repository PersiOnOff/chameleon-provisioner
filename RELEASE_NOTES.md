# Release v1.2.0 — Wireless Bluetooth LE HID Support & Mobile Payloads

## 🇷🇺 Что нового в v1.2.0

### Беспроводной транспорт Bluetooth LE HID (`USB` ↔ `BLE`)
* **Переключение транспорта на лету:** Долгое удержание кнопки **OK** (`Long Press`) переключает режим работы между проводным **USB HID** и беспроводным **Bluetooth LE HID** (`ble_profile_hid`).
* **Изолированное хранилище ключей сопряжения:** Ключи Bluetooth-привязки сохраняются отдельно в `/ext/apps_data/chameleon_provisioner/hid.keys`, не затрагивая основное сопряжение Flipper Zero со смартфоном.
* **Индикация статуса подключения:** В шапке экрана отображается текущий транспорт (`USB`, `BLE` при ожидании подключения и `BLE*` при активном сопряжении).
* **Адаптивные тайминги по воздуху:** При работе через Bluetooth движок автоматически корректирует задержки между нажатиями клавиш и кликами мыши для стабильного приема на ПК, планшетах и смартфонах.
* **Корректное восстановление профиля:** При выходе из приложения (`Back`) или обратном переключении в `USB` системный профиль Bluetooth автоматически возвращается в исходное состояние.

### Новые сценарии и документация
* Добавлены беспроводные сценарии для мобильных платформ: `android_ble_quick.txt` (Android) и `ios_ble_notes.txt` (iOS / iPadOS).
* Обновлена двуязычная документация (`README.md` и `README_EN.md`) с пошаговой инструкцией по сопряжению по Bluetooth LE HID.
* Увеличен размер стека приложения в `application.fam` (`4 KB`) для стабильной работы BLE-стека.

---

## 🇬🇧 What's New in v1.2.0

### Wireless Bluetooth LE HID Transport (`USB` ↔ `BLE`)
* **On-the-fly Transport Switching:** Long-pressing **OK** toggles between wired **USB HID** and wireless **Bluetooth LE HID** (`ble_profile_hid`).
* **Isolated Pairing Keys Storage:** BLE bonding keys are stored separately in `/ext/apps_data/chameleon_provisioner/hid.keys`, preserving your primary Flipper mobile app pairing.
* **Live Connection Indicator:** The top bar displays the active transport mode (`USB`, `BLE` when advertising, and `BLE*` when connected).
* **Adaptive Wireless Timings:** Automatically adjusts keystroke and mouse delays over BLE for reliable execution across desktops, tablets, and smartphones.
* **Clean Profile Restoration:** Restores the default Bluetooth profile upon exiting the app (`Back`) or switching back to `USB`.

### New Payloads & Documentation
* Added mobile wireless payloads: `android_ble_quick.txt` (Android) and `ios_ble_notes.txt` (iOS / iPadOS).
* Updated bilingual documentation (`README.md` and `README_EN.md`) with a step-by-step BLE pairing guide.
* Increased app stack size in `application.fam` (`4 KB`) for reliable BLE operation.

---

## 📦 Установка / Installation
1. Скачайте `chameleon_provisioner.fap` из раздела **Assets** ниже.
2. Скопируйте файл на SD-карту Flipper Zero в папку `/ext/apps/USB/`.
3. Запустите **Apps → USB → Chameleon Provisioner** (при первом запуске автоматически создастся папка `/ext/chameleon/`).
