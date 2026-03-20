// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce BlueJammer — Implementation                                        ║
// ║  Based on BlueJammer_ino.txt and Bruce NRF24 jammer                       ║
// ║  Apache License 2.0 — Copyright 2024 IonOS Contributors                   ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "bruce_bluejammer.h"
#include "drivers/wireless/nrf24_driver.h"
#include "config/pin_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "BlueJammer";

// ── NRF24 register addresses and constants ────────────────────────────────
// (mirrors RF24 library registers used by BlueJammer_ino.txt)
#define NRF_REG_CONFIG      0x00
#define NRF_REG_RF_CH       0x05
#define NRF_REG_RF_SETUP    0x06
#define NRF_REG_STATUS      0x07
#define NRF_REG_FEATURE     0x1D

// ── Constant carrier mode via NRF24 ──────────────────────────────────────
// The original BlueJammer uses RF24::startConstCarrier(RF24_PA_MAX, channel)
// This puts the NRF24L01+ into a continuous carrier output — maximum interference.
// In ESP-IDF, we access this via SPI writes to the NRF24 registers directly.
//
// Register sequence for constant carrier (from Nordic nRF24L01+ datasheet §6.3.2):
//   1. Set PWR_UP, PRIM_RX=0 in CONFIG
//   2. Set CONT_WAVE=1, PLL_LOCK=1 in RF_SETUP
//   3. CE pin high → radio outputs continuous carrier on selected channel

bool BlueJammer::startConstCarrier(uint8_t channel)
{
    auto& nrf = NRF24Driver::getInstance();
    nrf.setChannel(channel);

    // Write RF_SETUP: DR=2Mbps, PA=MAX, CONT_WAVE=1, PLL_LOCK=1
    // RF_SETUP register bits: [CONT_WAVE|0|RF_DR_LOW|PLL_LOCK|RF_DR_HIGH|RF_PWR|RF_PWR]
    // 0b10011110 = CONT_WAVE | PLL_LOCK | RF_DR_HIGH | PA_MAX
    nrf.writeRegister(NRF_REG_RF_SETUP, 0b10011110);

    // CONFIG: PWR_UP=1, PRIM_TX=0 (PTX mode), EN_CRC=0
    nrf.writeRegister(NRF_REG_CONFIG, 0b00000010);  // PWR_UP only

    vTaskDelay(pdMS_TO_TICKS(2));  // power-up settling time

    // CE high = start transmitting
    nrf.setCE(true);

    ESP_LOGI(TAG, "Constant carrier on ch%d", channel);
    return true;
}

void BlueJammer::stopConstCarrier()
{
    auto& nrf = NRF24Driver::getInstance();
    nrf.setCE(false);
    // Restore normal RF_SETUP
    nrf.writeRegister(NRF_REG_RF_SETUP, 0b00001110); // RF_DR_HIGH | PA_MAX (no CONT_WAVE)
    nrf.writeRegister(NRF_REG_CONFIG, 0b00001110);    // MASK_TX_DS | MASK_MAX_RT | EN_CRC | CRC0 | PWR_UP
}

bool BlueJammer::sendNoise(uint8_t channel)
{
    auto& nrf = NRF24Driver::getInstance();
    nrf.setChannel(channel);
    uint8_t payload[32];
    for (int i=0;i<32;i++) payload[i] = esp_random()&0xFF;
    return nrf.send(payload, 32);
}

// ═════════════════════════════════════════════════════════════════════════
// Jammer task — mirrors the BlueJammer loop() function exactly
// Mode SWEEP_WIDE: "two()" — bounce channels 37-79 with ±2 spacing
// Mode SWEEP_LOW:  "one()" — flood channels 0-14
// Mode SWEEP_FULL: hop all 126 channels
// Mode CONSTANT:   startConstCarrier on one channel
// ═════════════════════════════════════════════════════════════════════════
void BlueJammer::jamTask(void* arg)
{
    BlueJammer* self = (BlueJammer*)arg;
    auto& nrf = NRF24Driver::getInstance();

    // Setup NRF24 for jamming (mirrors BlueJammer initSP())
    // autoAck=off, retries=0, payload=5, addrWidth=3, PA=MAX, DR=2Mbps, CRC=off
    nrf.setAutoAck(false);
    nrf.setPALevel(3);        // RF24_PA_MAX
    nrf.setDataRate(2);       // RF24_2MBPS
    nrf.setCRCLength(0);      // RF24_CRC_DISABLED
    nrf.setPayloadSize(5);
    nrf.setAddressWidth(3);
    nrf.stopListening();

    // State for SWEEP_WIDE (original "two()"):
    uint8_t ch  = 45;   // start channel (original: i=45)
    int     flag= 0;    // 0=ascending, 1=descending

    while (!self->m_stop) {
        switch (self->m_mode) {

        // ── SWEEP_WIDE: bounce 37-79 with ±2 spacing ───────────────────
        // Direct port of BlueJammer "two()" function:
        //   if (flag==0) i+=2; else i-=2;
        //   if (i>79 && flag==0) flag=1;
        //   else if (i<2 && flag==1) flag=0;
        //   radio.setChannel(i);
        case Mode::SWEEP_WIDE: {
            if (flag == 0) ch += 2;
            else           ch -= 2;
            if (ch > 79 && flag == 0) flag = 1;
            else if (ch < 37 && flag == 1) { flag = 0; ch = 37; }
            nrf.setChannel(ch);
            self->m_channel = ch;
            self->m_hops++;
            if (self->m_cb) self->m_cb(ch, "SWEEP_WIDE");
            vTaskDelay(pdMS_TO_TICKS(1));
            break;
        }

        // ── SWEEP_LOW: flood channels 0-14 ──────────────────────────────
        // Direct port of BlueJammer "one()" function:
        //   for (int i=0; i<15; i++) radio.setChannel(i);
        case Mode::SWEEP_LOW: {
            for (int i=0; i<15 && !self->m_stop; i++) {
                nrf.setChannel(i);
                self->m_channel = i;
                self->m_hops++;
                if (self->m_cb) self->m_cb(i, "SWEEP_LOW");
                // No delay in original — as fast as possible
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            break;
        }

        // ── SWEEP_FULL: all 126 channels ──────────────────────────────
        case Mode::SWEEP_FULL: {
            for (int i=0; i<126 && !self->m_stop; i++) {
                nrf.setChannel(i);
                self->m_channel = i;
                self->m_hops++;
                uint8_t noise[5];
                for (int j=0;j<5;j++) noise[j]=esp_random()&0xFF;
                nrf.send(noise, 5);
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            break;
        }

        // ── CONSTANT: continuous carrier on one channel ────────────────
        case Mode::CONSTANT: {
            self->startConstCarrier(self->m_channel);
            // Hold until mode changes or stop requested
            while (!self->m_stop && self->m_mode == Mode::CONSTANT) {
                vTaskDelay(pdMS_TO_TICKS(100));
                self->m_hops++;
                if (self->m_cb) self->m_cb(self->m_channel, "CONSTANT");
            }
            self->stopConstCarrier();
            break;
        }
        } // switch
    }

    // Cleanup
    nrf.setCE(false);
    nrf.setAutoAck(true);
    nrf.setCRCLength(1);   // RE-enable CRC
    nrf.setChannel(76);    // Restore to IonOS default channel
    self->m_running = false;

    ESP_LOGI(TAG, "Jammer stopped after %lu channel hops",
             (unsigned long)self->m_hops);
    vTaskDelete(nullptr);
}

// ── Start / Stop ───────────────────────────────────────────────────────────
bool BlueJammer::start(Mode mode, uint8_t fixedChannel, JammerEventCb cb)
{
    stop();
    m_mode    = mode;
    m_channel = fixedChannel;
    m_cb      = cb;
    m_stop    = false;
    m_hops    = 0;
    m_running = true;

    const char* modeStr[] = {"SWEEP_WIDE","SWEEP_LOW","SWEEP_FULL","CONSTANT"};
    ESP_LOGI(TAG, "BlueJammer start: mode=%s ch=%d",
             modeStr[(int)mode], fixedChannel);

    xTaskCreatePinnedToCore(jamTask, "bruce_jammer", 4096, this, 6, &m_task, 0);
    return true;
}

void BlueJammer::stop()
{
    if (!m_task) return;
    m_stop    = true;
    m_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    vTaskDelete(m_task);
    m_task = nullptr;
    m_stop = false;
}
