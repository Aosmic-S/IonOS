#include "app_manager.h"
#include "ui/homescreen.h"
#include "ui/notification_popup.h"
#include "kernel/kernel.h"
#include "esp_log.h"
// Forward declares
#include "settings/settings_app.h"
#include "music_player/music_player_app.h"
#include "browser/browser_app.h"
#include "chatbot/chatbot_app.h"
#include "file_manager/file_manager_app.h"
#include "emulator/emulator_app.h"
#include "app_store/app_store_app.h"
#include "bruce/bruce_app.h"
#include "installer/app_installer.h"

static const char* TAG = "AppMgr";
AppManager& AppManager::getInstance(){ static AppManager i; return i; }

void IonApp::buildScreen(const char* title) {
    m_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x0A0E1A), 0);
    lv_obj_t* bar = lv_obj_create(m_screen);
    lv_obj_set_size(bar, DISPLAY_WIDTH, 44); lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x090D17), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xEEF2FF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 2);
}

void AppManager::init() {
    registerApp("Settings",    ION_ICON_SETTINGS,   0x00D4FF, []()->IonApp*{ return new SettingsApp(); });
    registerApp("Files",       ION_ICON_FILES,       0xFFB800, []()->IonApp*{ return new FileManagerApp(); });
    registerApp("Music",       ION_ICON_MUSIC,       0x00FF9F, []()->IonApp*{ return new MusicPlayerApp(); });
    registerApp("Browser",     ION_ICON_BROWSER,     0x7B2FFF, []()->IonApp*{ return new BrowserApp(); });
    registerApp("IonBot",      ION_ICON_CHATBOT,     0xFF6B6B, []()->IonApp*{ return new ChatbotApp(); });
    registerApp("Emulator",    ION_ICON_EMULATOR,    0xFF9F00, []()->IonApp*{ return new EmulatorApp(); });
    registerApp("App Store",   ION_ICON_DOWNLOAD,   0x7B2FFF, []()->IonApp*{ return new AppStoreApp(); });
    registerApp("Bruce",       ION_ICON_WARNING_ICO,0xF85149, []()->IonApp*{ return new BruceApp(); });
    ESP_LOGI(TAG, "%d apps registered", (int)m_apps.size());

    // Subscribe to key events and forward to current app
    m_keySubId = IonKernel::getInstance().subscribeEvent(ION_EVENT_KEY_DOWN, [](const ion_event_t& e){
        if (AppManager::getInstance().m_current)
            AppManager::getInstance().m_current->onKey((ion_key_t)e.data, true);
    });
}

void AppManager::registerApp(const char* name, ion_icon_id_t ico, uint32_t color,
                              std::function<IonApp*()> f) {
    m_apps.push_back({ name, ico, color, f });
}

void AppManager::launchApp(int idx) {
    if (idx < 0 || idx >= (int)m_apps.size()) return;
    if (m_current) { m_current->onPause(); }
    IonApp* app = m_apps[idx].factory();
    app->onCreate();
    m_current    = app;
    m_currentIdx = idx;
    lv_scr_load_anim(app->m_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    ESP_LOGI(TAG, "Launched: %s", m_apps[idx].name);
}

void AppManager::launchAppByName(const char* name) {
    for (int i=0; i<(int)m_apps.size(); i++)
        if (strcmp(m_apps[i].name, name)==0) { launchApp(i); return; }
}

void AppManager::closeCurrentApp() {
    if (!m_current) return;
    IonApp* old = m_current; m_current = nullptr; m_currentIdx = -1;
    lv_scr_load_anim(HomeScreen::getInstance().m_screen ? HomeScreen::getInstance().m_screen : lv_scr_act(),
                     LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    old->onDestroy();
    delete old;
}

void AppManager::showHome() {
    if (m_current) closeCurrentApp();
    HomeScreen::getInstance().show();
}

void AppManager::showSystemMenu() {
    lv_obj_t* menu = lv_obj_create(lv_layer_top());
    lv_obj_set_size(menu, 200, 220); lv_obj_center(menu);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0x131929), 0);
    lv_obj_set_style_border_color(menu, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_border_width(menu, 1, 0);
    lv_obj_set_style_radius(menu, 10, 0);
    lv_obj_set_style_shadow_color(menu, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_shadow_width(menu, 20, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(menu, 8, 0);
    lv_obj_set_style_pad_gap(menu, 6, 0);

    const char* items[] = { LV_SYMBOL_HOME " Home",
                             LV_SYMBOL_SETTINGS " Settings",
                             LV_SYMBOL_POWER " Power Off",
                             LV_SYMBOL_CLOSE " Close" };
    for (int i=0; i<4; i++) {
        lv_obj_t* btn = lv_btn_create(menu);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E2D4A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x00D4FF), LV_STATE_FOCUSED);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t* l = lv_label_create(btn); lv_label_set_text(l, items[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, [](lv_event_t* e){
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            lv_obj_t* m = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
            lv_obj_del(m);
            if (idx==0) AppManager::getInstance().showHome();
            else if (idx==1) AppManager::getInstance().launchAppByName("Settings");
            else if (idx==2) IonKernel::getInstance().shutdown();
        }, LV_EVENT_CLICKED, nullptr);
    }
}

void AppManager::registerSDApp(const char* name, uint32_t color,
                                const std::string& iconBinPath,
                                const std::string& installPath,
                                std::function<void(IonApp*)> onLaunch) {
    // Create a dynamic SDApp class wrapping an installed app
    // Uses a lambda factory that builds a minimal IonApp showing
    // the installed app's launch screen
    std::string appName   = name;
    std::string iPath     = installPath;
    std::function<void(IonApp*)> cb = onLaunch;

    registerApp(name, ION_ICON_DOWNLOAD, color, [appName, iPath, cb]() -> IonApp* {
        // SDApp — lightweight IonApp that launches the installed binary
        struct SDApp : public IonApp {
            std::string name, path;
            std::function<void(IonApp*)> onLaunch;
            void onCreate() override {
                buildScreen(name.c_str());
                lv_obj_t* lbl = lv_label_create(m_screen);
                char buf[80]; snprintf(buf, sizeof(buf),
                    LV_SYMBOL_PLAY "  %s
Installed from SD", name.c_str());
                lv_label_set_text(lbl, buf);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0xEEF2FF), 0);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl, 200);
                lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -16);
                lv_obj_t* hint = lv_label_create(m_screen);
                lv_label_set_text(hint, "Dynamic loading coming in IonOS v1.1\nPress B to go back");
                lv_obj_set_style_text_color(hint, lv_color_hex(0x8899BB), 0);
                lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
                lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(hint, 200);
                lv_obj_align(hint, LV_ALIGN_CENTER, 0, 30);
                if (onLaunch) onLaunch(this);
            }
            void onKey(ion_key_t k, bool pressed) override {
                if (pressed && k == ION_KEY_B) AppManager::getInstance().closeCurrentApp();
            }
            void onDestroy() override { if(m_screen){lv_obj_del(m_screen);m_screen=nullptr;} }
        };
        auto* app = new SDApp();
        app->name = appName; app->path = iPath; app->onLaunch = cb;
        return app;
    });
}

