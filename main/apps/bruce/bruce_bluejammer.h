#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce BlueJammer — 2.4GHz Jammer                                        ║
// ║  Based on BlueJammer by [original author] and IonOS NRF24Driver          ║
// ║  Ported from uploaded BlueJammer_ino.txt                                  ║
// ║                                                                           ║
// ║  Modes (from original code):                                              ║
// ║   SWEEP_WIDE  — bounce channels 37-79 with ±2 spacing (Bluetooth range)  ║
// ║   SWEEP_LOW   — flood channels 0-14 (WiFi ch 1-3, Zigbee, BLE adv)      ║
// ║   SWEEP_FULL  — hop all 0-125 channels rapidly                           ║
// ║   CONSTANT    — hold one channel with constant carrier (max interference) ║
// ║                                                                           ║
// ║  Hardware: NRF24L01+ (already wired in IonOS) + constant carrier mode    ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include <stdint.h>
#include <functional>

using JammerEventCb = std::function<void(uint8_t channel, const char* mode)>;

class BlueJammer {
public:
    enum class Mode {
        SWEEP_WIDE,   // BT+BLE channels 37-79 bounce (original "two()" mode)
        SWEEP_LOW,    // WiFi low channels 0-14 (original "one()" mode)
        SWEEP_FULL,   // Full 0-125 sweep
        CONSTANT,     // Constant carrier on one channel
    };

    BlueJammer() = default;
    ~BlueJammer() { stop(); }

    bool start(Mode mode = Mode::SWEEP_WIDE,
               uint8_t fixedChannel = 45,
               JammerEventCb cb = nullptr);
    void stop();

    bool   isRunning()    const { return m_running; }
    uint8_t currentChannel() const { return m_channel; }
    Mode   currentMode()   const { return m_mode; }
    uint32_t hopCount()    const { return m_hops; }

    // Change mode on-the-fly (takes effect immediately)
    void setMode(Mode m) { m_mode = m; }

private:
    volatile bool    m_running  = false;
    volatile bool    m_stop     = false;
    volatile uint8_t m_channel  = 45;
    volatile Mode    m_mode     = Mode::SWEEP_WIDE;
    volatile uint32_t m_hops    = 0;
    JammerEventCb    m_cb;
    TaskHandle_t     m_task     = nullptr;

    // NRF24 constant carrier mode
    bool    startConstCarrier(uint8_t channel);
    void    stopConstCarrier();
    bool    sendNoise(uint8_t channel);

    static void jamTask(void* arg);
};
