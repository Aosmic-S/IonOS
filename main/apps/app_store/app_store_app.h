#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║  IonOS App Store — Install apps from SD card                     ║
// ║                                                                  ║
// ║  Screens:                                                        ║
// ║   1. Library      → installed apps list (manage/uninstall)       ║
// ║   2. Available    → .ionapp packages found on SD card            ║
// ║   3. Detail view  → app info + permissions + install/update btn  ║
// ║   4. Progress     → animated install progress bar                ║
// ╚══════════════════════════════════════════════════════════════════╝
#include "apps/app_manager.h"
#include "apps/installer/app_installer.h"

enum class StoreScreen { LIBRARY, AVAILABLE, DETAIL_PACKAGE, DETAIL_INSTALLED };

class AppStoreApp : public IonApp {
public:
    void onCreate()  override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;

private:
    // ── Screen builders ────────────────────────────────────────────
    void showLibrary();
    void showAvailable();
    void showPackageDetail(const std::string& pkgPath);
    void showInstalledDetail(const InstalledApp& app);
    void showProgress(const char* appName);
    void showResult(PackageStatus status, const char* appName);

    // ── Install / uninstall ────────────────────────────────────────
    void beginInstall(const std::string& pkgPath);
    void beginUninstall(const std::string& appId);
    static void installTask(void* arg);
    static void uninstallTask(void* arg);

    // ── UI helpers ─────────────────────────────────────────────────
    lv_obj_t* makeCard(lv_obj_t* parent, const char* name, const char* sub,
                       uint32_t color, bool installed);
    void updateProgressBar(int pct, const char* stage);
    void buildTabBar(int activeTab);
    void refreshContent();

    // ── State ──────────────────────────────────────────────────────
    StoreScreen    m_screen   = StoreScreen::LIBRARY;
    lv_obj_t*      m_content  = nullptr;   // scrollable body
    lv_obj_t*      m_tabBar   = nullptr;
    lv_obj_t*      m_progBar  = nullptr;
    lv_obj_t*      m_progLbl  = nullptr;
    std::string    m_pendingPkg;       // path being installed
    std::string    m_pendingId;        // id being uninstalled
    bool           m_busy     = false;

    struct InstallArg {
        AppStoreApp* app;
        char         path[256];
    };
    struct UninstallArg {
        AppStoreApp* app;
        char         id[128];
    };
};
