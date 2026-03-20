#if 1  // Set to 0 to disable (required by LVGL build system)
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>
#include "config/pin_config.h"

// ── Color depth ───────────────────────────────────────────────────
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP         1   // Byte-swap for SPI display

// ── Memory ────────────────────────────────────────────────────────
#define LV_MEM_CUSTOM            1
#define LV_MEM_CUSTOM_INCLUDE   "esp_heap_caps.h"
// Use internal SRAM for LVGL objects (faster, saves 2MB PSRAM budget)
#define LV_MEM_CUSTOM_ALLOC(s)  heap_caps_malloc(s, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE       heap_caps_free
#define LV_MEM_CUSTOM_REALLOC    heap_caps_realloc

// ── Tick ──────────────────────────────────────────────────────────
#define LV_TICK_CUSTOM           1
#define LV_TICK_CUSTOM_INCLUDE  "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)(esp_timer_get_time() / 1000LL))

// ── Logging ───────────────────────────────────────────────────────
#define LV_USE_LOG               1
#define LV_LOG_LEVEL             LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF            0

// ── Display ───────────────────────────────────────────────────────
#define LV_HOR_RES_MAX           DISPLAY_WIDTH
#define LV_VER_RES_MAX           DISPLAY_HEIGHT

// ── Fonts ─────────────────────────────────────────────────────────
#define LV_FONT_MONTSERRAT_8     0
#define LV_FONT_MONTSERRAT_10    0
#define LV_FONT_MONTSERRAT_12    1
#define LV_FONT_MONTSERRAT_14    1
#define LV_FONT_MONTSERRAT_16    1
#define LV_FONT_MONTSERRAT_18    0
#define LV_FONT_MONTSERRAT_20    1
#define LV_FONT_MONTSERRAT_22    0
#define LV_FONT_MONTSERRAT_24    1
#define LV_FONT_MONTSERRAT_26    0
#define LV_FONT_MONTSERRAT_28    0
#define LV_FONT_DEFAULT          &lv_font_montserrat_14

// ── Widgets ───────────────────────────────────────────────────────
#define LV_USE_ARC               1
#define LV_USE_BAR               1
#define LV_USE_BTN               1
#define LV_USE_BTNMATRIX         1
#define LV_USE_CANVAS            1
#define LV_USE_CHECKBOX          1
#define LV_USE_DROPDOWN          1
#define LV_USE_IMG               1
#define LV_USE_LABEL             1
#define LV_USE_LINE              1
#define LV_USE_LIST              1
#define LV_USE_MENU              1
#define LV_USE_METER             1
#define LV_USE_MSGBOX            1
#define LV_USE_ROLLER            1
#define LV_USE_SLIDER            1
#define LV_USE_SPAN              1
#define LV_USE_SPINBOX           1
#define LV_USE_SPINNER           1
#define LV_USE_SWITCH            1
#define LV_USE_TABLE             1
#define LV_USE_TABVIEW           1
#define LV_USE_TEXTAREA          1
#define LV_USE_WIN               0
#define LV_USE_TILEVIEW          1

// ── Layouts ───────────────────────────────────────────────────────
#define LV_USE_FLEX              1
#define LV_USE_GRID              1

// ── Drawing extras ────────────────────────────────────────────────
#define LV_USE_GPU_ESP32S3_PPA   1   // ESP32-S3 hardware blend
#define LV_USE_PERF_MONITOR      0
#define LV_USE_MEM_MONITOR       0

// ── Animations ────────────────────────────────────────────────────
#define LV_USE_ANIMATION         1
#define LV_USE_SNAPSHOT          1

// ── File system ───────────────────────────────────────────────────
#define LV_USE_FS_POSIX          1
#define LV_FS_POSIX_LETTER       'S'  // S:/sdcard/...
#define LV_USE_IMG_PNG           0
#define LV_USE_IMG_BMP           0

// ── Themes ────────────────────────────────────────────────────────
#define LV_USE_THEME_DEFAULT     1
#define LV_USE_THEME_BASIC       1

#endif  // LV_CONF_H
#endif  // if 1

// QR code widget (needed by BruceApp)
#define LV_USE_QRCODE 1
