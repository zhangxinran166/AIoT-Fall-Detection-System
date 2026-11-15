#include "cloud_client.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


// ============================================================
// WiFi报警上传
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
    const char* locationSource,
    const char* event,
    const char* eventId,
    float eventAiProb,
    int eventAiDir,
    unsigned long eventAiTime
) {

    if (WiFi.status() != WL_CONNECTED) {

        Serial.println(
            "[Cloud] WiFi未连接，无法上传报警"
        );

        return false;
    }


    HTTPClient http;

    String url =
        serverBaseUrl +
        "/api/alarm";


    Serial.println(
        "[Cloud] 上传报警："
        + url
    );


    http.begin(url);

    http.addHeader(
        "Content-Type",
        "application/json"
    );


    JsonDocument doc;

    doc["device_id"] =
        "ESP32_001";

    doc["fallen"] =
        fallen;

    doc["pitch"] =
        pitch;

    doc["roll"] =
        roll;

    doc["ai_prob"] = aiProb;
    doc["ai_dir"] = aiDir;
    doc["ai_conf"] = aiConf;
    doc["ax"] = ax;
    doc["ay"] = ay;
    doc["az"] = az;
    doc["gx"] = gx;
    doc["gy"] = gy;
    doc["gz"] = gz;
    doc["acc_mag"] = sqrtf(ax * ax + ay * ay + az * az);
    doc["gyro_mag"] = sqrtf(gx * gx + gy * gy + gz * gz);
    doc["wearing_ok"] = wearingOk;

    doc["source"] =
        "wifi";

    // 云端 /api/alarm 实际读取的是 location_source；两个都写，兼容旧/新后端。
    doc["location_source"] =
        locationSource != nullptr ? locationSource : "wifi";

    if (event != nullptr && event[0] != '\0') {
        doc["event"] = event;
        doc["message"] = String("老人求助：") + event;
    }

    if (eventId != nullptr && eventId[0] != '\0') {
        doc["event_id"] = eventId;
    }

    if (eventAiProb >= 0.0f) {
        doc["event_ai_prob"] = eventAiProb;
        doc["event_ai_dir"] = eventAiDir;
        doc["event_ai_time"] = eventAiTime;
    }


    String body;

    serializeJson(
        doc,
        body
    );


    int httpCode =
        http.POST(body);


    if (
        httpCode > 0
    ) {

        Serial.print(
            "[Cloud] HTTP状态码: "
        );

        Serial.println(
            httpCode
        );


        String response =
            http.getString();

        Serial.print("[Cloud] 响应长度: ");
        Serial.println(response.length());


        http.end();

        return (
            httpCode >= 200 &&
            httpCode < 300
        );

    }


    Serial.print(
        "[Cloud] POST失败: "
    );

    Serial.println(
        http.errorToString(
            httpCode
        )
    );


    http.end();

    return false;
}


// ============================================================
// 上报设备状态
// ============================================================

bool postDeviceStatusViaWiFi(
    const String& serverBaseUrl,
    bool wifiConnected,
    bool bluetoothConnected,
    bool fourGActive,
    bool monitoring
) {

    if (
        WiFi.status() !=
        WL_CONNECTED
    ) {

        return false;
    }


    HTTPClient http;


    String url =
        serverBaseUrl +
        "/api/device/status";


    http.begin(url);


    http.addHeader(
        "Content-Type",
        "application/json"
    );


    JsonDocument doc;


    doc["device_id"] ="ESP32_001";

    doc["wifi"] = wifiConnected;

    doc["bluetooth"] = bluetoothConnected;

    doc["4g"] = fourGActive;
    
    doc["monitoring"] = monitoring;


    String body;


    serializeJson(
        doc,
        body
    );


    int httpCode =
        http.POST(body);


    if (
        httpCode > 0
    ) {

        Serial.print(
            "[Cloud] 设备状态上传: "
        );

        Serial.println(
            httpCode
        );


        http.end();

        return (
            httpCode >= 200 &&
            httpCode < 300
        );
    }


    http.end();

    return false;
}


// ============================================================
// 获取云端命令
// ============================================================

bool getCloudCommand(
    const String& serverBaseUrl,
    String& command
) {

    command = "";


    if (
        WiFi.status() !=
        WL_CONNECTED
    ) {

        return false;
    }


    HTTPClient http;


    String url =
        serverBaseUrl +
        "/api/command/pending";


    http.begin(url);


    int httpCode =
        http.GET();


    if (
        httpCode != 200
    ) {

        Serial.print(
            "[Cloud] 获取命令失败 HTTP="
        );

        Serial.println(
            httpCode
        );


        http.end();

        return false;
    }


    String response =
        http.getString();


    http.end();


    Serial.println(
        "[Cloud] 命令返回："
        + response
    );


    JsonDocument doc;


    DeserializationError error =
        deserializeJson(
            doc,
            response
        );


    if (error) {

        Serial.println(
            "[Cloud] JSON解析失败"
        );

        return false;
    }


    bool hasCommand =
        doc["has_command"] |
        false;


    if (!hasCommand) {

        return false;
    }


    const char* cmd =
        doc["data"]["command"];


    if (
        cmd == nullptr
    ) {

        return false;
    }


    command =
        String(cmd);


    return true;
}


// ============================================================
// 云端 AI 助手：只取 voice_code
// ============================================================

bool getAiVoiceCode(
    const String& serverBaseUrl,
    const String& intent,
    String& voiceCode
) {
    HTTPClient http;
    String url = serverBaseUrl + "/api/ai/chat";

    http.begin(url);
    http.setTimeout(15000);
    http.addHeader("Content-Type", "application/json; charset=utf-8");

    JsonDocument doc;
    doc["device_id"] = "ESP32_001";
    doc["intent"] = intent;

    String body;
    serializeJson(doc, body);

    int httpCode = http.POST(body);
    String response = http.getString();
    http.end();

    if (httpCode < 200 || httpCode >= 300) {
        Serial.printf("[AI] HTTP %d\n", httpCode);
        return false;
    }

    JsonDocument responseDoc;
    if (deserializeJson(responseDoc, response)) {
        Serial.println("[AI] JSON解析失败");
        return false;
    }

    const char* code = responseDoc["voice_code"];
    if (code == nullptr) {
        return false;
    }

    voiceCode = String(code);
    return true;
}