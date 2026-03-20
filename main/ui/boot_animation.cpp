#include "boot_animation.h"
#include "ui_engine.h"
#include "resources/resource_loader.h"
#include "apps/app_manager.h"
#include "esp_log.h"
static const char* TAG = "BootAnim";
BootAnimation& BootAnimation::getInstance(){ static BootAnimation i; return i; }

void BootAnimation::start() {
    if (!UIEngine::getInstance().lock(200)) return;
    m_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x0A0E1A), 0);
    lv_scr_load(m_screen);

    m_canvas = lv_img_create(m_screen);
    lv_obj_center(m_canvas);

    auto& rl = ResourceLoader::getInstance();
    lv_img_set_src(m_canvas, rl.bootFrame(0));
    // Scale 120x80 → 240x160 (centered on 240x320)
    lv_img_set_zoom(m_canvas, 512); // 2x
    lv_obj_align(m_canvas, LV_ALIGN_CENTER, 0, 0);

    m_frame = 0; m_done = false;
    m_timer = lv_timer_create(timerCb, ION_BOOT_FRAME_MS, this);
    UIEngine::getInstance().unlock();
    ESP_LOGI(TAG, "Boot animation started (%d frames)", rl.bootFrameCount());
}

void BootAnimation::timerCb(lv_timer_t* t) {
    BootAnimation* self = (BootAnimation*)t->user_data;
    auto& rl = ResourceLoader::getInstance();
    self->m_frame++;
    if (self->m_frame >= rl.bootFrameCount()) {
        lv_timer_del(t); self->m_timer = nullptr;
        self->m_done = true;
        // Transition to homescreen
        AppManager::getInstance().showHome();
        return;
    }
    lv_img_set_src(self->m_canvas, rl.bootFrame(self->m_frame));
}

void BootAnimation::stop() {
    if (m_timer) { lv_timer_del(m_timer); m_timer = nullptr; }
    m_done = true;
}