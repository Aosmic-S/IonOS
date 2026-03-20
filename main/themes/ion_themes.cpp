#include "ion_themes.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "Themes";
static ion_theme_id_t s_current = ION_THEME_DARK_PRO;

const ion_theme_t ion_themes[ION_THEME_COUNT] = {
    {   // Dark Pro — Default IonOS dark cyberpunk
        .name    = "Dark Pro",
        .bg      = 0x0A0E1A, .surface  = 0x131929,
        .accent  = 0x00D4FF, .accent2  = 0x7B2FFF,
        .text    = 0xEEF2FF, .text_dim = 0x8899BB,
        .border  = 0x1E2D4A,
        .success = 0x00FF9F, .warning  = 0xFFB800, .error = 0xFF3366,
        .dark    = true,
    },
    {   // Neon Gaming — High-contrast green-on-black arcade
        .name    = "Neon Gaming",
        .bg      = 0x050505, .surface  = 0x0F0F0F,
        .accent  = 0x39FF14, .accent2  = 0xFF2079,
        .text    = 0xEEFFEE, .text_dim = 0x558855,
        .border  = 0x1A3320,
        .success = 0x00FF44, .warning  = 0xFFCC00, .error = 0xFF0040,
        .dark    = true,
    },
    {   // Retro Console — Warm amber tones, classic gaming feel
        .name    = "Retro Console",
        .bg      = 0x101820, .surface  = 0x1C2A38,
        .accent  = 0xFFCC00, .accent2  = 0xFF6B35,
        .text    = 0xFFF8DC, .text_dim = 0xB8A870,
        .border  = 0x2E4055,
        .success = 0x88CC00, .warning  = 0xFF9900, .error = 0xFF3300,
        .dark    = true,
    },
};

void ion_theme_apply(ion_theme_id_t id) {
    if (id >= ION_THEME_COUNT) id = ION_THEME_DARK_PRO;
    s_current = id;
    const ion_theme_t* t = &ion_themes[id];
    ESP_LOGI(TAG, "Applying theme: %s", t->name);

    lv_theme_t* th = lv_theme_default_init(
        lv_disp_get_default(),
        lv_color_hex(t->accent),
        lv_color_hex(t->accent2),
        t->dark,
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(lv_disp_get_default(), th);
}

ion_theme_id_t ion_theme_load() {
    nvs_handle_t h;
    uint8_t val = 0;
    if (nvs_open("ionos_ui", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "theme", &val);
        nvs_close(h);
    }
    return (ion_theme_id_t)(val < ION_THEME_COUNT ? val : 0);
}

void ion_theme_save(ion_theme_id_t id) {
    nvs_handle_t h;
    if (nvs_open("ionos_ui", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "theme", (uint8_t)id);
        nvs_commit(h); nvs_close(h);
    }
}

const ion_theme_t* ion_theme_get(ion_theme_id_t id) {
    return &ion_themes[id < ION_THEME_COUNT ? id : 0];
}

ion_theme_id_t ion_theme_current() { return s_current; }
