#include "task_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <algorithm>

static const char* TAG = "TaskMgr";

TaskManager& TaskManager::getInstance() {
    static TaskManager inst; return inst;
}

TaskHandle_t TaskManager::create(TaskFunction_t fn, const char* name,
                                  uint32_t stack, void* arg,
                                  uint8_t priority, int core) {
    TaskHandle_t h = nullptr;
    BaseType_t r = (core >= 0)
        ? xTaskCreatePinnedToCore(fn, name, stack, arg, priority, &h, core)
        : xTaskCreate(fn, name, stack, arg, priority, &h);

    if (r == pdPASS && h) {
        TaskInfo ti;
        ti.handle      = h;
        ti.name        = name;
        ti.stackSize   = stack;
        ti.priority    = priority;
        ti.core        = core;
        ti.createdAtMs = (uint32_t)(esp_timer_get_time()/1000);
        ti.alive       = true;
        m_tasks.push_back(ti);
        ESP_LOGI(TAG, "+ '%s' core=%d prio=%d", name, core, priority);
    } else {
        ESP_LOGE(TAG, "FAIL create '%s' (stack=%lu)", name, (unsigned long)stack);
    }
    return h;
}

// Lambda wrapper — allocates a copy of the std::function on heap
struct LambdaArg { std::function<void()> fn; };
static void lambdaRunner(void* arg) {
    auto* la = (LambdaArg*)arg;
    la->fn();
    delete la;
    vTaskDelete(nullptr);
}

TaskHandle_t TaskManager::createLambda(std::function<void()> fn, const char* name,
                                        uint32_t stack, uint8_t prio, int core) {
    auto* arg = new LambdaArg{ fn };
    return create(lambdaRunner, name, stack, arg, prio, core);
}

void TaskManager::deleteTask(TaskHandle_t h) {
    for (auto& t : m_tasks) if (t.handle == h) { t.alive = false; break; }
    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
        [h](const TaskInfo& t){ return t.handle == h; }), m_tasks.end());
    vTaskDelete(h);
}

void TaskManager::deleteByName(const char* name) {
    auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
        [name](const TaskInfo& t){ return t.name == name; });
    if (it != m_tasks.end()) deleteTask(it->handle);
}

bool TaskManager::isAlive(const char* name) const {
    for (auto& t : m_tasks) if (t.name == name && t.alive) return true;
    return false;
}

void TaskManager::printStats() const {
    ESP_LOGI(TAG, "Tasks (%zu running):", m_tasks.size());
    for (auto& t : m_tasks) {
        uint32_t hwm = uxTaskGetStackHighWaterMark(t.handle);
        ESP_LOGI(TAG, "  %-18s  stack=%4lu/%4lu  prio=%d  core=%d",
            t.name.c_str(), (unsigned long)(t.stackSize - hwm*4),
            (unsigned long)t.stackSize, t.priority, t.core);
    }
}

void TaskManager::suspendAll() {
    for (auto& t : m_tasks) {
        if (t.name != "ion_events" && t.name != "ion_input")
            vTaskSuspend(t.handle);
    }
}
void TaskManager::resumeAll() {
    for (auto& t : m_tasks) vTaskResume(t.handle);
}
