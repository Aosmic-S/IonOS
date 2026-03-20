#pragma once
// Memory Manager — monitors heap health, provides PSRAM wrappers
#include <stddef.h>
#include <stdint.h>

struct MemSnapshot {
    size_t heapFree;
    size_t heapMin;
    size_t psramFree;
    size_t psramMin;
    uint32_t timestamp;
};

class MemoryManager {
public:
    static MemoryManager& getInstance();
    void init();

    // Monitored allocation — logs warning if heap drops below threshold
    void*  safeAlloc(size_t bytes, bool preferPsram = true);
    void*  psramAlloc(size_t bytes);
    void   safeFree(void* p);

    MemSnapshot snapshot() const;
    size_t freeHeap() const;
    size_t freePsram() const;
    bool   isLow() const;   // true if internal heap < 32KB
    void   logStats() const;
    void   monitorTask();   // Periodic watchdog

private:
    MemoryManager() = default;
    size_t m_heapMin  = SIZE_MAX;
    size_t m_psramMin = SIZE_MAX;
};
