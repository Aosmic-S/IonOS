# ⚡ IonOS — Embedded Handheld Operating System

![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange)
![Framework](https://img.shields.io/badge/Framework-ESP--IDF%20v5+-green)
![UI](https://img.shields.io/badge/UI-LVGL%208.3-purple)
![Kernel](https://img.shields.io/badge/Kernel-FreeRTOS-red)
![Status](https://img.shields.io/badge/Status-Active%20Development-brightgreen)

> **IonOS** is a powerful **embedded handheld operating system** built for **ESP32-S3 devices**, designed to power portable electronics with a modern UI, modular apps, WiFi connectivity, audio playback, and retro gaming support.

```
╔══════════════════════════════════════════════════════╗
║                    IonOS v1.0.0                      ║
║          ESP32-S3 Handheld OS Platform               ║
║    Boot → Home → Apps → Music → Browser → Games     ║
╚══════════════════════════════════════════════════════╝
```

---

# 🚀 Quick Start

### 1. Install ESP-IDF

IonOS requires **ESP-IDF v5.1+**

```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
source export.sh
```

### 2. Build IonOS

```bash
git clone https://github.com/Aosmic-S/IonOS.git
cd IonOS

idf.py set-target esp32s3
idf.py build
```

### 3. Flash to Device

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

---

# 📦 Feature Matrix

| Feature | Status | Details |
|------|------|------|
| Boot Animation | ✅ | IonOS logo with animated progress bar |
| Home Screen | ✅ | 3×3 icon grid with D-Pad navigation |
| Status Bar | ✅ | WiFi, battery %, and time display |
| Notifications | ✅ | Slide-in notifications with LED + sound |
| Settings App | ✅ | WiFi, brightness, volume, system info |
| File Manager | ✅ | Browse SD card files |
| Music Player | ✅ | WAV / MP3 playback with playlist |
| Browser | ✅ | Lightweight HTTP text browser |
| Chatbot | ✅ | Offline rules + OpenAI API support |
| GameBoy Emulator | ⚙️ | ROM loader and emulator interface |
| WiFi Manager | ✅ | Scan and auto connect |
| Audio System | ✅ | PCM5102A I2S DAC |
| RGB LED Engine | ✅ | WS2812B animations |
| nRF24 Radio | ✅ | SPI wireless communication |
| Power Manager | ✅ | Battery monitoring and sleep |
| External Apps | ✅ | Dynamic SD card apps |
| PSRAM Support | ✅ | Large buffers and ROM loading |
| OTA Updates | 🔄 | Partition ready |

---

# 🎮 Button Layout

```
         [UP]           [X]
   [LEFT]    [RIGHT]       [A] 
        [DOWN]           [B]              [MENU]       [START]                
```

| Button | Function |
|------|------|
| D-Pad | Navigate UI |
| A | Select |
| B | Back |
| X | Secondary action |
| START | App switcher |
| MENU | System menu |

---

# 🏗 Architecture

```
IonOS Kernel (FreeRTOS)
    │
    ├─ Hardware Drivers
    │     SPI / I2S / RMT / GPIO / ADC
    │     ST7789V · PCM5102A · WS2812B · nRF24 · SD · WiFi
    │
    ├─ UI Engine
    │     LVGL v8.3
    │     Boot Animation · Home Screen · Status Bar
    │
    ├─ System Services
    │     WiFi Manager
    │     Audio Manager
    │     Power Manager
    │     File System Manager
    │     Notification Service
    │
    └─ Applications
          Settings · Files · Music
          Browser · Chatbot · Emulator
          + External SD Card Apps
```

---

# 📁 SD Card Layout

```
/sdcard/
├── apps/
│   └── MyApp/
│       ├── manifest.json
│       ├── app.bin
│       └── icon.png
│
├── music/
│
├── roms/
│   ├── gb/
│   ├── gbc/
│   └── gba/
│
├── data/
│
└── photos/
```

---

# ⚙️ Hardware Pinout

| Component | GPIO Pins |
|------|------|
| ST7789V Display | MOSI:11 SCLK:12 CS:10 DC:9 RST:8 BLK:13 |
| SD Card | MOSI:35 MISO:37 SCLK:36 CS:38 |
| PCM5102A | BCK:4 LRCK:5 DATA:6 |
| WS2812B LEDs | DATA:48 |
| nRF24L01+ | MOSI:2 MISO:3 SCLK:7 CS:39 CE:40 IRQ:41 |
| Buttons | UP:14 DN:15 L:16 R:17 A:18 B:19 X:20 ST:21 MN:47 |
| Battery ADC | GPIO1 |

---

# 📖 Documentation

Full developer documentation can be found in

```
docs/README.md
```

---

# 🤝 Contributing

1. Fork the repository  
2. Create a feature branch  
3. Commit your changes  
4. Submit a Pull Request

---

# 📜 License

Licensed under the **Apache License 2.0**

You are free to:

• Use commercially  
• Modify the source  
• Distribute the software  

You must include the license and state significant changes.

See the `LICENSE` file for full details.

---

# 👨‍💻 Author

**Aosmic Studio**

Embedded Systems • Firmware • Hardware Innovation
