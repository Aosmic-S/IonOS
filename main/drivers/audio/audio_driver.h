#pragma once
// ── Audio Driver ─────────────────────────────────────────────────────────
#include "config/pin_config.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <functional>

class AudioDriver {
public:
    static AudioDriver& getInstance();
    esp_err_t init();
    void play(const uint8_t* pcm16stereo, size_t len);
    void playFile(const char* path);          // WAV from SD
    void stop();
    void pause();
    void resume();
    void setVolume(uint8_t vol);              // 0-100
    uint8_t getVolume() const { return m_vol; }
    bool isPlaying() const { return m_playing; }
    void setDoneCallback(std::function<void()> cb) { m_doneCb = cb; }
    void streamTask();                        // Blocking — run in dedicated task

private:
    AudioDriver() = default;
    void writeI2S(const int16_t* buf, size_t samples);
    i2s_chan_handle_t      m_tx      = nullptr;
    const uint8_t*         m_pcm     = nullptr;
    size_t                 m_pcmLen  = 0;
    size_t                 m_pcmPos  = 0;
    bool                   m_playing = false;
    bool                   m_paused  = false;
    uint8_t                m_vol     = 80;
    std::function<void()>  m_doneCb;
    char                   m_filePath[256] = {};
    bool                   m_fileMode = false;
    static const size_t    CHUNK     = 512;
};
