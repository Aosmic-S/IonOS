#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║                 IonOS System Configuration                       ║
// ╚══════════════════════════════════════════════════════════════════╝
#include <stdint.h>

// ── Version ───────────────────────────────────────────────────────
#define IONOS_VERSION_MAJOR  1
#define IONOS_VERSION_MINOR  0
#define IONOS_VERSION_PATCH  0
#define IONOS_VERSION_STR    "1.0.0"
#define IONOS_BUILD_DATE     __DATE__

// ── Color Palette (RGB888 hex) ────────────────────────────────────
#define ION_COLOR_BG         0x0A0E1A
#define ION_COLOR_SURFACE    0x131929
#define ION_COLOR_SURFACE2   0x1A2236
#define ION_COLOR_ACCENT     0x00D4FF
#define ION_COLOR_ACCENT2    0x7B2FFF
#define ION_COLOR_SUCCESS    0x00FF9F
#define ION_COLOR_WARNING    0xFFB800
#define ION_COLOR_ERROR      0xFF3366
#define ION_COLOR_TEXT       0xEEF2FF
#define ION_COLOR_TEXT_DIM   0x8899BB
#define ION_COLOR_BORDER     0x1E2D4A

// ── Task Priorities ───────────────────────────────────────────────
#define PRIORITY_IDLE        0
#define PRIORITY_LOW         2
#define PRIORITY_NORMAL      5
#define PRIORITY_HIGH        8
#define PRIORITY_REALTIME   12

// ── Task Stack Sizes ──────────────────────────────────────────────
#define STACK_UI            8192
#define STACK_AUDIO         4096
#define STACK_INPUT         2048
#define STACK_LED           2048
#define STACK_POWER         2048
#define STACK_NETWORK       6144
#define STACK_APP          16384

// ── Core Assignments ──────────────────────────────────────────────
#define CORE_EVENT          0
#define CORE_UI             1
#define CORE_AUDIO          0
#define CORE_INPUT          0

// ── Event Types ───────────────────────────────────────────────────
typedef enum {
    ION_EVENT_NONE           = 0,
    ION_EVENT_KEY_DOWN       = 1,
    ION_EVENT_KEY_UP         = 2,
    ION_EVENT_KEY_LONG       = 3,
    ION_EVENT_WIFI_CONNECTED = 10,
    ION_EVENT_WIFI_DISCONNECTED = 11,
    ION_EVENT_WIFI_SCAN_DONE = 12,
    ION_EVENT_BATTERY_LOW    = 20,
    ION_EVENT_BATTERY_CRITICAL=21,
    ION_EVENT_BATTERY_CHARGING=22,
    ION_EVENT_AUDIO_DONE     = 30,
    ION_EVENT_APP_LAUNCH     = 40,
    ION_EVENT_APP_CLOSE      = 41,
    ION_EVENT_THEME_CHANGE   = 50,
    ION_EVENT_SYSTEM_SLEEP   = 60,
    ION_EVENT_SYSTEM_WAKE    = 61,
    ION_EVENT__MAX
} ion_event_type_t;

// ── Key Codes ─────────────────────────────────────────────────────
typedef enum {
    ION_KEY_NONE  = 0,
    ION_KEY_UP,
    ION_KEY_DOWN,
    ION_KEY_LEFT,
    ION_KEY_RIGHT,
    ION_KEY_X,
    ION_KEY_B,
    ION_KEY_A,
    ION_KEY_START,
    ION_KEY_MENU,
    ION_KEY__MAX
} ion_key_t;

// ── Notification Levels ───────────────────────────────────────────
typedef enum {
    ION_NOTIF_INFO    = 0,
    ION_NOTIF_SUCCESS = 1,
    ION_NOTIF_WARNING = 2,
    ION_NOTIF_ERROR   = 3,
} ion_notif_level_t;

// ── Event Struct ──────────────────────────────────────────────────
typedef struct {
    ion_event_type_t type;
    uint32_t         data;
    void*            ptr;
} ion_event_t;

// ── App States ────────────────────────────────────────────────────
typedef enum {
    APP_STATE_IDLE,
    APP_STATE_RUNNING,
    APP_STATE_PAUSED,
    APP_STATE_DESTROYED,
} ion_app_state_t;
