#include "audio_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static const char* TAG = "AudioDrv";
AudioDriver& AudioDriver::getInstance() { static AudioDriver i; return i; }

esp_err_t AudioDriver::init() {
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ch.dma_desc_num  = AUDIO_DMA_BUF_NUM;
    ch.dma_frame_num = AUDIO_DMA_BUF_LEN;
    ESP_ERROR_CHECK(i2s_new_channel(&ch, &m_tx, nullptr));

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_I2S_BCK,
            .ws   = (gpio_num_t)PIN_I2S_LRCK,
            .dout = (gpio_num_t)PIN_I2S_DATA,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv=false,.bclk_inv=false,.ws_inv=false }
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(m_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(m_tx));
    ESP_LOGI(TAG, "Init I2S %dHz stereo 16-bit", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

void AudioDriver::play(const uint8_t* pcm, size_t len) {
    m_pcm      = pcm;
    m_pcmLen   = len;
    m_pcmPos   = 0;
    m_playing  = true;
    m_paused   = false;
    m_fileMode = false;
}

void AudioDriver::playFile(const char* path) {
    strncpy(m_filePath, path, sizeof(m_filePath)-1);
    m_fileMode = true;
    m_playing  = true;
    m_paused   = false;
    m_pcmPos   = 0;
}

void AudioDriver::stop()   { m_playing = false; m_paused = false; m_pcm = nullptr; }
void AudioDriver::pause()  { m_paused  = true; }
void AudioDriver::resume() { m_paused  = false; }
void AudioDriver::setVolume(uint8_t v) { m_vol = v > 100 ? 100 : v; }

void AudioDriver::writeI2S(const int16_t* buf, size_t samples) {
    // Apply software volume
    static int16_t vbuf[CHUNK*2];
    float gain = m_vol / 100.0f;
    for (size_t i=0; i<samples; i++)
        vbuf[i] = (int16_t)(buf[i] * gain);
    size_t written = 0;
    i2s_channel_write(m_tx, vbuf, samples*2, &written, pdMS_TO_TICKS(100));
}

void AudioDriver::streamTask() {
    static int16_t chunk[CHUNK*2];
    while (true) {
        if (!m_playing || m_paused) {
            // Write silence to keep I2S clock alive
            memset(chunk, 0, sizeof(chunk));
            size_t w=0; i2s_channel_write(m_tx, chunk, sizeof(chunk), &w, pdMS_TO_TICKS(20));
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (m_fileMode) {
            // Stream WAV from SD card
            FILE* f = fopen(m_filePath, "rb");
            if (!f) { m_playing = false; continue; }
            fseek(f, 44, SEEK_SET); // Skip WAV header
            size_t n;
            while (m_playing && !m_paused && (n = fread(chunk, 2, CHUNK*2, f)) > 0) {
                writeI2S(chunk, n);
            }
            fclose(f);
            m_playing  = false;
            m_fileMode = false;
            if (m_doneCb) m_doneCb();
        } else if (m_pcm) {
            size_t remaining = m_pcmLen - m_pcmPos;
            size_t take = (remaining > CHUNK*2*2) ? CHUNK*2 : remaining/2;
            if (take == 0) {
                m_playing = false;
                if (m_doneCb) m_doneCb();
                continue;
            }
            writeI2S((const int16_t*)(m_pcm + m_pcmPos), take);
            m_pcmPos += take * 2;
        }
    }
}
