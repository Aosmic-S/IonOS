#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║                     IonOS Kernel                                 ║
// ║  Singleton orchestrator:                                         ║
// ║   • 5-phase boot sequence                                        ║
// ║   • FreeRTOS task registry                                       ║
// ║   • Event bus (publish/subscribe via queue)                      ║
// ║   • System state machine                                         ║
// ╚══════════════════════════════════════════════════════════════════╝
#include "config/ion_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <functional>
#include <vector>
#include <string>

// ── Event subscriber callback ─────────────────────────────────────
using IonEventCallback = std::function<void(const ion_event_t&)>;

struct IonSubscription {
    ion_event_type_t type;
    IonEventCallback cb;
    int              id;
};

// ── Task descriptor ───────────────────────────────────────────────
struct IonTask {
    TaskHandle_t handle;
    std::string  name;
    uint32_t     stackSize;
    uint8_t      priority;
    uint8_t      core;
};

// ── System states ─────────────────────────────────────────────────
enum class KernelState {
    BOOT_HARDWARE,
    BOOT_UI,
    BOOT_SERVICES,
    BOOT_APPS,
    RUNNING,
    SLEEP,
    FAULT
};

// ═════════════════════════════════════════════════════════════════════
class IonKernel {
public:
    static IonKernel& getInstance();

    // ── Boot ───────────────────────────────────────────────────────
    void boot();
    void shutdown();
    KernelState getState() const { return m_state; }
    const char* getStateStr() const;

    // ── Event Bus ──────────────────────────────────────────────────
    void postEvent(ion_event_type_t type, uint32_t data = 0, void* ptr = nullptr);
    int  subscribeEvent(ion_event_type_t type, IonEventCallback cb);
    void unsubscribeEvent(int subscriptionId);
    void broadcastEvent(const ion_event_t& ev);  // Synchronous, call from UI task only

    // ── Task Registry ─────────────────────────────────────────────
    TaskHandle_t createTask(TaskFunction_t fn, const char* name,
                            uint32_t stackBytes, void* arg,
                            uint8_t priority, int core = -1);
    void deleteTask(TaskHandle_t h);
    size_t getTaskCount() const { return m_tasks.size(); }

    // ── Memory helpers ─────────────────────────────────────────────
    static void* psramAlloc(size_t bytes);
    static void  psramFree(void* p);
    static size_t freeHeap();
    static size_t freePsram();

    // ── Diagnostics ────────────────────────────────────────────────
    void printTaskList();
    void printMemStats();
    uint32_t getUptimeMs() const;

private:
    IonKernel() = default;

    void phaseHardware();
    void phaseUI();
    void phaseServices();
    void phaseApps();
    void phaseTasks();

    static void eventLoopTask(void* arg);

    KernelState              m_state      = KernelState::BOOT_HARDWARE;
    QueueHandle_t            m_eventQueue = nullptr;
    SemaphoreHandle_t        m_subMutex   = nullptr;
    std::vector<IonSubscription> m_subs;
    std::vector<IonTask>         m_tasks;
    int                      m_nextSubId  = 1;
    uint32_t                 m_bootTime   = 0;
};
