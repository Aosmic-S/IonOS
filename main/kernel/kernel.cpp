// ╔══════════════════════════════════════════════════════════════════════════╗
// ║                  IonOS Kernel — Implementation                           ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#include "kernel.h"
#include "drivers/display/st7789_driver.h"
#include "drivers/audio/audio_driver.h"
#include "drivers/input/button_driver.h"
#include "drivers/rgb/ws2812_driver.h"
#include "drivers/storage/sd_driver.h"
#include "drivers/wireless/wifi_driver.h"
#include "drivers/wireless/nrf24_driver.h"
#include "ui/ui_engine.h"
#include "ui/boot_animation.h"
#include "ui/homescreen.h"
#include "ui/statusbar.h"
#include "services/wifi_manager.h"
#include "services/audio_manager.h"
#include "services/power_manager.h"
#include "services/fs_manager.h"
#include "services/notification_service.h"
#include "apps/app_manager.h"
#include "apps/installer/app_installer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "Kernel";

IonKernel& IonKernel::getInstance() {
    static IonKernel inst;
    return inst;
}

// ─── Boot Entry ───────────────────────────────────────────────────────────
void IonKernel::boot() {
    m_bootTime = (uint32_t)(esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "╔══════════════════════╗");
    ESP_LOGI(TAG, "║   IonOS v%s          ║", IONOS_VERSION_STR);
    ESP_LOGI(TAG, "╚══════════════════════╝");

    // Create event queue & mutex
    m_eventQueue = xQueueCreate(64, sizeof(ion_event_t));
    m_subMutex   = xSemaphoreCreateMutex();

    phaseHardware();
    phaseUI();
    phaseServices();
    phaseApps();
    phaseTasks();

    m_state = KernelState::RUNNING;
    uint32_t elapsed = getUptimeMs();
    ESP_LOGI(TAG, "Boot complete in %lums — IonOS running", (unsigned long)elapsed);
    printMemStats();

    // Play boot sound
    AudioManager::getInstance().playSystemSound("boot");
}

// ─── Phase 1: Hardware Drivers ────────────────────────────────────────────
void IonKernel::phaseHardware() {
    m_state = KernelState::BOOT_HARDWARE;
    ESP_LOGI(TAG, "[1/5] Hardware drivers...");

    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Display — first so we can show boot animation immediately
    ST7789Driver::getInstance().init();
    ST7789Driver::getInstance().setBacklight(100);
    ESP_LOGI(TAG, "  ST7789V   ✓  %dx%d @40MHz SPI DMA", DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Input
    ButtonDriver::getInstance().init();
    ESP_LOGI(TAG, "  Buttons   ✓  9-key debounced");

    // Audio
    AudioDriver::getInstance().init();
    ESP_LOGI(TAG, "  PCM5102A  ✓  I2S 44100Hz stereo");

    // SD card (non-fatal)
    if (SDDriver::getInstance().init() == ESP_OK)
        ESP_LOGI(TAG, "  SD Card   ✓  FAT mounted at /sdcard");
    else
        ESP_LOGW(TAG, "  SD Card   ✗  Not found — continuing");

    // RGB LEDs
    WS2812Driver::getInstance().init();
    WS2812Driver::getInstance().bootSweep();
    ESP_LOGI(TAG, "  WS2812B   ✓  %d LEDs RMT", LED_COUNT);

    // WiFi (init only, connect later)
    WiFiDriver::getInstance().init();
    ESP_LOGI(TAG, "  WiFi      ✓  STA mode ready");

    // nRF24 (non-fatal)
    if (NRF24Driver::getInstance().init() == ESP_OK)
        ESP_LOGI(TAG, "  nRF24     ✓  Channel %d", NRF_CHANNEL);
    else
        ESP_LOGW(TAG, "  nRF24     ✗  Not found");
}

// ─── Phase 2: UI Engine ───────────────────────────────────────────────────
void IonKernel::phaseUI() {
    m_state = KernelState::BOOT_UI;
    ESP_LOGI(TAG, "[2/5] UI engine...");
    UIEngine::getInstance().init();
    BootAnimation::getInstance().start();  // Non-blocking, runs on UI task
}

// ─── Phase 3: Services ────────────────────────────────────────────────────
void IonKernel::phaseServices() {
    m_state = KernelState::BOOT_SERVICES;
    ESP_LOGI(TAG, "[3/5] Services...");
    AudioManager::getInstance().init();
    FSManager::getInstance().init();
    WiFiManager::getInstance().init();      // Auto-connect to saved network
    NotificationService::getInstance().init();
    PowerManager::getInstance().init();
}

// ─── Phase 4: Apps ────────────────────────────────────────────────────────
void IonKernel::phaseApps() {
    m_state = KernelState::BOOT_APPS;
    ESP_LOGI(TAG, "[4/5] App manager...");
    AppManager::getInstance().init();
    // Load SD-installed apps from /sdcard/installed/
    AppInstaller::getInstance().scanInstalled();
    AppInstaller::getInstance().loadAllIntoAppManager();
}

// ─── Phase 5: FreeRTOS Tasks ─────────────────────────────────────────────
void IonKernel::phaseTasks() {
    ESP_LOGI(TAG, "[5/5] Spawning tasks...");

    // Event loop — Core 0
    createTask(eventLoopTask, "ion_events", 4096, this, PRIORITY_HIGH, CORE_EVENT);

    // Audio stream — Core 0
    createTask([](void*){ AudioManager::getInstance().streamTask(); },
               "ion_audio", STACK_AUDIO, nullptr, PRIORITY_REALTIME, CORE_AUDIO);

    // Power monitor — Core 0
    createTask([](void*){ PowerManager::getInstance().monitorTask(); },
               "ion_power", STACK_POWER, nullptr, PRIORITY_LOW, CORE_EVENT);

    // UI render — Core 1 (runs lv_timer_handler at 60fps)
    createTask([](void*){ UIEngine::getInstance().runLoop(); },
               "ion_ui", STACK_UI, nullptr, PRIORITY_HIGH, CORE_UI);

    // Input poll — Core 0
    createTask([](void*){ ButtonDriver::getInstance().pollTask(); },
               "ion_input", STACK_INPUT, nullptr, PRIORITY_NORMAL, CORE_INPUT);

    // LED animation — Core 0
    createTask([](void*){ WS2812Driver::getInstance().animTask(); },
               "ion_leds", STACK_LED, nullptr, PRIORITY_LOW, CORE_EVENT);
}

// ─── Event Loop Task ──────────────────────────────────────────────────────
void IonKernel::eventLoopTask(void* arg) {
    IonKernel* self = (IonKernel*)arg;
    ion_event_t ev;
    while (true) {
        if (xQueueReceive(self->m_eventQueue, &ev, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(self->m_subMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                for (auto& sub : self->m_subs) {
                    if (sub.type == ev.type || sub.type == ION_EVENT_NONE) {
                        sub.cb(ev);
                    }
                }
                xSemaphoreGive(self->m_subMutex);
            }
        }
    }
}

// ─── Event Bus ────────────────────────────────────────────────────────────
void IonKernel::postEvent(ion_event_type_t type, uint32_t data, void* ptr) {
    if (!m_eventQueue) return;
    ion_event_t ev = { type, data, ptr };
    xQueueSend(m_eventQueue, &ev, pdMS_TO_TICKS(10));
}

void IonKernel::broadcastEvent(const ion_event_t& ev) {
    if (xSemaphoreTake(m_subMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (auto& sub : m_subs) {
            if (sub.type == ev.type || sub.type == ION_EVENT_NONE)
                sub.cb(ev);
        }
        xSemaphoreGive(m_subMutex);
    }
}

int IonKernel::subscribeEvent(ion_event_type_t type, IonEventCallback cb) {
    IonSubscription sub = { type, cb, m_nextSubId++ };
    if (xSemaphoreTake(m_subMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        m_subs.push_back(sub);
        xSemaphoreGive(m_subMutex);
    }
    return sub.id;
}

void IonKernel::unsubscribeEvent(int id) {
    if (xSemaphoreTake(m_subMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        m_subs.erase(std::remove_if(m_subs.begin(), m_subs.end(),
            [id](const IonSubscription& s){ return s.id == id; }), m_subs.end());
        xSemaphoreGive(m_subMutex);
    }
}

// ─── Task Registry ────────────────────────────────────────────────────────
TaskHandle_t IonKernel::createTask(TaskFunction_t fn, const char* name,
                                    uint32_t stack, void* arg,
                                    uint8_t priority, int core) {
    TaskHandle_t h = nullptr;
    BaseType_t res;
    if (core >= 0)
        res = xTaskCreatePinnedToCore(fn, name, stack, arg, priority, &h, core);
    else
        res = xTaskCreate(fn, name, stack, arg, priority, &h);

    if (res == pdPASS && h) {
        m_tasks.push_back({ h, name, stack, priority, (uint8_t)core });
        ESP_LOGD(TAG, "Task '%s' created on core %d", name, core);
    } else {
        ESP_LOGE(TAG, "Failed to create task '%s'", name);
    }
    return h;
}

void IonKernel::deleteTask(TaskHandle_t h) {
    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
        [h](const IonTask& t){ return t.handle == h; }), m_tasks.end());
    vTaskDelete(h);
}

// ─── Memory Helpers ───────────────────────────────────────────────────────
void* IonKernel::psramAlloc(size_t bytes) {
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void IonKernel::psramFree(void* p) { heap_caps_free(p); }
size_t IonKernel::freeHeap()  { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
size_t IonKernel::freePsram() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

uint32_t IonKernel::getUptimeMs() const {
    return (uint32_t)(esp_timer_get_time() / 1000) - m_bootTime;
}

// ─── Diagnostics ──────────────────────────────────────────────────────────
void IonKernel::printMemStats() {
    ESP_LOGI(TAG, "Memory — Internal: %zuKB free | PSRAM: %zuKB free",
             freeHeap()/1024, freePsram()/1024);
}

void IonKernel::printTaskList() {
    ESP_LOGI(TAG, "Tasks (%zu):", m_tasks.size());
    for (auto& t : m_tasks) {
        ESP_LOGI(TAG, "  %-16s  stack=%lu  prio=%d  core=%d",
                 t.name.c_str(), (unsigned long)t.stackSize, t.priority, t.core);
    }
}

const char* IonKernel::getStateStr() const {
    switch(m_state) {
        case KernelState::BOOT_HARDWARE:  return "BOOT_HW";
        case KernelState::BOOT_UI:        return "BOOT_UI";
        case KernelState::BOOT_SERVICES:  return "BOOT_SVC";
        case KernelState::BOOT_APPS:      return "BOOT_APP";
        case KernelState::RUNNING:        return "RUNNING";
        case KernelState::SLEEP:          return "SLEEP";
        case KernelState::FAULT:          return "FAULT";
        default:                          return "UNKNOWN";
    }
}

void IonKernel::shutdown() {
    ESP_LOGI(TAG, "Shutdown initiated");
    WS2812Driver::getInstance().clear();
    ST7789Driver::getInstance().setBacklight(0);
    AudioDriver::getInstance().stop();
    esp_deep_sleep_start();
}
