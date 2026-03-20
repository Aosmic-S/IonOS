#include "settings_app.h"
#include "ui/ui_engine.h"
#include "ui/notification_popup.h"
#include "services/wifi_manager.h"
#include "services/audio_manager.h"
#include "drivers/display/st7789_driver.h"
#include "kernel/memory_manager.h"
#include "themes/ion_themes.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char* TAG = "SettingsApp";
void SettingsApp::onCreate() {
    buildScreen("Settings");
    m_tabs = lv_tabview_create(m_screen, LV_DIR_TOP, 28);
    lv_obj_set_pos(m_tabs, 0, 44); lv_obj_set_size(m_tabs, 320, 276);
    lv_obj_set_style_bg_color(m_tabs, lv_color_hex(0x0A0E1A), 0);
    lv_obj_set_style_text_font(lv_tabview_get_tab_btns(m_tabs), &lv_font_montserrat_12, 0);

    buildWifiTab(lv_tabview_add_tab(m_tabs, LV_SYMBOL_WIFI " WiFi"));
    buildDisplayTab(lv_tabview_add_tab(m_tabs, LV_SYMBOL_IMAGE " Display"));
    buildAudioTab(lv_tabview_add_tab(m_tabs, LV_SYMBOL_AUDIO " Audio"));
    buildThemeTab(lv_tabview_add_tab(m_tabs, LV_SYMBOL_EDIT " Theme"));
    buildInfoTab(lv_tabview_add_tab(m_tabs, LV_SYMBOL_LIST " Info"));
}
void SettingsApp::buildWifiTab(lv_obj_t* p) {
    bool ok = WiFiManager::getInstance().isConnected();
    lv_obj_t* status = lv_label_create(p);
    char buf[64]; snprintf(buf,sizeof(buf),"%s %s", ok?LV_SYMBOL_WIFI:"No WiFi",
        ok?WiFiManager::getInstance().getIP().c_str():"Not connected");
    lv_label_set_text(status, buf);
    lv_obj_set_style_text_color(status, lv_color_hex(ok?0x00D4FF:0xFF3366),0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_t* scanBtn = lv_btn_create(p);
    lv_obj_set_size(scanBtn, 200, 32); lv_obj_set_pos(scanBtn, 0, 22);
    lv_obj_set_style_bg_color(scanBtn, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_radius(scanBtn, 8, 0);
    lv_obj_t* sl = lv_label_create(scanBtn);
    lv_label_set_text(sl, LV_SYMBOL_REFRESH " Scan Networks");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
    lv_obj_center(sl);
    lv_obj_add_event_cb(scanBtn, [](lv_event_t*){
        SettingsApp* self = (SettingsApp*)AppManager::getInstance().getCurrentApp();
        if (self) self->runWifiScan();
    }, LV_EVENT_CLICKED, nullptr);
    m_scanList = lv_list_create(p);
    lv_obj_set_size(m_scanList, 228, 160); lv_obj_set_pos(m_scanList, 0, 60);
    lv_obj_set_style_bg_color(m_scanList, lv_color_hex(0x0A0E1A), 0);
    lv_obj_set_style_border_width(m_scanList, 0, 0);
}
void SettingsApp::runWifiScan() {
    NotificationPopup::getInstance().show("WiFi","Scanning...",ION_NOTIF_INFO,2000);
    xTaskCreate([](void* arg){
        WiFiManager::getInstance().startScan();
        SettingsApp* self = (SettingsApp*)arg;
        if (UIEngine::getInstance().lock(200)) {
            lv_obj_clean(self->m_scanList);
            for (auto& net : WiFiManager::getInstance().getResults()) {
                lv_obj_t* btn = lv_list_add_btn(self->m_scanList, LV_SYMBOL_WIFI, net.ssid.c_str());
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x131929), 0);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x00D4FF), LV_STATE_FOCUSED);
                lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
            }
            UIEngine::getInstance().unlock();
        }
        vTaskDelete(nullptr);
    }, "wifi_scan", 4096, this, 3, nullptr);
}
void SettingsApp::buildDisplayTab(lv_obj_t* p) {
    lv_obj_t* l = lv_label_create(p);
    lv_label_set_text(l, "Brightness");
    lv_obj_set_style_text_color(l, lv_color_hex(0xEEF2FF), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_t* sl = lv_slider_create(p);
    lv_obj_set_width(sl, 200); lv_obj_set_pos(sl, 0, 22);
    lv_slider_set_range(sl, 10, 100); lv_slider_set_value(sl, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x00D4FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x00D4FF), LV_PART_KNOB);
    lv_obj_add_event_cb(sl,[](lv_event_t* e){
        int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
        ST7789Driver::getInstance().setBacklight(v);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
}
void SettingsApp::buildAudioTab(lv_obj_t* p) {
    lv_obj_t* l = lv_label_create(p);
    lv_label_set_text(l, "Volume");
    lv_obj_set_style_text_color(l, lv_color_hex(0xEEF2FF), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_t* sl = lv_slider_create(p);
    lv_obj_set_width(sl, 200); lv_obj_set_pos(sl, 0, 22);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, AudioManager::getInstance().getVolume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x00FF9F), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x00FF9F), LV_PART_KNOB);
    lv_obj_add_event_cb(sl,[](lv_event_t* e){
        AudioManager::getInstance().setVolume(lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
}
void SettingsApp::buildThemeTab(lv_obj_t* p) {
    lv_obj_t* rl = lv_roller_create(p);
    lv_roller_set_options(rl, "Dark Pro\nNeon Gaming\nRetro Console", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(rl, (uint16_t)ion_theme_current(), LV_ANIM_OFF);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(rl, lv_color_hex(0x131929), 0);
    lv_obj_set_style_text_color(rl, lv_color_hex(0xEEF2FF), 0);
    lv_obj_set_style_text_color(rl, lv_color_hex(0x00D4FF), LV_PART_SELECTED);
    lv_obj_add_event_cb(rl,[](lv_event_t* e){
        uint16_t sel = lv_roller_get_selected((lv_obj_t*)lv_event_get_target(e));
        ion_theme_apply((ion_theme_id_t)sel);
        ion_theme_save((ion_theme_id_t)sel);
        NotificationPopup::getInstance().show("Theme","Theme applied!",ION_NOTIF_SUCCESS,2000);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
}
void SettingsApp::buildInfoTab(lv_obj_t* p) {
    esp_chip_info_t ci; esp_chip_info(&ci);
    auto ms = MemoryManager::getInstance().snapshot();
    char info[300];
    snprintf(info, sizeof(info),
        "IonOS v1.0.0\n"
        "ESP-IDF: %s\n"
        "Chip: ESP32-S3 rev%d\n"
        "Cores: %d @ 240MHz\n"
        "Heap: %zuKB free\n"
        "PSRAM: %zuKB free\n"
        "Flash: 16MB",
        esp_get_idf_version(), ci.revision, ci.cores,
        ms.heapFree/1024, ms.psramFree/1024);
    lv_obj_t* l = lv_label_create(p);
    lv_label_set_text(l, info);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8899BB), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 220);
}
void SettingsApp::onKey(ion_key_t k, bool pressed) {
    if (!pressed) return;
    if (k==ION_KEY_B) AppManager::getInstance().closeCurrentApp();
}
void SettingsApp::onDestroy() { if(m_screen){lv_obj_del(m_screen);m_screen=nullptr;} }