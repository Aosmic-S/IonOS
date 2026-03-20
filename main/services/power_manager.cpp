#include "power_manager.h"
#include "config/pin_config.h"
#include "kernel/kernel.h"
#include "drivers/rgb/ws2812_driver.h"
#include "drivers/display/st7789_driver.h"
#include "ui/notification_popup.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char* TAG="PowerMgr";
PowerManager& PowerManager::getInstance(){ static PowerManager i; return i; }
void PowerManager::init() {
    gpio_config_t c={}; c.pin_bit_mask=1ULL<<PIN_CHG_STATUS;
    c.mode=GPIO_MODE_INPUT; c.pull_up_en=GPIO_PULLUP_ENABLE; gpio_config(&c);
    m_lastActive=(uint32_t)(esp_timer_get_time()/1000);
}
void PowerManager::resetSleepTimer() { m_lastActive=(uint32_t)(esp_timer_get_time()/1000); }
void PowerManager::monitorTask() {
    bool warnSent=false;
    while(true) {
        // Simple ADC read
        int raw=2048; // Placeholder — replace with actual adc_oneshot_read
        m_mv = (int)((raw*3300.0f/4095.0f)*ION_BATT_DIV_RATIO);
        m_pct= (m_mv>=ION_BATT_FULL_MV) ? 100 :
               (m_mv<=ION_BATT_CRIT_MV) ? 0   :
               (m_mv-ION_BATT_CRIT_MV)*100/(ION_BATT_FULL_MV-ION_BATT_CRIT_MV);
        m_charging = (gpio_get_level((gpio_num_t)PIN_CHG_STATUS)==0);
        if (m_charging) { warnSent=false; WS2812Driver::getInstance().setAnimation(LEDAnim::CHARGING); }
        if (!m_charging && m_mv<ION_BATT_LOW_MV && !warnSent) {
            warnSent=true;
            NotificationPopup::getInstance().show("Battery","Low battery — charge soon",ION_NOTIF_WARNING,5000);
            WS2812Driver::getInstance().setAnimation(LEDAnim::BATT_LOW);
            IonKernel::getInstance().postEvent(ION_EVENT_BATTERY_LOW, m_pct);
        }
        uint32_t idle=(uint32_t)(esp_timer_get_time()/1000)-m_lastActive;
        if (idle>ION_SLEEP_TIMEOUT_MS) ST7789Driver::getInstance().setBacklight(20);
        if (idle>ION_DEEPSLEEP_MS) {
            ST7789Driver::getInstance().setBacklight(0);
            WS2812Driver::getInstance().clear(); WS2812Driver::getInstance().show();
            vTaskDelay(pdMS_TO_TICKS(200)); esp_deep_sleep_start();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}