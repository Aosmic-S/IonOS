#include "notification_service.h"
#include "ui/notification_popup.h"
#include "services/audio_manager.h"
#include "drivers/rgb/ws2812_driver.h"
NotificationService& NotificationService::getInstance(){ static NotificationService i; return i; }
void NotificationService::init() {}
void NotificationService::post(const char* title, const char* msg, ion_notif_level_t lv, uint32_t ms) {
    NotificationPopup::getInstance().show(title, msg, lv, ms);
    switch(lv) {
        case ION_NOTIF_SUCCESS: WS2812Driver::getInstance().notificationFlash(RGBColor::GREEN(),2); break;
        case ION_NOTIF_WARNING: WS2812Driver::getInstance().notificationFlash(RGBColor::AMBER(),3); break;
        case ION_NOTIF_ERROR:   WS2812Driver::getInstance().notificationFlash(RGBColor::RED(),  4);
                                AudioManager::getInstance().playSystemSound("error"); break;
        default:                AudioManager::getInstance().playSystemSound("notification"); break;
    }
}