#include "notification_popup.h"
#include "ui_engine.h"
#include "esp_log.h"
static const char* TAG = "Notif";
static const uint32_t LEVEL_COLORS[] = { 0x00D4FF, 0x00FF9F, 0xFFB800, 0xFF3366 };
static const char* LEVEL_ICONS[]  = { LV_SYMBOL_BELL, LV_SYMBOL_OK, LV_SYMBOL_WARNING, LV_SYMBOL_CLOSE };
NotificationPopup& NotificationPopup::getInstance(){ static NotificationPopup i; return i; }
void NotificationPopup::show(const char* title, const char* msg, ion_notif_level_t lv, uint32_t ms) {
    if (!UIEngine::getInstance().lock(50)) return;
    if (m_popup) { lv_obj_del(m_popup); m_popup = nullptr; }
    if (m_timer) { lv_timer_del(m_timer); m_timer = nullptr; }
    uint32_t color = LEVEL_COLORS[(int)lv];
    lv_obj_t* top = lv_layer_top();
    m_popup = lv_obj_create(top);
    lv_obj_set_size(m_popup, 224, 52);
    lv_obj_set_pos(m_popup, 8, -60);
    lv_obj_set_style_bg_color(m_popup, lv_color_hex(0x131929), 0);
    lv_obj_set_style_border_color(m_popup, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(m_popup, 1, 0);
    lv_obj_set_style_radius(m_popup, 8, 0);
    lv_obj_set_style_shadow_color(m_popup, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(m_popup, 12, 0);
    lv_obj_clear_flag(m_popup, LV_OBJ_FLAG_CLICKABLE);
    // Accent bar
    lv_obj_t* bar = lv_obj_create(m_popup);
    lv_obj_set_size(bar, 4, 44); lv_obj_set_pos(bar, 0, 4);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(bar, 0, 0); lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    // Icon
    lv_obj_t* ico = lv_label_create(m_popup);
    lv_label_set_text(ico, LEVEL_ICONS[(int)lv]);
    lv_obj_set_style_text_color(ico, lv_color_hex(color), 0);
    lv_obj_set_pos(ico, 12, 8);
    // Title
    lv_obj_t* ttl = lv_label_create(m_popup);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_color(ttl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(ttl, 28, 6);
    // Message
    lv_obj_t* mlbl = lv_label_create(m_popup);
    lv_label_set_text(mlbl, msg);
    lv_obj_set_style_text_color(mlbl, lv_color_hex(0xEEF2FF), 0);
    lv_obj_set_style_text_font(mlbl, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(mlbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(mlbl, 190); lv_obj_set_pos(mlbl, 28, 26);
    // Slide in
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, m_popup);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v){ lv_obj_set_y((lv_obj_t*)obj, v); });
    lv_anim_set_values(&a, -60, 24); lv_anim_set_time(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    // Auto-dismiss
    m_timer = lv_timer_create([](lv_timer_t* t){ ((NotificationPopup*)t->user_data)->dismiss(); }, ms, this);
    lv_timer_set_repeat_count(m_timer, 1);
    UIEngine::getInstance().unlock();
}
void NotificationPopup::dismiss() {
    if (!m_popup) return;
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, m_popup);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v){ lv_obj_set_y((lv_obj_t*)obj, v); });
    lv_anim_set_values(&a, 24, -60); lv_anim_set_time(&a, 180);
    lv_anim_set_ready_cb(&a, [](lv_anim_t* a){ lv_obj_del((lv_obj_t*)a->var); });
    lv_anim_start(&a);
    m_popup = nullptr; m_timer = nullptr;
}