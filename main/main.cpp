// ╔══════════════════════════════════════════════════════════════════════════╗
// ║                    IonOS v1.0.0 — Entry Point                            ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "kernel/kernel.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"

extern "C" void app_main(void) {
    // PSRAM check
    if (!esp_psram_is_initialized()) {
        ESP_LOGE("main", "CRITICAL: PSRAM not initialized! Check sdkconfig.");
        return;
    }
    ESP_LOGI("main", "PSRAM: %zuMB available",
             esp_psram_get_size() / (1024*1024));

    // Boot IonOS
    IonKernel::getInstance().boot();

    // app_main must not return — kernel tasks keep running
    vTaskDelete(nullptr);
}