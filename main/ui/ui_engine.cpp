#include "ui_engine.h"
#include "themes/ion_themes.h"
#include "drivers/display/st7789_driver.h"
#include "drivers/input/button_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char* TAG = "UI";
UIEngine& UIEngine::getInstance(){ static UIEngine i; return i; }

esp_err_t UIEngine::init() {
    lv_init();
    m_mutex = xSemaphoreCreateRecursiveMutex();
    m_disp  = ST7789Driver::getInstance().init(); // LVGL disp registered inside

    // LVGL tick via esp_timer
    static esp_timer_handle_t th;
    esp_timer_create_args_t ta = { .callback=[](void*){ lv_tick_inc(2); }, .name="lvgl_tick" };
    esp_timer_create(&ta, &th);
    esp_timer_start_periodic(th, 2000); // 2ms = 500Hz tick

    // Apply saved theme
    ion_theme_apply(ion_theme_load());

    m_keypadIndev = ButtonDriver::getInstance().getIndev();
    // Create default focus group
    lv_group_t* g = lv_group_create();
    lv_group_set_default(g);
    if (m_keypadIndev) lv_indev_set_group(m_keypadIndev, g);

    ESP_LOGI(TAG, "LVGL %d.%d.%d ready", lv_version_major(),lv_version_minor(),lv_version_patch());
    return ESP_OK;
}

void UIEngine::runLoop() {
    TickType_t lastWake = xTaskGetTickCount();
    while(true) {
        if (lock(5)) { lv_timer_handler(); unlock(); }
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(16)); // ~60fps
    }
}

bool UIEngine::lock(uint32_t ms) {
    return xSemaphoreTakeRecursive(m_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
void UIEngine::unlock() { xSemaphoreGiveRecursive(m_mutex); }

void UIEngine::stylePanel(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x131929), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x1E2D4A), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
}
void UIEngine::styleBtn(lv_obj_t* btn, uint32_t color) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(color), LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(btn, 12, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
}
void UIEngine::styleLabel(lv_obj_t* lbl, uint32_t color, const lv_font_t* font) {
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
}
