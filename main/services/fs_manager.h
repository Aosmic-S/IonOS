#pragma once
#include "drivers/storage/sd_driver.h"
#include <string>
#include <vector>
class FSManager {
public:
    static FSManager& getInstance();
    void init();
    bool readFile(const char* path, std::string& out);
    bool writeFile(const char* path, const char* data, size_t len);
    bool deleteFile(const char* p);
    bool listDir(const char* p, std::vector<FileEntry>& out);
    uint64_t freeSpace();
    uint64_t totalSpace();
    bool isMounted() const;
};