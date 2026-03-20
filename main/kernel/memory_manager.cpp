#include "memory_manager.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "MemMgr";
static const size_t HEAP_LOW_THRESHOLD = 32768; // 32KB

MemoryManager& MemoryManager::getInstance() {
    static MemoryManager inst; return inst;
}

void MemoryManager::init() {
    m_heapMin  = freeHeap();
    m_psramMin = freePsram();
    ESP_LOGI(TAG, "Init — Heap: %zuKB  PSRAM: %zuKB",
             m_heapMin/1024, m_psramMin/1024);
}

void* MemoryManager::safeAlloc(size_t bytes, bool preferPsram) {
    void* p = nullptr;
    if (preferPsram)
        p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
        p = heap_caps_malloc(bytes, MALLOC_CAP_DEFAULT);
    if (!p)
        ESP_LOGE(TAG, "safeAlloc(%zu) FAILED", bytes);
    else if (isLow())
        ESP_LOGW(TAG, "Heap low! Free: %zuKB", freeHeap()/1024);
    return p;
}

void* MemoryManager::psramAlloc(size_t bytes) {
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void MemoryManager::safeFree(void* p) {
    heap_caps_free(p);
}

size_t MemoryManager::freeHeap() const {
    size_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (f < m_heapMin) const_cast<MemoryManager*>(this)->m_heapMin = f;
    return f;
}

size_t MemoryManager::freePsram() const {
    size_t f = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (f < m_psramMin) const_cast<MemoryManager*>(this)->m_psramMin = f;
    return f;
}

bool MemoryManager::isLow() const {
    return freeHeap() < HEAP_LOW_THRESHOLD;
}

MemSnapshot MemoryManager::snapshot() const {
    return {
        freeHeap(), m_heapMin,
        freePsram(), m_psramMin,
        (uint32_t)(esp_timer_get_time()/1000)
    };
}

void MemoryManager::logStats() const {
    ESP_LOGI(TAG, "Heap: %zuKB (min %zuKB)  PSRAM: %zuKB (min %zuKB)",
             freeHeap()/1024, m_heapMin/1024,
             freePsram()/1024, m_psramMin/1024);
}

void MemoryManager::monitorTask() {
    while (true) {
        freeHeap(); freePsram(); // Update mins
        if (isLow()) {
            ESP_LOGW(TAG, "LOW HEAP: %zuKB remaining!", freeHeap()/1024);
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // Every 10s
    }
}
