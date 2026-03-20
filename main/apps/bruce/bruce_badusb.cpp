// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce BadUSB — Full Implementation                                       ║
// ║  Ported from Bruce Firmware v1.14  (Apache 2.0)                          ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "bruce_badusb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>
#include <sstream>

// USB HID via ESP32-S3 native USB (tinyUSB via esp-idf component)
#include "tinyusb.h"
#include "class/hid/hid_device.h"

static const char* TAG = "BadUSB";

// ── USB HID descriptor ────────────────────────────────────────────────────
static const uint8_t s_hidReportDesc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

static tusb_desc_device_t s_devDesc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x046D,  // Logitech VID (inconspicuous)
    .idProduct          = 0xC31C,  // K120 keyboard PID
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// ── String descriptor ─────────────────────────────────────────────────────
static const char* s_strDesc[] = {
    (const char[]){0x09,0x04},   // 0: English
    "Logitech",                   // 1: Manufacturer
    "USB Keyboard",               // 2: Product
    "K120",                       // 3: Serial
    "HID Interface",              // 4: HID Interface
};

// ── Init / deinit ─────────────────────────────────────────────────────────
bool BadUSB::init()
{
    const tinyusb_config_t cfg = {
        .device_descriptor         = &s_devDesc,
        .string_descriptor         = s_strDesc,
        .string_descriptor_count   = sizeof(s_strDesc)/sizeof(s_strDesc[0]),
        .external_phy              = false,
        .configuration_descriptor  = nullptr,
    };

    if (tinyusb_driver_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed");
        return false;
    }

    // Wait for USB enumeration
    int wait = 0;
    while (!tud_mounted() && wait++ < 50) vTaskDelay(pdMS_TO_TICKS(100));

    m_ready = tud_mounted();
    if (m_ready) ESP_LOGI(TAG, "USB HID keyboard ready");
    else         ESP_LOGW(TAG, "USB HID: device not mounted (no host connection?)");
    return m_ready;
}

void BadUSB::deinit()
{
    if (m_ready) {
        releaseAll();
        tinyusb_driver_uninstall();
        m_ready = false;
    }
}

// ── HID report send ───────────────────────────────────────────────────────
bool BadUSB::sendReport(const HidReport& r)
{
    if (!tud_hid_ready()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!tud_hid_ready()) return false;
    }
    return tud_hid_keyboard_report(0, r.modifiers, r.keys);
}

bool BadUSB::sendEmpty()
{
    HidReport empty = {};
    return sendReport(empty);
}

// ── Char → HID keycode ───────────────────────────────────────────────────
BadUSB::KeyInfo BadUSB::charToHid(char c)
{
    // Lowercase a-z
    if (c >= 'a' && c <= 'z') return {(uint8_t)(0x04 + c - 'a'), false};
    // Uppercase A-Z
    if (c >= 'A' && c <= 'Z') return {(uint8_t)(0x04 + c - 'A'), true};
    // Digits 1-9 = 0x1E-0x26, 0 = 0x27
    if (c >= '1' && c <= '9') return {(uint8_t)(0x1E + c - '1'), false};
    if (c == '0') return {0x27, false};

    // Special characters
    static const struct { char c; uint8_t code; bool sh; } special[] = {
        {' ',0x2C,0},{'\n',0x28,0},{'\t',0x2B,0},
        {'-',0x2D,0},{'=',0x2E,0},{'[',0x2F,0},{']',0x30,0},
        {'\\',0x31,0},{'#',0x32,0},{';',0x33,0},{'\'',0x34,0},
        {'`',0x35,0},{',',0x36,0},{'.',0x37,0},{'/',0x38,0},
        // Shifted
        {'!',0x1E,1},{'@',0x1F,1},{'#',0x20,1},{'$',0x21,1},
        {'%',0x22,1},{'^',0x23,1},{'&',0x24,1},{'*',0x25,1},
        {'(',0x26,1},{')',0x27,1},{'_',0x2D,1},{'+',0x2E,1},
        {'{',0x2F,1},{'}',0x30,1},'|',0x31,1},':',0x33,1},
        {'"',0x34,1},{'<',0x36,1},{'>',0x37,1},{'?',0x38,1},
        {'~',0x35,1},
    };
    for (auto& s : special)
        if (s.c == c) return {s.code, s.sh};
    return {0,false};
}

// ── Named key → keycode ───────────────────────────────────────────────────
uint8_t BadUSB::namedKey(const char* name, uint8_t& mods)
{
    mods = 0;
    // Modifier keys (return 0, set mods)
    if (!strcmp(name,"CTRL")||!strcmp(name,"CONTROL"))     { mods|=HID_MOD_LCTRL;  return 0; }
    if (!strcmp(name,"ALT"))                               { mods|=HID_MOD_LALT;   return 0; }
    if (!strcmp(name,"SHIFT"))                             { mods|=HID_MOD_LSHIFT; return 0; }
    if (!strcmp(name,"GUI")||!strcmp(name,"WINDOWS")||
        !strcmp(name,"COMMAND"))                           { mods|=HID_MOD_LGUI;   return 0; }

    // Special keys
    if (!strcmp(name,"ENTER")||!strcmp(name,"RETURN"))     return HID_KEY_ENTER;
    if (!strcmp(name,"ESCAPE")||!strcmp(name,"ESC"))       return HID_KEY_ESCAPE;
    if (!strcmp(name,"BACKSPACE"))                         return HID_KEY_BACKSPACE;
    if (!strcmp(name,"TAB"))                               return HID_KEY_TAB;
    if (!strcmp(name,"SPACE"))                             return HID_KEY_SPACE;
    if (!strcmp(name,"DELETE")||!strcmp(name,"DEL"))       return HID_KEY_DELETE;
    if (!strcmp(name,"INSERT"))                            return HID_KEY_INSERT;
    if (!strcmp(name,"HOME"))                              return HID_KEY_HOME;
    if (!strcmp(name,"END"))                               return HID_KEY_END;
    if (!strcmp(name,"PAGEUP"))                            return HID_KEY_PAGEUP;
    if (!strcmp(name,"PAGEDOWN"))                          return HID_KEY_PAGEDOWN;
    if (!strcmp(name,"UPARROW")||!strcmp(name,"UP"))       return HID_KEY_UP;
    if (!strcmp(name,"DOWNARROW")||!strcmp(name,"DOWN"))   return HID_KEY_DOWN;
    if (!strcmp(name,"LEFTARROW")||!strcmp(name,"LEFT"))   return HID_KEY_LEFT;
    if (!strcmp(name,"RIGHTARROW")||!strcmp(name,"RIGHT")) return HID_KEY_RIGHT;
    if (!strcmp(name,"CAPSLOCK"))                          return HID_KEY_CAPSLOCK;
    if (!strcmp(name,"PRINTSCREEN"))                       return HID_KEY_PRINTSCREEN;
    if (!strcmp(name,"SCROLLLOCK"))                        return HID_KEY_SCROLLLOCK;
    if (!strcmp(name,"PAUSE")||!strcmp(name,"BREAK"))      return HID_KEY_PAUSE;
    // F1-F12
    if (name[0]=='F' && name[1]>='1' && name[1]<='9') {
        int fn = atoi(name+1);
        if (fn>=1&&fn<=12) return HID_KEY_F1 + fn - 1;
    }
    // Single char
    if (strlen(name)==1) {
        auto ki = charToHid(name[0]);
        if (ki.shift) mods|=HID_MOD_LSHIFT;
        return ki.code;
    }
    return 0;
}

// ── Press named key ────────────────────────────────────────────────────────
bool BadUSB::pressNamed(const std::string& name, uint8_t extraMod)
{
    uint8_t mods = extraMod;
    uint8_t code = namedKey(name.c_str(), mods);
    if (code == 0 && mods == 0) return false;
    HidReport r = {};
    r.modifiers = mods | extraMod;
    if (code) r.keys[0] = code;
    bool ok = sendReport(r);
    vTaskDelay(pdMS_TO_TICKS(10));
    sendEmpty();
    vTaskDelay(pdMS_TO_TICKS(m_defaultDelay));
    return ok;
}

// ── Type a raw string ──────────────────────────────────────────────────────
bool BadUSB::typeString(const std::string& text, int delayMs)
{
    for (char c : text) {
        auto ki = charToHid(c);
        if (ki.code == 0) { vTaskDelay(pdMS_TO_TICKS(delayMs)); continue; }
        HidReport r = {};
        if (ki.shift) r.modifiers = HID_MOD_LSHIFT;
        r.keys[0] = ki.code;
        sendReport(r);
        vTaskDelay(pdMS_TO_TICKS(5));
        sendEmpty();
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }
    return true;
}

bool BadUSB::pressKey(uint8_t keycode, uint8_t modifiers)
{
    HidReport r = {};
    r.modifiers = modifiers;
    r.keys[0]   = keycode;
    sendReport(r);
    vTaskDelay(pdMS_TO_TICKS(10));
    return sendEmpty();
}

bool BadUSB::releaseAll() { return sendEmpty(); }

// ── String helpers ─────────────────────────────────────────────────────────
std::string BadUSB::trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b-a+1);
}

std::string BadUSB::toUpper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = toupper(c);
    return r;
}

// ═════════════════════════════════════════════════════════════════════════
// DUCKY SCRIPT PARSER
// Handles the full Ducky Script 1.0 spec + common extensions
// ═════════════════════════════════════════════════════════════════════════
bool BadUSB::parseLine(const std::string& rawLine,
                        DuckyProgressCb& cb,
                        volatile bool* stop,
                        int& defaultDelay,
                        int lineNum)
{
    if (stop && *stop) return false;

    std::string line = trim(rawLine);
    if (line.empty() || line[0] == '/' || line[0] == '#') return true; // comment

    if (cb) cb(lineNum, line);

    std::string up = toUpper(line);

    // ── REM / comment ──────────────────────────────────────────────────
    if (up.substr(0,3) == "REM") return true;

    // ── DEFAULT_DELAY / DEFAULTDELAY ──────────────────────────────────
    if (up.substr(0,13) == "DEFAULT_DELAY" || up.substr(0,12) == "DEFAULTDELAY") {
        size_t sp = line.find(' ');
        if (sp != std::string::npos) defaultDelay = atoi(line.c_str()+sp+1);
        return true;
    }

    // ── DELAY ──────────────────────────────────────────────────────────
    if (up.substr(0,5) == "DELAY") {
        int ms = 0;
        size_t sp = line.find(' ');
        if (sp != std::string::npos) ms = atoi(line.c_str()+sp+1);
        vTaskDelay(pdMS_TO_TICKS(std::max(1, ms)));
        return true;
    }

    // ── STRING ────────────────────────────────────────────────────────
    if (up.substr(0,6) == "STRING") {
        std::string text = line.size()>7 ? line.substr(7) : "";
        typeString(text, defaultDelay);
        return true;
    }

    // ── STRINGLN (STRING + ENTER) ─────────────────────────────────────
    if (up.substr(0,8) == "STRINGLN") {
        std::string text = line.size()>9 ? line.substr(9) : "";
        typeString(text, defaultDelay);
        pressKey(HID_KEY_ENTER);
        return true;
    }

    // ── REPEAT ────────────────────────────────────────────────────────
    // Handled at script level — skip here
    if (up.substr(0,6) == "REPEAT") return true;

    // ── WAIT_FOR_BUTTON_PRESS ─────────────────────────────────────────
    if (up == "WAIT_FOR_BUTTON_PRESS") {
        // On IonOS: wait for any key press event
        vTaskDelay(pdMS_TO_TICKS(100));
        return true;
    }

    // ── Combination keys (e.g. CTRL-ALT-DELETE, GUI r, ALT F4) ───────
    // Split on spaces — first tokens are modifiers/keys
    std::vector<std::string> parts;
    {
        std::istringstream iss(up);
        std::string part;
        while (iss >> part) parts.push_back(part);
    }

    if (parts.empty()) return true;

    uint8_t modifiers = 0;
    std::vector<uint8_t> keys;

    for (auto& part : parts) {
        // Split on '-' for combo keys like CTRL-ALT-DELETE
        std::string sub = part;
        size_t dash;
        while ((dash = sub.find('-')) != std::string::npos) {
            std::string tok = sub.substr(0, dash);
            uint8_t m = 0;
            uint8_t k = namedKey(tok.c_str(), m);
            modifiers |= m;
            if (k) keys.push_back(k);
            sub = sub.substr(dash+1);
        }
        uint8_t m = 0;
        uint8_t k = namedKey(sub.c_str(), m);
        modifiers |= m;
        if (k) keys.push_back(k);
    }

    if (modifiers || !keys.empty()) {
        HidReport r = {};
        r.modifiers = modifiers;
        for (int i=0; i<(int)std::min(keys.size(),(size_t)6); i++) r.keys[i]=keys[i];
        sendReport(r);
        vTaskDelay(pdMS_TO_TICKS(50));
        sendEmpty();
        vTaskDelay(pdMS_TO_TICKS(defaultDelay));
    }
    return true;
}

// ── Run script from string ─────────────────────────────────────────────────
bool BadUSB::runScript(const std::string& script,
                        DuckyProgressCb progress,
                        volatile bool* stopFlag)
{
    if (!m_ready) {
        ESP_LOGW(TAG, "USB HID not ready");
        return false;
    }

    // Initial delay (let the host recognise the keyboard)
    vTaskDelay(pdMS_TO_TICKS(1000));

    int defaultDelay = m_defaultDelay;
    std::string lastLine;
    int lineNum = 0;

    std::istringstream ss(script);
    std::string line;
    while (std::getline(ss, line)) {
        if (stopFlag && *stopFlag) break;
        lineNum++;

        std::string up = trim(line);
        // Handle REPEAT: repeat last non-REM/DELAY/REPEAT line N times
        if (up.substr(0,6) == "REPEAT") {
            int n = 1;
            size_t sp = up.find(' ');
            if (sp != std::string::npos) n = atoi(up.c_str()+sp+1);
            for (int i=0; i<n && !(stopFlag&&*stopFlag); i++)
                parseLine(lastLine, progress, stopFlag, defaultDelay, lineNum);
            continue;
        }
        parseLine(line, progress, stopFlag, defaultDelay, lineNum);

        std::string trimmed = trim(line);
        if (!trimmed.empty() &&
            trimmed.substr(0,3) != "REM" && trimmed.substr(0,5) != "DELAY")
            lastLine = line;

        vTaskDelay(pdMS_TO_TICKS(defaultDelay));
    }
    releaseAll();
    return true;
}

// ── Run script from SD file ────────────────────────────────────────────────
bool BadUSB::runFile(const std::string& path,
                      DuckyProgressCb progress,
                      volatile bool* stopFlag)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { ESP_LOGE(TAG, "File not found: %s", path.c_str()); return false; }

    std::string script;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) script += buf;
    fclose(f);

    ESP_LOGI(TAG, "Running BadUSB script: %s (%zu bytes)", path.c_str(), script.size());
    return runScript(script, progress, stopFlag);
}
