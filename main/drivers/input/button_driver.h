#pragma once
#include "config/pin_config.h"
#include "config/ion_config.h"
#include "lvgl/lvgl.h"
#include "esp_err.h"

class ButtonDriver {
public:
    static ButtonDriver& getInstance();
    esp_err_t init();
    void pollTask();           // 100Hz FreeRTOS task
    bool isPressed(ion_key_t k) const;
    void lvglIndevCb(lv_indev_drv_t* drv, lv_indev_data_t* data);
    lv_indev_t* getIndev() const { return m_indev; }

private:
    ButtonDriver() = default;
    static const int PIN[ION_KEY__MAX];
    uint32_t  m_state     = 0;   // Bitmask: bit N = key N pressed
    uint32_t  m_lastState = 0;
    uint32_t  m_pressTime[ION_KEY__MAX] = {};
    bool      m_longFired[ION_KEY__MAX] = {};
    lv_indev_t* m_indev = nullptr;
    ion_key_t   m_lastLvglKey = ION_KEY_NONE;
    bool        m_lastLvglState = false;
};
