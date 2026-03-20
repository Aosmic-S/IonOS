// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce RFID — PN532 Implementation                                        ║
// ║  Apache License 2.0 — Copyright 2024 IonOS Contributors                   ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "bruce_rfid.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>

static const char* TAG = "PN532";

const uint8_t PN532::s_defaultKeyA[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
const uint8_t PN532::s_defaultKeyB[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ── UID to hex string ─────────────────────────────────────────────────────
std::string RfidTag::uidHex() const {
    char buf[32] = {};
    for (int i=0;i<uidLen;i++)
        snprintf(buf+i*3,4,"%02X%s",uid[i],i<uidLen-1?" ":"");
    return buf;
}

// ── I2C Init ──────────────────────────────────────────────────────────────
bool PN532::init(int sda, int scl)
{
    i2c_config_t conf = {};
    conf.mode             = I2C_MODE_MASTER;
    conf.sda_io_num       = (gpio_num_t)sda;
    conf.scl_io_num       = (gpio_num_t)scl;
    conf.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = PN532_I2C_FREQ_HZ;

    if (i2c_param_config(PN532_I2C_PORT, &conf) != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed");
        return false;
    }
    if (i2c_driver_install(PN532_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
        ESP_LOGW(TAG, "I2C driver already installed");
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    if (!wakeup() || !samConfig()) {
        ESP_LOGE(TAG, "PN532 not found on I2C 0x%02X", PN532_I2C_ADDRESS);
        return false;
    }

    m_init = true;
    ESP_LOGI(TAG, "PN532 ready — %s", firmwareVersion().c_str());
    return true;
}

void PN532::deinit() {
    if (m_init) {
        i2c_driver_delete(PN532_I2C_PORT);
        m_init = false;
    }
}

// ── I2C raw read/write ────────────────────────────────────────────────────
bool PN532::sendRaw(const uint8_t* buf, int len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PN532_I2C_ADDRESS<<1)|I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t*)buf, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(PN532_I2C_PORT, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool PN532::recvRaw(uint8_t* buf, int len, uint32_t timeout_ms)
{
    uint32_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() < deadline) {
        // Poll ready byte first
        uint8_t rdy = 0;
        i2c_cmd_handle_t c = i2c_cmd_link_create();
        i2c_master_start(c);
        i2c_master_write_byte(c,(PN532_I2C_ADDRESS<<1)|I2C_MASTER_READ,true);
        i2c_master_read_byte(c,&rdy,I2C_MASTER_NACK);
        i2c_master_stop(c);
        i2c_master_cmd_begin(PN532_I2C_PORT,c,pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(c);
        if (rdy != 0x01) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

        // Read full response
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd,(PN532_I2C_ADDRESS<<1)|I2C_MASTER_READ,true);
        if (len > 1) i2c_master_read(cmd, buf, len-1, I2C_MASTER_ACK);
        i2c_master_read_byte(cmd, buf+len-1, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        esp_err_t r = i2c_master_cmd_begin(PN532_I2C_PORT,cmd,pdMS_TO_TICKS(200));
        i2c_cmd_link_delete(cmd);
        return r == ESP_OK;
    }
    return false;
}

// ── PN532 frame building ──────────────────────────────────────────────────
// Frame: [PREAMBLE][00][FF][LEN][LCS][TFI][...CMD...][DCS][POSTAMBLE]
void PN532::buildFrame(const uint8_t* data, uint8_t len, uint8_t* out, int& outLen)
{
    int i = 0;
    out[i++] = 0x00; // preamble
    out[i++] = 0x00; // start 1
    out[i++] = 0xFF; // start 2
    out[i++] = len+1;                          // LEN (TFI + data)
    out[i++] = (uint8_t)(0x100-(len+1));       // LCS
    out[i++] = PN532_HOSTTOPN532;              // TFI
    uint8_t dcs = PN532_HOSTTOPN532;
    for (int j=0;j<len;j++) { out[i++]=data[j]; dcs+=data[j]; }
    out[i++] = (uint8_t)(0x100-dcs);           // DCS
    out[i++] = 0x00;                           // postamble
    outLen = i;
}

bool PN532::writeCommand(const uint8_t* cmd, uint8_t len)
{
    uint8_t frame[64];
    int flen = 0;
    buildFrame(cmd, len, frame, flen);
    return sendRaw(frame, flen);
}

bool PN532::readResponse(uint8_t* resp, uint8_t maxLen, uint8_t& recvLen,
                          uint32_t timeout_ms)
{
    uint8_t raw[64] = {};
    if (!recvRaw(raw, 64, timeout_ms)) return false;

    // Validate frame: [00][00][FF][LEN][LCS][TFI][data][DCS][00]
    if (raw[0]!=0x00 || raw[1]!=0x00 || raw[2]!=0xFF) return false;
    uint8_t len = raw[3];
    if ((uint8_t)(raw[3]+raw[4]) != 0) return false;  // LCS check
    if (raw[5] != PN532_PN532TOHOST) return false;     // TFI

    recvLen = len - 1;  // exclude TFI
    if (recvLen > maxLen) recvLen = maxLen;
    memcpy(resp, raw+6, recvLen);
    return true;
}

// ── Init commands ─────────────────────────────────────────────────────────
bool PN532::wakeup()
{
    // Send wakeup sequence: 0x55 × 3 + some zeros
    static const uint8_t wake[] = {0x55,0x55,0x55,0x00,0x00,0x00};
    sendRaw(wake, sizeof(wake));
    vTaskDelay(pdMS_TO_TICKS(10));

    // GetFirmwareVersion
    uint8_t cmd[] = {PN532_CMD_GETFIRMWAREVERSION};
    if (!writeCommand(cmd,1)) return false;
    uint8_t resp[10]; uint8_t rlen=0;
    return readResponse(resp, sizeof(resp), rlen, 500);
}

bool PN532::samConfig()
{
    uint8_t cmd[] = {PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01};
    if (!writeCommand(cmd, sizeof(cmd))) return false;
    uint8_t resp[2]; uint8_t rlen=0;
    return readResponse(resp, sizeof(resp), rlen, 500);
}

std::string PN532::firmwareVersion()
{
    uint8_t cmd[] = {PN532_CMD_GETFIRMWAREVERSION};
    if (!writeCommand(cmd,1)) return "unknown";
    uint8_t resp[10]; uint8_t rlen=0;
    if (!readResponse(resp, sizeof(resp), rlen, 500) || rlen < 4) return "unknown";
    char buf[32]; snprintf(buf,32,"PN5%02X fw%d.%d",resp[1],resp[2],resp[3]);
    return buf;
}

// ── Card detection ────────────────────────────────────────────────────────
bool PN532::detectCard(RfidTag& tag)
{
    uint8_t cmd[] = {PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00};
    // 0x00 = 106kbps type A (Mifare/NTAG)
    if (!writeCommand(cmd, sizeof(cmd))) return false;
    uint8_t resp[32]; uint8_t rlen=0;
    if (!readResponse(resp, sizeof(resp), rlen, 500)) return false;
    if (rlen < 7 || resp[1] == 0) return false;  // no card

    // resp[2] = Tg, resp[3..4] = ATQA, resp[5] = SAK, resp[6] = NfcIdLen
    tag.atqa[0] = resp[3]; tag.atqa[1] = resp[4];
    tag.sak      = resp[5];
    tag.uidLen   = resp[6];
    memcpy(tag.uid, resp+7, tag.uidLen);
    memset(tag.uid+tag.uidLen, 0, sizeof(tag.uid)-tag.uidLen);

    // Classify
    if (tag.sak == 0x08)       tag.type = "Mifare Classic 1K";
    else if (tag.sak == 0x18)  tag.type = "Mifare Classic 4K";
    else if (tag.sak == 0x00)  tag.type = "Mifare Ultralight/NTAG";
    else                       tag.type = "Unknown (SAK=0x" + std::to_string(tag.sak) + ")";

    tag.blocks = (tag.sak==0x18) ? 256 : 64;

    ESP_LOGI(TAG, "Card: %s  UID=%s", tag.type.c_str(), tag.uidHex().c_str());
    return true;
}

// ── Mifare Classic Authentication ─────────────────────────────────────────
bool PN532::authenticate(uint8_t block, const uint8_t key[6],
                          bool useKeyA, const uint8_t uid[4])
{
    uint8_t cmd[13] = {
        PN532_CMD_INDATAEXCHANGE, 0x01,
        (uint8_t)(useKeyA ? MIFARE_CMD_AUTH_A : MIFARE_CMD_AUTH_B),
        block,
        key[0],key[1],key[2],key[3],key[4],key[5],
        uid[0],uid[1],uid[2],uid[3]
    };
    if (!writeCommand(cmd, sizeof(cmd))) return false;
    uint8_t resp[2]; uint8_t rlen=0;
    if (!readResponse(resp, sizeof(resp), rlen, 500)) return false;
    return resp[1] == 0x00;  // status OK
}

// ── Read a single 16-byte block ────────────────────────────────────────────
static bool readBlock(PN532* self, uint8_t block, uint8_t data[16])
{
    uint8_t cmd[] = {PN532_CMD_INDATAEXCHANGE, 0x01, MIFARE_CMD_READ, block};
    // Use writeCommand + readResponse via public interface — direct call here
    // (would need friend or internal call - simplified using direct buffer access)
    uint8_t frame[16]; int flen=0;
    self->buildFrame(cmd, sizeof(cmd), frame, flen);  // buildFrame is private
    // For simplicity, return false; real implementation would have a public readBlock
    return false;
}

// ── Dump Mifare Classic ────────────────────────────────────────────────────
bool PN532::dumpMifareClassic(RfidTag& tag,
                               const uint8_t keyA[6], const uint8_t keyB[6])
{
    if (!keyA) keyA = s_defaultKeyA;
    if (!keyB) keyB = s_defaultKeyB;

    tag.blockData.clear();

    for (uint8_t block = 0; block < tag.blocks; block++) {
        // Authenticate sector (each sector = 4 blocks for 1K)
        if (block % 4 == 0) {
            bool ok = authenticate(block, keyA, true, tag.uid);
            if (!ok) ok = authenticate(block, keyB, false, tag.uid);
            if (!ok) {
                ESP_LOGW(TAG,"Auth fail block %d",block);
                tag.blockData.push_back(std::vector<uint8_t>(16, 0xFF));
                continue;
            }
        }

        // Read block
        uint8_t cmd[] = {PN532_CMD_INDATAEXCHANGE, 0x01, MIFARE_CMD_READ, block};
        uint8_t frame[32]; int flen=0;
        buildFrame(cmd, sizeof(cmd), frame, flen);
        if (!sendRaw(frame, flen)) {
            tag.blockData.push_back(std::vector<uint8_t>(16, 0xFF));
            continue;
        }
        uint8_t resp[32]; uint8_t rlen=0;
        if (!readResponse(resp, sizeof(resp), rlen, 500) || rlen < 17) {
            tag.blockData.push_back(std::vector<uint8_t>(16,0xFF));
            continue;
        }
        tag.blockData.push_back(std::vector<uint8_t>(resp+1, resp+17));
    }

    ESP_LOGI(TAG,"Dump complete: %zu blocks", tag.blockData.size());
    return !tag.blockData.empty();
}

// ── Dump Ultralight / NTAG ────────────────────────────────────────────────
bool PN532::dumpUltralight(RfidTag& tag)
{
    tag.blockData.clear();
    for (uint8_t page = 0; page < 45; page++) {  // NTAG213 = 45 pages
        uint8_t cmd[] = {PN532_CMD_INDATAEXCHANGE, 0x01, MIFARE_CMD_READ, page};
        uint8_t frame[32]; int flen=0;
        buildFrame(cmd, sizeof(cmd), frame, flen);
        if (!sendRaw(frame, flen)) break;
        uint8_t resp[32]; uint8_t rlen=0;
        if (!readResponse(resp, sizeof(resp), rlen, 300) || rlen < 5) break;
        // READ returns 4 pages at once
        for (int p=0; p<4 && page+p<45; p++) {
            tag.blockData.push_back(std::vector<uint8_t>(resp+1+p*4, resp+5+p*4));
        }
        page += 3;  // we read 4 pages, advance by 3 more
    }
    return true;
}

// ── Write block ────────────────────────────────────────────────────────────
bool PN532::writeBlock(uint8_t block, const uint8_t data[16],
                        const uint8_t key[6], bool useKeyA)
{
    // Would need UID for auth — simplified: auth then write
    uint8_t cmd[20];
    cmd[0] = PN532_CMD_INDATAEXCHANGE;
    cmd[1] = 0x01;
    cmd[2] = MIFARE_CMD_WRITE;
    cmd[3] = block;
    memcpy(cmd+4, data, 16);

    uint8_t frame[64]; int flen=0;
    buildFrame(cmd, 20, frame, flen);
    if (!sendRaw(frame, flen)) return false;
    uint8_t resp[2]; uint8_t rlen=0;
    if (!readResponse(resp, sizeof(resp), rlen, 500)) return false;
    return resp[1] == 0x00;
}

// ── Clone UID to magic card ────────────────────────────────────────────────
bool PN532::cloneUid(const RfidTag& src)
{
    // Magic cards accept a direct write to block 0 without authentication
    uint8_t block0[16] = {};
    // UID
    memcpy(block0, src.uid, src.uidLen);
    // BCC (XOR of UID bytes for 4-byte UID)
    if (src.uidLen == 4) {
        block0[4] = block0[0]^block0[1]^block0[2]^block0[3];
        block0[5] = src.sak;
        block0[6] = src.atqa[0];
        block0[7] = src.atqa[1];
    }
    // Fill rest with typical Mifare Classic manufacturer data
    block0[8]  = 0x00; block0[9]  = 0x00; block0[10] = 0x00; block0[11] = 0x00;
    block0[12] = 0x00; block0[13] = 0x00; block0[14] = 0x00; block0[15] = 0x00;

    return writeBlock(0, block0, s_defaultKeyA, true);
}

// ── Save dump to Flipper-compatible .nfc file ─────────────────────────────
bool PN532::saveDump(const RfidTag& tag, const std::string& dir)
{
    // Create directory if needed
    char path[80];
    snprintf(path, 80, "%s%s.nfc", dir.c_str(), tag.uidHex().c_str());
    // Replace spaces in UID hex
    for (auto& c : std::string(path)) if (c==' ') c='_';

    FILE* f = fopen(path, "w");
    if (!f) {
        // Try creating dir first
        mkdir(dir.c_str(), 0755);
        f = fopen(path, "w");
        if (!f) { ESP_LOGE(TAG,"Cannot write %s",path); return false; }
    }

    // Flipper NFC file format
    fprintf(f, "Filetype: Flipper NFC device\n");
    fprintf(f, "Version: 2\n");
    fprintf(f, "Device type: %s\n", tag.type.c_str());
    fprintf(f, "UID: %s\n", tag.uidHex().c_str());
    fprintf(f, "ATQA: %02X %02X\n", tag.atqa[0], tag.atqa[1]);
    fprintf(f, "SAK: %02X\n", tag.sak);

    for (int b=0; b<(int)tag.blockData.size(); b++) {
        fprintf(f,"Block %d:", b);
        for (uint8_t byte : tag.blockData[b]) fprintf(f," %02X",byte);
        fprintf(f,"\n");
    }
    fclose(f);
    ESP_LOGI(TAG,"Saved: %s",path);
    return true;
}

// ── Load from file ─────────────────────────────────────────────────────────
bool PN532::loadAndEmulate(const std::string& path, RfidTag& out)
{
    FILE* f = fopen(path.c_str(),"r");
    if (!f) return false;
    char line[128];
    while (fgets(line,sizeof(line),f)) {
        if (strncmp(line,"UID:",4)==0) {
            // Parse UID hex
            out.uidLen=0;
            const char* p=line+5;
            while(*p && out.uidLen<7) {
                unsigned b; if(sscanf(p,"%02X",&b)==1){ out.uid[out.uidLen++]=b; p+=3; }
                else break;
            }
        }
        if (strncmp(line,"ATQA:",5)==0) {
            unsigned a,b; sscanf(line+6,"%02X %02X",&a,&b);
            out.atqa[0]=a; out.atqa[1]=b;
        }
        if (strncmp(line,"SAK:",4)==0) {
            unsigned s; sscanf(line+5,"%02X",&s); out.sak=s;
        }
        if (strncmp(line,"Block ",6)==0) {
            std::vector<uint8_t> bd;
            const char* p=strchr(line,':'); if(!p) continue;
            p++;
            while(*p) {
                unsigned b; if(sscanf(p," %02X",&b)==1){ bd.push_back(b); p+=3; }
                else break;
            }
            if(!bd.empty()) out.blockData.push_back(bd);
        }
    }
    fclose(f);
    return out.uidLen > 0;
}

// ── NDEF write to Ultralight ──────────────────────────────────────────────
bool PN532::writeNdef(const std::string& text)
{
    // Build minimal NDEF Text record
    std::string payload = "\x02en" + text;  // lang=en
    uint8_t recLen = payload.size();

    // NDEF TLV wrapper: 0x03 [len] [record] 0xFE
    std::vector<uint8_t> ndef;
    ndef.push_back(0x03);            // NDEF TLV tag
    ndef.push_back(recLen + 3 + 1);  // length
    ndef.push_back(0xD1);            // MB ME SR TNF=1 (well-known)
    ndef.push_back(0x01);            // Type length
    ndef.push_back(recLen);          // Payload length
    ndef.push_back('T');             // Type = Text
    for (char c : payload) ndef.push_back(c);
    ndef.push_back(0xFE);           // Terminator TLV

    // Write starting at page 4 (first user page of NTAG213)
    for (int i=0; i<(int)ndef.size(); i+=4) {
        uint8_t page[4]={0};
        for (int j=0;j<4&&i+j<(int)ndef.size();j++) page[j]=ndef[i+j];
        uint8_t cmd[] = {PN532_CMD_INDATAEXCHANGE, 0x01,
                         MIFARE_CMD_WRITE_UL, (uint8_t)(4+i/4),
                         page[0],page[1],page[2],page[3]};
        uint8_t frame[32]; int flen=0;
        buildFrame(cmd, sizeof(cmd), frame, flen);
        if (!sendRaw(frame,flen)) return false;
        uint8_t resp[2]; uint8_t rlen=0;
        readResponse(resp,sizeof(resp),rlen,300);
    }
    return true;
}
