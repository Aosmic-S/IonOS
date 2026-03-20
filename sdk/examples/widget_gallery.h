#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║  IonOS SDK Example — Widget Gallery                              ║
// ║  Demonstrates all major LVGL widgets styled for IonOS.           ║
// ╚══════════════════════════════════════════════════════════════════╝
#include "ion_sdk.h"

class WidgetGalleryApp : public IonApp {
public:
    void onCreate() override {
        buildScreen("Widgets");
        lv_obj_t* tabs = lv_tabview_create(m_screen, LV_DIR_TOP, 28);
        lv_obj_set_pos(tabs, 0, 44); lv_obj_set_size(tabs, 240, 276);
        lv_obj_set_style_bg_color(tabs, lv_color_hex(ION_COLOR_BG), 0);

        buildButtonsTab(lv_tabview_add_tab(tabs, "Buttons"));
        buildSlidersTab(lv_tabview_add_tab(tabs, "Sliders"));
        buildListTab(lv_tabview_add_tab(tabs, "List"));
    }
    void onDestroy() override { if(m_screen){lv_obj_del(m_screen);m_screen=nullptr;} }
    void onKey(ion_key_t k, bool pressed) override {
        if(!pressed && k==ION_KEY_B) AppManager::getInstance().closeCurrentApp();
    }
private:
    void buildButtonsTab(lv_obj_t* p) {
        lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(p, 6, 0);
        struct { const char* lbl; uint32_t col; } btns[] = {
            {"Primary",    ION_COLOR_ACCENT},
            {"Secondary",  ION_COLOR_ACCENT2},
            {"Success",    ION_COLOR_SUCCESS},
            {"Warning",    ION_COLOR_WARNING},
            {"Error",      ION_COLOR_ERROR},
        };
        for(auto& b : btns) {
            lv_obj_t* btn = lv_btn_create(p);
            lv_obj_set_width(btn, LV_PCT(100)); lv_obj_set_height(btn, 36);
            UIEngine::styleBtn(btn, b.col);
            lv_obj_t* l = lv_label_create(btn);
            lv_label_set_text(l, b.lbl);
            lv_obj_set_style_text_color(l, lv_color_hex(ION_COLOR_BG), 0);
            lv_obj_center(l);
        }
    }
    void buildSlidersTab(lv_obj_t* p) {
        for(auto col : {ION_COLOR_ACCENT, ION_COLOR_SUCCESS, ION_COLOR_WARNING}) {
            lv_obj_t* sl = lv_slider_create(p);
            lv_obj_set_width(sl, 200); lv_obj_set_height(sl, 8);
            lv_slider_set_value(sl, 60, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(sl, lv_color_hex(col), LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(sl, lv_color_hex(col), LV_PART_KNOB);
        }
    }
    void buildListTab(lv_obj_t* p) {
        lv_obj_t* lst = lv_list_create(p);
        lv_obj_set_size(lst, 228, 200);
        lv_obj_set_style_bg_color(lst, lv_color_hex(ION_COLOR_BG), 0);
        lv_obj_set_style_border_width(lst, 0, 0);
        const char* items[] = {"Item One","Item Two","Item Three","Item Four","Item Five"};
        for(auto item : items) {
            lv_obj_t* btn = lv_list_add_btn(lst, LV_SYMBOL_RIGHT, item);
            lv_obj_set_style_bg_color(btn, lv_color_hex(ION_COLOR_SURFACE), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(ION_COLOR_ACCENT), LV_STATE_FOCUSED);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        }
    }
};