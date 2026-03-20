#pragma once
// ── SD Card Driver ───────────────────────────────────────────────────────
#include "config/pin_config.h"
#include "esp_err.h"
#include <string>
#include <vector>
#include <stdint.h>

struct FileEntry { std::string name; bool isDir; size_t size; };

class SDDriver {
public:
    static SDDriver& getInstance();
    esp_err_t init();
    bool      isMounted() const { return m_mounted; }
    bool      listDir(const char* path, std::vector<FileEntry>& out);
    bool      exists(const char* path);
    int64_t   fileSize(const char* path);
    uint64_t  freeSpace();
    uint64_t  totalSpace();
    void      ensureDir(const char* path);

private:
    SDDriver() = default;
    bool m_mounted = false;
};
