// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  IonOS App Store — UI Implementation                                      ║
// ║  Apache License 2.0 — Copyright 2026 IonOS Contributors                   ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "app_store_app.h"
#include "apps/installer/app_installer.h"
#include "ui/ui_engine.h"
#include "ui/notification_popup.h"
#include "services/audio_manager.h"
#include "drivers/storage/sd_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "AppStore";

// ── Colour palette ────────────────────────────────────────────────────────
#define COL_BG        0x0A0E1A
#define COL_SURFACE   0x131929
#define COL_SURFACE2  0x1A2236
#define COL_ACCENT    0x00D4FF
#define COL_GREEN     0x00FF9F
#define COL_AMBER     0xFFB800
#define COL_RED       0xFF3366
#define COL_TEXT      0xEEF2FF
#define COL_DIM       0x8899BB
#define COL_BORDER    0x1E2D4A

// ─────────────────────────────────────────────────────────────────────────────
// App lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::onCreate() {
    buildScreen("App Store");

    // Tab bar below title bar
    buildTabBar(0);

    // Scrollable content area
    m_content = lv_obj_create(m_screen);
    lv_obj_set_size(m_content, 320, 160);
    lv_obj_set_pos(m_content, 0, 64);
    lv_obj_set_style_bg_color(m_content, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_border_width(m_content, 0, 0);
    lv_obj_set_style_pad_all(m_content, 6, 0);
    lv_obj_set_style_pad_gap(m_content, 6, 0);
    lv_obj_set_scroll_dir(m_content, LV_DIR_VER);
    lv_obj_set_flex_flow(m_content, LV_FLEX_FLOW_COLUMN);

    // Refresh installed + available lists
    AppInstaller::getInstance().scanInstalled();
    AppInstaller::getInstance().scanPackages();

    showLibrary();
}

void AppStoreApp::onDestroy() {
    if (m_screen) { lv_obj_del(m_screen); m_screen = nullptr; }
}

void AppStoreApp::onKey(ion_key_t k, bool pressed) {
    if (!pressed) return;
    if (m_busy)   return;   // Block navigation during install
    switch (k) {
        case ION_KEY_LEFT:
            if (m_screen == StoreScreen::AVAILABLE) {
                m_screen = StoreScreen::LIBRARY;
                buildTabBar(0);
                showLibrary();
            } else if (m_screen == StoreScreen::DETAIL_PACKAGE ||
                       m_screen == StoreScreen::DETAIL_INSTALLED) {
                m_screen = StoreScreen::LIBRARY;
                buildTabBar(0);
                showLibrary();
            }
            break;
        case ION_KEY_RIGHT:
            if (m_screen == StoreScreen::LIBRARY) {
                m_screen = StoreScreen::AVAILABLE;
                buildTabBar(1);
                showAvailable();
            }
            break;
        case ION_KEY_B:
            if (m_screen == StoreScreen::DETAIL_PACKAGE ||
                m_screen == StoreScreen::DETAIL_INSTALLED) {
                refreshContent();
            } else {
                AppManager::getInstance().closeCurrentApp();
            }
            break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab bar
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::buildTabBar(int activeTab) {
    if (m_tabBar) lv_obj_del(m_tabBar);
    m_tabBar = lv_obj_create(m_screen);
    lv_obj_set_size(m_tabBar, 320, 24);
    lv_obj_set_pos(m_tabBar, 0, 44);
    lv_obj_set_style_bg_color(m_tabBar, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_width(m_tabBar, 0, 0);
    lv_obj_set_style_pad_all(m_tabBar, 0, 0);
    lv_obj_clear_flag(m_tabBar, LV_OBJ_FLAG_CLICKABLE);

    const char* tabs[] = { LV_SYMBOL_LIST "  Library", LV_SYMBOL_DOWNLOAD "  Available" };
    for (int i = 0; i < 2; i++) {
        lv_obj_t* btn = lv_btn_create(m_tabBar);
        lv_obj_set_size(btn, 118, 22);
        lv_obj_set_pos(btn, i * 120 + 2, 1);
        bool active = (i == activeTab);
        lv_obj_set_style_bg_color(btn, lv_color_hex(active ? COL_ACCENT : COL_SURFACE2), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_t* l = lv_label_create(btn);
        lv_label_set_text(l, tabs[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(active ? COL_BG : COL_DIM), 0);
        lv_obj_center(l);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int tab = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            AppStoreApp* self = (AppStoreApp*)AppManager::getInstance().getCurrentApp();
            if (!self || self->m_busy) return;
            if (tab == 0) { self->m_screen = StoreScreen::LIBRARY;   self->buildTabBar(0); self->showLibrary(); }
            if (tab == 1) { self->m_screen = StoreScreen::AVAILABLE; self->buildTabBar(1); self->showAvailable(); }
        }, LV_EVENT_CLICKED, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Library screen — list installed apps
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::showLibrary() {
    lv_obj_clean(m_content);
    m_screen = StoreScreen::LIBRARY;

    auto& installed = AppInstaller::getInstance().getInstalledApps();

    if (installed.empty()) {
        lv_obj_t* empty = lv_obj_create(m_content);
        lv_obj_set_width(empty, 308); lv_obj_set_height(empty, 120);
        lv_obj_set_style_bg_color(empty, lv_color_hex(COL_SURFACE), 0);
        lv_obj_set_style_border_width(empty, 0, 0);
        lv_obj_set_style_radius(empty, 12, 0);
        lv_obj_clear_flag(empty, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* ico = lv_label_create(empty);
        lv_label_set_text(ico, LV_SYMBOL_DOWNLOAD);
        lv_obj_set_style_text_color(ico, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
        lv_obj_align(ico, LV_ALIGN_CENTER, 0, -18);

        lv_obj_t* t1 = lv_label_create(empty);
        lv_label_set_text(t1, "No apps installed");
        lv_obj_set_style_text_color(t1, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(t1, &lv_font_montserrat_14, 0);
        lv_obj_align(t1, LV_ALIGN_CENTER, 0, 6);

        lv_obj_t* t2 = lv_label_create(empty);
        lv_label_set_text(t2, "Copy .ionapp files to /sdcard/apps/");
        lv_obj_set_style_text_color(t2, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_text_font(t2, &lv_font_montserrat_12, 0);
        lv_obj_align(t2, LV_ALIGN_CENTER, 0, 22);
        return;
    }

    // Header count
    lv_obj_t* hdr = lv_label_create(m_content);
    char hbuf[40];
    snprintf(hbuf, sizeof(hbuf), "%zu app%s installed",
             installed.size(), installed.size() == 1 ? "" : "s");
    lv_label_set_text(hdr, hbuf);
    lv_obj_set_style_text_color(hdr, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);

    for (auto& app : installed) {
        // Check if update available
        bool hasUpdate = false;
        for (auto& pkg : AppInstaller::getInstance().getPackagePaths()) {
            if (AppInstaller::getInstance().hasUpdate(app.id, pkg.c_str())) {
                hasUpdate = true; break;
            }
        }

        lv_obj_t* card = lv_obj_create(m_content);
        lv_obj_set_width(card, 308);
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_SURFACE), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(app.accentColor), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_SURFACE2), LV_STATE_PRESSED);

        // Colour accent bar
        lv_obj_t* bar = lv_obj_create(card);
        lv_obj_set_size(bar, 4, 44); lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(app.accentColor), 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

        // App name
        lv_obj_t* name = lv_label_create(card);
        lv_label_set_text(name, app.name.c_str());
        lv_obj_set_style_text_color(name, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(name, 12, 0);

        // Version + author
        char meta[64];
        snprintf(meta, sizeof(meta), "v%s  ·  %s", app.version.c_str(), app.author.c_str());
        lv_obj_t* sub = lv_label_create(card);
        lv_label_set_text(sub, meta);
        lv_obj_set_style_text_color(sub, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(sub, 12, 18);

        // Update badge
        if (hasUpdate) {
            lv_obj_t* badge = lv_obj_create(card);
            lv_obj_set_size(badge, 56, 16); lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_set_style_bg_color(badge, lv_color_hex(COL_AMBER), 0);
            lv_obj_set_style_radius(badge, 8, 0);
            lv_obj_set_style_border_width(badge, 0, 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* bl = lv_label_create(badge);
            lv_label_set_text(bl, LV_SYMBOL_UP " Update");
            lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(bl, lv_color_hex(COL_BG), 0);
            lv_obj_center(bl);
        }

        // Click → detail
        std::string appId = app.id;
        lv_obj_set_user_data(card, new std::string(app.id));
        lv_obj_add_event_cb(card, [](lv_event_t* e) {
            auto* id = (std::string*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            AppStoreApp* self = (AppStoreApp*)AppManager::getInstance().getCurrentApp();
            if (!self) return;
            // Find installed app by id
            for (auto& a : AppInstaller::getInstance().getInstalledApps()) {
                if (a.id == *id) { self->showInstalledDetail(a); return; }
            }
        }, LV_EVENT_CLICKED, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Available screen — .ionapp packages on SD
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::showAvailable() {
    lv_obj_clean(m_content);
    m_screen = StoreScreen::AVAILABLE;

    // Refresh scan
    AppInstaller::getInstance().scanPackages();
    auto& pkgs = AppInstaller::getInstance().getPackagePaths();

    // Scan button
    lv_obj_t* scanBtn = lv_btn_create(m_content);
    lv_obj_set_width(scanBtn, 228); lv_obj_set_height(scanBtn, 30);
    lv_obj_set_style_bg_color(scanBtn, lv_color_hex(COL_SURFACE2), 0);
    lv_obj_set_style_border_color(scanBtn, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_border_width(scanBtn, 1, 0);
    lv_obj_set_style_radius(scanBtn, 8, 0);
    lv_obj_t* sl = lv_label_create(scanBtn);
    lv_label_set_text(sl, LV_SYMBOL_REFRESH "  Refresh /sdcard/apps/");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(COL_ACCENT), 0);
    lv_obj_center(sl);
    lv_obj_add_event_cb(scanBtn, [](lv_event_t*) {
        AppStoreApp* s = (AppStoreApp*)AppManager::getInstance().getCurrentApp();
        if (s) s->showAvailable();
    }, LV_EVENT_CLICKED, nullptr);

    if (pkgs.empty()) {
        lv_obj_t* empty = lv_obj_create(m_content);
        lv_obj_set_width(empty, 308); lv_obj_set_height(empty, 90);
        lv_obj_set_style_bg_color(empty, lv_color_hex(COL_SURFACE), 0);
        lv_obj_set_style_border_width(empty, 0, 0);
        lv_obj_set_style_radius(empty, 10, 0);
        lv_obj_clear_flag(empty, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* t = lv_label_create(empty);
        lv_label_set_text(t, LV_SYMBOL_WARNING "  No .ionapp files found\n"
                             "Copy packages to /sdcard/apps/\nthen tap Refresh");
        lv_obj_set_style_text_color(t, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(t, 280);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    char hbuf[40];
    snprintf(hbuf, sizeof(hbuf), "%zu package%s found on SD", pkgs.size(), pkgs.size()==1?"":"s");
    lv_obj_t* hdr = lv_label_create(m_content);
    lv_label_set_text(hdr, hbuf);
    lv_obj_set_style_text_color(hdr, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);

    for (auto& pkg : pkgs) {
        IonAppHeader hdr2 = {};
        PackageStatus vs = AppInstaller::getInstance().validate(pkg.c_str(), hdr2);

        bool valid     = (vs == PackageStatus::OK);
        bool installed = valid && AppInstaller::getInstance().isInstalled(hdr2.app_id);
        bool update    = valid && installed && AppInstaller::getInstance().hasUpdate(hdr2.app_id, pkg.c_str());

        // Extract filename from path
        std::string fname = pkg;
        size_t sl2 = pkg.rfind('/');
        if (sl2 != std::string::npos) fname = pkg.substr(sl2 + 1);

        lv_obj_t* card = lv_obj_create(m_content);
        lv_obj_set_width(card, 308);
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_SURFACE), 0);
        uint32_t borderCol = !valid    ? COL_RED    :
                              update   ? COL_AMBER  :
                              installed? COL_GREEN  : COL_ACCENT;
        lv_obj_set_style_border_color(card, lv_color_hex(borderCol), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_SURFACE2), LV_STATE_PRESSED);

        // Accent strip
        lv_obj_t* strip = lv_obj_create(card);
        lv_obj_set_size(strip, 4, 52); lv_obj_set_pos(strip, 0, 0);
        lv_obj_set_style_bg_color(strip, lv_color_hex(borderCol), 0);
        lv_obj_set_style_border_width(strip, 0, 0);
        lv_obj_set_style_radius(strip, 2, 0);
        lv_obj_clear_flag(strip, LV_OBJ_FLAG_CLICKABLE);

        // Name / filename
        lv_obj_t* nameL = lv_label_create(card);
        lv_label_set_text(nameL, valid ? hdr2.app_name : fname.c_str());
        lv_obj_set_style_text_color(nameL, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(nameL, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(nameL, 12, 0);

        // Version + author
        lv_obj_t* sub = lv_label_create(card);
        char meta[80];
        if (valid) snprintf(meta, sizeof(meta), "v%s  ·  %s", hdr2.app_version, hdr2.author);
        else       snprintf(meta, sizeof(meta), "%s", packageStatusStr(vs));
        lv_label_set_text(sub, meta);
        lv_obj_set_style_text_color(sub, lv_color_hex(valid ? COL_DIM : COL_RED), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(sub, 12, 18);

        // Description (first 48 chars)
        if (valid && hdr2.desc[0]) {
            lv_obj_t* desc = lv_label_create(card);
            char shortDesc[49]; snprintf(shortDesc, sizeof(shortDesc), "%.48s", hdr2.desc);
            lv_label_set_text(desc, shortDesc);
            lv_obj_set_style_text_color(desc, lv_color_hex(COL_DIM), 0);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
            lv_label_set_long_mode(desc, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(desc, 200);
            lv_obj_set_pos(desc, 12, 34);
        }

        // Status badge
        lv_obj_t* badge = lv_obj_create(card);
        lv_obj_set_size(badge, 70, 18); lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_radius(badge, 9, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* bl = lv_label_create(badge);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
        lv_obj_center(bl);
        if (!valid) {
            lv_obj_set_style_bg_color(badge, lv_color_hex(COL_RED), 0);
            lv_label_set_text(bl, LV_SYMBOL_CLOSE " Invalid");
            lv_obj_set_style_text_color(bl, lv_color_hex(COL_TEXT), 0);
        } else if (update) {
            lv_obj_set_style_bg_color(badge, lv_color_hex(COL_AMBER), 0);
            lv_label_set_text(bl, LV_SYMBOL_UP " Update");
            lv_obj_set_style_text_color(bl, lv_color_hex(COL_BG), 0);
        } else if (installed) {
            lv_obj_set_style_bg_color(badge, lv_color_hex(COL_SURFACE2), 0);
            lv_label_set_text(bl, LV_SYMBOL_OK " Installed");
            lv_obj_set_style_text_color(bl, lv_color_hex(COL_GREEN), 0);
        } else {
            lv_obj_set_style_bg_color(badge, lv_color_hex(COL_ACCENT), 0);
            lv_label_set_text(bl, LV_SYMBOL_DOWNLOAD " Install");
            lv_obj_set_style_text_color(bl, lv_color_hex(COL_BG), 0);
        }

        if (valid) {
            lv_obj_set_user_data(card, new std::string(pkg));
            lv_obj_add_event_cb(card, [](lv_event_t* e) {
                auto* p = (std::string*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                AppStoreApp* self = (AppStoreApp*)AppManager::getInstance().getCurrentApp();
                if (self) self->showPackageDetail(*p);
            }, LV_EVENT_CLICKED, nullptr);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Package detail screen — shown before installing
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::showPackageDetail(const std::string& pkgPath) {
    lv_obj_clean(m_content);
    m_pendingPkg = pkgPath;

    IonAppHeader hdr = {};
    PackageStatus vs = AppInstaller::getInstance().validate(pkgPath.c_str(), hdr);
    if (vs != PackageStatus::OK) {
        NotificationPopup::getInstance().show("App Store", packageStatusStr(vs), ION_NOTIF_ERROR, 3000);
        showAvailable(); return;
    }

    bool isUpdate   = AppInstaller::getInstance().isInstalled(hdr.app_id) &&
                      AppInstaller::getInstance().hasUpdate(hdr.app_id, pkgPath.c_str());
    bool alreadyHave= AppInstaller::getInstance().isInstalled(hdr.app_id) && !isUpdate;

    // App name header
    lv_obj_t* nameL = lv_label_create(m_content);
    lv_label_set_text(nameL, hdr.app_name);
    lv_obj_set_style_text_color(nameL, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(nameL, &lv_font_montserrat_20, 0);

    // Meta row
    char meta[80];
    snprintf(meta, sizeof(meta), "v%s  ·  by %s  ·  %lu KB",
             hdr.app_version, hdr.author, (unsigned long)(hdr.code_size / 1024));
    lv_obj_t* metaL = lv_label_create(m_content);
    lv_label_set_text(metaL, meta);
    lv_obj_set_style_text_color(metaL, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(metaL, &lv_font_montserrat_12, 0);

    // Description card
    lv_obj_t* descCard = lv_obj_create(m_content);
    lv_obj_set_width(descCard, 308); lv_obj_set_height(descCard, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(descCard, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_width(descCard, 0, 0);
    lv_obj_set_style_radius(descCard, 8, 0);
    lv_obj_set_style_pad_all(descCard, 8, 0);
    lv_obj_clear_flag(descCard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* descL = lv_label_create(descCard);
    lv_label_set_text(descL, hdr.desc[0] ? hdr.desc : "No description provided.");
    lv_obj_set_style_text_color(descL, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(descL, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(descL, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(descL, 290);

    // Permissions card
    if (hdr.perms) {
        lv_obj_t* permCard = lv_obj_create(m_content);
        lv_obj_set_width(permCard, 308); lv_obj_set_height(permCard, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(permCard, lv_color_hex(COL_SURFACE), 0);
        lv_obj_set_style_border_color(permCard, lv_color_hex(COL_AMBER), 0);
        lv_obj_set_style_border_width(permCard, 1, 0);
        lv_obj_set_style_radius(permCard, 8, 0);
        lv_obj_set_style_pad_all(permCard, 8, 0);
        lv_obj_clear_flag(permCard, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(permCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(permCard, 3, 0);

        lv_obj_t* permHdr = lv_label_create(permCard);
        lv_label_set_text(permHdr, LV_SYMBOL_WARNING "  Permissions requested:");
        lv_obj_set_style_text_color(permHdr, lv_color_hex(COL_AMBER), 0);
        lv_obj_set_style_text_font(permHdr, &lv_font_montserrat_12, 0);

        struct { uint8_t bit; const char* icon; const char* label; } perms[] = {
            { ION_PERM_WIFI,  LV_SYMBOL_WIFI,    "Network access (WiFi)" },
            { ION_PERM_SD,    LV_SYMBOL_SD_CARD, "Read/write SD card" },
            { ION_PERM_AUDIO, LV_SYMBOL_AUDIO,   "Play audio" },
            { ION_PERM_LED,   LV_SYMBOL_CHARGE,  "Control RGB LEDs" },
            { ION_PERM_RADIO, LV_SYMBOL_WIFI,    "nRF24 radio" },
        };
        for (auto& p : perms) {
            if (!(hdr.perms & p.bit)) continue;
            char perm[64]; snprintf(perm, sizeof(perm), "%s  %s", p.icon, p.label);
            lv_obj_t* pl = lv_label_create(permCard);
            lv_label_set_text(pl, perm);
            lv_obj_set_style_text_color(pl, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_text_font(pl, &lv_font_montserrat_12, 0);
        }
    }

    // Install / Already Installed button
    lv_obj_t* installBtn = lv_btn_create(m_content);
    lv_obj_set_width(installBtn, 308); lv_obj_set_height(installBtn, 38);
    lv_obj_set_style_radius(installBtn, 10, 0);
    lv_obj_set_style_border_width(installBtn, 0, 0);
    lv_obj_t* btnLbl = lv_label_create(installBtn);
    lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_16, 0);
    lv_obj_center(btnLbl);

    if (alreadyHave) {
        lv_obj_set_style_bg_color(installBtn, lv_color_hex(COL_SURFACE2), 0);
        lv_label_set_text(btnLbl, LV_SYMBOL_OK "  Already Installed");
        lv_obj_set_style_text_color(btnLbl, lv_color_hex(COL_GREEN), 0);
        lv_obj_clear_flag(installBtn, LV_OBJ_FLAG_CLICKABLE);
    } else if (isUpdate) {
        lv_obj_set_style_bg_color(installBtn, lv_color_hex(COL_AMBER), 0);
        lv_label_set_text(btnLbl, LV_SYMBOL_UP "  Update App");
        lv_obj_set_style_text_color(btnLbl, lv_color_hex(COL_BG), 0);
        std::string pkg = pkgPath;
        lv_obj_set_user_data(installBtn, new std::string(pkg));
        lv_obj_add_event_cb(installBtn, [](lv_event_t* e) {
            auto* p = (std::string*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            AppStoreApp* s = (AppStoreApp*)AppManager::getInstance().getCurrentApp();
            if (s) s->beginInstall(*p);
        }, LV_EVENT_CLICKED, nullptr);
    } else {
        lv_obj_set_style_bg_color(installBtn, lv_color_hex(COL_ACCENT), 0);
        lv_label_set_text(btnLbl, LV_SYMBOL_DOWNLOAD "  Install App");
        lv_obj_set_style_text_color(btnLbl, lv_color_hex(COL_BG), 0);
        std::string pkg = pkgPath;
        lv_obj_set_user_data(installBtn, new std::string(pkg));
        lv_obj_add_event_cb(installBtn, [](lv_event_t* e) {
            auto* p = (std::string*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            AppStoreApp* s = (AppStoreApp*)AppManager::getInstance().getCurrentApp();
            if (s) s->beginInstall(*p);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Back hint
    lv_obj_t* hint = lv_label_create(m_content);
    lv_label_set_text(hint, "B = Back to Available");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Installed detail screen — manage / uninstall
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::showInstalledDetail(const InstalledApp& app) {
    lv_obj_clean(m_content);

    lv_obj_t* nameL = lv_label_create(m_content);
    lv_label_set_text(nameL, app.name.c_str());
    lv_obj_set_style_text_color(nameL, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(nameL, &lv_font_montserrat_20, 0);

    char meta[80];
    snprintf(meta, sizeof(meta), "v%s  ·  %s  ·  %u KB",
             app.version.c_str(), app.author.c_str(), app.sizeKb);
    lv_obj_t* metaL = lv_label_create(m_content);
    lv_label_set_text(metaL, meta);
    lv_obj_set_style_text_color(metaL, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(metaL, &lv_font_montserrat_12, 0);

    // ID
    char idbuf[80]; snprintf(idbuf, sizeof(idbuf), "ID: %s", app.id.c_str());
    lv_obj_t* idL = lv_label_create(m_content);
    lv_label_set_text(idL, idbuf);
    lv_obj_set_style_text_color(idL, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(idL, &lv_font_montserrat_12, 0);

    // Install path
    lv_obj_t* pathL = lv_label_create(m_content);
    lv_label_set_text(pathL, app.installPath.c_str());
    lv_obj_set_style_text_color(pathL, lv_color_hex(0x4A5568), 0);
    lv_obj_set_style_text_font(pathL, &lv_font_montserrat_12, 0);

    // Launch button
    lv_obj_t* launchBtn = lv_btn_create(m_content);
    lv_obj_set_width(launchBtn, 308); lv_obj_set_height(launchBtn, 38);
    lv_obj_set_style_bg_color(launchBtn, lv_color_hex(app.accentColor), 0);
    lv_obj_set_style_radius(launchBtn, 10, 0);
    lv_obj_set_style_border_width(launchBtn, 0, 0);
    lv_obj_t* ll = lv_label_create(launchBtn);
    lv_label_set_text(ll, LV_SYMBOL_PLAY "  Launch App");
    lv_obj_set_style_text_font(ll, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ll, lv_color_hex(COL_BG), 0);
    lv_obj_center(ll);
    std::string appName = app.name;
    lv_obj_add_event_cb(launchBtn, [](lv_event_t*) {
        AppManager::getInstance().closeCurrentApp();
        // AppManager will find the registered SD app by name
    }, LV_EVENT_CLICKED, nullptr);

    // Divider
    lv_obj_t* div = lv_obj_create(m_content);
    lv_obj_set_width(div, 308); lv_obj_set_height(div, 1);
    lv_obj_set_style_bg_color(div, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_CLICKABLE);

    // Uninstall button
    lv_obj_t* unBtn = lv_btn_create(m_content);
    lv_obj_set_width(unBtn, 308); lv_obj_set_height(unBtn, 38);
    lv_obj_set_style_bg_color(unBtn, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_color(unBtn, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_border_width(unBtn, 1, 0);
    lv_obj_set_style_radius(unBtn, 10, 0);
    lv_obj_t* ul = lv_label_create(unBtn);
    lv_label_set_text(ul, LV_SYMBOL_TRASH "  Uninstall");
    lv_obj_set_style_text_font(ul, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ul, lv_color_hex(COL_RED), 0);
    lv_obj_center(ul);
    lv_obj_set_user_data(unBtn, new std::string(app.id));
    lv_obj_add_event_cb(unBtn, [](lv_event_t* e) {
        auto* id = (std::string*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
        AppStoreApp* s = (AppStoreApp*)AppManager::getInstance().getCurrentApp();
        if (s) s->beginUninstall(*id);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* hint = lv_label_create(m_content);
    lv_label_set_text(hint, "B = Back to Library");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Progress screen
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::showProgress(const char* appName) {
    lv_obj_clean(m_content); m_busy = true;

    lv_obj_t* card = lv_obj_create(m_content);
    lv_obj_set_width(card, 308); lv_obj_set_height(card, 140);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* ico = lv_spinner_create(card, 1000, 60);
    lv_obj_set_size(ico, 36, 36); lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_arc_color(ico, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ico, 3, LV_PART_INDICATOR);

    lv_obj_t* nameL = lv_label_create(card);
    lv_label_set_text(nameL, appName);
    lv_obj_set_style_text_color(nameL, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(nameL, &lv_font_montserrat_14, 0);
    lv_obj_align(nameL, LV_ALIGN_TOP_MID, 0, 46);

    m_progLbl = lv_label_create(card);
    lv_label_set_text(m_progLbl, "Preparing…");
    lv_obj_set_style_text_color(m_progLbl, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(m_progLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(m_progLbl, LV_ALIGN_TOP_MID, 0, 66);

    m_progBar = lv_bar_create(card);
    lv_obj_set_size(m_progBar, 276, 8);
    lv_obj_align(m_progBar, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_style_bg_color(m_progBar, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_bg_color(m_progBar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_progBar, 4, 0);
    lv_obj_set_style_radius(m_progBar, 4, LV_PART_INDICATOR);
    lv_bar_set_value(m_progBar, 0, LV_ANIM_OFF);
}

void AppStoreApp::updateProgressBar(int pct, const char* stage) {
    if (!UIEngine::getInstance().lock(100)) return;
    if (m_progBar) lv_bar_set_value(m_progBar, pct, LV_ANIM_ON);
    if (m_progLbl) lv_label_set_text(m_progLbl, stage);
    UIEngine::getInstance().unlock();
}

// ─────────────────────────────────────────────────────────────────────────────
// Install / Uninstall task launchers
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::beginInstall(const std::string& pkgPath) {
    IonAppHeader hdr = {};
    AppInstaller::getInstance().validate(pkgPath.c_str(), hdr);
    showProgress(hdr.app_name[0] ? hdr.app_name : "Installing…");

    auto* arg  = new InstallArg();
    arg->app   = this;
    strncpy(arg->path, pkgPath.c_str(), 255);

    xTaskCreate(installTask, "ion_install", 8192, arg, 4, nullptr);
}

void AppStoreApp::installTask(void* arg) {
    auto* a = (InstallArg*)arg;
    AppStoreApp* self = a->app;

    PackageStatus r = AppInstaller::getInstance().install(a->path,
        [self](const char* stage, int pct) {
            self->updateProgressBar(pct, stage);
        });

    IonAppHeader hdr = {}; AppInstaller::getInstance().validate(a->path, hdr);
    delete a;

    vTaskDelay(pdMS_TO_TICKS(400)); // Show 100% briefly

    if (UIEngine::getInstance().lock(200)) {
        self->m_busy = false;
        self->showResult(r, hdr.app_name);
        UIEngine::getInstance().unlock();
    }
    vTaskDelete(nullptr);
}

void AppStoreApp::beginUninstall(const std::string& appId) {
    std::string name = appId;
    for (auto& a : AppInstaller::getInstance().getInstalledApps())
        if (a.id == appId) { name = a.name; break; }

    showProgress(("Removing " + name + "…").c_str());
    auto* arg = new UninstallArg();
    arg->app  = this;
    strncpy(arg->id, appId.c_str(), 127);
    xTaskCreate(uninstallTask, "ion_uninstall", 4096, arg, 4, nullptr);
}

void AppStoreApp::uninstallTask(void* arg) {
    auto* a = (UninstallArg*)arg;
    AppStoreApp* self = a->app;
    self->updateProgressBar(50, "Removing files…");
    PackageStatus r = AppInstaller::getInstance().uninstall(a->id);
    self->updateProgressBar(100, "Done");
    std::string id = a->id;
    delete a;
    vTaskDelay(pdMS_TO_TICKS(300));
    if (UIEngine::getInstance().lock(200)) {
        self->m_busy = false;
        self->showResult(r, id.c_str());
        UIEngine::getInstance().unlock();
    }
    vTaskDelete(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Result screen (success / error) with auto-return
// ─────────────────────────────────────────────────────────────────────────────
void AppStoreApp::showResult(PackageStatus status, const char* appName) {
    bool ok = (status == PackageStatus::OK);
    lv_obj_clean(m_content);

    lv_obj_t* card = lv_obj_create(m_content);
    lv_obj_set_width(card, 308); lv_obj_set_height(card, 120);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(ok ? COL_GREEN : COL_RED), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* ico = lv_label_create(card);
    lv_label_set_text(ico, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico, lv_color_hex(ok ? COL_GREEN : COL_RED), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
    lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* t1 = lv_label_create(card);
    lv_label_set_text(t1, ok ? "Installation Complete!" : "Installation Failed");
    lv_obj_set_style_text_color(t1, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_14, 0);
    lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t* t2 = lv_label_create(card);
    lv_label_set_text(t2, ok ? appName : packageStatusStr(status));
    lv_obj_set_style_text_color(t2, lv_color_hex(ok ? COL_GREEN : COL_RED), 0);
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_12, 0);
    lv_obj_align(t2, LV_ALIGN_TOP_MID, 0, 64);

    lv_obj_t* t3 = lv_label_create(card);
    lv_label_set_text(t3, "Returning to library…");
    lv_obj_set_style_text_color(t3, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(t3, &lv_font_montserrat_12, 0);
    lv_obj_align(t3, LV_ALIGN_TOP_MID, 0, 82);

    // Show notification
    if (ok) {
        NotificationPopup::getInstance().show("App Store",
            (std::string(appName) + " installed!").c_str(), ION_NOTIF_SUCCESS, 4000);
        AudioManager::getInstance().playSystemSound("success");
    } else {
        NotificationPopup::getInstance().show("Install Failed",
            packageStatusStr(status), ION_NOTIF_ERROR, 4000);
        AudioManager::getInstance().playSystemSound("error");
    }

    // Auto-return after 2 seconds
    lv_timer_create([](lv_timer_t* t) {
        lv_timer_set_repeat_count(t, 1);
        AppStoreApp* self = (AppStoreApp*)AppManager::getInstance().getCurrentApp();
        if (self) { self->m_screen = StoreScreen::LIBRARY; self->buildTabBar(0); self->showLibrary(); }
    }, 2000, this);
}

void AppStoreApp::refreshContent() {
    AppInstaller::getInstance().scanInstalled();
    AppInstaller::getInstance().scanPackages();
    buildTabBar(m_screen == StoreScreen::AVAILABLE ? 1 : 0);
    if (m_screen == StoreScreen::AVAILABLE) showAvailable();
    else showLibrary();
}
