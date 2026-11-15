#include "wifi_manager.h"
#include "wifi_config.h"

WifiManager::WifiManager() = default;

void WifiManager::begin() {
    preferences.begin("wifi", false);
    const String storedSsid = preferences.getString("ssid", "");
    const String storedPassword = preferences.getString("password", "");
    configured_ = preferences.getBool("configured", false);
    preferences.end();

    if (configured_ && storedSsid.length() > 0) {
        ssid_ = storedSsid;
        password_ = storedPassword;
    } else {
        ssid_ = WIFI_SSID_DEFAULT;
        password_ = WIFI_PASSWORD_DEFAULT;
    }

    connectWiFi();
}

void WifiManager::connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(true);
    WiFi.begin(ssid_.c_str(), password_.c_str());

    Serial.print("[WiFi] connecting to ");
    Serial.println(ssid_);

    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(200);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WiFi] connected, IP=");
        Serial.println(WiFi.localIP());
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid_);
        preferences.putString("password", password_);
        preferences.putBool("configured", true);
        preferences.end();
    } else {
        Serial.println("[WiFi] connect timeout; keep retrying in loop");
    }
}

void WifiManager::checkWiFiStatus() {
    updateReconnectState();

    const unsigned long now = millis();
    if (now - lastCheckMs_ < kCheckIntervalMs) return;
    lastCheckMs_ = now;

    if (WiFi.status() != WL_CONNECTED && !reconnectInProgress_) {
        reconnectWiFi();
    }
}

void WifiManager::reconnectWiFi() {
    if (reconnectInProgress_) return;
    Serial.print("[WiFi] reconnecting to ");
    Serial.println(ssid_);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_.c_str(), password_.c_str());
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(true);
    reconnectInProgress_ = true;
    reconnectStartMs_ = millis();
}

void WifiManager::updateReconnectState() {
    if (!reconnectInProgress_) return;
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WiFi] reconnected, IP=");
        Serial.println(WiFi.localIP());
        reconnectInProgress_ = false;
    } else if (millis() - reconnectStartMs_ >= kReconnectTimeoutMs) {
        Serial.println("[WiFi] reconnect timeout");
        reconnectInProgress_ = false;
    }
}

// The local web UI was removed from the runtime image. These hooks remain
// no-ops so the existing application code stays stable.
void WifiManager::updateStatus(const String&) {}
void WifiManager::updateCommand(const String&) {}
void WifiManager::update(float, float, bool, unsigned long, bool) {}
void WifiManager::setLastCalibrationTime(unsigned long) {}
void WifiManager::addFallHistory(const String&, unsigned long) {}
void WifiManager::setMonitoringActive(bool) {}
void WifiManager::processDNS() {}
void WifiManager::setMonitoringCallback(MonitoringCallback) {}
void WifiManager::setCalibrateCallback(CalibrateCallback) {}
void WifiManager::setEmergencyCallback(EmergencyCallback) {}
void WifiManager::setVoiceCommandCallback(VoiceCommandCallback) {}
