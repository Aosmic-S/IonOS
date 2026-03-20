#include "button_driver.h"
#include "kernel/kernel.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "Buttons";

// Key → GPIO pin map (must match ION_KEY_* order)
const int ButtonDriver::PIN[ION_KEY__MAX] = {
    -1,               // ION_KEY_NONE
    PIN_BTN_UP,       // ION_KEY_UP
    PIN_BTN_DOWN,     // ION_KEY_DOWN
    PIN_BTN_LEFT,     // ION_KEY_LEFT
    PIN_BTN_RIGHT,    // ION_KEY_RIGHT
    PIN_BTN_A,        // ION_KEY_X
    PIN_BTN_B,        // ION_KEY_B
    PIN_BTN_X,        // ION_KEY_A
    PIN_BTN_START,    // ION_KEY_START
    PIN_BTN_MENU,     // ION_KEY_MENU
};

// LVGL key map
static lv_key_t toIvglKey(ion_key_t k) {
    switch(k) {
        case ION_KEY_UP:    return LV_KEY_UP;
        case ION_KEY_DOWN:  return LV_KEY_DOWN;
        case ION_KEY_LEFT:  return LV_KEY_LEFT;
        case ION_KEY_RIGHT: return LV_KEY_RIGHT;
        case ION_KEY_X:     return LV_KEY_ENTER;
        case ION_KEY_B:     return LV_KEY_ESC;
        case ION_KEY_MENU:  return LV_KEY_HOME;
        default:            return LV_KEY_ENTER;
    }
}

ButtonDriver& ButtonDriver::getInstance() { static ButtonDriver i; return i; }

esp_err_t ButtonDriver::init() {
    for (int k = 1; k < ION_KEY__MAX; k++) {
        gpio_config_t c = {};
        c.pin_bit_mask = 1ULL << PIN[k];
        c.mode         = GPIO_MODE_INPUT;
        c.pull_up_en   = GPIO_PULLUP_ENABLE;
        c.pull_down_en = GPIO_PULLDOWN_DISABLE;
        c.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&c);
    }

    // LVGL keypad indev
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type    = LV_INDEV_TYPE_KEYPAD;
    drv.read_cb = [](lv_indev_drv_t* d, lv_indev_data_t* dat){
        ButtonDriver::getInstance().lvglIndevCb(d,dat);
    };
    m_indev = lv_indev_drv_register(&drv);
    ESP_LOGI(TAG, "9-button input registered");
    return ESP_OK;
}

void ButtonDriver::pollTask() {
    uint32_t debounce[ION_KEY__MAX] = {};
    while(true) {
        uint32_t now = (uint32_t)(esp_timer_get_time()/1000);
        for (int k=1; k<ION_KEY__MAX; k++) {
            bool raw = (gpio_get_level((gpio_num_t)PIN[k]) == 0); // active LOW
            uint32_t bit = (1u << k);
            bool wasPressed = (m_state & bit) != 0;

            if (raw && !wasPressed) {
                // Debounce: must be held for BTN_DEBOUNCE_MS
                if (!debounce[k]) debounce[k] = now;
                if ((now - debounce[k]) >= BTN_DEBOUNCE_MS) {
                    m_state |= bit;
                    m_pressTime[k] = now;
                    m_longFired[k] = false;
                    IonKernel::getInstance().postEvent(ION_EVENT_KEY_DOWN, k);
                    // Update LVGL active key
                    m_lastLvglKey   = (ion_key_t)k;
                    m_lastLvglState = true;
                }
            } else if (!raw) {
                debounce[k] = 0;
                if (wasPressed) {
                    m_state &= ~bit;
                    IonKernel::getInstance().postEvent(ION_EVENT_KEY_UP, k);
                    m_lastLvglState = false;
                }
            } else if (wasPressed && !m_longFired[k]) {
                if ((now - m_pressTime[k]) >= BTN_LONGPRESS_MS) {
                    m_longFired[k] = true;
                    IonKernel::getInstance().postEvent(ION_EVENT_KEY_LONG, k);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // 100Hz
    }
}

bool ButtonDriver::isPressed(ion_key_t k) const {
    return (m_state & (1u<<k)) != 0;
}

void ButtonDriver::lvglIndevCb(lv_indev_drv_t*, lv_indev_data_t* data) {
    if (m_lastLvglKey != ION_KEY_NONE) {
        data->key   = toIvglKey(m_lastLvglKey);
        data->state = m_lastLvglState ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
