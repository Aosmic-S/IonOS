#pragma once
#include "lvgl/lvgl.h"
typedef enum {
    ION_FONT_DEFAULT=0, ION_FONT_ORBITRON, ION_FONT_AUDIOWIDE, ION_FONT_RAJDHANI
} ion_font_id_t;
// After running lv_font_conv, declare generated fonts here:
// LV_FONT_DECLARE(ion_font_orbitron_12)
// LV_FONT_DECLARE(ion_font_orbitron_16)
const lv_font_t* ion_font_get(ion_font_id_t id, uint8_t size);
void             ion_font_set_active(ion_font_id_t id);
const lv_font_t* ion_font_active(uint8_t size);