#pragma once
#include "apps/app_manager.h"
class SettingsApp : public IonApp {
public:
    void onCreate()  override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;
private:
    void buildWifiTab(lv_obj_t* parent);
    void buildDisplayTab(lv_obj_t* parent);
    void buildAudioTab(lv_obj_t* parent);
    void buildInfoTab(lv_obj_t* parent);
    void buildThemeTab(lv_obj_t* parent);
    void runWifiScan();
    lv_obj_t* m_tabs   = nullptr;
    lv_obj_t* m_scanList = nullptr;
};
