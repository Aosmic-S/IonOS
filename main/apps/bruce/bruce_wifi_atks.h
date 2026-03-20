#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce WiFi Attacks — PMKID / Handshake / Karma / Probe Sniffer         ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include "esp_wifi_types.h"

// ── AP record ─────────────────────────────────────────────────────────────
struct BruceAP {
    uint8_t     bssid[6];
    std::string ssid;
    int8_t      rssi;
    uint8_t     channel;
    uint8_t     authMode;  // wifi_auth_mode_t
};

// ── Probe request record ──────────────────────────────────────────────────
struct ProbeReq {
    uint8_t     mac[6];
    std::string ssid;      // requested SSID (empty = wildcard)
    int8_t      rssi;
    uint32_t    ts;
};

// ── Handshake tracker ─────────────────────────────────────────────────────
struct HandshakeTracker {
    uint8_t  bssid[6]  = {};
    std::string ssid;
    bool     msg1 = false, msg2 = false, msg3 = false, msg4 = false;
    uint32_t firstSeen = 0;
    bool complete() const { return (msg1&&msg2)||(msg3&&msg4); }
};

// ── PCAP file header constants ────────────────────────────────────────────
static constexpr uint32_t PCAP_MAGIC    = 0xA1B2C3D4;
static constexpr uint16_t PCAP_VER_MAJ  = 2;
static constexpr uint16_t PCAP_VER_MIN  = 4;
static constexpr uint32_t PCAP_LINKTYPE = 105; // IEEE 802.11

using SnifferEventCb = std::function<void(const std::string& event)>;

// ══════════════════════════════════════════════════════════════════════════
// WifiAttacks — all WiFi passive/active attack modes
// ══════════════════════════════════════════════════════════════════════════
class WifiAttacks {
public:
    WifiAttacks() = default;
    ~WifiAttacks() { stop(); }

    // ── Scan ──────────────────────────────────────────────────────────────
    bool scan(std::vector<BruceAP>& out, bool hidden = false);

    // ── Handshake / EAPOL capture ─────────────────────────────────────────
    // Captures WPA 4-way handshake frames; saves PCAP to /sdcard/bruce_hs/
    // Optionally sends deauth bursts to trigger re-auth
    bool startHandshakeCapture(const BruceAP& target,
                               bool deauth,
                               SnifferEventCb cb = nullptr);

    // ── PMKID capture ─────────────────────────────────────────────────────
    // Captures EAPOL Msg1 (contains PMKID in RSN IE for PMKID attack)
    // Saves raw .pcap; compatible with hcxtools/hashcat -m 22000
    bool startPmkidCapture(const BruceAP& target,
                           SnifferEventCb cb = nullptr);

    // ── Karma (rogue AP) attack ───────────────────────────────────────────
    // Listens for probe requests; responds to each with a matching AP
    bool startKarmaAttack(SnifferEventCb cb = nullptr);

    // ── Probe sniffer ─────────────────────────────────────────────────────
    // Passively sniffs probe request frames; logs to /sdcard/bruce_probes.txt
    bool startProbeSniffer(SnifferEventCb cb = nullptr);

    // ── Raw packet sniffer ────────────────────────────────────────────────
    // Channel-hopping PCAP capture; saves to /sdcard/bruce_raw_N.pcap
    bool startRawSniffer(SnifferEventCb cb = nullptr);

    void stop();
    bool isRunning() const { return m_running; }

    // Results
    const std::vector<ProbeReq>&        probeRequests()  const { return m_probes; }
    const std::vector<HandshakeTracker>&handshakes()     const { return m_handshakes; }
    int pmkidCount() const { return m_pmkidCount; }

private:
    bool        m_running       = false;
    int         m_pmkidCount    = 0;
    SnifferEventCb m_cb;
    BruceAP     m_targetAP;
    TaskHandle_t m_task         = nullptr;
    volatile bool m_stop        = false;

    std::vector<ProbeReq>          m_probes;
    std::vector<HandshakeTracker>  m_handshakes;
    std::map<uint64_t, HandshakeTracker> m_hsMap;
    std::map<std::string, bool>    m_karmaBeacons;
    FILE*       m_pcapFile      = nullptr;
    int         m_pcapFileIdx   = 0;

    enum class Mode { HANDSHAKE, PMKID, KARMA, PROBE, RAW } m_mode;

    // Promiscuous callback (static)
    static void promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
    static WifiAttacks* s_inst;

    // Frame analysis
    bool isEAPOL(const uint8_t* payload, int len, int* msgNum);
    bool isPmkidFrame(const uint8_t* payload, int len);
    bool isProbeRequest(const uint8_t* payload, int len, ProbeReq& out);
    bool isBeacon(const uint8_t* payload, int len, BruceAP& out);
    uint64_t macKey(const uint8_t* mac);

    // PCAP writing
    void pcapWriteHeader(FILE* f);
    void pcapWritePacket(FILE* f, const uint8_t* data, uint32_t len);

    // Deauth helper
    void sendDeauth(const uint8_t* bssid, const uint8_t* target, uint8_t channel);

    // Task functions
    static void taskHandshake (void* arg);
    static void taskPmkid     (void* arg);
    static void taskKarma     (void* arg);
    static void taskProbe     (void* arg);
    static void taskRaw       (void* arg);

    void event(const std::string& msg);
};
