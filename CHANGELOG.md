# Changelog

All notable changes to IonOS are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [1.1.0] — Bruce Integration

### Added
- **Bruce Pentesting Toolkit** — full port of [Bruce firmware v1.14](https://github.com/pr3y/Bruce)
  into IonOS as a native LVGL app. Appears as "Bruce" in the home screen grid.
- **WiFi features**: network scan, AP connect, deauth frames, beacon flood, packet sniffer
- **BLE features**: proximity spam (Apple AirDrop + Google Fast Pair triggers), BLE scan
- **Infrared**: TV-B-Gone (NEC codes for 12+ TV brands via RMT), custom .ir file playback
  from SD card (`/sdcard/infrared/`)
- **NRF24**: 2.4GHz jammer (channel-hopping noise transmit), 126-channel spectrum analyzer
  with real-time LVGL bar chart
- **Files**: SD card browser showing `/sdcard/` contents and free space
- **Others**: QR code generator, system info, device reboot
- **Config**: IR TX pin, NRF24 channel info, Bruce repo QR link
- **Back-to-menu**: press **B** from any screen to return to IonOS home screen instantly.
  Running attack tasks are stopped cleanly before returning.

### Changed
- `lv_conf.h`: enabled `LV_USE_QRCODE` for QR code widget
- `sdkconfig.defaults`: enabled `CONFIG_BT_ENABLED` for BLE features
- `NRF24Driver`: added `readCarrierDetect()` for spectrum scanner
- `AppManager`: registered BruceApp with red `ION_ICON_WARNING_ICO` icon

### Notes
- Bruce features that require hardware not present on IonOS (CC1101 RF, RFID/PN532)
  are shown as stubs. They will be implemented when the corresponding hardware drivers
  are added.
- BLE features require ESP32-S3's built-in Bluetooth controller. BLE and WiFi cannot
  run simultaneously — Bruce disables one before enabling the other.
- TV-B-Gone IR codes: wire an IR LED + 68Ω resistor between `BRUCE_IR_TX_PIN`
  (default GPIO 45) and GND.

---

## [1.0.0] — 2024 (Initial Release)

### Added
- **Kernel** — 5-phase boot, dual-core task assignment, FreeRTOS event bus (64-slot queue),
  task registry, PSRAM memory manager
- **ST7789V driver** — SPI2 DMA, LEDC backlight PWM, LVGL double-buffer flush,
  landscape MADCTL=0x60 (320×240)
- **PCM5102A driver** — I2S0 44.1 kHz stereo, DMA streaming, WAV file playback,
  software volume
- **Button driver** — 9 buttons, 20 ms debounce, 800 ms long-press, LVGL keypad indev.
  **X is primary select** (maps to LV_KEY_ENTER)
- **WS2812B driver** — 7 LEDs, RMT 10 MHz, GRB, 6 animations
  (RAINBOW, PULSE, WIFI_BLINK, BATT_LOW, CHARGING, MUSIC_BEAT)
- **SD card driver** — SPI3, FAT/VFS at /sdcard, auto-creates default dirs
- **WiFi driver** — STA mode, async connect, auto-reconnect, scan
- **nRF24L01+ driver** — SPI 8 MHz, channel 76, 32-byte payload, auto-ACK, poll task
- **LVGL UI engine** — 60 fps timer loop on Core 1, recursive mutex, style helpers,
  screen slide transitions
- **Boot animation** — 10-frame logo materialise (160×100 RGB565, 800 ms)
- **Home screen** — 4×2 icon grid (landscape), D-pad focus with scale/glow, X=launch
- **Status bar** — WiFi icon, battery gauge + colour, HH:MM time, overlay on lv_layer_top
- **Notification popup** — slide-in toast, 4 severity levels, auto-dismiss
- **3 themes** — Dark Pro, Neon Gaming, Retro Console; NVS persistence
- **Settings app** — WiFi scan+connect, brightness, volume, theme, system info
- **Music Player** — WAV/MP3/FLAC from SD, playlist, LED MUSIC_BEAT sync
- **Browser** — HTTP bookmarks, HTML stripping, async fetch
- **IonBot chatbot** — 20-rule offline engine + OpenAI API online mode
- **File Manager** — SD directory browser, file type icons, delete
- **Game Boy emulator** — Peanut-GB integration, 160×144 RGB565 framebuf in PSRAM,
  1.5× scaled via lv_img (no canvas copy), save/load .sav files,
  X=GB_A, A=GB_B, START=pause, B=exit
- **App Store** — Library + Available tabs, .ionapp package install/update/uninstall,
  permission cards, animated progress bar
- **App Installer** — .ionapp binary format (256-byte header, CRC32, RGB565 icon,
  code binary), validate/install/uninstall pipeline
- **Developer SDK** — `ion_sdk.h` single-header, hello_world_app, widget_gallery
- **Asset pipeline** — `gen_assets.py` generates all icons, sounds, boot frames,
  bitmap font from pure Python (Pillow + NumPy). No external asset files.
- **Package builder** — `create_ionapp.py` packs apps into .ionapp for SD install
- **Apache 2.0 license**

### Hardware target
- ESP32-S3, 2 MB PSRAM, 8 MB Flash
- ST7789V 240×320 used in landscape (320×240), MADCTL=0x60
- PCM5102A DAC, WS2812B×7, nRF24L01+, microSD, 9 buttons, LiPo+TP4056

---

## [Unreleased]

### Planned for v1.1
- Dynamic app loading (position-independent ELF from SD)
- OTA firmware update via HTTPS
- GBC colour palette support in emulator
- Audio: MP3 decode via helix-mp3
- nRF24 peer-to-peer chat app
- Camera app (ESP32-S3 DVP interface)
