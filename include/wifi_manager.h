#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

class WifiManager {
public:
    WifiManager();

    void begin();
    void checkWiFiStatus();
    void processDNS();

    // Kept as no-op compatibility hooks for the former local web UI.
    void updateStatus(const String& status);
    void updateCommand(const String& command);
    void update(float pitch, float roll, bool fallen,
                unsigned long fallDuration, bool monitoring);
    void setLastCalibrationTime(unsigned long time);
    void addFallHistory(const String& type, unsigned long duration);
    void setMonitoringActive(bool active);

    using MonitoringCallback = void (*)(bool active);
    using CalibrateCallback = void (*)();
    using EmergencyCallback = void (*)();
    using VoiceCommandCallback = void (*)(const String& command);

    void setMonitoringCallback(MonitoringCallback callback);
    void setCalibrateCallback(CalibrateCallback callback);
    void setEmergencyCallback(EmergencyCallback callback);
    void setVoiceCommandCallback(VoiceCommandCallback callback);

private:
    void connectWiFi();
    void reconnectWiFi();
    void updateReconnectState();

    Preferences preferences;
    String ssid_;
    String password_;
    bool configured_ = false;
    bool reconnectInProgress_ = false;
    unsigned long lastCheckMs_ = 0;
    unsigned long reconnectStartMs_ = 0;
    static constexpr unsigned long kCheckIntervalMs = 10000;
    static constexpr unsigned long kReconnectTimeoutMs = 10000;
};

#endif
