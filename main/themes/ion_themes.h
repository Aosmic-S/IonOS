#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║                 IonOS Theme Engine                               ║
// ║  3 themes: Dark Pro, Neon Gaming, Retro Console                  ║
// ║  Applied to LVGL at runtime. Saved to NVS.                       ║
// ╚══════════════════════════════════════════════════════════════════╝
#include "lvgl/lvgl.h"
#include <stdint.h>

typedef struct {
    const char* name;
    uint32_t    bg;
    uint32_t    surface;
    uint32_t    accent;
    uint32_t    accent2;
    uint32_t    text;
    uint32_t    text_dim;
    uint32_t    border;
    uint32_t    success;
    uint32_t    warning;
    uint32_t    error;
    bool        dark;
} ion_theme_t;

typedef enum {
    ION_THEME_DARK_PRO      = 0,
    ION_THEME_NEON_GAMING   = 1,
    ION_THEME_RETRO_CONSOLE = 2,
    ION_THEME_COUNT
} ion_theme_id_t;

extern const ion_theme_t ion_themes[ION_THEME_COUNT];

void           ion_theme_apply(ion_theme_id_t id);
ion_theme_id_t ion_theme_load();
void           ion_theme_save(ion_theme_id_t id);
const ion_theme_t* ion_theme_get(ion_theme_id_t id);
ion_theme_id_t ion_theme_current();
