#pragma once
#include "config/ion_config.h"
#include "lvgl/lvgl.h"
class NotificationPopup {
public:
    static NotificationPopup& getInstance();
    void show(const char* title, const char* msg, ion_notif_level_t level=ION_NOTIF_INFO, uint32_t ms=3000);
private:
    NotificationPopup() = default;
    lv_obj_t*   m_popup  = nullptr;
    lv_timer_t* m_timer  = nullptr;
    void dismiss();
};