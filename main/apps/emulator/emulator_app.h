#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  IonOS Emulator App — Landscape 320×240 edition                          ║
// ║                                                                          ║
// ║  Display layout (320×240 landscape):                                     ║
// ║  ┌────────────────────────────────┐ ─ 0                                 ║
// ║  │  Status bar  320×20            │                                     ║
// ║  ├──────────────────────────────── ─ 20                                 ║
// ║  │  ◄40►│  GB Screen  240×216  │◄40►│  GB 160×144 scaled 1.5×          ║
// ║  │       │  centred in 320×220  │    │                                  ║
// ║  └───────────────────────────────── ─ 240                               ║
// ║                                                                          ║
// ║  2MB PSRAM budget:                                                       ║
// ║    struct gb_s   ~17 KB  (WRAM/VRAM/OAM/HRAM embedded)                  ║
// ║    ROM data      max 1 MB  (most GB/GBC games ≤512 KB)                  ║
// ║    Cart save RAM max 32 KB                                               ║
// ║    Framebuf      160×144×2 = 45 KB  (raw RGB565, NOT canvas)            ║
// ║    Total emulator PSRAM: ~1094 KB worst case                             ║
// ║                                                                          ║
// ║  Rendering: framebuf → lv_img with zoom=384 (1.5×) via LVGL             ║
// ║  No separate canvas buffer — LVGL reads directly from framebuf.         ║
// ║                                                                          ║
// ║  Button map (IonOS → Game Boy):                                          ║
// ║    X=GB_A  A=GB_B  UP/DN/LT/RT=D-pad  START=GB_Start  MENU=GB_Select   ║
// ║    B = Exit (saves game)                                                 ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "apps/app_manager.h"
#include "core/peanut_gb_core.h"
#include <string>
#include <vector>

struct RomEntry {
    std::string name;
    std::string path;
    std::string savePath;
    bool        hasSave;
};

enum class EmuScreen { ROM_SELECT, RUNNING, PAUSED };

class EmulatorApp : public IonApp {
public:
    void onCreate()  override;
    void onResume()  override;
    void onPause()   override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;

private:
    void showRomSelect();
    void launchRom(const RomEntry& rom);
    void buildGameScreen();
    void showPauseMenu();
    void resumeGame();
    void exitGame(bool save);

    static void emuTask(void* arg);
    void startEmuTask();
    void stopEmuTask();

    void setupFrameImg();   // Wire framebuf to lv_img (no canvas copy)
    void refreshFrameImg(); // lv_obj_invalidate to trigger redraw

    void scanRoms();
    std::string makeSavePath(const std::string& romPath);

    // ── State ──────────────────────────────────────────────────────────────
    EmuScreen    m_screen    = EmuScreen::ROM_SELECT;
    GBCore       m_core;
    RomEntry     m_currentRom;
    std::vector<RomEntry> m_roms;

    // Game screen UI (created fresh on each launch)
    lv_obj_t*    m_gameScreen = nullptr;
    lv_obj_t*    m_gbImg      = nullptr;   // lv_img pointing at framebuf
    lv_img_dsc_t m_imgDsc     = {};        // descriptor for framebuf
    lv_obj_t*    m_fpsLabel   = nullptr;
    lv_obj_t*    m_pausePanel = nullptr;

    lv_obj_t*    m_listScreen = nullptr;
    lv_timer_t*  m_fpsTimer   = nullptr;
    lv_timer_t*  m_drawTimer  = nullptr;

    TaskHandle_t m_emuTask   = nullptr;
    volatile bool m_running  = false;
    volatile bool m_paused   = false;

    // Landscape game screen metrics
    // GB 160×144 × 1.5 = 240×216, centred in 320×220 (below 20px status bar)
    static constexpr int SCR_W    = 320;
    static constexpr int SCR_H    = 240;
    static constexpr int STAT_H   = 20;
    static constexpr int GAME_H   = SCR_H - STAT_H;   // 220
    static constexpr int GB_ZOOM  = 384;  // LV_IMG_ZOOM: 256=1× 384=1.5×
    static constexpr int SCALED_W = 240;  // 160 × 1.5
    static constexpr int SCALED_H = 216;  // 144 × 1.5
    static constexpr int IMG_X    = (SCR_W - SCALED_W) / 2;  // 40
    static constexpr int IMG_Y    = STAT_H + (GAME_H - SCALED_H) / 2; // 22

    static constexpr size_t ROM_MAX = ROM_MAX_SIZE;  // 4 MB — paged SD streaming cache
    static constexpr const char* ROMS_DIR_GB  = "/sdcard/roms/gb";
    static constexpr const char* ROMS_DIR_GBC = "/sdcard/roms/gbc";
    static constexpr const char* SAVES_DIR    = "/sdcard/saves";
};
