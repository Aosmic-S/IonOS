#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  IonOS — Game Boy Emulator Core                                          ║
// ║  Wraps Peanut-GB (MIT, Mahyar Koshkouei)                                ║
// ║  https://github.com/deltabeard/Peanut-GB                                 ║
// ║                                                                          ║
// ║  Memory layout (PSRAM):                                                  ║
// ║    struct gb_s      ~17 KB   Core state (WRAM/VRAM/OAM/HRAM)            ║
// ║    ROM page cache  256 KB    16 × 16 KB pages (LRU, bank 0 pinned)      ║
// ║    Cart RAM        ≤128 KB   Save RAM                                    ║
// ║    Framebuf         46 KB    160×144 RGB565                              ║
// ║    Total:          ~447 KB   Supports ROMs up to 4 MB streamed from SD   ║
// ║                                                                          ║
// ║  ROM streaming: the ROM file stays open on SD. Peanut-GB calls           ║
// ║  rom_read(addr) with the absolute physical ROM byte offset. We resolve   ║
// ║  to a 16 KB page number, check the LRU cache, and on a miss fseek+fread ║
// ║  16 KB from SD (~1-3 ms). Bank 0 is pinned (interrupt vectors, boot).   ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// Peanut-GB compile-time config — must precede include
#define ENABLE_LCD                  1
#define ENABLE_SOUND                0
#define PEANUT_GB_HIGH_LCD_ACCURACY 1
#define PEANUT_GB_IS_CGB 1

#include "peanut_gb.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

// ── ROM page cache constants ──────────────────────────────────────────────
#define ROM_PAGE_SIZE    (16u * 1024u)          // 16 KB = one GB ROM bank
#define ROM_CACHE_SLOTS  16u                    // 16 pages = 256 KB PSRAM
#define ROM_MAX_SIZE     (4u * 1024u * 1024u)   // 4 MB maximum ROM

// ── DMG colour palette (RGB565, classic green screen) ────────────────────
#define DMG_SHADE0  0x9BF3u   // #9BBC0F  lightest
#define DMG_SHADE1  0x8BA9u   // #8BAC0F
#define DMG_SHADE2  0x30C6u   // #306230
#define DMG_SHADE3  0x0700u   // #0F380F  darkest

// ── LCD dimensions ────────────────────────────────────────────────────────
#define LCD_WIDTH   160
#define LCD_HEIGHT  144

// ── ROM page cache slot ───────────────────────────────────────────────────
typedef struct {
    uint8_t*  data;       // ROM_PAGE_SIZE bytes in PSRAM (NULL = unallocated)
    uint32_t  bankNum;    // which ROM bank is loaded (0xFFFFFFFF = empty)
    uint32_t  lastUsed;   // LRU timestamp — higher is more recently used
    bool      pinned;     // if true: never evict (bank 0)
} RomPageSlot;

// ── Core context (zero-initialise before calling gbcore_create) ───────────
typedef struct {
    // Peanut-GB state — lives in PSRAM (~17 KB, includes WRAM/VRAM/OAM/HRAM)
    struct gb_s* gb;

    // Cart save RAM — NULL if cartridge has no battery RAM
    uint8_t*  ramData;
    size_t    ramSize;

    // 160×144 RGB565 framebuffer — lives in PSRAM (46 080 bytes)
    uint16_t* framebuf;
    uint16_t  palette[4]; // pre-computed DMG shades as RGB565

    // ── ROM streaming cache ───────────────────────────────────────────────
    // The file is opened by gbcore_load_rom and closed by gbcore_unload.
    // It is kept open the entire time the game is running so cache misses
    // only need a fseek+fread, no open/close overhead.
    FILE*        romFile;
    size_t       romSize;                 // total ROM file size in bytes
    RomPageSlot  cache[ROM_CACHE_SLOTS];  // 16 pages × 16 KB = 256 KB PSRAM
    uint32_t     cacheTime;               // monotonic counter for LRU
    uint32_t     cacheHits;               // stats — visible in emulator HUD
    uint32_t     cacheMisses;             // stats

    // ── State flags ───────────────────────────────────────────────────────
    bool      initialized;
    bool      romLoaded;
    char      romTitle[17]; // null-terminated, max 16 chars

    // ── FPS tracking ──────────────────────────────────────────────────────
    uint32_t  frameCount;
    int64_t   lastFpsTime;
    uint32_t  fps;
} GBCore;

#ifdef __cplusplus
extern "C" {
#endif

// Allocate gb_s, framebuf, and 16 cache page buffers in PSRAM.
// Call once at startup. ctx must be zeroed first.
void gbcore_create(GBCore* ctx);

// Open romPath, validate it, set up the page cache (load bank 0 + pin it),
// initialise Peanut-GB, and optionally load a matching .sav file.
// Supports ROMs up to ROM_MAX_SIZE (4 MB).
bool gbcore_load_rom(GBCore* ctx, const char* romPath);

// Run one complete Game Boy frame (~70 224 clock cycles).
// Calls lcd_draw_line 144 times, filling ctx->framebuf.
void gbcore_run_frame(GBCore* ctx);

// Update joypad state. mask = JOYPAD_* constant from peanut_gb.h.
void gbcore_set_button(GBCore* ctx, uint8_t mask, bool pressed);

// Write cart RAM to savePath as raw binary.
bool gbcore_save(GBCore* ctx, const char* savePath);

// Load cart RAM from savePath.
bool gbcore_load_save(GBCore* ctx, const char* savePath);

// Free ROM file handle and cart RAM. Keeps gb_s and framebuf alive
// so the emulator can reload a different game without full re-init.
void gbcore_unload(GBCore* ctx);

// Free everything including gb_s and framebuf. Call on emulator exit.
void gbcore_destroy(GBCore* ctx);

// ── Inline accessors ──────────────────────────────────────────────────────
static inline const uint16_t* gbcore_framebuf(const GBCore* ctx)
    { return ctx->framebuf; }
static inline const char* gbcore_rom_title(const GBCore* ctx)
    { return ctx->romTitle; }
static inline uint32_t gbcore_fps(const GBCore* ctx)
    { return ctx->fps; }
static inline uint32_t gbcore_cache_hits(const GBCore* ctx)
    { return ctx->cacheHits; }
static inline uint32_t gbcore_cache_misses(const GBCore* ctx)
    { return ctx->cacheMisses; }

#ifdef __cplusplus
}
#endif
