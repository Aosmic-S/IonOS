#pragma once
#include "config/ion_config.h"
#include "resources/generated/ion_icons.h"
#include "lvgl/lvgl.h"
#include <string>
#include <functional>
#include <vector>

struct AppDef {
    const char*   name;
    ion_icon_id_t iconId;
    uint32_t      color;
    std::function<class IonApp*()> factory;
};

class IonApp {
public:
    virtual ~IonApp() = default;
    virtual void onCreate()  = 0;
    virtual void onDestroy() = 0;
    virtual void onResume()  {}
    virtual void onPause()   {}
    virtual void onKey(ion_key_t k, bool pressed) {}
    lv_obj_t* m_screen = nullptr;
protected:
    void buildScreen(const char* title);
};

class AppManager {
public:
    static AppManager& getInstance();
    void init();
    void registerApp(const char* name, ion_icon_id_t icon, uint32_t color,
                     std::function<IonApp*()> factory);
    void registerSDApp(const char* name, uint32_t color,
                       const std::string& iconBinPath,
                       const std::string& installPath,
                       std::function<void(IonApp*)> onLaunch={});
    void launchApp(int idx);
    void launchAppByName(const char* name);
    void closeCurrentApp();
    void showHome();
    void showSystemMenu();
    IonApp* getCurrentApp() const { return m_current; }
    int     getAppCount()   const { return (int)m_apps.size(); }
    const AppDef& getAppDef(int i) const { return m_apps[i]; }

private:
    AppManager() = default;
    std::vector<AppDef> m_apps;
    IonApp*             m_current = nullptr;
    int                 m_currentIdx = -1;
    int                 m_keySubId   = -1;
};