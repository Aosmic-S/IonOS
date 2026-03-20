#pragma once
#include "drivers/wireless/wifi_driver.h"
#include <string>
#include <vector>
class WiFiManager {
public:
    static WiFiManager& getInstance();
    void init();
    bool connect(const char* ssid, const char* pass);
    void disconnect();
    bool isConnected() const;
    std::string getIP() const;
    int8_t getRSSI() const;
    void startScan();
    const std::vector<WifiNetwork>& getResults() const { return m_results; }
private:
    WiFiManager() = default;
    std::vector<WifiNetwork> m_results;
};