#pragma once
#include "lvgl/lvgl.h"
class HomeScreen {
public:
    static HomeScreen& getInstance();
    void build(lv_obj_t* screen);
    void show();
    void focusApp(int idx);
private:
    HomeScreen() = default;
    static void onKeyEvent(const struct ion_event_t& e);
    lv_obj_t* m_grid   = nullptr;
    lv_obj_t* m_screen = nullptr;
    int m_focusIdx = 0;
    int m_subId    = -1;
};