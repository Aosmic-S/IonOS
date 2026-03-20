#pragma once
#include "config/pin_config.h"
#include "led_strip.h"
#include "esp_err.h"
#include <stdint.h>

struct RGBColor { uint8_t r,g,b;
    static RGBColor BLACK()  { return {0,0,0}; }
    static RGBColor WHITE()  { return {255,255,255}; }
    static RGBColor CYAN()   { return {0,212,255}; }
    static RGBColor GREEN()  { return {0,255,159}; }
    static RGBColor RED()    { return {255,51,102}; }
    static RGBColor AMBER()  { return {255,184,0}; }
    static RGBColor PURPLE() { return {123,47,255}; }
};

enum class LEDAnim { NONE, RAINBOW, PULSE, WIFI_BLINK, BATT_LOW, CHARGING, MUSIC_BEAT };

class WS2812Driver {
public:
    static WS2812Driver& getInstance();
    esp_err_t init();
    void setPixel(int idx, RGBColor c);
    void setAll(RGBColor c);
    void clear();
    void show();
    void setBrightness(uint8_t b) { m_brightness = b; }
    void setAnimation(LEDAnim anim);
    void notificationFlash(RGBColor c, int times);
    void bootSweep();
    void animTask();   // FreeRTOS task

private:
    WS2812Driver() = default;
    static RGBColor hsv(float h, float s, float v);
    led_strip_handle_t m_strip  = nullptr;
    LEDAnim  m_anim      = LEDAnim::NONE;
    uint8_t  m_brightness= 80;
    uint32_t m_animStep  = 0;
};
