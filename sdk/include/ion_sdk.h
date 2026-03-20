#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║                   IonOS Developer SDK v1.0                       ║
// ║  Include this single header to build IonOS apps.                 ║
// ╚══════════════════════════════════════════════════════════════════╝
#include "config/ion_config.h"
#include "config/pin_config.h"
#include "apps/app_manager.h"
#include "ui/ui_engine.h"
#include "ui/notification_popup.h"
#include "services/wifi_manager.h"
#include "services/audio_manager.h"
#include "services/power_manager.h"
#include "services/fs_manager.h"
#include "services/notification_service.h"
#include "drivers/display/st7789_driver.h"
#include "drivers/rgb/ws2812_driver.h"
#include "drivers/input/button_driver.h"
#include "kernel/kernel.h"
#include "resources/resource_loader.h"
#include "themes/ion_themes.h"
// ── Quick helpers ──────────────────────────────────────────────────
#define ION_LOCK()    UIEngine::getInstance().lock(100)
#define ION_UNLOCK()  UIEngine::getInstance().unlock()
#define ION_NOTIFY(t,m,l)  NotificationService::getInstance().post(t,m,l)
#define ION_SOUND(n)       AudioManager::getInstance().playSystemSound(n)
#define ION_ICON(id)       ResourceLoader::getInstance().icon(id)