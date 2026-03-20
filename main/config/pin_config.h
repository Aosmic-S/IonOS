#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║              IonOS Hardware Pin Configuration                    ║
// ╚══════════════════════════════════════════════════════════════════╝

// ── Display ST7789V (SPI2) ────────────────────────────────────────
#define PIN_LCD_MOSI    11
#define PIN_LCD_SCLK    12
#define PIN_LCD_CS      10
#define PIN_LCD_DC       9
#define PIN_LCD_RST      8
#define PIN_LCD_BLK     13
#define DISPLAY_WIDTH  320   // Landscape
#define DISPLAY_HEIGHT 240   // Landscape
#define DISPLAY_SPI_HZ 40000000

// ── SD Card (SPI3) ────────────────────────────────────────────────
#define PIN_SD_MOSI     35
#define PIN_SD_MISO     37
#define PIN_SD_SCLK     36
#define PIN_SD_CS       38
#define SD_MOUNT_POINT  "/sdcard"
#define SD_MAX_FILES     5

// ── Audio PCM5102A (I2S0) ─────────────────────────────────────────
#define PIN_I2S_BCK      4
#define PIN_I2S_LRCK     5
#define PIN_I2S_DATA     6
#define AUDIO_SAMPLE_RATE  44100
#define AUDIO_DMA_BUF_LEN  512
#define AUDIO_DMA_BUF_NUM  8

// ── Buttons (active LOW, internal pull-up) ────────────────────────
#define PIN_BTN_UP      14
#define PIN_BTN_DOWN    15
#define PIN_BTN_LEFT    16
#define PIN_BTN_RIGHT   17
#define PIN_BTN_A       18
#define PIN_BTN_B       19
#define PIN_BTN_X       20
#define PIN_BTN_START   21
#define PIN_BTN_MENU    47
#define BTN_DEBOUNCE_MS 20
#define BTN_LONGPRESS_MS 800

// ── WS2812B RGB LEDs (RMT) ────────────────────────────────────────
#define PIN_LED_DATA    48
#define LED_COUNT        7
#define LED_RMT_CLK_HZ  10000000

// ── nRF24L01+ Radio (shares SPI2 with display, different CS) ─────
#define PIN_NRF_MOSI     2
#define PIN_NRF_MISO     3
#define PIN_NRF_SCLK     7
#define PIN_NRF_CS      39
#define PIN_NRF_CE      40
#define PIN_NRF_IRQ     41
#define NRF_SPI_HZ       8000000
#define NRF_CHANNEL      76
#define NRF_PAYLOAD_SIZE 32

// ── Power Management ──────────────────────────────────────────────
#define PIN_BATT_ADC     1   // ADC1_CH0  (voltage divider 2:1)
#define PIN_CHG_STATUS   0   // Low = charging
#define ION_BATT_FULL_MV   4200
#define ION_BATT_LOW_MV    3600
#define ION_BATT_CRIT_MV   3300
#define ION_BATT_DIV_RATIO 2.0f
#define ION_SLEEP_TIMEOUT_MS   60000  // 1 min dim
#define ION_DEEPSLEEP_MS      120000  // 2 min deep sleep
