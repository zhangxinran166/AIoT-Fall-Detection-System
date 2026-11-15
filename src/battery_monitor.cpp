#include "battery_monitor.h"
#include <Wire.h>

// MAX17048:
// VCELL 0x02, 78.125 uV/LSB
// SOC   0x04, 1/256 %

void BatteryMonitor::begin() {
    update();
}

bool BatteryMonitor::readRegisters(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    if (Wire.requestFrom((int)ADDRESS, 2) != 2) {
        return false;
    }

    const uint8_t hi = Wire.read();
    const uint8_t lo = Wire.read();
    value = ((uint16_t)hi << 8) | lo;
    return true;
}

void BatteryMonitor::update() {
    const unsigned long now = millis();
    // 未接电池时连续失败会刷 I2C 错误，失败 3 次后降到每 60 秒探测一次。
    const unsigned long interval = failCount >= 3 ? 60000UL : READ_INTERVAL_MS;
    if (lastReadMs != 0 && now - lastReadMs < interval) {
        return;
    }
    lastReadMs = now;

    uint16_t vcell = 0;
    uint16_t soc = 0;

    if (!readRegisters(0x02, vcell) || !readRegisters(0x04, soc)) {
        available = false;
        voltageV = 0.0f;
        percent = 0;
        if (failCount < 255) ++failCount;
        return;
    }

    failCount = 0;
    voltageV = (float)vcell * 0.000078125f;
    percent = (int)((float)soc / 256.0f);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    available = true;
}
