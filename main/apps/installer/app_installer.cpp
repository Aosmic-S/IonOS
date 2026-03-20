// ╔══════════════════════════════════════════════════════════════════════════╗
// ║              IonOS App Installer — Implementation                         ║
// ║  Apache License 2.0 — see LICENSE                                        ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#include "app_installer.h"
#include "apps/app_manager.h"
#include "drivers/storage/sd_driver.h"
#include "ui/notification_popup.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

static const char* TAG = "Installer";

AppInstaller& AppInstaller::getInstance() {
    static AppInstaller inst; return inst;
}

const char* packageStatusStr(PackageStatus s) {
    switch (s) {
        case PackageStatus::OK:                 return "OK";
        case PackageStatus::ERR_NOT_FOUND:      return "File not found";
        case PackageStatus::ERR_INVALID_MAGIC:  return "Not a valid .ionapp file";
        case PackageStatus::ERR_CORRUPT_HEADER: return "Package header corrupted";
        case PackageStatus::ERR_CORRUPT_CODE:   return "Package code corrupted (bad CRC)";
        case PackageStatus::ERR_VERSION_TOO_NEW:return "Requires newer IonOS version";
        case PackageStatus::ERR_ALREADY_INSTALLED: return "Already installed";
        case PackageStatus::ERR_NO_SPACE:       return "Not enough SD card space";
        case PackageStatus::ERR_PERMISSION_DENIED: return "Permission denied";
        default:                                return "Unknown error";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CRC32 (standard polynomial 0xEDB88320)
// ─────────────────────────────────────────────────────────────────────────────
uint32_t AppInstaller::crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan /sdcard/apps/ for .ionapp files
// ─────────────────────────────────────────────────────────────────────────────
void AppInstaller::scanPackages() {
    m_packages.clear();
    DIR* dir = opendir(PACKAGES_DIR);
    if (!dir) {
        SDDriver::getInstance().ensureDir(PACKAGES_DIR);
        ESP_LOGW(TAG, "No /sdcard/apps/ directory");
        return;
    }
    struct dirent* e;
    while ((e = readdir(dir)) != nullptr) {
        std::string name = e->d_name;
        if (name.size() > 7 &&
            name.substr(name.size() - 7) == IONAPP_EXT) {
            m_packages.push_back(std::string(PACKAGES_DIR) + "/" + name);
            ESP_LOGI(TAG, "Found package: %s", name.c_str());
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Scan complete: %zu .ionapp package(s)", m_packages.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan /sdcard/installed/ for installed apps
// ─────────────────────────────────────────────────────────────────────────────
void AppInstaller::scanInstalled() {
    m_installed.clear();
    SDDriver::getInstance().ensureDir(INSTALLED_DIR);
    DIR* dir = opendir(INSTALLED_DIR);
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) != nullptr) {
        if (e->d_type != DT_DIR || e->d_name[0] == '.') continue;
        std::string appDir = std::string(INSTALLED_DIR) + "/" + e->d_name;
        std::string manifestPath = appDir + "/manifest.json";
        InstalledApp app;
        if (parseManifest(manifestPath, app)) {
            app.installPath = appDir + "/";
            app.hasIcon     = SDDriver::getInstance().exists((appDir + "/icon.bin").c_str());
            m_installed.push_back(app);
            ESP_LOGI(TAG, "Loaded: %s v%s", app.name.c_str(), app.version.c_str());
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Installed apps: %zu", m_installed.size());
}

bool AppInstaller::parseManifest(const std::string& path, InstalledApp& out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END); size_t sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::string buf(sz, '\0');
    fread(&buf[0], 1, sz, f); fclose(f);

    cJSON* root = cJSON_Parse(buf.c_str());
    if (!root) return false;
    auto gs = [&](const char* k) -> std::string {
        cJSON* v = cJSON_GetObjectItem(root, k);
        return (v && cJSON_IsString(v)) ? v->valuestring : "";
    };
    out.id          = gs("id");
    out.name        = gs("name");
    out.version     = gs("version");
    out.author      = gs("author");
    out.desc        = gs("desc");
    cJSON* col      = cJSON_GetObjectItem(root, "icon_color");
    out.accentColor = (col && cJSON_IsNumber(col)) ? (uint32_t)col->valuedouble : 0x00D4FF;
    cJSON* sk       = cJSON_GetObjectItem(root, "size_kb");
    out.sizeKb      = (sk && cJSON_IsNumber(sk)) ? (uint32_t)sk->valuedouble : 0;
    cJSON* perms    = cJSON_GetObjectItem(root, "perms");
    out.perms = 0;
    if (cJSON_IsArray(perms)) {
        cJSON* p = nullptr;
        cJSON_ArrayForEach(p, perms) {
            if (!cJSON_IsString(p)) continue;
            if (strcmp(p->valuestring, "wifi")  == 0) out.perms |= ION_PERM_WIFI;
            if (strcmp(p->valuestring, "sd")    == 0) out.perms |= ION_PERM_SD;
            if (strcmp(p->valuestring, "audio") == 0) out.perms |= ION_PERM_AUDIO;
            if (strcmp(p->valuestring, "led")   == 0) out.perms |= ION_PERM_LED;
            if (strcmp(p->valuestring, "radio") == 0) out.perms |= ION_PERM_RADIO;
        }
    }
    cJSON_Delete(root);
    return !out.id.empty() && !out.name.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Validate a .ionapp package (check magic + header CRC)
// ─────────────────────────────────────────────────────────────────────────────
PackageStatus AppInstaller::validate(const char* path, IonAppHeader& hdrOut) {
    FILE* f = fopen(path, "rb");
    if (!f) return PackageStatus::ERR_NOT_FOUND;

    if (fread(&hdrOut, 1, sizeof(IonAppHeader), f) != sizeof(IonAppHeader)) {
        fclose(f); return PackageStatus::ERR_INVALID_MAGIC;
    }
    fclose(f);

    // Magic check
    if (memcmp(hdrOut.magic, "IONAPP\0\0", 8) != 0)
        return PackageStatus::ERR_INVALID_MAGIC;

    // Header CRC (bytes 12-255 = everything after the first 12 bytes)
    uint32_t computed = crc32(((uint8_t*)&hdrOut) + 12, sizeof(IonAppHeader) - 12);
    if (computed != hdrOut.header_crc32)
        return PackageStatus::ERR_CORRUPT_HEADER;

    // IonOS version check (simple major.minor compare)
    if (versionCompare(hdrOut.app_version, IONOS_VERSION_STR) > 0)
        return PackageStatus::ERR_VERSION_TOO_NEW;

    return PackageStatus::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Install pipeline
// ─────────────────────────────────────────────────────────────────────────────
PackageStatus AppInstaller::install(const char* ionappPath,
                                     InstallProgressCb progress) {
    ESP_LOGI(TAG, "Installing: %s", ionappPath);

    auto prog = [&](const char* stage, int pct) {
        ESP_LOGI(TAG, "[%3d%%] %s", pct, stage);
        if (progress) progress(stage, pct);
    };

    // ── Step 1: Validate ─────────────────────────────────────────────────
    prog("Validating package…", 5);
    IonAppHeader hdr;
    PackageStatus vs = validate(ionappPath, hdr);
    if (vs != PackageStatus::OK) {
        ESP_LOGE(TAG, "Validation failed: %s", packageStatusStr(vs));
        return vs;
    }
    ESP_LOGI(TAG, "Package: %s v%s by %s", hdr.app_name, hdr.app_version, hdr.author);

    // ── Step 2: Already installed? ───────────────────────────────────────
    prog("Checking existing install…", 10);
    if (isInstalled(hdr.app_id)) {
        // Allow reinstall if same or newer version
        for (auto& app : m_installed) {
            if (app.id == hdr.app_id &&
                versionCompare(hdr.app_version, app.version.c_str()) <= 0) {
                ESP_LOGW(TAG, "Already installed (same or older version)");
                return PackageStatus::ERR_ALREADY_INSTALLED;
            }
        }
        // Newer version — uninstall first
        prog("Removing old version…", 15);
        uninstall(hdr.app_id);
    }

    // ── Step 3: Space check ───────────────────────────────────────────────
    prog("Checking SD space…", 20);
    uint64_t needed = (uint64_t)(hdr.code_size + ICON_BYTES + 4096);
    if (SDDriver::getInstance().freeSpace() < needed) {
        ESP_LOGE(TAG, "Not enough space: need %lu KB, have %llu KB",
                 (unsigned long)(needed/1024),
                 (unsigned long long)(SDDriver::getInstance().freeSpace()/1024));
        return PackageStatus::ERR_NO_SPACE;
    }

    // ── Step 4: Create install directory ─────────────────────────────────
    prog("Creating install directory…", 25);
    std::string destDir = installDir(hdr.app_id);
    SDDriver::getInstance().ensureDir(destDir.c_str());
    SDDriver::getInstance().ensureDir((destDir + "/data").c_str());

    // ── Step 5: Open package file ─────────────────────────────────────────
    FILE* f = fopen(ionappPath, "rb");
    if (!f) return PackageStatus::ERR_NOT_FOUND;
    fseek(f, sizeof(IonAppHeader), SEEK_SET);   // Skip header

    // ── Step 6: Extract icon (32×32 RGB565 = 2048 bytes) ─────────────────
    prog("Extracting icon…", 35);
    if (!extractIcon(f, destDir)) {
        fclose(f);
        ESP_LOGE(TAG, "Icon extraction failed");
        return PackageStatus::ERR_CORRUPT_CODE;
    }

    // ── Step 7: Extract code binary ───────────────────────────────────────
    prog("Extracting app code…", 50);
    if (!extractCode(f, hdr.code_size, hdr.code_crc32, destDir)) {
        fclose(f);
        rmdir_recursive(destDir);
        ESP_LOGE(TAG, "Code extraction failed — CRC mismatch");
        return PackageStatus::ERR_CORRUPT_CODE;
    }
    fclose(f);

    // ── Step 8: Write manifest.json ───────────────────────────────────────
    prog("Writing manifest…", 85);
    writeManifestJson(hdr, destDir);

    // ── Step 9: Register with AppManager ─────────────────────────────────
    prog("Registering app…", 95);
    scanInstalled();  // Refresh installed list
    loadAllIntoAppManager();

    prog("Done!", 100);
    ESP_LOGI(TAG, "✓ Installed: %s v%s → %s", hdr.app_name, hdr.app_version, destDir.c_str());
    return PackageStatus::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract 32×32 icon (2048 bytes raw RGB565)
// ─────────────────────────────────────────────────────────────────────────────
bool AppInstaller::extractIcon(FILE* f, const std::string& destDir) {
    std::string dst = destDir + "/icon.bin";
    uint8_t buf[ICON_BYTES];
    if (fread(buf, 1, ICON_BYTES, f) != ICON_BYTES) return false;
    FILE* o = fopen(dst.c_str(), "wb");
    if (!o) return false;
    size_t w = fwrite(buf, 1, ICON_BYTES, o);
    fclose(o);
    return w == ICON_BYTES;
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract code binary and verify CRC32
// ─────────────────────────────────────────────────────────────────────────────
bool AppInstaller::extractCode(FILE* f, uint32_t codeSize, uint32_t expectedCrc,
                                const std::string& destDir) {
    if (codeSize == 0) return true; // Scripts-only app, no binary

    std::string dst = destDir + "/app.bin";
    FILE* o = fopen(dst.c_str(), "wb");
    if (!o) return false;

    // Stream in 4KB chunks, compute CRC on the fly
    static const size_t CHUNK = 4096;
    auto* buf = (uint8_t*)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(o); return false; }

    uint32_t crc     = 0xFFFFFFFF;
    uint32_t written = 0;
    bool     ok      = true;

    while (written < codeSize && !ferror(f)) {
        size_t take = std::min((size_t)(codeSize - written), CHUNK);
        size_t n    = fread(buf, 1, take, f);
        if (n == 0) { ok = false; break; }

        // Update running CRC
        for (size_t i = 0; i < n; i++) {
            crc ^= buf[i];
            for (int j = 0; j < 8; j++)
                crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }

        if (fwrite(buf, 1, n, o) != n) { ok = false; break; }
        written += n;
    }

    heap_caps_free(buf);
    fclose(o);

    crc = ~crc;
    if (!ok || written != codeSize || crc != expectedCrc) {
        remove(dst.c_str());
        ESP_LOGE(TAG, "CRC mismatch: expected 0x%08lX got 0x%08lX",
                 (unsigned long)expectedCrc, (unsigned long)crc);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Write manifest.json from header
// ─────────────────────────────────────────────────────────────────────────────
void AppInstaller::writeManifestJson(const IonAppHeader& hdr, const std::string& destDir) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id",         hdr.app_id);
    cJSON_AddStringToObject(root, "name",       hdr.app_name);
    cJSON_AddStringToObject(root, "version",    hdr.app_version);
    cJSON_AddStringToObject(root, "author",     hdr.author);
    cJSON_AddStringToObject(root, "desc",       hdr.desc);
    cJSON_AddNumberToObject(root, "icon_color", hdr.accent_color);
    cJSON_AddNumberToObject(root, "size_kb",    hdr.code_size / 1024);

    cJSON* permsArr = cJSON_AddArrayToObject(root, "perms");
    if (hdr.perms & ION_PERM_WIFI)  cJSON_AddItemToArray(permsArr, cJSON_CreateString("wifi"));
    if (hdr.perms & ION_PERM_SD)    cJSON_AddItemToArray(permsArr, cJSON_CreateString("sd"));
    if (hdr.perms & ION_PERM_AUDIO) cJSON_AddItemToArray(permsArr, cJSON_CreateString("audio"));
    if (hdr.perms & ION_PERM_LED)   cJSON_AddItemToArray(permsArr, cJSON_CreateString("led"));
    if (hdr.perms & ION_PERM_RADIO) cJSON_AddItemToArray(permsArr, cJSON_CreateString("radio"));

    char* str = cJSON_Print(root);
    if (str) {
        std::string mpath = destDir + "/manifest.json";
        FILE* f = fopen(mpath.c_str(), "w");
        if (f) { fputs(str, f); fclose(f); }
        free(str);
    }
    cJSON_Delete(root);
}

// ─────────────────────────────────────────────────────────────────────────────
// Uninstall — removes /sdcard/installed/<appId> recursively
// ─────────────────────────────────────────────────────────────────────────────
PackageStatus AppInstaller::uninstall(const std::string& appId) {
    if (!isInstalled(appId)) return PackageStatus::ERR_NOT_FOUND;

    std::string dir = installDir(appId.c_str());
    if (!rmdir_recursive(dir)) {
        ESP_LOGE(TAG, "Failed to remove: %s", dir.c_str());
        return PackageStatus::ERR_PERMISSION_DENIED;
    }

    m_installed.erase(std::remove_if(m_installed.begin(), m_installed.end(),
        [&appId](const InstalledApp& a){ return a.id == appId; }),
        m_installed.end());

    ESP_LOGI(TAG, "Uninstalled: %s", appId.c_str());
    return PackageStatus::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Register all installed apps with AppManager at boot / after install
// ─────────────────────────────────────────────────────────────────────────────
void AppInstaller::loadAllIntoAppManager() {
    for (auto& app : m_installed) {
        // Determine icon: load from icon.bin if present, else fallback
        std::string iconPath = app.installPath + "icon.bin";

        // Register with AppManager as a dynamic "SD App"
        // The factory creates a lightweight SDApp wrapper
        std::string appId = app.id;
        std::string name  = app.name;
        uint32_t    color = app.accentColor;
        std::string path  = app.installPath;

        AppManager::getInstance().registerSDApp(name.c_str(), color, iconPath, path,
            [appId, name, path](IonApp* base) {
                ESP_LOGI(TAG, "Launching SD app: %s from %s", name.c_str(), path.c_str());
                // In a full implementation with dynamic loading:
                // dlopen(path+"app.bin") → call app_create() symbol
                // For now: show info screen (bridge until dynamic loader ready)
            });
    }
    ESP_LOGI(TAG, "Registered %zu SD apps with AppManager", m_installed.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string AppInstaller::installDir(const char* appId) const {
    // Use last component of reverse-domain as directory name
    // "com.example.myapp" → "/sdcard/installed/myapp"
    std::string id = appId;
    size_t dot = id.rfind('.');
    std::string dirName = (dot != std::string::npos) ? id.substr(dot + 1) : id;
    // Capitalise first letter
    if (!dirName.empty()) dirName[0] = toupper(dirName[0]);
    return std::string(INSTALLED_DIR) + "/" + dirName;
}

bool AppInstaller::isInstalled(const std::string& appId) const {
    for (auto& a : m_installed) if (a.id == appId) return true;
    return false;
}

bool AppInstaller::hasUpdate(const std::string& appId, const char* packagePath) {
    IonAppHeader hdr;
    if (validate(packagePath, hdr) != PackageStatus::OK) return false;
    for (auto& a : m_installed) {
        if (a.id == appId)
            return versionCompare(hdr.app_version, a.version.c_str()) > 0;
    }
    return false;
}

int AppInstaller::versionCompare(const char* a, const char* b) {
    // Parse "major.minor.patch" and compare numerically
    int ma=0,mi=0,mp=0, ba=0,bi=0,bp=0;
    sscanf(a, "%d.%d.%d", &ma, &mi, &mp);
    sscanf(b, "%d.%d.%d", &ba, &bi, &bp);
    if (ma != ba) return ma - ba;
    if (mi != bi) return mi - bi;
    return mp - bp;
}

bool AppInstaller::rmdir_recursive(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) return true; // Already gone
    struct dirent* e;
    while ((e = readdir(dir)) != nullptr) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        std::string full = path + "/" + e->d_name;
        if (e->d_type == DT_DIR) rmdir_recursive(full);
        else remove(full.c_str());
    }
    closedir(dir);
    return rmdir(path.c_str()) == 0;
}
