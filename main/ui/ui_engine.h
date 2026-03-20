#pragma once
#include "lvgl/lvgl.h"
#include "freertos/semphr.h"
#include "esp_err.h"

class UIEngine {
public:
    static UIEngine& getInstance();
    esp_err_t init();
    void      runLoop();           // 60fps FreeRTOS task
    bool      lock(uint32_t ms=100);
    void      unlock();
    lv_indev_t* getKeypadIndev() const { return m_keypadIndev; }
    lv_disp_t*  getDisp()        const { return m_disp; }
    lv_obj_t*   getScreen()      const { return lv_scr_act(); }

    // Style helpers
    static void stylePanel(lv_obj_t* obj);
    static void styleBtn(lv_obj_t* btn, uint32_t accentColor);
    static void styleLabel(lv_obj_t* lbl, uint32_t color=0xEEF2FF, const lv_font_t* font=nullptr);

private:
    UIEngine() = default;
    SemaphoreHandle_t m_mutex       = nullptr;
    lv_indev_t*       m_keypadIndev = nullptr;
    lv_disp_t*        m_disp        = nullptr;
};
