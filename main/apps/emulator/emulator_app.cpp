// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  IonOS Emulator — Landscape 320×240 / 2MB PSRAM edition                 ║
// ║  Apache License 2.0 — Copyright 2024 IonOS Contributors                  ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "emulator_app.h"
#include "ui/ui_engine.h"
#include "ui/notification_popup.h"
#include "services/audio_manager.h"
#include "drivers/storage/sd_driver.h"
#include "drivers/rgb/ws2812_driver.h"
#include "kernel/kernel.h"
#include "config/ion_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <string.h>
#include <algorithm>

static const char* TAG = "EmuApp";

#define COL_BG      0x000000
#define COL_SURFACE 0x131929
#define COL_ACCENT  0xFF9F00
#define COL_TEXT    0xEEF2FF
#define COL_DIM     0x8899BB
#define COL_RED     0xFF3366
#define COL_GREEN   0x00FF9F

// IonOS X→GB A, IonOS A→GB B (X is primary select in IonOS)
static uint8_t ionToGB(ion_key_t k) {
    switch (k) {
        case ION_KEY_X:     return JOYPAD_A;
        case ION_KEY_A:     return JOYPAD_B;
        case ION_KEY_UP:    return JOYPAD_UP;
        case ION_KEY_DOWN:  return JOYPAD_DOWN;
        case ION_KEY_LEFT:  return JOYPAD_LEFT;
        case ION_KEY_RIGHT: return JOYPAD_RIGHT;
        case ION_KEY_START: return JOYPAD_START;
        case ION_KEY_MENU:  return JOYPAD_SELECT;
        default:            return 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void EmulatorApp::onCreate()
{
    gbcore_create(&m_core);
    if (!m_core.initialized) {
        NotificationPopup::getInstance().show("Emulator",
            "Not enough PSRAM", ION_NOTIF_ERROR, 3000);
        AppManager::getInstance().closeCurrentApp();
        return;
    }
    SDDriver::getInstance().ensureDir(SAVES_DIR);
    buildScreen("Game Boy");
    showRomSelect();
}

void EmulatorApp::onResume()
{
    if (m_paused && m_screen == EmuScreen::RUNNING) resumeGame();
}

void EmulatorApp::onPause()
{
    if (m_running) m_paused = true;
}

void EmulatorApp::onDestroy()
{
    stopEmuTask();
    if (m_core.romLoaded)
        gbcore_save(&m_core, m_currentRom.savePath.c_str());
    gbcore_destroy(&m_core);

    if (m_fpsTimer)  { lv_timer_del(m_fpsTimer);  m_fpsTimer  = nullptr; }
    if (m_drawTimer) { lv_timer_del(m_drawTimer); m_drawTimer = nullptr; }
    if (m_gameScreen){ lv_obj_del(m_gameScreen); m_gameScreen = nullptr; }
    if (m_listScreen){ lv_obj_del(m_listScreen); m_listScreen = nullptr; }
    WS2812Driver::getInstance().setAnimation(LEDAnim::NONE);
}

// ─────────────────────────────────────────────────────────────────────────────
void EmulatorApp::onKey(ion_key_t k, bool pressed)
{
    if (m_screen == EmuScreen::ROM_SELECT) {
        if (pressed && k == ION_KEY_B) AppManager::getInstance().closeCurrentApp();
        return;
    }
    if (m_screen == EmuScreen::PAUSED) {
        if (pressed && (k == ION_KEY_START || k == ION_KEY_B)) resumeGame();
        return;
    }
    if (m_screen == EmuScreen::RUNNING) {
        if (pressed && k == ION_KEY_START) { showPauseMenu(); return; }
        if (pressed && k == ION_KEY_B)     { exitGame(true);  return; }
        uint8_t gb = ionToGB(k);
        if (gb) gbcore_set_button(&m_core, gb, pressed);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ROM list — landscape 320×240 layout
// ─────────────────────────────────────────────────────────────────────────────
void EmulatorApp::scanRoms()
{
    m_roms.clear();
    const char* dirs[] = { ROMS_DIR_GB, ROMS_DIR_GBC };
    const char* exts[] = { "gb", "gbc" };

    for (int d = 0; d < 2; d++) {
        SDDriver::getInstance().ensureDir(dirs[d]);
        DIR* dir = opendir(dirs[d]);
        if (!dir) continue;
        struct dirent* e;
        while ((e = readdir(dir))) {
            std::string n = e->d_name;
            if (n.size() < 4) continue;
            std::string ext = n.substr(n.rfind('.')+1);
            for (auto& c : ext) c = tolower(c);
            if (ext != exts[d]) continue;
            RomEntry rom;
            rom.path     = std::string(dirs[d]) + "/" + n;
            rom.name     = n.substr(0, n.rfind('.'));
            rom.savePath = makeSavePath(rom.path);
            FILE* sf = fopen(rom.savePath.c_str(), "rb");
            rom.hasSave  = (sf != nullptr);
            if (sf) fclose(sf);
            m_roms.push_back(rom);
        }
        closedir(dir);
    }
    std::sort(m_roms.begin(), m_roms.end(),
              [](const RomEntry& a, const RomEntry& b){ return a.name < b.name; });
}

std::string EmulatorApp::makeSavePath(const std::string& romPath)
{
    std::string base = romPath.substr(romPath.rfind('/')+1);
    base = base.substr(0, base.rfind('.'));
    return std::string(SAVES_DIR) + "/" + base + ".sav";
}

void EmulatorApp::showRomSelect()
{
    m_screen = EmuScreen::ROM_SELECT;
    stopEmuTask();
    if (m_gameScreen) { lv_obj_del(m_gameScreen); m_gameScreen = nullptr; }
    if (m_listScreen) { lv_obj_del(m_listScreen); m_listScreen = nullptr; }

    scanRoms();

    m_listScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(m_listScreen, SCR_W, SCR_H);
    lv_obj_set_pos(m_listScreen, 0, 0);
    lv_obj_set_style_bg_color(m_listScreen, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_width(m_listScreen, 0, 0);
    lv_obj_clear_flag(m_listScreen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Header — 320×36
    lv_obj_t* hdr = lv_obj_create(m_listScreen);
    lv_obj_set_size(hdr, SCR_W, 36);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0A0E1A), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_PLAY "  Game Boy");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    char cb[32];
    snprintf(cb, sizeof(cb), "%zu ROM%s  [X]=Launch  [B]=Back",
             m_roms.size(), m_roms.size() == 1 ? "" : "s");
    lv_obj_t* sub = lv_label_create(hdr);
    lv_label_set_text(sub, cb);
    lv_obj_set_style_text_color(sub, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_align(sub, LV_ALIGN_RIGHT_MID, -8, 0);

    // Scrollable ROM list — 320×204
    lv_obj_t* list = lv_obj_create(m_listScreen);
    lv_obj_set_size(list, SCR_W, SCR_H - 36);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_style_bg_color(list, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_gap(list, 3, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    if (m_roms.empty()) {
        lv_obj_t* empty = lv_obj_create(list);
        lv_obj_set_size(empty, 308, 80);
        lv_obj_set_style_bg_color(empty, lv_color_hex(0x0A0E1A), 0);
        lv_obj_set_style_border_width(empty, 0, 0);
        lv_obj_set_style_radius(empty, 8, 0);
        lv_obj_clear_flag(empty, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* t = lv_label_create(empty);
        lv_label_set_text(t, "No ROMs found — copy .gb/.gbc files to:\n"
                             "/sdcard/roms/gb/   or   /sdcard/roms/gbc/");
        lv_obj_set_style_text_color(t, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(t, 290);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    for (size_t i = 0; i < m_roms.size(); i++) {
        const RomEntry& rom = m_roms[i];

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_width(row, 308);
        lv_obj_set_height(row, 38);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x0A0E1A), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1E2D4A), LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(row, lv_color_hex(COL_ACCENT), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1E2D4A), LV_STATE_PRESSED);

        // Play icon
        lv_obj_t* ico = lv_label_create(row);
        lv_label_set_text(ico, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(ico, lv_color_hex(COL_ACCENT), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        // ROM name (clipped to fit 320 layout)
        lv_obj_t* nameL = lv_label_create(row);
        lv_label_set_text(nameL, rom.name.c_str());
        lv_obj_set_style_text_color(nameL, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(nameL, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(nameL, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(nameL, 220);
        lv_obj_align(nameL, LV_ALIGN_LEFT_MID, 22, 0);

        // Save badge (right side)
        if (rom.hasSave) {
            lv_obj_t* svbadge = lv_label_create(row);
            lv_label_set_text(svbadge, LV_SYMBOL_SAVE " SAV");
            lv_obj_set_style_text_color(svbadge, lv_color_hex(COL_GREEN), 0);
            lv_obj_set_style_text_font(svbadge, &lv_font_montserrat_12, 0);
            lv_obj_align(svbadge, LV_ALIGN_RIGHT_MID, 0, 0);
        }

        lv_obj_set_user_data(row, (void*)i);
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            size_t idx = (size_t)(intptr_t)lv_obj_get_user_data(
                            (lv_obj_t*)lv_event_get_target(e));
            EmulatorApp* self = (EmulatorApp*)AppManager::getInstance().getCurrentApp();
            if (self && idx < self->m_roms.size())
                self->launchRom(self->m_roms[idx]);
        }, LV_EVENT_CLICKED, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Launch
// ─────────────────────────────────────────────────────────────────────────────
void EmulatorApp::launchRom(const RomEntry& rom)
{
    ESP_LOGI(TAG, "Launch: %s", rom.path.c_str());

    // ROM size guard — streaming cache supports up to 4 MB
    FILE* rf = fopen(rom.path.c_str(), "rb");
    if (rf) {
        fseek(rf, 0, SEEK_END);
        size_t sz = ftell(rf);
        fclose(rf);
        if (sz > ROM_MAX) {
            char msg[64];
            snprintf(msg, sizeof(msg), "ROM too large: %zu KB (max %zu KB). Use ROMs ≤ 4 MB.",
                     sz/1024, ROM_MAX/1024);
            NotificationPopup::getInstance().show("Emulator", msg, ION_NOTIF_ERROR, 4000);
            return;
        }
    }

    if (!gbcore_load_rom(&m_core, rom.path.c_str())) {
        NotificationPopup::getInstance().show("Emulator",
            "Failed to load ROM", ION_NOTIF_ERROR, 3000);
        return;
    }
    gbcore_load_save(&m_core, rom.savePath.c_str());
    m_currentRom = rom;

    if (m_listScreen) { lv_obj_del(m_listScreen); m_listScreen = nullptr; }
    buildGameScreen();

    m_screen = EmuScreen::RUNNING;
    m_paused = false;
    startEmuTask();

    WS2812Driver::getInstance().setAnimation(LEDAnim::PULSE);
    NotificationPopup::getInstance().show(m_core.romTitle,
        rom.hasSave ? "Save loaded" : "New game", ION_NOTIF_INFO, 2000);
}

// ─────────────────────────────────────────────────────────────────────────────
// Game screen — landscape 320×240
// ─────────────────────────────────────────────────────────────────────────────
void EmulatorApp::setupFrameImg()
{
    // Wire the raw PSRAM framebuffer to an LVGL image descriptor
    // LVGL will read pixels directly from PSRAM — no canvas copy needed
    m_imgDsc.header.cf     = LV_IMG_CF_TRUE_COLOR;
    m_imgDsc.header.always_zero = 0;
    m_imgDsc.header.w      = LCD_WIDTH;   // 160
    m_imgDsc.header.h      = LCD_HEIGHT;  // 144
    m_imgDsc.data_size     = LCD_WIDTH * LCD_HEIGHT * LV_COLOR_SIZE / 8;
    m_imgDsc.data          = (const uint8_t*)m_core.framebuf;

    m_gbImg = lv_img_create(m_gameScreen);
    lv_img_set_src(m_gbImg, &m_imgDsc);
    lv_img_set_zoom(m_gbImg, GB_ZOOM);   // 384 = 1.5×
    lv_obj_set_pos(m_gbImg, IMG_X, IMG_Y);
    lv_obj_clear_flag(m_gbImg, LV_OBJ_FLAG_CLICKABLE);
}

void EmulatorApp::buildGameScreen()
{
    m_gameScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(m_gameScreen, SCR_W, SCR_H);
    lv_obj_set_pos(m_gameScreen, 0, 0);
    lv_obj_set_style_bg_color(m_gameScreen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_border_width(m_gameScreen, 0, 0);
    lv_obj_clear_flag(m_gameScreen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Status/HUD bar (320×20) at top
    lv_obj_t* hudBar = lv_obj_create(m_gameScreen);
    lv_obj_set_size(hudBar, SCR_W, STAT_H);
    lv_obj_set_pos(hudBar, 0, 0);
    lv_obj_set_style_bg_color(hudBar, lv_color_hex(0x050810), 0);
    lv_obj_set_style_border_width(hudBar, 0, 0);
    lv_obj_clear_flag(hudBar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* romL = lv_label_create(hudBar);
    lv_label_set_text(romL, m_core.romTitle);
    lv_obj_set_style_text_color(romL, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_text_font(romL, &lv_font_montserrat_12, 0);
    lv_obj_align(romL, LV_ALIGN_LEFT_MID, 6, 0);

    m_fpsLabel = lv_label_create(hudBar);
    lv_label_set_text(m_fpsLabel, "-- fps");
    lv_obj_set_style_text_color(m_fpsLabel, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(m_fpsLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(m_fpsLabel, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* hintL = lv_label_create(hudBar);
    lv_label_set_text(hintL, "START=Pause B=Exit");
    lv_obj_set_style_text_color(hintL, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(hintL, &lv_font_montserrat_12, 0);
    lv_obj_align(hintL, LV_ALIGN_RIGHT_MID, -6, 0);

    // Left and right letterbox bars (40px each side, dark)
    for (int side = 0; side < 2; side++) {
        lv_obj_t* lb = lv_obj_create(m_gameScreen);
        lv_obj_set_size(lb, IMG_X, GAME_H);
        lv_obj_set_pos(lb, side ? (IMG_X + SCALED_W) : 0, STAT_H);
        lv_obj_set_style_bg_color(lb, lv_color_hex(0x080808), 0);
        lv_obj_set_style_border_width(lb, 0, 0);
        lv_obj_clear_flag(lb, LV_OBJ_FLAG_CLICKABLE);
    }

    // Wire GB framebuffer to lv_img (PSRAM-direct, no canvas)
    setupFrameImg();

    // FPS update timer (1s interval, UI core)
    m_fpsTimer = lv_timer_create([](lv_timer_t*) {
        EmulatorApp* s = (EmulatorApp*)AppManager::getInstance().getCurrentApp();
        if (!s || !s->m_fpsLabel) return;
        // Show fps + cache miss count so we can see streaming is working
        // Format: "60 fps  0♦" (diamond = misses since last second)
        uint32_t fps     = gbcore_fps(&s->m_core);
        uint32_t misses  = gbcore_cache_misses(&s->m_core);
        static uint32_t lastMisses = 0;
        uint32_t newMisses = misses - lastMisses;
        lastMisses = misses;
        char buf[28];
        if (newMisses > 0)
            snprintf(buf, sizeof(buf), "%lu fps  %lu\xE2\x97\x86", // ◆
                     (unsigned long)fps, (unsigned long)newMisses);
        else
            snprintf(buf, sizeof(buf), "%lu fps", (unsigned long)fps);
        lv_label_set_text(s->m_fpsLabel, buf);
    }, 1000, nullptr);

    // Frame redraw timer — invalidate the img every 16ms
    // LVGL will then re-read m_core.framebuf during the next render pass
    m_drawTimer = lv_timer_create([](lv_timer_t*) {
        EmulatorApp* s = (EmulatorApp*)AppManager::getInstance().getCurrentApp();
        if (s && s->m_gbImg && !s->m_paused)
            lv_obj_invalidate(s->m_gbImg);
    }, 16, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Emulation task — Core 0, ~60fps, independent of LVGL
// ─────────────────────────────────────────────────────────────────────────────
void EmulatorApp::emuTask(void* arg)
{
    EmulatorApp* self = (EmulatorApp*)arg;
    const TickType_t FRAME_TICKS = pdMS_TO_TICKS(16); // ~60fps
    TickType_t wake = xTaskGetTickCount();

    while (self->m_running) {
        if (!self->m_paused)
            gbcore_run_frame(&self->m_core);
        vTaskDelayUntil(&wake, FRAME_TICKS);
    }
    vTaskDelete(nullptr);
}

void EmulatorApp::startEmuTask()
{
    m_running = true;
    xTaskCreatePinnedToCore(emuTask, "ion_emu", 8192, this, 5, &m_emuTask, 0);
}

void EmulatorApp::stopEmuTask()
{
    if (!m_emuTask) return;
    m_running = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    m_emuTask = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pause menu — compact for landscape 320×240
// ─────────────────────────────────────────────────────────────────────────────
void EmulatorApp::showPauseMenu()
{
    m_screen = EmuScreen::PAUSED;
    m_paused = true;

    if (m_pausePanel) { lv_obj_del(m_pausePanel); m_pausePanel = nullptr; }

    // Semi-transparent overlay
    m_pausePanel = lv_obj_create(m_gameScreen);
    lv_obj_set_size(m_pausePanel, 260, 180);
    lv_obj_align(m_pausePanel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(m_pausePanel, lv_color_hex(0x0A0E1A), 0);
    lv_obj_set_style_bg_opa(m_pausePanel, LV_OPA_90, 0);
    lv_obj_set_style_border_color(m_pausePanel, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_border_width(m_pausePanel, 1, 0);
    lv_obj_set_style_radius(m_pausePanel, 12, 0);
    lv_obj_set_style_pad_all(m_pausePanel, 10, 0);
    lv_obj_set_flex_flow(m_pausePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(m_pausePanel, 8, 0);
    lv_obj_clear_flag(m_pausePanel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(m_pausePanel);
    lv_label_set_text(title, "⏸  Paused");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_width(title, 238);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* romL = lv_label_create(m_pausePanel);
    lv_label_set_text(romL, m_core.romTitle);
    lv_obj_set_style_text_color(romL, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(romL, &lv_font_montserrat_12, 0);
    lv_obj_set_width(romL, 238);
    lv_obj_set_style_text_align(romL, LV_TEXT_ALIGN_CENTER, 0);

    auto btn = [&](const char* lbl, uint32_t col, lv_event_cb_t cb) {
        lv_obj_t* b = lv_btn_create(m_pausePanel);
        lv_obj_set_width(b, 238); lv_obj_set_height(b, 34);
        lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, lbl);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x0A0E1A), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    };

    btn(LV_SYMBOL_PLAY "  Resume", COL_ACCENT, [](lv_event_t*) {
        auto* s = (EmulatorApp*)AppManager::getInstance().getCurrentApp();
        if (s) s->resumeGame();
    });
    btn(LV_SYMBOL_SAVE "  Save & Exit", COL_GREEN, [](lv_event_t*) {
        auto* s = (EmulatorApp*)AppManager::getInstance().getCurrentApp();
        if (s) s->exitGame(true);
    });
    btn(LV_SYMBOL_CLOSE "  Exit (no save)", COL_RED, [](lv_event_t*) {
        auto* s = (EmulatorApp*)AppManager::getInstance().getCurrentApp();
        if (s) s->exitGame(false);
    });
}

void EmulatorApp::resumeGame()
{
    if (m_pausePanel) { lv_obj_del(m_pausePanel); m_pausePanel = nullptr; }
    m_screen = EmuScreen::RUNNING;
    m_paused = false;
}

void EmulatorApp::exitGame(bool save)
{
    stopEmuTask();
    if (save && m_core.romLoaded) {
        bool ok = gbcore_save(&m_core, m_currentRom.savePath.c_str());
        NotificationPopup::getInstance().show("Game Boy",
            ok ? "Game saved" : "Save failed",
            ok ? ION_NOTIF_SUCCESS : ION_NOTIF_WARNING, 2500);
    }
    if (m_fpsTimer)  { lv_timer_del(m_fpsTimer);  m_fpsTimer  = nullptr; }
    if (m_drawTimer) { lv_timer_del(m_drawTimer); m_drawTimer = nullptr; }
    if (m_gameScreen){ lv_obj_del(m_gameScreen); m_gameScreen = nullptr; }
    gbcore_unload(&m_core);
    WS2812Driver::getInstance().setAnimation(LEDAnim::NONE);
    showRomSelect();
}
