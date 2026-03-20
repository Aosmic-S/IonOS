#pragma once
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  Bruce — Pentesting Toolkit for IonOS                                    ║
// ║  Ported from Bruce Firmware v1.14  (Apache 2.0)                          ║
// ║  Original: https://github.com/pr3y/Bruce                                 ║
// ║                                                                          ║
// ║  Mirrors Bruce's full feature set in LVGL-native UI.                     ║
// ║  Navigation:  UP/DOWN = list  X = select  B = back to IonOS menu         ║
// ╚══════════════════════════════════════════════════════════════════════════╝
#include "apps/app_manager.h"
#include <string>
#include <vector>
#include <functional>

// ── GPIO for IR LED (wire IR LED + 68Ω resistor between this pin and GND) ──
#define BRUCE_IR_TX_PIN  45

enum class BruceState {
    MAIN, WIFI, BLE, IR, NRF24, FILES, OTHERS, CONFIG,
    RUNNING, RESULT
};

struct BruceOpt {
    std::string           label;
    std::function<void()> action;
    bool                  enabled = true;
};

class BruceApp : public IonApp {
public:
    void onCreate()  override;
    void onResume()  override;
    void onPause()   override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;

private:
    // ── State ──────────────────────────────────────────────────────────────
    BruceState        m_state      = BruceState::MAIN;
    BruceState        m_prevState  = BruceState::MAIN;
    int               m_focusIdx   = 0;
    int               m_mainFocus  = 0;
    std::vector<BruceOpt> m_opts;

    // ── Content container (child of m_screen) ─────────────────────────────
    lv_obj_t*         m_content    = nullptr;
    lv_timer_t*       m_uiTimer    = nullptr;

    // ── Background task ────────────────────────────────────────────────────
    TaskHandle_t      m_bgTask     = nullptr;
    volatile bool     m_taskActive = false;
    volatile bool     m_taskStop   = false;

    void startTask(const char* name, TaskFunction_t fn, void* arg, int stack=8192);
    void stopTask();

    // ── Screen builders ────────────────────────────────────────────────────
    void showMain();
    void showList(const char* title, std::vector<BruceOpt> opts, uint32_t accentCol);
    void showRunning(const char* title, const char* detail);
    void showResult(const char* title, const char* msg, bool ok);
    void showQR(const char* data, const char* caption);
    void clearContent();
    void rebuildList(uint32_t accentCol);

    // ── Feature menus ──────────────────────────────────────────────────────
    void wifiMenu();    void wifiScan();    void wifiAtks();
    void wifiDeauth();  void wifiBeacon();  void wifiSniffer();
    void wifiEvilPortal();

    void bleMenu();     void bleSpam();     void bleScan();
    void irMenu();      void irTvBGone();   void irCustom();
    void nrf24Menu();   void nrf24Jammer(); void nrf24Spectrum();
    void filesMenu();
    void othersMenu();  void qrMenu();
    void configMenu();

    // ── Static task functions ──────────────────────────────────────────────
    static void taskBleSpam     (void* arg);
    static void taskWifiDeauth  (void* arg);
    static void taskWifiBeacon  (void* arg);
    static void taskWifiSniffer (void* arg);
    static void taskBleScan     (void* arg);
    static void taskIrTvBGone   (void* arg);
    static void taskNrf24Jammer (void* arg);
    static void taskNrf24Spectrum(void*arg);

    // ── Runtime state ──────────────────────────────────────────────────────
    int          m_bleSpamCount = 0;
    int          m_nrfRssi[126] = {};
    lv_obj_t*    m_specChart    = nullptr;
    lv_chart_series_t* m_specSeries = nullptr;

    // ── UI helpers ─────────────────────────────────────────────────────────
    static void makeTitleBar(lv_obj_t* parent, const char* title, uint32_t col);
    static uint32_t accentOf(BruceState s);

    // ── Colours ────────────────────────────────────────────────────────────
    static constexpr uint32_t CB  = 0x000000;
    static constexpr uint32_t CS  = 0x0d1117;
    static constexpr uint32_t CT  = 0xe6edf3;
    static constexpr uint32_t CD  = 0x8b949e;
    static constexpr uint32_t CWIFI = 0x58a6ff;
    static constexpr uint32_t CBLE  = 0x1f6feb;
    static constexpr uint32_t CIR   = 0xbc8cff;
    static constexpr uint32_t CNRF  = 0x56d364;
    static constexpr uint32_t CFILE = 0x79c0ff;
    static constexpr uint32_t COTH  = 0xd29922;
    static constexpr uint32_t CCFG  = 0x8b949e;
    static constexpr uint32_t COK   = 0x56d364;
    static constexpr uint32_t CERR  = 0xf85149;
};
