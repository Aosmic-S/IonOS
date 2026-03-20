#pragma once
#include "config/pin_config.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <functional>
using NRFCallback = std::function<void(const uint8_t*, uint8_t)>;

class NRF24Driver {
public:
    static NRF24Driver& getInstance();
    esp_err_t init();
    void setChannel(uint8_t ch);
    void setAddress(const uint8_t addr[5]);
    bool readCarrierDetect();
    // BlueJammer / low-level NRF24 control
    void setAutoAck(int val);
    void setPALevel(int val);
    void setDataRate(int val);
    void setCRCLength(int val);
    void setPayloadSize(int val);
    void setAddressWidth(int val);
    void setCE(bool high);
    bool writeRegister(uint8_t reg, uint8_t val);
  // Read CD register for spectrum scan
    bool send(const uint8_t* data, uint8_t len);
    bool available();
    uint8_t read(uint8_t* buf);
    void startListening();
    void stopListening();
    void setRxCallback(NRFCallback cb) { m_cb = cb; }
    void pollTask();
private:
    NRF24Driver() = default;
    void     writeReg(uint8_t reg, uint8_t val);
    uint8_t  readReg(uint8_t reg);
    void     writeRegs(uint8_t reg, const uint8_t* buf, uint8_t len);
    void     command(uint8_t cmd);
    void     csn(bool low);
    void     ce(bool high);
    spi_device_handle_t m_spi = nullptr;
    NRFCallback m_cb;
    bool m_listening = false;
};
