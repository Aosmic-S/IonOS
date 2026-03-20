#pragma once
#include "esp_wifi.h"
#include "esp_err.h"
#include <string>
#include <vector>
#include <functional>

struct WifiNetwork { std::string ssid; int8_t rssi; uint8_t authmode; };
using WifiCallback = std::function<void(bool connected, const char* ip)>;

class WiFiDriver {
public:
    static WiFiDriver& getInstance();
    esp_err_t init();
    esp_err_t connect(const char* ssid, const char* pass);
    void      disconnect();
    void      scan(std::vector<WifiNetwork>& out);
    bool      isConnected() const { return m_connected; }
    std::string getSSID()   const { return m_ssid; }
    std::string getIP()     const { return m_ip; }
    int8_t    getRSSI()     const;
    void      setCallback(WifiCallback cb) { m_cb = cb; }

private:
    WiFiDriver() = default;
    static void eventHandler(void*, esp_event_base_t, int32_t, void*);
    bool        m_connected = false;
    std::string m_ssid, m_ip;
    WifiCallback m_cb;
};
