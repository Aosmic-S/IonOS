#include "st7789_driver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "ST7789";

// ── ST7789 command set ───────────────────────────────────────────────────
#define ST7789_NOP      0x00
#define ST7789_SWRESET  0x01
#define ST7789_SLPIN    0x10
#define ST7789_SLPOUT   0x11
#define ST7789_NORON    0x13
#define ST7789_INVOFF   0x20
#define ST7789_INVON    0x21
#define ST7789_DISPOFF  0x28
#define ST7789_DISPON   0x29
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_MADCTL   0x36
#define ST7789_COLMOD   0x3A
#define ST7789_PORCTRL  0xB2
#define ST7789_GCTRL    0xB7
#define ST7789_VCOMS    0xBB
#define ST7789_LCMCTRL  0xC0
#define ST7789_VDVVRHEN 0xC2
#define ST7789_VRHS     0xC3
#define ST7789_VDVSET   0xC4
#define ST7789_FRCTR2   0xC6
#define ST7789_PWCTRL1  0xD0
#define ST7789_PVGAMCTRL 0xE0
#define ST7789_NVGAMCTRL 0xE1

ST7789Driver& ST7789Driver::getInstance() { static ST7789Driver i; return i; }

esp_err_t ST7789Driver::init() {
    ESP_LOGI(TAG, "Init %dx%d, SPI@%dMHz", DISPLAY_WIDTH, DISPLAY_HEIGHT,
             DISPLAY_SPI_HZ/1000000);

    // ── SPI bus ──────────────────────────────────────────────────────────
    spi_bus_config_t bus = {};
    bus.mosi_io_num   = PIN_LCD_MOSI;
    bus.miso_io_num   = -1;
    bus.sclk_io_num   = PIN_LCD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = DISPLAY_WIDTH * BUF_LINES * 2 + 8;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = DISPLAY_SPI_HZ;
    dev.mode           = 0;
    dev.spics_io_num   = PIN_LCD_CS;
    dev.queue_size     = 7;
    dev.pre_cb         = nullptr;
    dev.post_cb        = flushReadyCb;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &m_spi));

    // ── Control GPIOs ────────────────────────────────────────────────────
    gpio_set_direction((gpio_num_t)PIN_LCD_DC,  GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)PIN_LCD_RST, GPIO_MODE_OUTPUT);

    // ── Hardware reset ───────────────────────────────────────────────────
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    initRegisters();

    // ── Backlight via LEDC PWM ────────────────────────────────────────────
    ledc_timer_config_t lcTimer = {};
    lcTimer.speed_mode      = LEDC_LOW_SPEED_MODE;
    lcTimer.timer_num       = LEDC_TIMER_0;
    lcTimer.duty_resolution = LEDC_TIMER_8_BIT;
    lcTimer.freq_hz         = 5000;
    lcTimer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&lcTimer);

    ledc_channel_config_t lcChan = {};
    lcChan.speed_mode = LEDC_LOW_SPEED_MODE;
    lcChan.channel    = LEDC_CHANNEL_0;
    lcChan.timer_sel  = LEDC_TIMER_0;
    lcChan.intr_type  = LEDC_INTR_DISABLE;
    lcChan.gpio_num   = PIN_LCD_BLK;
    lcChan.duty       = 0;
    ledc_channel_config(&lcChan);
    setBacklight(100);

    // ── LVGL display buffers (PSRAM) ──────────────────────────────────────
    size_t bufSz = DISPLAY_WIDTH * BUF_LINES * sizeof(lv_color_t);
    m_buf1 = (lv_color_t*)heap_caps_malloc(bufSz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    m_buf2 = (lv_color_t*)heap_caps_malloc(bufSz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(m_buf1 && m_buf2);

    lv_disp_draw_buf_t* drawBuf = (lv_disp_draw_buf_t*)heap_caps_malloc(
        sizeof(lv_disp_draw_buf_t), MALLOC_CAP_INTERNAL);
    lv_disp_draw_buf_init(drawBuf, m_buf1, m_buf2, DISPLAY_WIDTH * BUF_LINES);

    lv_disp_drv_init(&m_driver);
    m_driver.draw_buf    = drawBuf;
    m_driver.flush_cb    = [](lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* b){
        ST7789Driver::getInstance().lvglFlushCb(d,a,b);
    };
    m_driver.hor_res     = DISPLAY_WIDTH;
    m_driver.ver_res     = DISPLAY_HEIGHT;
    m_driver.full_refresh = 0;
    m_disp = lv_disp_drv_register(&m_driver);

    ESP_LOGI(TAG, "Init OK  buf=%zuKB PSRAM", bufSz*2/1024);
    return ESP_OK;
}

void ST7789Driver::initRegisters() {
    sendCmd(ST7789_SWRESET); vTaskDelay(pdMS_TO_TICKS(150));
    sendCmd(ST7789_SLPOUT);  vTaskDelay(pdMS_TO_TICKS(10));
    sendCmd(ST7789_COLMOD);  sendByte(0x55);  // 16-bit color
    // Landscape: MX=1 MV=1 (0x60). If display is mirrored try 0xA0 or 0x00.
    sendCmd(ST7789_MADCTL);  sendByte(0x60);  // Landscape 320×240
    sendCmd(ST7789_INVON);
    sendCmd(ST7789_PORCTRL); { uint8_t d[]={0x0C,0x0C,0x00,0x33,0x33}; sendData(d,5); }
    sendCmd(ST7789_GCTRL);   sendByte(0x35);
    sendCmd(ST7789_VCOMS);   sendByte(0x19);
    sendCmd(ST7789_LCMCTRL); sendByte(0x2C);
    sendCmd(ST7789_VDVVRHEN);sendByte(0x01);
    sendCmd(ST7789_VRHS);    sendByte(0x12);
    sendCmd(ST7789_VDVSET);  sendByte(0x20);
    sendCmd(ST7789_FRCTR2);  sendByte(0x0F);
    sendCmd(ST7789_PWCTRL1); { uint8_t d[]={0xA4,0xA1}; sendData(d,2); }
    sendCmd(ST7789_PVGAMCTRL);{uint8_t g[]={0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23};sendData(g,14);}
    sendCmd(ST7789_NVGAMCTRL);{uint8_t g[]={0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23};sendData(g,14);}
    sendCmd(ST7789_DISPON);
    sendCmd(ST7789_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void ST7789Driver::sendCmd(uint8_t cmd) {
    gpio_set_level((gpio_num_t)PIN_LCD_DC, 0);
    spi_transaction_t t = {};
    t.length    = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(m_spi, &t);
}
void ST7789Driver::sendByte(uint8_t b) {
    gpio_set_level((gpio_num_t)PIN_LCD_DC, 1);
    spi_transaction_t t = {};
    t.length    = 8;
    t.tx_buffer = &b;
    spi_device_polling_transmit(m_spi, &t);
}
void ST7789Driver::sendData(const uint8_t* data, size_t len) {
    gpio_set_level((gpio_num_t)PIN_LCD_DC, 1);
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(m_spi, &t);
}

void ST7789Driver::setAddrWindow(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) {
    sendCmd(ST7789_CASET);
    uint8_t cx[] = {(uint8_t)(x0>>8),(uint8_t)x0,(uint8_t)(x1>>8),(uint8_t)x1};
    sendData(cx,4);
    sendCmd(ST7789_RASET);
    uint8_t ry[] = {(uint8_t)(y0>>8),(uint8_t)y0,(uint8_t)(y1>>8),(uint8_t)y1};
    sendData(ry,4);
    sendCmd(ST7789_RAMWR);
}

void ST7789Driver::fillScreen(uint16_t c) {
    fillRect(0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,c);
}
void ST7789Driver::fillRect(int x,int y,int w,int h,uint16_t c) {
    setAddrWindow(x,y,x+w-1,y+h-1);
    uint8_t hi = c>>8, lo = c&0xFF;
    for (int i=0; i<w*h; i++) { sendByte(hi); sendByte(lo); }
}

void ST7789Driver::setBacklight(uint8_t pct) {
    uint32_t duty = (pct * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void ST7789Driver::lvglFlushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf) {
    uint16_t x1=area->x1, y1=area->y1, x2=area->x2, y2=area->y2;
    setAddrWindow(x1,y1,x2,y2);
    size_t len = (x2-x1+1)*(y2-y1+1)*2;
    gpio_set_level((gpio_num_t)PIN_LCD_DC, 1);
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = (uint8_t*)buf;
    t.user      = (void*)drv;
    m_busy = true;
    spi_device_queue_trans(m_spi, &t, portMAX_DELAY);
}
void ST7789Driver::flushReadyCb(spi_transaction_t* t) {
    ST7789Driver::getInstance().m_busy = false;
    lv_disp_flush_ready((lv_disp_drv_t*)t->user);
}
