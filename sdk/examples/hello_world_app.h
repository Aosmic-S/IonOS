#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║  IonOS SDK Example — Hello World App                             ║
// ║  Minimal app showing basic IonOS app development patterns.       ║
// ╚══════════════════════════════════════════════════════════════════╝
#include "ion_sdk.h"

class HelloWorldApp : public IonApp {
public:
    void onCreate() override {
        // 1. Build titled screen (inherited helper)
        buildScreen("Hello World");

        // 2. Add UI widgets using LVGL
        lv_obj_t* card = lv_obj_create(m_screen);
        lv_obj_set_size(card, 200, 120);
        lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
        UIEngine::stylePanel(card);

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_STAR);
        lv_obj_set_style_text_color(ico, lv_color_hex(ION_COLOR_ACCENT), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, "Hello from IonOS!\nPress A to notify");
        lv_obj_set_style_text_color(lbl, lv_color_hex(ION_COLOR_TEXT), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 8);

        // 3. Subscribe to events
        m_keySubId = IonKernel::getInstance().subscribeEvent(ION_EVENT_KEY_DOWN,
            [this](const ion_event_t& e) {
                if (e.data == ION_KEY_X) {
                    ION_NOTIFY("Hello App", "Button A pressed!", ION_NOTIF_SUCCESS);
                    ION_SOUND("notification");
                    WS2812Driver::getInstance().notificationFlash(RGBColor::CYAN(), 2);
                }
                if (e.data == ION_KEY_B) {
                    AppManager::getInstance().closeCurrentApp();
                }
            });
    }

    void onDestroy() override {
        // Always clean up
        IonKernel::getInstance().unsubscribeEvent(m_keySubId);
        if (m_screen) { lv_obj_del(m_screen); m_screen = nullptr; }
    }

private:
    int m_keySubId = -1;
};