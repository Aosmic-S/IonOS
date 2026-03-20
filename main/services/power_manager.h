#pragma once
class PowerManager {
public:
    static PowerManager& getInstance();
    void init();
    void monitorTask();
    int  getBatteryPercent() const { return m_pct; }
    int  getBatteryMV()      const { return m_mv; }
    bool isCharging()        const { return m_charging; }
    void resetSleepTimer();
private:
    PowerManager() = default;
    int  m_mv=4200, m_pct=100;
    bool m_charging=false;
    uint32_t m_lastActive=0;
};