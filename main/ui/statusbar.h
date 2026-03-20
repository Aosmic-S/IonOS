#pragma once
#include "lvgl/lvgl.h"
class StatusBar {
public:
    static StatusBar& getInstance();
    void init();
    void update();
private:
    StatusBar() = default;
    lv_obj_t* m_bar, *m_wifi, *m_time, *m_batt;
    lv_timer_t* m_timer = nullptr;
};