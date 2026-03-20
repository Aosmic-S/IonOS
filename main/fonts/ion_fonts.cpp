#include "ion_fonts.h"
static ion_font_id_t s_active = ION_FONT_DEFAULT;
const lv_font_t* ion_font_get(ion_font_id_t id, uint8_t size) {
    // Fallback to built-in Montserrat until lv_font_conv generates custom fonts
    // Uncomment the font you need after generating with:
    //   lv_font_conv --font Orbitron-Regular.ttf --size 12 --bpp 4 --format lvgl \
    //                --range 0x20-0x7F --no-compress -o ion_font_orbitron_12.c
    if(size<=12) return &lv_font_montserrat_12;
    if(size<=14) return &lv_font_montserrat_14;
    if(size<=16) return &lv_font_montserrat_16;
    if(size<=20) return &lv_font_montserrat_20;
    return &lv_font_montserrat_24;
}
void ion_font_set_active(ion_font_id_t id) { s_active=id; }
const lv_font_t* ion_font_active(uint8_t size) { return ion_font_get(s_active,size); }