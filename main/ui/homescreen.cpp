#include "homescreen.h"
#include "ui_engine.h"
#include "apps/app_manager.h"
#include "kernel/kernel.h"
#include "resources/resource_loader.h"
#include "esp_log.h"
static const char* TAG = "HomeScreen";
HomeScreen& HomeScreen::getInstance(){ static HomeScreen i; return i; }

void HomeScreen::show() {
    if (!m_screen) {
        m_screen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x0A0E1A), 0);
        build(m_screen);
    }
    lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
    if (m_subId < 0)
        m_subId = IonKernel::getInstance().subscribeEvent(ION_EVENT_KEY_DOWN,
            [](const ion_event_t& e){ HomeScreen::getInstance().onKeyEvent(e); });
}

void HomeScreen::build(lv_obj_t* scr) {
    // Status bar area (top 20px reserved for StatusBar overlay)
    // App grid: 3 columns
    static lv_coord_t col_dsc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t row_dsc[] = { 75, 75, 75, LV_GRID_TEMPLATE_LAST };

    m_grid = lv_obj_create(scr);
    lv_obj_set_size(m_grid, 320, 225);
    lv_obj_set_pos(m_grid, 0, 48);
    lv_obj_set_style_bg_opa(m_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_grid, 0, 0);
    lv_obj_set_style_pad_all(m_grid, 4, 0);
    lv_obj_set_style_pad_gap(m_grid, 4, 0);
    lv_obj_set_layout(m_grid, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(m_grid, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(m_grid, row_dsc, 0);

    auto& am = AppManager::getInstance();
    int count = am.getAppCount();
    for (int i = 0; i < count && i < 9; i++) {
        const AppDef& def = am.getAppDef(i);

        lv_obj_t* cell = lv_obj_create(m_grid);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, i%3, 1, LV_GRID_ALIGN_STRETCH, i/3, 1);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x131929), 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(def.color), LV_STATE_FOCUSED);
        lv_obj_set_style_radius(cell, 12, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(def.color), 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_shadow_color(cell, lv_color_hex(def.color), LV_STATE_FOCUSED);
        lv_obj_set_style_shadow_width(cell, 16, LV_STATE_FOCUSED);
        lv_obj_set_style_pad_all(cell, 4, 0);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);

        // Icon
        lv_obj_t* ico = lv_img_create(cell);
        lv_img_set_src(ico, ResourceLoader::getInstance().icon(def.iconId));
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 4);

        // Label
        lv_obj_t* lbl = lv_label_create(cell);
        lv_label_set_text(lbl, def.name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEEF2FF), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl, 72);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -4);

        lv_obj_set_user_data(cell, (void*)(intptr_t)i);
        lv_obj_add_event_cb(cell, [](lv_event_t* e){
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            AppManager::getInstance().launchApp(idx);
        }, LV_EVENT_CLICKED, nullptr);
    }

    lv_group_t* g = lv_group_get_default();
    // Add all cells to focus group
    lv_obj_t* child = lv_obj_get_child(m_grid, 0);
    while(child) { lv_group_add_obj(g, child); child = lv_obj_get_next_sibling(child); }
    focusApp(0);
}

void HomeScreen::focusApp(int idx) {
    m_focusIdx = idx;
    lv_obj_t* child = lv_obj_get_child(m_grid, idx);
    if (child) lv_group_focus_obj(child);
}

void HomeScreen::onKeyEvent(const ion_event_t& e) {
    // D-pad navigation handled by LVGL group automatically
    (void)e;
}