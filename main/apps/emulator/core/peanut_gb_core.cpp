// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  IonOS — Peanut-GB Core with 4 MB ROM Streaming Cache                   ║
// ║  Apache License 2.0 — Copyright 2024 IonOS Contributors                  ║
// ║                                                                          ║
// ║  ROM access model:                                                       ║
// ║    Peanut-GB calls rom_read(gb, addr) with the absolute physical byte    ║
// ║    offset into the ROM file (MBC banking already resolved internally).   ║
// ║    We divide addr by ROM_PAGE_SIZE (16 KB) to get a bank number, look    ║
// ║    that bank up in the 16-slot LRU cache, and on a miss fseek+fread     ║
// ║    exactly 16 KB from the open SD file descriptor.                       ║
// ║                                                                          ║
// ║  Cache performance:                                                      ║
// ║    A typical GB game touches 2-4 banks per frame. With 16 slots and     ║
// ║    bank 0 pinned, the effective working set of any game fits in cache.   ║
// ║    Miss penalty: ~1-3 ms (SD fseek+16 KB fread).                        ║
// ║    Expected hit rate: >99% after first few frames.                       ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#define ENABLE_LCD                  1
#define ENABLE_SOUND                0
#define PEANUT_GB_HIGH_LCD_ACCURACY 1

#include "peanut_gb_core.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "GBCore";

// ═════════════════════════════════════════════════════════════════════════════
// ROM PAGE CACHE  —  internal helpers
// ═════════════════════════════════════════════════════════════════════════════

// Find the cache slot holding bankNum. Returns slot index or -1.
static int cache_find(GBCore* ctx, uint32_t bankNum)
{
    for (int i = 0; i < (int)ROM_CACHE_SLOTS; i++) {
        if (ctx->cache[i].data && ctx->cache[i].bankNum == bankNum)
            return i;
    }
    return -1;
}

// Choose the best slot to evict: prefer empty slots, then oldest LRU
// non-pinned slot.  Returns slot index.  Never returns a pinned slot.
static int cache_evict(GBCore* ctx)
{
    // First pass: find an empty slot
    for (int i = 0; i < (int)ROM_CACHE_SLOTS; i++) {
        if (!ctx->cache[i].data)
            return i;   // slot has no page buffer yet — will be allocated
        if (ctx->cache[i].bankNum == 0xFFFFFFFFu)
            return i;   // slot allocated but empty
    }

    // Second pass: find the oldest (smallest lastUsed) non-pinned slot
    int    oldest    = -1;
    uint32_t oldTime = 0xFFFFFFFFu;
    for (int i = 0; i < (int)ROM_CACHE_SLOTS; i++) {
        if (ctx->cache[i].pinned) continue;
        if (ctx->cache[i].lastUsed < oldTime) {
            oldTime = ctx->cache[i].lastUsed;
            oldest  = i;
        }
    }

    // Should never be -1 given ROM_CACHE_SLOTS > 1 and only slot 0 is pinned
    if (oldest < 0) oldest = 1;
    return oldest;
}

// Load bankNum into slot idx from the open ROM file.
// Allocates the page buffer in PSRAM if not already allocated.
// Returns true on success.
static bool cache_load(GBCore* ctx, int slotIdx, uint32_t bankNum)
{
    RomPageSlot* slot = &ctx->cache[slotIdx];

    // Allocate PSRAM buffer on first use of this slot
    if (!slot->data) {
        slot->data = (uint8_t*)heap_caps_malloc(
            ROM_PAGE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!slot->data) {
            ESP_LOGE(TAG, "PSRAM alloc failed for ROM page slot %d", slotIdx);
            return false;
        }
    }

    // Compute file offset and clamp read length at EOF
    uint32_t fileOffset = bankNum * ROM_PAGE_SIZE;
    size_t   readLen    = ROM_PAGE_SIZE;
    if ((size_t)fileOffset >= ctx->romSize) {
        // Bank beyond EOF — fill with 0xFF (open bus)
        memset(slot->data, 0xFF, ROM_PAGE_SIZE);
        slot->bankNum = bankNum;
        return true;
    }
    if (fileOffset + readLen > ctx->romSize)
        readLen = ctx->romSize - fileOffset;

    if (fseek(ctx->romFile, (long)fileOffset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "fseek failed for bank %lu", (unsigned long)bankNum);
        return false;
    }
    size_t got = fread(slot->data, 1, readLen, ctx->romFile);
    if (got < readLen)
        memset(slot->data + got, 0xFF, readLen - got); // pad partial reads

    slot->bankNum = bankNum;
    ctx->cacheMisses++;

    ESP_LOGD(TAG, "Cache miss: bank %lu → slot %d  (miss#%lu)",
             (unsigned long)bankNum, slotIdx,
             (unsigned long)ctx->cacheMisses);
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// PEANUT-GB CALLBACKS
// gb->direct.priv holds our GBCore* (set by gb_init's priv parameter)
// ═════════════════════════════════════════════════════════════════════════════

// rom_read — called for every ROM byte access (~4 million times per second)
// This is the hot path.  Keep it as lean as possible.
static uint8_t rom_read(struct gb_s* gb, const uint_fast32_t addr)
{
    GBCore* ctx = (GBCore*)gb->direct.priv;

    // Bounds check
    if ((size_t)addr >= ctx->romSize) return 0xFF;

    // Which 16 KB bank does this address belong to?
    uint32_t bankNum = (uint32_t)(addr / ROM_PAGE_SIZE);
    uint32_t offset  = (uint32_t)(addr % ROM_PAGE_SIZE);

    // Cache lookup
    int slotIdx = cache_find(ctx, bankNum);
    if (slotIdx < 0) {
        // Cache miss — evict LRU slot and load from SD
        slotIdx = cache_evict(ctx);
        if (!cache_load(ctx, slotIdx, bankNum))
            return 0xFF;
    }

    // Update LRU timestamp (never overflow-evict pinned slot 0)
    ctx->cache[slotIdx].lastUsed = ++ctx->cacheTime;
    ctx->cacheHits++;

    return ctx->cache[slotIdx].data[offset];
}

static uint8_t cart_ram_read(struct gb_s* gb, const uint_fast32_t addr)
{
    GBCore* ctx = (GBCore*)gb->direct.priv;
    if (!ctx->ramData || addr >= ctx->ramSize) return 0xFF;
    return ctx->ramData[addr];
}

static void cart_ram_write(struct gb_s* gb, const uint_fast32_t addr,
                            const uint8_t val)
{
    GBCore* ctx = (GBCore*)gb->direct.priv;
    if (!ctx->ramData || addr >= ctx->ramSize) return;
    ctx->ramData[addr] = val;
}

static void gb_error_cb(struct gb_s* gb, const enum gb_error_e err,
                         const uint16_t addr)
{
    (void)gb;
    static const char* msgs[] = {
        "Unknown error", "Invalid opcode", "Invalid read",
        "Invalid write",  "Halt forever"
    };
    ESP_LOGW(TAG, "GB error: %s at 0x%04X",
             (err < GB_INVALID_MAX) ? msgs[err] : "?", addr);
}

// lcd_draw_line — called 144 times per frame, once per scanline
// pixels[0..159]: bits[1:0] = shade index 0-3
static void lcd_draw_line(struct gb_s* gb,
                           const uint8_t* pixels,
                           const uint_fast8_t line)
{
    GBCore* ctx = (GBCore*)gb->direct.priv;
    if (line >= LCD_HEIGHT) return;
    uint16_t* row = ctx->framebuf + ((uint32_t)line * LCD_WIDTH);
    for (int x = 0; x < LCD_WIDTH; x++)
        row[x] = ctx->palette[pixels[x] & 0x03];
}

// ═════════════════════════════════════════════════════════════════════════════
// PUBLIC API
// ═════════════════════════════════════════════════════════════════════════════

void gbcore_create(GBCore* ctx)
{
    memset(ctx, 0, sizeof(GBCore));

    // DMG palette
    ctx->palette[0] = DMG_SHADE0;
    ctx->palette[1] = DMG_SHADE1;
    ctx->palette[2] = DMG_SHADE2;
    ctx->palette[3] = DMG_SHADE3;

    // struct gb_s in PSRAM (~17 KB)
    ctx->gb = (struct gb_s*)heap_caps_calloc(
        1, sizeof(struct gb_s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx->gb) {
        ESP_LOGE(TAG, "Failed to alloc gb_s (%zu B) in PSRAM", sizeof(struct gb_s));
        return;
    }

    // 160×144 framebuffer in PSRAM (46 080 bytes)
    ctx->framebuf = (uint16_t*)heap_caps_malloc(
        LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx->framebuf) {
        ESP_LOGE(TAG, "Failed to alloc framebuf in PSRAM");
        heap_caps_free(ctx->gb); ctx->gb = nullptr;
        return;
    }
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
        ctx->framebuf[i] = ctx->palette[0];

    // Initialise cache slots — buffers will be allocated lazily on first miss
    for (int i = 0; i < (int)ROM_CACHE_SLOTS; i++) {
        ctx->cache[i].data     = nullptr;
        ctx->cache[i].bankNum  = 0xFFFFFFFFu;
        ctx->cache[i].lastUsed = 0;
        ctx->cache[i].pinned   = false;
    }

    ctx->initialized = true;
    ESP_LOGI(TAG, "GBCore ready — gb_s=%zu B  framebuf=%d B  cache=16×16KB=256KB (PSRAM)",
             sizeof(struct gb_s), LCD_WIDTH * LCD_HEIGHT * 2);
}

bool gbcore_load_rom(GBCore* ctx, const char* romPath)
{
    if (!ctx->initialized) {
        ESP_LOGE(TAG, "Call gbcore_create first");
        return false;
    }

    gbcore_unload(ctx);

    // ── Open ROM file ──────────────────────────────────────────────────────
    ctx->romFile = fopen(romPath, "rb");
    if (!ctx->romFile) {
        ESP_LOGE(TAG, "ROM not found: %s", romPath);
        return false;
    }

    fseek(ctx->romFile, 0, SEEK_END);
    ctx->romSize = (size_t)ftell(ctx->romFile);
    fseek(ctx->romFile, 0, SEEK_SET);

    if (ctx->romSize < 0x8000) {
        ESP_LOGE(TAG, "ROM too small: %zu B (minimum 32 KB)", ctx->romSize);
        fclose(ctx->romFile); ctx->romFile = nullptr; return false;
    }
    if (ctx->romSize > ROM_MAX_SIZE) {
        ESP_LOGE(TAG, "ROM too large: %zu KB (max %u KB)",
                 ctx->romSize / 1024, ROM_MAX_SIZE / 1024);
        fclose(ctx->romFile); ctx->romFile = nullptr; return false;
    }

    // ── Pre-load and pin bank 0 into cache slot 0 ─────────────────────────
    // Bank 0 contains the RST vectors (0x0000-0x0007), interrupt vectors
    // (0x0040-0x0067), and much of the game's boot/init code. It is accessed
    // on almost every frame. Pinning it eliminates those misses entirely.
    ctx->cache[0].pinned = true;
    if (!cache_load(ctx, 0, 0)) {
        ESP_LOGE(TAG, "Failed to pre-load ROM bank 0");
        fclose(ctx->romFile); ctx->romFile = nullptr; return false;
    }
    ctx->cache[0].lastUsed = ++ctx->cacheTime;
    // Correct the miss counter — this pre-load is expected, not a real miss
    ctx->cacheMisses = 0;

    // ── Initialise Peanut-GB ───────────────────────────────────────────────
    // gb_init reads the ROM header (title, MBC type, RAM size, checksum).
    // It calls rom_read for bytes 0x0100-0x014F.  These are in bank 0 so
    // they will be served from the already-pinned cache slot immediately.
    enum gb_init_error_e gerr = gb_init(
        ctx->gb,
        rom_read, cart_ram_read, cart_ram_write, gb_error_cb,
        ctx);   // ctx becomes gb->direct.priv

    if (gerr != GB_INIT_NO_ERROR) {
        const char* msg = (gerr == GB_INIT_CARTRIDGE_UNSUPPORTED)
                          ? "Unsupported cartridge type"
                          : "ROM header checksum mismatch";
        ESP_LOGE(TAG, "gb_init: %s (code %d)", msg, (int)gerr);
        fclose(ctx->romFile); ctx->romFile = nullptr;
        ctx->romSize = 0; return false;
    }

    // ── Allocate cart RAM ──────────────────────────────────────────────────
    int sr = gb_get_save_size_s(ctx->gb, &ctx->ramSize);
    if (sr == 0 && ctx->ramSize > 0) {
        ctx->ramData = (uint8_t*)heap_caps_calloc(
            1, ctx->ramSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ctx->ramData) {
            ESP_LOGW(TAG, "No PSRAM for cart RAM (%zu B) — saves disabled",
                     ctx->ramSize);
            ctx->ramSize = 0;
        }
    }

    // ── Register LCD callback ──────────────────────────────────────────────
    gb_init_lcd(ctx->gb, lcd_draw_line);

    // ── Read ROM title from header ─────────────────────────────────────────
    gb_get_rom_name(ctx->gb, ctx->romTitle);
    ctx->romTitle[16] = '\0';

    // ── Joypad: 0xFF = all buttons released (active-low in Peanut-GB) ─────
    ctx->gb->direct.joypad = 0xFF;

    ctx->romLoaded   = true;
    ctx->frameCount  = 0;
    ctx->fps         = 0;
    ctx->cacheHits   = 0;
    ctx->cacheMisses = 0;
    ctx->lastFpsTime = esp_timer_get_time();

    // MBC type string for logging
    static const char* mbcNames[] = {
        "ROM only","MBC1","MBC2","MBC3","MBC5","MBC6","MBC7","HuC1"
    };
    int mbc = (int)ctx->gb->mbc;

    ESP_LOGI(TAG, "Loaded: \"%s\"  %zu KB  RAM=%zu B  MBC=%d(%s)  banks=%lu",
             ctx->romTitle,
             ctx->romSize / 1024,
             ctx->ramSize,
             mbc,
             (mbc >= 0 && mbc <= 7) ? mbcNames[mbc] : "unknown",
             (unsigned long)(ctx->romSize / ROM_PAGE_SIZE));

    return true;
}

void gbcore_run_frame(GBCore* ctx)
{
    if (!ctx->romLoaded) return;
    gb_run_frame(ctx->gb);
    ctx->frameCount++;

    // Update FPS counter every 60 frames
    if (ctx->frameCount % 60 == 0) {
        int64_t now   = esp_timer_get_time();
        int64_t delta = now - ctx->lastFpsTime;
        if (delta > 0)
            ctx->fps = (uint32_t)(60ULL * 1000000ULL / (uint64_t)delta);
        ctx->lastFpsTime = now;

        // Log cache stats every 5 seconds (~300 frames)
        if (ctx->frameCount % 300 == 0) {
            uint32_t total = ctx->cacheHits + ctx->cacheMisses;
            uint32_t pct   = total ? (ctx->cacheHits * 100u / total) : 100u;
            ESP_LOGD(TAG, "Cache: %lu hits  %lu misses  %lu%% hit rate",
                     (unsigned long)ctx->cacheHits,
                     (unsigned long)ctx->cacheMisses,
                     (unsigned long)pct);
        }
    }
}

void gbcore_set_button(GBCore* ctx, uint8_t mask, bool pressed)
{
    if (!ctx->romLoaded) return;
    if (pressed)
        ctx->gb->direct.joypad &= (uint8_t)(~mask);   // press = clear bit
    else
        ctx->gb->direct.joypad |= mask;                // release = set bit
}

bool gbcore_save(GBCore* ctx, const char* savePath)
{
    if (!ctx->romLoaded || !ctx->ramData || ctx->ramSize == 0)
        return true;  // no save RAM — not an error

    FILE* f = fopen(savePath, "wb");
    if (!f) { ESP_LOGE(TAG, "Cannot write save: %s", savePath); return false; }
    size_t w = fwrite(ctx->ramData, 1, ctx->ramSize, f);
    fclose(f);
    if (w != ctx->ramSize) { ESP_LOGE(TAG, "Save write incomplete"); return false; }
    ESP_LOGI(TAG, "Saved %zu bytes → %s", ctx->ramSize, savePath);
    return true;
}

bool gbcore_load_save(GBCore* ctx, const char* savePath)
{
    if (!ctx->romLoaded || !ctx->ramData || ctx->ramSize == 0) return true;

    FILE* f = fopen(savePath, "rb");
    if (!f) { ESP_LOGI(TAG, "No save file (first play)"); return true; }

    fseek(f, 0, SEEK_END);
    size_t sz = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz != ctx->ramSize) {
        ESP_LOGW(TAG, "Save size mismatch (%zu vs %zu) — discarding old save",
                 sz, ctx->ramSize);
        fclose(f); return false;
    }
    size_t r = fread(ctx->ramData, 1, ctx->ramSize, f);
    fclose(f);
    ESP_LOGI(TAG, "Save loaded: %zu bytes", r);
    return r == ctx->ramSize;
}

void gbcore_unload(GBCore* ctx)
{
    if (!ctx->romLoaded) return;

    // Close the ROM file — no more SD reads needed
    if (ctx->romFile) { fclose(ctx->romFile); ctx->romFile = nullptr; }

    // Free cart RAM
    if (ctx->ramData) { heap_caps_free(ctx->ramData); ctx->ramData = nullptr; }
    ctx->ramSize = 0;

    // Clear cache slots (keep page buffers allocated — they'll be reused on
    // the next gbcore_load_rom call without re-allocating PSRAM)
    for (int i = 0; i < (int)ROM_CACHE_SLOTS; i++) {
        ctx->cache[i].bankNum  = 0xFFFFFFFFu;
        ctx->cache[i].lastUsed = 0;
        ctx->cache[i].pinned   = false;
        // do NOT free cache[i].data — keep PSRAM pages allocated for reuse
    }

    ctx->romSize    = 0;
    ctx->cacheTime  = 0;
    ctx->cacheHits  = 0;
    ctx->cacheMisses= 0;
    ctx->romLoaded  = false;
    ctx->frameCount = 0;
    ctx->fps        = 0;
    memset(ctx->romTitle, 0, sizeof(ctx->romTitle));
}

void gbcore_destroy(GBCore* ctx)
{
    gbcore_unload(ctx);

    // Free PSRAM page buffers
    for (int i = 0; i < (int)ROM_CACHE_SLOTS; i++) {
        if (ctx->cache[i].data) {
            heap_caps_free(ctx->cache[i].data);
            ctx->cache[i].data = nullptr;
        }
    }

    if (ctx->gb)       { heap_caps_free(ctx->gb);       ctx->gb       = nullptr; }
    if (ctx->framebuf) { heap_caps_free(ctx->framebuf); ctx->framebuf = nullptr; }
    ctx->initialized = false;
}
