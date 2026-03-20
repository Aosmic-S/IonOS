#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce BadUSB — Ducky Script executor via ESP32-S3 USB HID               ║
// ║  Ported from Bruce firmware v1.14  (Apache 2.0)                          ║
// ║                                                                           ║
// ║  Ducky Script commands supported:                                         ║
// ║    STRING, DELAY, REM, ENTER, GUI, ALT, CTRL, SHIFT, TAB, ESCAPE,        ║
// ║    UPARROW, DOWNARROW, LEFTARROW, RIGHTARROW, HOME, END, PAGEUP,         ║
// ║    PAGEDOWN, DELETE, BACKSPACE, CAPSLOCK, F1-F12, PRINTSCREEN,            ║
// ║    SCROLLLOCK, PAUSE, CTRL-ALT-DELETE, GUI-R, ALT-F4, etc.               ║
// ║                                                                           ║
// ║  Requires: ESP32-S3 with USB port, idf.py → Component config → USB OTG   ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>

// USB HID keycodes
#define HID_KEY_ENTER        0x28
#define HID_KEY_ESCAPE       0x29
#define HID_KEY_BACKSPACE    0x2A
#define HID_KEY_TAB          0x2B
#define HID_KEY_SPACE        0x2C
#define HID_KEY_CAPSLOCK     0x39
#define HID_KEY_F1           0x3A
#define HID_KEY_F12          0x45
#define HID_KEY_PRINTSCREEN  0x46
#define HID_KEY_SCROLLLOCK   0x47
#define HID_KEY_PAUSE        0x48
#define HID_KEY_INSERT       0x49
#define HID_KEY_HOME         0x4A
#define HID_KEY_PAGEUP       0x4B
#define HID_KEY_DELETE       0x4C
#define HID_KEY_END          0x4D
#define HID_KEY_PAGEDOWN     0x4E
#define HID_KEY_RIGHT        0x4F
#define HID_KEY_LEFT         0x50
#define HID_KEY_DOWN         0x51
#define HID_KEY_UP           0x52
#define HID_KEY_GUI          0xE3   // Windows/Mac key (left)
#define HID_KEY_ALT          0xE2   // Left Alt  (modifier bit)
#define HID_KEY_CTRL         0xE0   // Left Ctrl (modifier bit)
#define HID_KEY_SHIFT        0xE1   // Left Shift (modifier bit)
#define HID_MOD_LCTRL        (1<<0)
#define HID_MOD_LSHIFT       (1<<1)
#define HID_MOD_LALT         (1<<2)
#define HID_MOD_LGUI         (1<<3)
#define HID_MOD_RCTRL        (1<<4)
#define HID_MOD_RSHIFT       (1<<5)
#define HID_MOD_RALT         (1<<6)
#define HID_MOD_RGUI         (1<<7)

using DuckyProgressCb = std::function<void(int lineNum, const std::string& cmd)>;

// ══════════════════════════════════════════════════════════════════════════
class BadUSB {
public:
    BadUSB() = default;
    ~BadUSB() { deinit(); }

    // Init USB HID device descriptor
    bool init();
    void deinit();
    bool isReady() const { return m_ready; }

    // ── Execute a Ducky Script file from SD ───────────────────────────────
    bool runFile(const std::string& path,
                 DuckyProgressCb progress = nullptr,
                 volatile bool* stopFlag  = nullptr);

    // ── Execute Ducky Script from a string ───────────────────────────────
    bool runScript(const std::string& script,
                   DuckyProgressCb progress = nullptr,
                   volatile bool* stopFlag  = nullptr);

    // ── Type a raw string (no Ducky parsing) ──────────────────────────────
    bool typeString(const std::string& text, int delayMs = 5);

    // ── Single key press (with optional modifiers) ────────────────────────
    bool pressKey(uint8_t keycode, uint8_t modifiers = 0);
    bool releaseAll();

    // ── Named key press (from Ducky keyword) ─────────────────────────────
    bool pressNamed(const std::string& name, uint8_t extraMod = 0);

    // ── Default inter-key delay (ms) ─────────────────────────────────────
    void setDefaultDelay(int ms) { m_defaultDelay = ms; }
    int  getDefaultDelay() const { return m_defaultDelay; }

private:
    bool    m_ready       = false;
    int     m_defaultDelay= 5;     // ms between keystrokes

    // USB HID report descriptor buffer
    struct HidReport {
        uint8_t modifiers;
        uint8_t reserved;
        uint8_t keys[6];
    };

    bool    sendReport(const HidReport& r);
    bool    sendEmpty();

    // Ducky script line parser
    bool    parseLine(const std::string& line,
                      DuckyProgressCb& cb,
                      volatile bool* stop,
                      int& defaultDelay,
                      int lineNum);

    // ASCII char → HID keycode + shift flag
    struct KeyInfo { uint8_t code; bool shift; };
    static KeyInfo charToHid(char c);

    // Keyword → keycode table
    static uint8_t namedKey(const char* name, uint8_t& mods);

    // Trim whitespace
    static std::string trim(const std::string& s);
    static std::string toUpper(const std::string& s);
};
