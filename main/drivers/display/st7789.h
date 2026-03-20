#pragma once
#include "config/pin_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl/lvgl.h"
#include "esp_err.h"

class ST7789Driver {
public:
    static ST7789Driver& getInstance();
    esp_err_t init();
    void      setBacklight(uint8_t percent);   // 0-100
    void      fillScreen(uint16_t color565);
    void      fillRect(int x, int y, int w, int h, uint16_t color565);
    void      lvglFlushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf);
    bool      isBusy() const { return m_busy; }

private:
    ST7789Driver() = default;
    void      sendCmd(uint8_t cmd);
    void      sendData(const uint8_t* data, size_t len);
    void      sendByte(uint8_t b);
    void      setAddrWindow(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1);
    void      initRegisters();
    static void flushReadyCb(spi_transaction_t* t);

    spi_device_handle_t m_spi    = nullptr;
    lv_disp_drv_t       m_driver = {};
    lv_disp_t*          m_disp   = nullptr;
    lv_color_t*         m_buf1   = nullptr;
    lv_color_t*         m_buf2   = nullptr;
    volatile bool       m_busy   = false;
    static const int    BUF_LINES = 20;  // 2MB PSRAM: 320×20×2×2 = 25.6KB
};
