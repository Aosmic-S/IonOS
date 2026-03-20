#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║               IonOS App Package Format — .ionapp                         ║
// ║                                                                          ║
// ║  An .ionapp file is a binary package on the SD card:                    ║
// ║                                                                          ║
// ║  [HEADER 256 bytes] [ICON 32×32 RGB565 2048 bytes] [CODE binary]        ║
// ║                                                                          ║
// ║  SD layout:                                                              ║
// ║    /sdcard/apps/MyApp.ionapp       ← installable package                ║
// ║    /sdcard/installed/MyApp/        ← installed app directory             ║
// ║      manifest.json                 ← extracted metadata                 ║
// ║      icon.bin                      ← extracted 32×32 RGB565 icon        ║
// ║      app.bin                       ← extracted code binary               ║
// ║      data/                         ← app private data directory          ║
// ║                                                                          ║
// ║  manifest.json schema:                                                   ║
// ║  {                                                                       ║
// ║    "id":       "com.example.myapp",   unique reverse-domain ID           ║
// ║    "name":     "My App",              display name (≤24 chars)           ║
// ║    "version":  "1.2.0",               semver string                     ║
// ║    "author":   "Your Name",                                              ║
// ║    "desc":     "What this app does",  ≤128 chars                         ║
// ║    "icon_color": 16711680,            accent color (uint32 hex)          ║
// ║    "min_ionos": "1.0.0",              minimum IonOS version required      ║
// ║    "size_kb":  42,                    uncompressed code size             ║
// ║    "perms":    ["wifi","sd","audio"]  required permissions               ║
// ║  }                                                                       ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#include <stdint.h>
#include <string>
#include <vector>

// ── Binary package header (256 bytes, packed) ─────────────────────────────
#pragma pack(push, 1)
struct IonAppHeader {
    char     magic[8];       // "IONAPP\0\0"
    uint8_t  version;        // Package format version = 1
    uint8_t  _reserved[3];
    uint32_t header_crc32;   // CRC32 of bytes 12-255
    char     app_id[64];     // "com.example.myapp\0..."
    char     app_name[24];   // Display name
    char     app_version[12];// "1.0.0\0..."
    char     author[32];     // Author name
    char     desc[80];       // Short description
    uint32_t accent_color;   // RGB888 accent color
    uint32_t code_size;      // Bytes of code binary
    uint32_t code_crc32;     // CRC32 of code binary
    uint8_t  perms;          // Permission bitmask
    uint8_t  _pad[11];       // Pad to 256 bytes total
};
#pragma pack(pop)

static_assert(sizeof(IonAppHeader) == 256, "IonAppHeader must be 256 bytes");

// ── Permission flags ─────────────────────────────────────────────────────
#define ION_PERM_WIFI   (1 << 0)   // Access network
#define ION_PERM_SD     (1 << 1)   // Read/write SD card
#define ION_PERM_AUDIO  (1 << 2)   // Play audio
#define ION_PERM_LED    (1 << 3)   // Control LEDs
#define ION_PERM_RADIO  (1 << 4)   // nRF24 radio
#define ION_PERM_ADC    (1 << 5)   // Battery/ADC read

// ── Installed app descriptor (runtime) ───────────────────────────────────
struct InstalledApp {
    std::string id;           // "com.example.myapp"
    std::string name;         // "My App"
    std::string version;      // "1.0.0"
    std::string author;
    std::string desc;
    std::string installPath;  // "/sdcard/installed/MyApp/"
    uint32_t    accentColor;  // 0xRRGGBB
    uint8_t     perms;        // permission bitmask
    uint32_t    sizeKb;
    bool        hasIcon;      // icon.bin present
};

// ── Package validation result ─────────────────────────────────────────────
enum class PackageStatus {
    OK,
    ERR_NOT_FOUND,
    ERR_INVALID_MAGIC,
    ERR_CORRUPT_HEADER,
    ERR_CORRUPT_CODE,
    ERR_VERSION_TOO_NEW,
    ERR_ALREADY_INSTALLED,
    ERR_NO_SPACE,
    ERR_PERMISSION_DENIED,
};

const char* packageStatusStr(PackageStatus s);
