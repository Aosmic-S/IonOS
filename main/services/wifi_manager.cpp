#include "wifi_manager.h"
#include "ui/notification_popup.h"
#include "drivers/rgb/ws2812_driver.h"
#include "esp_log.h"
#include "nvs.h"
static const char* TAG = "WiFiMgr";
WiFiManager& WiFiManager::getInstance(){ static WiFiManager i; return i; }
void WiFiManager::init() {
    WiFiDriver::getInstance().setCallback([](bool ok, const char* ip){
        if (ok) { NotificationPopup::getInstance().show("WiFi",
                    (std::string("Connected: ")+ip).c_str(), ION_NOTIF_SUCCESS, 3000);
                  WS2812Driver::getInstance().setAnimation(LEDAnim::NONE); }
        else { WS2812Driver::getInstance().setAnimation(LEDAnim::WIFI_BLINK); }
    });
    // Auto-connect to saved credentials
    nvs_handle_t h; char ssid[64]={}, pass[64]={};
    size_t s1=sizeof(ssid), s2=sizeof(pass);
    if (nvs_open("ionos_wifi",NVS_READONLY,&h)==ESP_OK) {
        nvs_get_str(h,"ssid",ssid,&s1); nvs_get_str(h,"pass",pass,&s2); nvs_close(h);
        if (ssid[0]) { ESP_LOGI(TAG,"Auto-connect to %s",ssid); connect(ssid,pass); }
    }
}
bool WiFiManager::connect(const char* ssid, const char* pass) {
    WS2812Driver::getInstance().setAnimation(LEDAnim::WIFI_BLINK);
    bool ok = WiFiDriver::getInstance().connect(ssid,pass)==ESP_OK;
    if (ok) {
        nvs_handle_t h;
        if (nvs_open("ionos_wifi",NVS_READWRITE,&h)==ESP_OK) {
            nvs_set_str(h,"ssid",ssid); nvs_set_str(h,"pass",pass);
            nvs_commit(h); nvs_close(h);
        }
    }
    return ok;
}
void WiFiManager::disconnect() { WiFiDriver::getInstance().disconnect(); }
bool WiFiManager::isConnected() const { return WiFiDriver::getInstance().isConnected(); }
std::string WiFiManager::getIP() const { return WiFiDriver::getInstance().getIP(); }
int8_t WiFiManager::getRSSI() const { return WiFiDriver::getInstance().getRSSI(); }
void WiFiManager::startScan() { m_results.clear(); WiFiDriver::getInstance().scan(m_results); }