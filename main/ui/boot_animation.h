#pragma once
#include "lvgl/lvgl.h"
class BootAnimation {
public:
    static BootAnimation& getInstance();
    void start();   // Non-blocking: launches timer-driven frame sequence
    void stop();
    bool isDone() const { return m_done; }
private:
    BootAnimation() = default;
    static void timerCb(lv_timer_t* t);
    lv_obj_t*   m_canvas  = nullptr;
    lv_obj_t*   m_screen  = nullptr;
    lv_timer_t* m_timer   = nullptr;
    int         m_frame   = 0;
    bool        m_done    = false;
};