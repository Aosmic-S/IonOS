#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce RFID — PN532 Read / Write / Clone                                 ║
// ║  Supports: Mifare Classic 1K/4K, Mifare Ultralight, NTAG2xx (13.56MHz)  ║
// ║  Hardware: PN532 module on I2C (SDA=GPIO8/GPIO9 or configurable)         ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include <stdint.h>
#include <string>
#include <vector>
#include "driver/i2c.h"

// ── PN532 I2C constants ───────────────────────────────────────────────────
#define PN532_I2C_ADDRESS      0x24
#define PN532_I2C_PORT         I2C_NUM_0
#define PN532_SDA_PIN          GPIO_NUM_8    // Change to your I2C SDA
#define PN532_SCL_PIN          GPIO_NUM_9    // Change to your I2C SCL
#define PN532_I2C_FREQ_HZ      400000

// ── PN532 commands ────────────────────────────────────────────────────────
#define PN532_CMD_GETFIRMWAREVERSION  0x02
#define PN532_CMD_INLISTPASSIVETARGET 0x4A
#define PN532_CMD_INDATAEXCHANGE      0x40
#define PN532_CMD_SAMCONFIGURATION    0x14

// ── Mifare commands ───────────────────────────────────────────────────────
#define MIFARE_CMD_AUTH_A   0x60
#define MIFARE_CMD_AUTH_B   0x61
#define MIFARE_CMD_READ     0x30
#define MIFARE_CMD_WRITE    0xA0
#define MIFARE_CMD_WRITE_UL 0xA2  // Ultralight

// ── Tag info ──────────────────────────────────────────────────────────────
struct RfidTag {
    uint8_t  uid[7];
    uint8_t  uidLen;
    uint8_t  atqa[2];
    uint8_t  sak;
    std::string type;   // "Mifare Classic 1K", "Ultralight", "NTAG213" etc
    uint8_t  blocks;    // number of readable blocks

    // Block data (for clone/dump)
    std::vector<std::vector<uint8_t>> blockData;

    std::string uidHex() const;
    bool isMifareClassic() const { return sak == 0x08 || sak == 0x18; }
    bool isUltralight()    const { return sak == 0x00; }
};

// ══════════════════════════════════════════════════════════════════════════
class PN532 {
public:
    PN532() = default;
    ~PN532() { deinit(); }

    bool init(int sda = PN532_SDA_PIN, int scl = PN532_SCL_PIN);
    void deinit();
    bool isPresent() const { return m_init; }

    // ── Card operations ───────────────────────────────────────────────────
    // Detect and read a card, returns true if found
    bool detectCard(RfidTag& tag);

    // Read all blocks from a Mifare Classic card
    bool dumpMifareClassic(RfidTag& tag,
                            const uint8_t keyA[6] = nullptr,
                            const uint8_t keyB[6] = nullptr);

    // Write a single block (Mifare Classic)
    bool writeBlock(uint8_t block, const uint8_t data[16],
                    const uint8_t key[6], bool useKeyA = true);

    // Clone UID from src to a magic (UID-changeable) card
    bool cloneUid(const RfidTag& src);

    // Read Ultralight / NTAG pages
    bool dumpUltralight(RfidTag& tag);

    // Write NDEF record to Ultralight
    bool writeNdef(const std::string& text);

    // Save dump to SD as .nfc file (Flipper-compatible format)
    bool saveDump(const RfidTag& tag,
                  const std::string& path = "/sdcard/bruce_rfid/");

    // Load and re-emit from saved .nfc file
    bool loadAndEmulate(const std::string& path, RfidTag& out);

    // Get firmware version string
    std::string firmwareVersion();

private:
    bool m_init = false;

    // I2C low-level
    bool     writeCommand(const uint8_t* cmd, uint8_t len);
    bool     readResponse(uint8_t* resp, uint8_t maxLen, uint8_t& recvLen,
                          uint32_t timeout_ms = 1000);
    bool     sendRaw(const uint8_t* buf, int len);
    bool     recvRaw(uint8_t* buf, int len, uint32_t timeout_ms = 1000);

    // Frame building
    void     buildFrame(const uint8_t* data, uint8_t len,
                        uint8_t* out, int& outLen);
    bool     wakeup();
    bool     samConfig();

    // Auth helper
    bool     authenticate(uint8_t block, const uint8_t key[6], bool useKeyA,
                          const uint8_t uid[4]);

    static constexpr uint8_t PN532_PREAMBLE  = 0x00;
    static constexpr uint8_t PN532_STARTCODE1= 0x00;
    static constexpr uint8_t PN532_STARTCODE2= 0xFF;
    static constexpr uint8_t PN532_POSTAMBLE = 0x00;
    static constexpr uint8_t PN532_HOSTTOPN532=0xD4;
    static constexpr uint8_t PN532_PN532TOHOST=0xD5;

    static const uint8_t s_defaultKeyA[6];
    static const uint8_t s_defaultKeyB[6];
};
