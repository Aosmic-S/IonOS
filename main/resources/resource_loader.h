#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║              IonOS Resource Loader                               ║
// ║  Central access point for all embedded assets.                   ║
// ║  Icons → LVGL img_dsc  |  Sounds → PCM16  |  Frames → LVGL     ║
// ╚══════════════════════════════════════════════════════════════════╝
#include "lvgl/lvgl.h"
#include "resources/generated/ion_icons.h"
#include "resources/generated/ion_sounds.h"
#include "resources/generated/ion_boot_frames.h"
#include "resources/generated/ion_font_7x10.h"
#include <stdint.h>

// ── Semantic icon names → ion_icon_id_t ──────────────────────────
// Matches order in gen_assets.py ICONS list
#define RICON_SETTINGS   ION_ICON_SETTINGS
#define RICON_WIFI       ION_ICON_WIFI
#define RICON_MUSIC      ION_ICON_MUSIC
#define RICON_FILES      ION_ICON_FILES
#define RICON_BROWSER    ION_ICON_BROWSER
#define RICON_CHATBOT    ION_ICON_CHATBOT
#define RICON_EMULATOR   ION_ICON_EMULATOR
#define RICON_POWER      ION_ICON_POWER
#define RICON_BATTERY    ION_ICON_BATTERY
#define RICON_VOLUME     ION_ICON_VOLUME
#define RICON_PLAY       ION_ICON_PLAY
#define RICON_PAUSE      ION_ICON_PAUSE
#define RICON_NEXT       ION_ICON_NEXT
#define RICON_PREV       ION_ICON_PREV
#define RICON_FOLDER     ION_ICON_FOLDER
#define RICON_HOME       ION_ICON_HOME
#define RICON_BACK       ION_ICON_BACK
#define RICON_ADD        ION_ICON_ADD
#define RICON_DELETE     ION_ICON_DELETE
#define RICON_SEARCH     ION_ICON_SEARCH
#define RICON_LOCK       ION_ICON_LOCK
#define RICON_STAR       ION_ICON_STAR
#define RICON_CLOUD      ION_ICON_CLOUD
#define RICON_RADIO      ION_ICON_RADIO
#define RICON_CONTROLLER ION_ICON_CONTROLLER
#define RICON_HEART      ION_ICON_HEART
#define RICON_MAP        ION_ICON_MAP
#define RICON_DOWNLOAD   ION_ICON_DOWNLOAD
#define RICON_INFO       ION_ICON_INFO
#define RICON_CHECK      ION_ICON_CHECK
#define RICON_WARNING    ION_ICON_WARNING_ICO
#define RICON_CLOCK      ION_ICON_CLOCK
#define RICON_EDIT       ION_ICON_EDIT

class ResourceLoader {
public:
    static ResourceLoader& getInstance();
    void init();

    // ── Icons ────────────────────────────────────────────────────
    const lv_img_dsc_t* icon(ion_icon_id_t id);
    lv_obj_t* makeIconImg(lv_obj_t* parent, ion_icon_id_t id);

    // ── Sounds ───────────────────────────────────────────────────
    // Returns pointer to ion_sound_t for AudioDriver::play()
    const ion_sound_t* sound(const char* name);   // "click","notification","error","boot","success"

    // ── Boot animation ───────────────────────────────────────────
    const lv_img_dsc_t* bootFrame(int idx);
    int bootFrameCount()   const { return ION_BOOT_FRAME_COUNT; }
    int bootFrameDelayMs() const { return ION_BOOT_FRAME_MS;    }

    // ── Font ──────────────────────────────────────────────────────
    // Returns row bitmap for the 7×10 custom font
    const uint8_t* fontGlyph(char c) { return ion_font_glyph(c); }

    // ── Memory report ────────────────────────────────────────────
    void logSizes() const;

private:
    ResourceLoader() = default;
    bool m_inited = false;
};
