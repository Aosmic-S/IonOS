# IonOS App Development Guide

Build your own apps — either compiled into the firmware or installable from SD card.

---

## Quick Start: Built-in App (compiled into firmware)

### 1. Create your app files

```
main/apps/myapp/
├── myapp.h
└── myapp.cpp
```

**myapp.h**
```cpp
#pragma once
#include "apps/app_manager.h"

class MyApp : public IonApp {
public:
    void onCreate()  override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;
private:
    lv_timer_t* m_timer = nullptr;
};
```

**myapp.cpp**
```cpp
#include "myapp.h"
#include "sdk/include/ion_sdk.h"

void MyApp::onCreate() {
    buildScreen("My App");           // Dark themed screen with title bar

    lv_obj_t* lbl = lv_label_create(m_screen);
    lv_label_set_text(lbl, "Hello IonOS!");
    UIEngine::styleLabel(lbl, ION_COLOR_ACCENT, &lv_font_montserrat_20);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    // Update label every second
    m_timer = lv_timer_create([](lv_timer_t* t) {
        // runs on UI core — no mutex needed
        static int n = 0;
        char buf[32]; snprintf(buf, sizeof(buf), "Tick %d", n++);
        lv_label_set_text((lv_obj_t*)t->user_data, buf);
    }, 1000, lbl);
}

void MyApp::onKey(ion_key_t k, bool pressed) {
    if (pressed && k == ION_KEY_B)
        AppManager::getInstance().closeCurrentApp();
}

void MyApp::onDestroy() {
    if (m_timer)  { lv_timer_del(m_timer);  m_timer  = nullptr; }
    if (m_screen) { lv_obj_del(m_screen);   m_screen = nullptr; }
}
```

### 2. Register in AppManager

In `main/apps/app_manager.cpp`, inside `AppManager::init()`:

```cpp
#include "myapp/myapp.h"
// ...
registerApp("My App", ION_ICON_STAR, 0xFF6B35,
    []()->IonApp*{ return new MyApp(); });
```

### 3. Add to CMakeLists.txt

In `main/CMakeLists.txt`, add to SRCS:
```cmake
"apps/myapp/myapp.cpp"
```

### 4. Add icon (optional)

In `tools/gen_assets.py`, add to the ICONS list:
```python
def icon_myapp():
    img, d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([8,8,24,24], outline=ACCENT, width=2)
    return img

ICONS = [
    ...
    ("myapp", icon_myapp),   # → ION_ICON_MYAPP
]
```
Then regenerate: `python3 tools/gen_assets.py`

---

## SD Card App (installable .ionapp package)

### 1. Write your app

Use the example scaffold:
```bash
python3 tools/create_ionapp.py --example --output my_project
```

This creates `my_project/main.cpp` with a full example using the IonOS SDK.

The entry point must be:
```cpp
extern "C" IonApp* app_create() { return new MyApp(); }
```

### 2. Create manifest.json

```json
{
  "id":         "com.yourname.myapp",
  "name":       "My App",
  "version":    "1.0.0",
  "author":     "Your Name",
  "desc":       "What your app does.",
  "icon_color": 16744192,
  "min_ionos":  "1.0.0",
  "perms":      ["audio", "led"]
}
```

### 3. Build & package

```bash
# Compile (requires IonOS SDK headers on your build path)
idf.py build
# or your custom Makefile/CMake

# Package into .ionapp
python3 tools/create_ionapp.py \
    --manifest my_project/manifest.json \
    --code build/my_app.bin \
    --icon  my_project/icon.png \
    --output MyApp.ionapp

# Inspect the package
python3 tools/create_ionapp.py --inspect MyApp.ionapp
```

### 4. Install on device

1. Copy `MyApp.ionapp` to `/sdcard/apps/`
2. Open **App Store** on the device
3. Go to **Available** tab → tap the package → **Install App**
4. App appears on home screen immediately

### 5. Update

Drop a newer version (higher `version` in manifest) into `/sdcard/apps/`.
App Store shows an amber **Update** badge. Tap → **Update App**.

---

## IonApp Lifecycle

```
onCreate()   ← app launched: build LVGL screen, subscribe events
onResume()   ← returned to foreground (another app closed)
onPause()    ← going to background (another app opened)
onDestroy()  ← app closed: MUST free all LVGL objects and unsubscribe events
onKey(k, pressed) ← button event forwarded by AppManager
```

**Always** delete LVGL objects in `onDestroy()`:
```cpp
void MyApp::onDestroy() {
    IonKernel::getInstance().unsubscribeEvent(m_eventSubId);
    if (m_timer)  { lv_timer_del(m_timer);  m_timer  = nullptr; }
    if (m_screen) { lv_obj_del(m_screen);   m_screen = nullptr; }
}
```

---

## LVGL Thread Safety

LVGL runs on **Core 1**. Any LVGL call from a background task (Core 0) MUST be wrapped:

```cpp
// In a FreeRTOS task / callback on Core 0:
if (UIEngine::getInstance().lock(100)) {   // 100ms timeout
    lv_label_set_text(myLabel, "updated");
    UIEngine::getInstance().unlock();
}
```

LVGL timer callbacks (`lv_timer_create`) run on the UI core automatically — **no locking needed** inside them.

---

## Background Tasks

Never block the UI core. For HTTP, file I/O, heavy processing:

```cpp
void MyApp::onCreate() {
    buildScreen("Loading…");
    m_label = lv_label_create(m_screen);
    lv_label_set_text(m_label, "Fetching data…");
    lv_obj_align(m_label, LV_ALIGN_CENTER, 0, 0);

    xTaskCreate([](void* arg) {
        MyApp* self = (MyApp*)arg;
        std::string result = doHeavyWork();   // blocking OK here

        if (UIEngine::getInstance().lock(200)) {
            lv_label_set_text(self->m_label, result.c_str());
            UIEngine::getInstance().unlock();
        }
        vTaskDelete(nullptr);
    }, "my_bg", 8192, this, 4, nullptr);
}
```

---

## Button Map

| IonOS Key | Role | LVGL | Emulator |
|-----------|------|------|---------|
| `ION_KEY_X` | **Confirm / Select** ★ | LV_KEY_ENTER | GB A |
| `ION_KEY_B` | Back / Cancel | LV_KEY_ESC | Exit emu |
| `ION_KEY_A` | Context / Secondary | — | GB B |
| `ION_KEY_UP` | Navigate up | LV_KEY_UP | GB Up |
| `ION_KEY_DOWN` | Navigate down | LV_KEY_DOWN | GB Down |
| `ION_KEY_LEFT` | Navigate left | LV_KEY_LEFT | GB Left |
| `ION_KEY_RIGHT` | Navigate right | LV_KEY_RIGHT | GB Right |
| `ION_KEY_START` | Pause / secondary action | — | GB Start |
| `ION_KEY_MENU` | System menu | LV_KEY_HOME | GB Select |
| `ION_KEY_MENU` (long) | Power off | — | — |

---

## SDK Quick Reference

```cpp
#include "sdk/include/ion_sdk.h"

// Notification
ION_NOTIFY("Title", "Message", ION_NOTIF_SUCCESS);

// Sound
ION_SOUND("click");   // "click" "notification" "error" "boot" "success"

// Icon resource
const lv_img_dsc_t* img = ION_ICON(RICON_SETTINGS);

// LVGL mutex macros
ION_LOCK();   // UIEngine::getInstance().lock(100)
ION_UNLOCK(); // UIEngine::getInstance().unlock()

// Style helpers
UIEngine::stylePanel(lv_obj_t* obj);
UIEngine::styleBtn(lv_obj_t* btn, 0x00D4FF);
UIEngine::styleLabel(lv_obj_t* lbl, 0xEEF2FF, &lv_font_montserrat_14);

// Colour constants
ION_COLOR_BG        // 0x0A0E1A
ION_COLOR_SURFACE   // 0x131929
ION_COLOR_ACCENT    // 0x00D4FF  cyan
ION_COLOR_TEXT      // 0xEEF2FF
ION_COLOR_DIM       // 0x8899BB
ION_COLOR_SUCCESS   // 0x00FF9F
ION_COLOR_WARNING   // 0xFFB800
ION_COLOR_ERROR     // 0xFF3366
```
