# Contributing to IonOS

Thank you for your interest in contributing!

## Getting Started

1. Fork the repository and clone it locally
2. Install ESP-IDF v5.1+: `source ~/esp/esp-idf/export.sh`
3. Install Python deps: `pip install Pillow numpy`
4. Generate assets: `python3 tools/gen_assets.py`
5. Build: `idf.py set-target esp32s3 && idf.py build`

## Code Style

- C++17, ESP-IDF idioms throughout
- Singleton pattern for all drivers and services (`getInstance()`)
- All PSRAM allocations via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`
- All LVGL calls from non-UI tasks must hold `UIEngine::getInstance().lock()`
- Log tags match class name: `static const char* TAG = "ClassName";`
- No `new` in interrupt context; no blocking calls in LVGL callbacks

## Branches

| Branch | Purpose |
|--------|---------|
| `main` | Stable releases |
| `develop` | Integration branch |
| `feature/*` | New features |
| `fix/*` | Bug fixes |

## Adding a New App

1. Create `main/apps/myapp/myapp.h` and `myapp.cpp`
2. Inherit from `IonApp`, implement `onCreate/onDestroy/onKey`
3. Register in `AppManager::init()` with icon, colour, factory lambda
4. Add `.cpp` to `main/CMakeLists.txt` SRCS
5. Add icon in `tools/gen_assets.py` if needed

## Adding a New Driver

1. Create `main/drivers/mydevice/mydevice_driver.h/.cpp`
2. Implement singleton with `init()` / task if needed
3. Call `init()` from `IonKernel::phaseHardware()`
4. Add to `main/CMakeLists.txt`

## Pull Request Checklist

- [ ] `python3 tools/gen_assets.py` run (regenerates assets)
- [ ] `idf.py build` passes with zero warnings
- [ ] All 6 built-in apps launch and close cleanly
- [ ] No PSRAM/heap leak after 10 minutes of use
- [ ] `CHANGELOG.md` updated under `[Unreleased]`

## Reporting Issues

Please include:
- IonOS version / commit hash
- ESP-IDF version (`idf.py --version`)
- Serial monitor log showing the error
- Steps to reproduce
