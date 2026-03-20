# IonOS Developer Guide — Deep Dive

This document covers internal architecture in depth for contributors and advanced app developers.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        IonOS Stack                          │
├─────────────────────────────────────────────────────────────┤
│  SDK Layer        ion_sdk.h  (single-header app include)    │
├──────────────┬──────────────────────────────────────────────┤
│  Apps        │ Settings│Music│Browser│Chatbot│Files│Emu     │
├──────────────┼──────────────────────────────────────────────┤
│  App Manager │ IonApp base │ Launch/Close │ Key forwarding  │
├──────────────┼──────────────────────────────────────────────┤
│  Services    │WiFiMgr│AudioMgr│PowerMgr│FSMgr│NotifSvc     │
├──────────────┼──────────────────────────────────────────────┤
│  UI Engine   │ LVGL v8 │ Themes │ Popups │ 60fps loop      │
├──────────────┼──────────────────────────────────────────────┤
│  Resources   │ Icons │ Sounds │ Boot Frames │ Fonts          │
├──────────────┼──────────────────────────────────────────────┤
│  Drivers     │ST7789│PCM5102A│Buttons│WS2812│SD│WiFi│nRF24 │
├──────────────┼──────────────────────────────────────────────┤
│  Kernel      │ Boot sequence │ Event bus │ Task registry    │
├──────────────┼──────────────────────────────────────────────┤
│  ESP-IDF     │ FreeRTOS │ SPI │ I2S │ GPIO │ ADC │ NVS      │
└──────────────┴──────────────────────────────────────────────┘
```

---

## Dual-Core Assignment

```
Core 0 (Protocol CPU)          Core 1 (Application CPU)
─────────────────────          ─────────────────────────
ion_events  (event bus)        ion_ui  (LVGL 60fps)
ion_audio   (I2S stream)
ion_input   (button poll)
ion_power   (battery ADC)
ion_leds    (RMT animations)
WiFi stack  (internal)
TCP/IP      (internal)
```

Core 1 is dedicated to the LVGL render loop. This prevents jitter from WiFi/audio interrupts affecting frame rate. The LVGL mutex (`UIEngine::lock/unlock`) gates all UI updates from Core 0 code.

---

## LVGL Integration Details

### Buffer Layout

```
PSRAM (8MB total)
├── LVGL Buffer A  [28.8 KB]  240×60 lines × 2 bytes/pixel
├── LVGL Buffer B  [28.8 KB]  240×60 lines × 2 bytes/pixel
├── Audio DMA buf  [8 × 512 × 4 = 16 KB]
├── ROM framebuf   [160×144×2 = 46 KB]  (Emulator only)
├── HTTP buffer    [4 KB]     (Browser/Chatbot)
└── ROM data       [up to ~8 MB for GBA]
```

### Flush Callback Flow

```
lv_timer_handler()
  → dirty region detected
  → lv_draw_*() renders into Buffer A
  → LVGL calls flush_cb (our lvglFlushCb)
      → setAddrWindow(x0, y0, x1, y1)
      → spi_device_queue_trans(Buffer A, DMA)   ← returns immediately
      → DMA fires flushReadyCb ISR
          → lv_disp_flush_ready()               ← tells LVGL it can render next frame
          → buffers swap (A becomes B, B becomes A)
```

---

## NVS Key Map

| Namespace | Key | Type | Value |
|-----------|-----|------|-------|
| `ionos_wifi` | `ssid` | string | Last connected SSID |
| `ionos_wifi` | `pass` | string | Last WiFi password |
| `ionos_ui` | `theme` | uint8 | Theme ID (0–2) |
| `ionos_ui` | `brightness` | uint8 | Display brightness % |
| `ionos_ui` | `volume` | uint8 | Audio volume 0–100 |

Clear all with: `./tools/erase_nvs.sh /dev/ttyUSB0`

---

## Extending the Asset Pipeline

### gen_assets.py Architecture

```python
# Icon generation pipeline:
icon_fn()          # Returns 32×32 PIL Image
  → img_to_c()     # Converts RGB pixels to RGB565 byte-swapped
  → c_array()      # Formats as uint8_t C array
  → lv_img_dsc_t   # Wraps in LVGL image descriptor struct

# Sound generation pipeline:
sound_fn()         # Returns numpy int16 stereo array
  → .tobytes()     # Raw PCM16 LE bytes
  → c_array()      # Formats as uint8_t C array
  → ion_sound_t    # Wraps with len/sr metadata

# Boot frame pipeline:
make_boot_frame(i) # Returns 120×80 PIL Image drawn procedurally
  → img_to_c()     # RGB565 conversion
  → lv_img_dsc_t   # LVGL descriptor
```

### RGB565 Byte Swap

IonOS displays use `LV_COLOR_16_SWAP=1` (bytes swapped for SPI). The `img_to_c()` function handles this:

```python
def to_bytes_swapped(val16):
    return struct.pack(">H", val16)  # big-endian = byte swapped for SPI
```

This avoids a CPU byte-swap on every pixel during display transfer.

---

## Emulator Integration Guide

### Integrating Peanut-GB

1. Download `peanut_gb.h` from https://github.com/deltabeard/Peanut-GB
2. Place it at `main/apps/emulator/core/peanut_gb.h`
3. In `peanut_gb_core.cpp`, uncomment and implement the callbacks:

```c
#define ENABLE_LCD   1
#define ENABLE_SOUND 0  // Use IonOS AudioManager for sound
#include "peanut_gb.h"

static uint8_t gb_rom_read(struct gb_s* gb, uint_fast32_t addr) {
    return ((GBCore*)gb->direct.priv)->romData[addr];
}
static uint8_t gb_cart_ram_read(struct gb_s* gb, uint_fast32_t addr) {
    return ((GBCore*)gb->direct.priv)->ramData[addr];
}
static void gb_cart_ram_write(struct gb_s* gb, uint_fast32_t addr, uint8_t val) {
    ((GBCore*)gb->direct.priv)->ramData[addr] = val;
}
static void gb_error(struct gb_s* gb, enum gb_error_e err, uint16_t val) {
    ESP_LOGE("GBCore", "Error %d at 0x%04X", err, val);
}
static void gb_lcd_draw_line(struct gb_s* gb, const uint8_t px[LCD_WIDTH], uint_fast8_t line) {
    GBCore* ctx = (GBCore*)gb->direct.priv;
    // DMG 4-shade palette → RGB565
    static const uint16_t pal[4] = {
        0xFFFF,  // 0 = white
        0xA514,  // 1 = light gray
        0x4228,  // 2 = dark gray
        0x0000,  // 3 = black
    };
    for (int x = 0; x < LCD_WIDTH; x++)
        ctx->framebuf[line * LCD_WIDTH + x] = pal[px[x] & 3];
}

bool gbcore_init(GBCore* ctx) {
    ctx->coreState = heap_caps_malloc(sizeof(struct gb_s), MALLOC_CAP_SPIRAM);
    struct gb_s* gb = (struct gb_s*)ctx->coreState;
    enum gb_init_error_e err = gb_init(gb, gb_rom_read, gb_cart_ram_read,
                                        gb_cart_ram_write, gb_error, ctx);
    if (err != GB_INIT_NO_ERROR) { ESP_LOGE("GBCore", "Init error: %d", err); return false; }
    gb_init_lcd(gb, gb_lcd_draw_line);
    ctx->initialized = true;
    return true;
}

void gbcore_run_frame(GBCore* ctx) {
    if (!ctx->initialized) return;
    struct gb_s* gb = (struct gb_s*)ctx->coreState;
    gb_run_frame(gb);
}

void gbcore_set_button(GBCore* ctx, int button, bool pressed) {
    if (!ctx->initialized) return;
    struct gb_s* gb = (struct gb_s*)ctx->coreState;
    gb->direct.joypad_bits ^= (pressed ? 0 : 1) << button;
}
```

4. In `emulator_app.cpp`, call `gbcore_run_frame(&m_gbcore)` inside the lv_timer callback
5. The 160×144 framebuffer is already in PSRAM and bound to an `lv_canvas`

### Button Mapping for GB

```cpp
// In EmulatorApp::onKey():
static const int GB_BTN_MAP[ION_KEY__MAX] = {
    -1,   // NONE
    6,    // UP    → GB Up
    7,    // DOWN  → GB Down
    5,    // LEFT  → GB Left
    4,    // RIGHT → GB Right
    0,    // A     → GB A
    1,    // B     → GB B
    2,    // X     → GB Select
    3,    // START → GB Start
    -1,   // MENU  → system
};
if (m_running && GB_BTN_MAP[k] >= 0)
    gbcore_set_button(&m_gbcore, GB_BTN_MAP[k], pressed);
```

---

## Adding OpenAI to Chatbot

Replace the placeholder key in `chatbot_app.cpp`:

```cpp
esp_http_client_set_header(c, "Authorization", "Bearer sk-YOUR_OPENAI_KEY");
```

For production use, store the key in NVS rather than hardcoding:

```cpp
// Store key
nvs_handle_t h;
nvs_open("ionos_ai", NVS_READWRITE, &h);
nvs_set_str(h, "openai_key", "sk-...");
nvs_commit(h); nvs_close(h);

// Read key
char key[60] = {};  size_t len = sizeof(key);
nvs_open("ionos_ai", NVS_READONLY, &h);
nvs_get_str(h, "openai_key", key, &len);
nvs_close(h);
esp_http_client_set_header(c, "Authorization", (std::string("Bearer ")+key).c_str());
```

---

## OTA Updates (Future)

The partition table includes `ota_0` and `ota_1` (4 MB each). To enable OTA:

```cpp
#include "esp_ota_ops.h"
#include "esp_https_ota.h"

esp_https_ota_config_t cfg = {
    .ota_config = {
        .url = "https://your-server.com/ionos.bin",
    }
};
esp_err_t r = esp_https_ota(&cfg.ota_config);
if (r == ESP_OK) esp_restart();
```

---

## Memory Optimization Checklist

- [ ] All image/audio data in PSRAM (`MALLOC_CAP_SPIRAM`)
- [ ] LVGL draw buffers in PSRAM
- [ ] ROM data (`m_romData`) in PSRAM
- [ ] HTTP buffers in PSRAM
- [ ] Internal SRAM reserved for: FreeRTOS stacks, I2S DMA descriptors, WiFi buffers
- [ ] No `std::string` for large data — use PSRAM-backed `std::vector<char>`
- [ ] LVGL objects deleted in `onDestroy()` — check with `lv_mem_monitor()`
- [ ] Audio chunks processed in-place, not double-buffered unnecessarily

---

## Debug Logging

```bash
# Enable verbose logging for a specific component
idf.py menuconfig
→ Component config → Log output → Default log verbosity → Debug

# Or at runtime:
esp_log_level_set("ST7789", ESP_LOG_DEBUG);
esp_log_level_set("AudioDrv", ESP_LOG_VERBOSE);
esp_log_level_set("*", ESP_LOG_WARN);  // Suppress everything except warnings
```

### Key Log Tags

| Tag | Component |
|-----|-----------|
| `Kernel` | IonKernel boot + event bus |
| `ST7789` | Display driver |
| `AudioDrv` | I2S audio |
| `Buttons` | Input debounce |
| `WS2812` | LED animations |
| `SD` | SD card mount |
| `WiFiDrv` | WiFi connection |
| `WiFiMgr` | WiFi service |
| `AppMgr` | App launches |
| `Resources` | Asset loader |
| `Themes` | Theme changes |
| `GBCore` | Emulator |
| `Browser` | HTTP requests |
| `Chatbot` | AI responses |

---

## Release Checklist

Before tagging a release:

- [ ] `python3 tools/gen_assets.py` — regenerate all assets
- [ ] `idf.py build` passes with zero warnings
- [ ] Binary size < 3.8 MB (leaves room in factory partition)
- [ ] Test all 6 apps launch and close cleanly
- [ ] Test WiFi connect/disconnect/reconnect
- [ ] Test SD card mount/unmount
- [ ] Test deep sleep wake (MENU button)
- [ ] Check heap after 10 minutes of use (no leak)
- [ ] `tools/erase_nvs.sh` + flash + verify first-boot sequence

---

## App Installer Architecture

### Install Pipeline (step by step)

```
AppInstaller::install(path)
  │
  ├─ 1. validate()            Read 256-byte header, check magic "IONAPP\0\0",
  │                           verify header CRC32 (bytes 12-255)
  │
  ├─ 2. isInstalled()?        If yes and same/older version → ERR_ALREADY_INSTALLED
  │                           If yes and newer version → uninstall first
  │
  ├─ 3. freeSpace()           Check SD has enough room (code + icon + manifest)
  │
  ├─ 4. ensureDir()           Create /sdcard/installed/<AppName>/
  │                           Create /sdcard/installed/<AppName>/data/
  │
  ├─ 5. extractIcon()         Read 2048 bytes (32×32 RGB565) → icon.bin
  │
  ├─ 6. extractCode()         Stream code in 4 KB chunks from SD → app.bin
  │                           Compute running CRC32 during streaming
  │                           Verify against header's code_crc32 field
  │
  ├─ 7. writeManifestJson()   Write cJSON manifest.json from header fields
  │
  ├─ 8. scanInstalled()       Refresh in-memory installed app list
  │
  └─ 9. loadAllIntoAppManager()  Register with AppManager → appears on home screen
```

### Package Binary Layout

```
Offset   Size     Content
──────── ──────── ────────────────────────────────────
0        8        Magic: "IONAPP\0\0"
8        1        Format version (currently 1)
9        3        Reserved (zero)
12       4        Header CRC32 (of bytes 12-255, little-endian)
16       64       App ID: "com.example.myapp\0..."
80       24       App name (null-terminated)
104      12       Version string "1.0.0\0..."
116      32       Author name
148      80       Description
228      4        Accent colour (RGB888, big-endian)
232      4        Code size in bytes (little-endian)
236      4        Code CRC32 (little-endian)
240      1        Permissions bitmask
241      15       Padding (zero)
───────────────────────────────────────────────────────
256      2048     Icon: 32×32 RGB565, byte-swapped for SPI
───────────────────────────────────────────────────────
2304     N        App binary (code_size bytes)
```

### SD Card Directory Structure

```
/sdcard/
├── apps/               ← Drop .ionapp files here for installation
│   ├── HelloIonOS.ionapp
│   └── GamePad.ionapp
└── installed/          ← Managed by AppInstaller (do not edit manually)
    ├── Helloion/
    │   ├── manifest.json   ← Extracted metadata
    │   ├── icon.bin        ← 32×32 RGB565 icon
    │   ├── app.bin         ← App binary
    │   └── data/           ← App private data directory (preserved on update)
    └── Gamepad/
        ├── manifest.json
        ├── icon.bin
        └── data/
```

### Adding SD App Support to AppManager

`registerSDApp()` creates an `SDApp` IonApp wrapper that:
1. Shows a placeholder screen (until full dynamic loading is implemented in v1.1)
2. Is registered in the main app grid just like built-in apps
3. Uses the app's `accent_color` from its manifest for the icon glow effect

### Future: Dynamic Loading (IonOS v1.1 roadmap)

IonOS v1.1 will add true dynamic loading via position-independent code (PIC):

```
1. Build external app as ESP32-S3 PIC binary:
   xtensa-esp32s3-elf-gcc -fPIC -shared -o app.so main.c

2. Load at runtime:
   void* handle = dlopen("/sdcard/installed/MyApp/app.bin", RTLD_LAZY);
   IonApp* (*create)() = dlsym(handle, "app_create");
   IonApp* app = create();
   AppManager::getInstance().launchLoadedApp(app);
```

The `extern "C" IonApp* app_create()` entry point is already defined in the
example scaffold (`tools/create_ionapp.py --example`).
