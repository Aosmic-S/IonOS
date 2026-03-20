#include "statusbar.h"
#include "services/wifi_manager.h"
#include "services/power_manager.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
StatusBar& StatusBar::getInstance(){ static StatusBar i; return i; }
void StatusBar::init() {
    lv_obj_t* top = lv_layer_top();
    m_bar = lv_obj_create(top);
    lv_obj_set_size(m_bar, DISPLAY_WIDTH, 20);
    lv_obj_set_pos(m_bar, 0, 0);
    lv_obj_set_style_bg_color(m_bar, lv_color_hex(0x090D17), 0);
    lv_obj_set_style_border_width(m_bar, 0, 0);
    lv_obj_set_style_radius(m_bar, 0, 0);
    lv_obj_clear_flag(m_bar, LV_OBJ_FLAG_CLICKABLE);

    m_wifi = lv_label_create(m_bar);
    lv_label_set_text(m_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(m_wifi, &lv_font_montserrat_12, 0);
    lv_obj_align(m_wifi, LV_ALIGN_LEFT_MID, 4, 0);

    m_time = lv_label_create(m_bar);
    lv_label_set_text(m_time, "--:--");
    lv_obj_set_style_text_color(m_time, lv_color_hex(0xEEF2FF), 0);
    lv_obj_set_style_text_font(m_time, &lv_font_montserrat_12, 0);
    lv_obj_align(m_time, LV_ALIGN_CENTER, 0, 0);

    m_batt = lv_label_create(m_bar);
    lv_obj_set_style_text_font(m_batt, &lv_font_montserrat_12, 0);
    lv_obj_align(m_batt, LV_ALIGN_RIGHT_MID, -4, 0);

    m_timer = lv_timer_create([](lv_timer_t* t){ ((StatusBar*)t->user_data)->update(); }, 10000, this);
    update();
}
void StatusBar::update() {
    bool wok = WiFiManager::getInstance().isConnected();
    lv_obj_set_style_text_color(m_wifi, lv_color_hex(wok ? 0x00D4FF : 0x4A5568), 0);

    time_t now = time(nullptr); struct tm* tm = localtime(&now);
    char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm->tm_hour, tm->tm_min);
    lv_label_set_text(m_time, tbuf);

    int pct = PowerManager::getInstance().getBatteryPercent();
    bool chg = PowerManager::getInstance().isCharging();
    char bbuf[16]; snprintf(bbuf, sizeof(bbuf), "%s%d%%", chg ? LV_SYMBOL_CHARGE : "", pct);
    lv_label_set_text(m_batt, bbuf);
    uint32_t bcol = pct>50 ? 0x00FF9F : pct>20 ? 0xFFB800 : 0xFF3366;
    lv_obj_set_style_text_color(m_batt, lv_color_hex(bcol), 0);
}