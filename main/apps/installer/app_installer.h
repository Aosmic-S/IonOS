#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║              IonOS App Installer Engine                          ║
// ║                                                                  ║
// ║  Lifecycle:                                                      ║
// ║   scan()       → find .ionapp files on SD                       ║
// ║   validate()   → check header + CRC32                            ║
// ║   install()    → unpack to /sdcard/installed/<name>/            ║
// ║   uninstall()  → remove installed directory                      ║
// ║   loadAll()    → register all installed apps with AppManager    ║
// ╚══════════════════════════════════════════════════════════════════╝

#include "ion_app_manifest.h"
#include <vector>
#include <string>
#include <functional>

using InstallProgressCb = std::function<void(const char* stage, int pct)>;

class AppInstaller {
public:
    static AppInstaller& getInstance();

    // ── Discovery ──────────────────────────────────────────────────
    // Scan /sdcard/apps/ for .ionapp packages
    void scanPackages();
    const std::vector<std::string>& getPackagePaths() const { return m_packages; }

    // Scan /sdcard/installed/ for already-installed apps
    void scanInstalled();
    const std::vector<InstalledApp>& getInstalledApps() const { return m_installed; }

    // ── Validation ─────────────────────────────────────────────────
    // Read header from .ionapp file, validate magic + CRC
    PackageStatus validate(const char* path, IonAppHeader& hdrOut);

    // ── Installation ───────────────────────────────────────────────
    // Full install pipeline: validate → check space → unpack → register
    PackageStatus install(const char* ionappPath,
                          InstallProgressCb progress = nullptr);

    // ── Uninstall ──────────────────────────────────────────────────
    PackageStatus uninstall(const std::string& appId);

    // ── Runtime registration ───────────────────────────────────────
    // Load all installed apps into AppManager at boot
    void loadAllIntoAppManager();

    // ── Update check ───────────────────────────────────────────────
    // Returns true if SD package version > installed version
    bool hasUpdate(const std::string& appId, const char* packagePath);

    // ── Helpers ────────────────────────────────────────────────────
    std::string installDir(const char* appId) const;
    bool        isInstalled(const std::string& appId) const;
    uint32_t    installedCount() const { return m_installed.size(); }

private:
    AppInstaller() = default;

    bool        parseManifest(const std::string& path, InstalledApp& out);
    bool        extractIcon(FILE* f, const std::string& destDir);
    bool        extractCode(FILE* f, uint32_t codeSize, uint32_t expectedCrc,
                            const std::string& destDir);
    void        writeManifestJson(const IonAppHeader& hdr, const std::string& destDir);
    uint32_t    crc32(const uint8_t* data, size_t len);
    bool        rmdir_recursive(const std::string& path);
    int         versionCompare(const char* a, const char* b); // <0,0,>0

    std::vector<std::string>  m_packages;   // .ionapp paths on SD
    std::vector<InstalledApp> m_installed;  // parsed installed apps

    static constexpr const char* PACKAGES_DIR  = "/sdcard/apps";
    static constexpr const char* INSTALLED_DIR = "/sdcard/installed";
    static constexpr const char* IONAPP_EXT    = ".ionapp";
    static constexpr const char* MAGIC         = "IONAPP\0\0";
    static constexpr uint32_t    ICON_BYTES    = 32 * 32 * 2;  // RGB565
};
