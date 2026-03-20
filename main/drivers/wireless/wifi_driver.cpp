#include "wifi_driver.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/ip4_addr.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char* TAG     = "WiFiDrv";
static EventGroupHandle_t s_wifi_eg;
static const int CONNECTED_BIT = BIT0;

WiFiDriver& WiFiDriver::getInstance(){ static WiFiDriver i; return i; }

esp_err_t WiFiDriver::init() {
    s_wifi_eg = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT,   ESP_EVENT_ANY_ID, &eventHandler, this);
    esp_event_handler_register(IP_EVENT,     IP_EVENT_STA_GOT_IP, &eventHandler, this);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi STA ready");
    return ESP_OK;
}

esp_err_t WiFiDriver::connect(const char* ssid, const char* pass) {
    wifi_config_t wc = {};
    strncpy((char*)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid)-1);
    strncpy((char*)wc.sta.password, pass, sizeof(wc.sta.password)-1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
    // Wait up to 15s
    EventBits_t b = xEventGroupWaitBits(s_wifi_eg, CONNECTED_BIT, false, true, pdMS_TO_TICKS(15000));
    return (b & CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void WiFiDriver::disconnect() { esp_wifi_disconnect(); m_connected=false; }

void WiFiDriver::scan(std::vector<WifiNetwork>& out) {
    wifi_scan_config_t sc = {}; sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_wifi_scan_start(&sc, true);
    uint16_t n = 0; esp_wifi_scan_get_ap_num(&n);
    if (!n) return;
    wifi_ap_record_t* aps = new wifi_ap_record_t[n];
    esp_wifi_scan_get_ap_records(&n, aps);
    for(int i=0;i<n;i++) out.push_back({(char*)aps[i].ssid, aps[i].rssi, aps[i].authmode});
    delete[] aps;
}

int8_t WiFiDriver::getRSSI() const {
    wifi_ap_record_t r; return esp_wifi_sta_get_ap_info(&r)==ESP_OK ? r.rssi : -100;
}

void WiFiDriver::eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    WiFiDriver* self = (WiFiDriver*)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        self->m_connected = false;
        esp_wifi_connect(); // Auto-reconnect
        if (self->m_cb) self->m_cb(false, "");
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* ev = (ip_event_got_ip_t*)data;
        char ip[16]; snprintf(ip,sizeof(ip),IPSTR,IP2STR(&ev->ip_info.ip));
        self->m_ip        = ip;
        self->m_connected = true;
        xEventGroupSetBits(s_wifi_eg, CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected! IP: %s", ip);
        if (self->m_cb) self->m_cb(true, ip);
    }
}
