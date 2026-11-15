#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

class BatteryMonitor {
public:
    void begin();
    void update();

    bool isAvailable() const { return available; }
    float getVoltage() const { return voltageV; }
    int getPercent() const { return percent; }
    bool isLow() const { return available && percent <= 15; }

private:
    static const uint8_t ADDRESS = 0x36;
    static const unsigned long READ_INTERVAL_MS = 5000UL;

    bool available = false;
    float voltageV = 0.0f;
    int percent = 0;
    unsigned long lastReadMs = 0;
    uint8_t failCount = 0;

    bool readRegisters(uint8_t reg, uint16_t& value);
};

#endif
