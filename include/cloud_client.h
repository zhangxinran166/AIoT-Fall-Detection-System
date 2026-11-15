#ifndef CLOUD_CLIENT_H
#define CLOUD_CLIENT_H

#include <Arduino.h>

// ============================================================
// WiFi上传报警
// ============================================================

bool postAlarmViaWiFi(
    const String& serverBaseUrl,
    bool fallen,
    float pitch,
    float roll,
    float aiProb,
    int aiDir,
    float aiConf,
    float ax,
    float ay,
    float az,
    float gx,
    float gy,
    float gz,
    bool wearingOk,
    const char* locationSource = "wifi",
    const char* event = nullptr,
    const char* eventId = nullptr,
    float eventAiProb = -1.0f,
    int eventAiDir = -1,
    unsigned long eventAiTime = 0
);


// ============================================================
// 上报设备状态
// ============================================================

bool postDeviceStatusViaWiFi(
    const String& serverBaseUrl,
    bool wifiConnected,
    bool bluetoothConnected,
    bool fourGActive,
    bool monitoring
);


// ============================================================
// 从云端获取APP命令
//
// 返回：
// true  = 成功获取到命令
// false = 没有命令 / 请求失败
//
// command 返回：
// start_monitoring
// stop_monitoring
// emergency
// ============================================================

bool getCloudCommand(
    const String& serverBaseUrl,
    String& command
);


// ============================================================
// 云端 AI 助手：只取 voice_code，不做 TTS
// ============================================================

bool getAiVoiceCode(
    const String& serverBaseUrl,
    const String& intent,
    String& voiceCode
);

#endif