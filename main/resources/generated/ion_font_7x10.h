// IonOS 7×10 Bitmap Font — Generated
#pragma once
#include <stdint.h>

#define ION_FONT_7x10_W 7
#define ION_FONT_7x10_H 10
#define ION_FONT_FIRST_CHAR 32
#define ION_FONT_CHAR_COUNT 95

// font_data[char - 32][row] — 10 bytes per glyph
extern const uint8_t ion_font_7x10[ION_FONT_CHAR_COUNT][10];
// Get glyph pointer for char c (returns space if not found)
static inline const uint8_t* ion_font_glyph(char c) {
    int idx = (int)c - ION_FONT_FIRST_CHAR;
    if (idx < 0 || idx >= ION_FONT_CHAR_COUNT) idx = 0;
    return ion_font_7x10[idx];
}