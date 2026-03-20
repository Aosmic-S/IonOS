#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║              IonOS Task Manager                                  ║
// ║  Wraps FreeRTOS task creation with name tracking,               ║
// ║  watchdog integration, and runtime stats.                        ║
// ╚══════════════════════════════════════════════════════════════════╝
#include "config/ion_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <vector>
#include <functional>

struct TaskInfo {
    TaskHandle_t  handle;
    std::string   name;
    uint32_t      stackSize;
    uint8_t       priority;
    int           core;
    uint32_t      createdAtMs;
    bool          alive;
};

class TaskManager {
public:
    static TaskManager& getInstance();

    // Create a task and register it
    TaskHandle_t create(TaskFunction_t fn, const char* name,
                        uint32_t stackBytes, void* arg,
                        uint8_t priority, int core = -1);

    // Create from C++ lambda (wrapper manages heap allocation)
    TaskHandle_t createLambda(std::function<void()> fn, const char* name,
                              uint32_t stackBytes, uint8_t priority, int core = -1);

    void  deleteTask(TaskHandle_t h);
    void  deleteByName(const char* name);
    bool  isAlive(const char* name) const;
    const std::vector<TaskInfo>& getAll() const { return m_tasks; }
    void  printStats() const;
    size_t count() const { return m_tasks.size(); }

    // Suspend/resume all non-kernel tasks (for safe SD access etc.)
    void  suspendAll();
    void  resumeAll();

private:
    TaskManager() = default;
    std::vector<TaskInfo> m_tasks;
};
