#include <Arduino.h>

// 默认只接受带唯一事件 ID 的 HELP，避免 ASR 重复旧命令造成“没喊也报警”。
// 联调旧 ASR 时可临时改成 1。
#ifndef ASR_ALLOW_LEGACY_HELP
#define ASR_ALLOW_LEGACY_HELP 0
#endif
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <SPIFFS.h>
#include <time.h>

#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "fall_detection.h"
#include "battery_monitor.h"
#include "asr_communication.h"
#include "wifi_manager.h"
#include "module_4g.h"
#include "ble_manager.h"
#include "log_utils.h"
#include "cloud_client.h"
#include <string.h>


// ============================================================
// 全局对象
// ============================================================

Module4G module4g;

BLEManager bleManager;

FallDetection fallDetector;

String telemetryJsonSuffix() {
    float accMag = 0.0f;
    float gyroMag = 0.0f;
    fallDetector.getLatestMagnitudes(accMag, gyroMag);

    String s = "";
    s += ",\"ai_prob\":" + String(fallDetector.getFallProbability(), 4);
    s += ",\"ai_dir\":" + String(fallDetector.getLastAiDirection());
    s += ",\"ai_conf\":" + String(fallDetector.getLastAiDirectionConfidence(), 4);
    s += ",\"ai_latency_ms\":" + String(fallDetector.getLastAiLatencyUs() / 1000.0f, 2);
    s += ",\"acc_mag\":" + String(accMag, 3);
    s += ",\"gyro_mag\":" + String(gyroMag, 2);
    s += ",\"wearing_ok\":";
    s += fallDetector.isOrientationAbnormal() ? "false" : "true";
    s += ",\"event_ai_prob\":" + String(fallDetector.getFallEventAiProbability(), 4);
    s += ",\"event_ai_dir\":" + String(fallDetector.getFallEventAiDirection());
    s += ",\"event_ai_time\":" + String((unsigned long)fallDetector.getFallEventAiTime());
    return s;
}


AsrCommunication asrComm;

MPU6050 mpu;

WifiManager wifiManager;


// ============================================================
// WiFi / BLE / 4G 联动状态
// ============================================================

bool waitingFor4GActivation = false;

unsigned long bothOfflineStartTime = 0;

bool module4gIsAwake = false;

// 4G报警正在执行时，网络任务不要同时 sleep/wake，避免串口AT指令互相踩踏。
volatile bool g4AlarmActive = false;

TaskHandle_t networkTaskHandle = nullptr;

const unsigned long
BOTH_OFFLINE_CONFIRM_DELAY = 15000;


// ============================================================
// 报警冷却
// ============================================================

bool hasEverAlerted = false;

unsigned long lastAlertSentTime = 0;

const unsigned long
ALERT_COOLDOWN = 60000;


// ============================================================
// BLE实时帧
// ============================================================

unsigned long lastBleRealtimeSend = 0;

const unsigned long
BLE_REALTIME_INTERVAL = 2000;


// ============================================================
// 系统状态
// ============================================================

unsigned long startTime = 0;

unsigned long lastCalibrationTime = 0;

BatteryMonitor batteryMonitor;


// ============================================================
// 云端配置
// ============================================================

const char* CLOUD_SERVER_BASE_URL =
    "http://101.43.88.85:5000";

const char* DEVICE_ID =
    "ESP32_001";


// ============================================================
// 离线报警缓存：无网络时保存，WiFi恢复后补传
// ============================================================

const char* OFFLINE_ALARM_PATH = "/offline_alarms.ndjson";
const size_t OFFLINE_ALARM_MAX_BYTES = 32 * 1024;

String buildOfflineFallJson(float pitch, float roll) {
    const time_t epoch = time(nullptr);
    return String("{\"device_id\":\"ESP32_001\",\"fallen\":true")
        + ",\"pitch\":" + String(pitch, 2)
        + ",\"roll\":" + String(roll, 2)
        + ",\"location_source\":\"cache\""
        + ",\"gps_pending\":false"
        + ",\"event_epoch\":" + String((unsigned long)(epoch > 0 ? epoch : 0))
        + telemetryJsonSuffix()
        + "}";
}

bool cacheAlarmJson(const String& json) {
    if (!SPIFFS.begin(true)) {
        return false;
    }

    File f = SPIFFS.open(OFFLINE_ALARM_PATH, FILE_APPEND);
    if (!f) {
        return false;
    }

    if (f.size() < OFFLINE_ALARM_MAX_BYTES) {
        f.println(json);
    }
    f.close();
    return true;
}

bool postCachedJson(const String& json) {
    HTTPClient http;
    String url = String(CLOUD_SERVER_BASE_URL) + "/api/alarm";
    http.setTimeout(4000);
    if (!http.begin(url)) {
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(json);
    http.end();
    return code >= 200 && code < 300;
}

int flushOfflineAlarms() {
    if (!SPIFFS.begin(true) || !SPIFFS.exists(OFFLINE_ALARM_PATH)) {
        return 0;
    }

    File f = SPIFFS.open(OFFLINE_ALARM_PATH, FILE_READ);
    if (!f) {
        return 0;
    }

    String remaining = "";
    int sent = 0;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() < 2 || !line.startsWith("{")) {
            continue;
        }
        if (postCachedJson(line)) {
            sent++;
        } else {
            remaining += line + "\n";
        }
    }
    f.close();

    SPIFFS.remove(OFFLINE_ALARM_PATH);

    if (remaining.length() > 0) {
        File out = SPIFFS.open(OFFLINE_ALARM_PATH, FILE_WRITE);
        if (out) {
            out.print(remaining);
            out.close();
        }
    }

    return sent;
}


// ============================================================
// 云端控制
// ============================================================

unsigned long lastCloudCommandCheck = 0;

const unsigned long
CLOUD_COMMAND_CHECK_INTERVAL = 2000;


// ============================================================
// 设备状态上传
// ============================================================

unsigned long lastDeviceStatusUpload = 0;

const unsigned long
DEVICE_STATUS_UPLOAD_INTERVAL = 10000;


// ============================================================
// 云端日志控制
// ============================================================

int lastDeviceStatusHttpCode = -999;

unsigned long lastCloudErrorLogTime = 0;

const unsigned long
CLOUD_ERROR_LOG_INTERVAL = 10000;


// ============================================================
// 云端跌倒状态同步
// ============================================================

bool lastUploadedFallenState = false;

unsigned long lastStateSyncAttempt = 0;

const unsigned long
STATE_SYNC_RETRY_INTERVAL = 10000;


// ============================================================
// 跌倒事件队列
//
// loopTask只负责入队；WiFi/BLE/4G网络动作全部在networkTask执行。
// ============================================================

enum class FallEventType : uint8_t {
    FALL = 0,
    RECOVERY = 1
};

struct FallEvent {
    FallEventType type;
    float pitch;
    float roll;
    uint32_t timestamp;
};

QueueHandle_t fallEventQueue = nullptr;

bool pendingRecoveryUpload = false;

unsigned long lastRecoveryUploadAttempt = 0;

const unsigned long
RECOVERY_RETRY_INTERVAL = 3000;


// ============================================================
// 函数前置声明
// ============================================================

void onFallDetected(
    const char* message
);

void onAsrCommand(
    const String& command
);

void notifyCloudHelp(const String& helpEventId = "");

void onMonitoringChanged(
    bool active
);

void onCalibrate();

void onEmergency();

void onVoiceCommand(
    const String& command
);

void onBleConfirmedDisconnect();

void onBleReconnect();

void checkConnectivityAndManage4G();

void executeCloudCommand(
    const String& command
);

void checkCloudCommand();

void uploadDeviceStatus();

bool uploadFallenState(
    bool fallen
);

void runThresholdReplay();

void runRawRecord();


// ============================================================
// 串口初始化
// ============================================================

void serial_init() {
    Serial.begin(115200);

    // USB CDC在电池供电/无主机时可能永远不置位；最多等2秒，不能让设备卡死。
    const unsigned long serialWaitStart = millis();

    while (
        !Serial &&
        millis() - serialWaitStart < 2000UL
    ) {
        delay(10);
    }

    Serial.printf(
        "\n[BOOT] reset_reason=%d, free_heap=%lu\n",
        (int)esp_reset_reason(),
        (unsigned long)ESP.getFreeHeap()
    );


    // --------------------------------------------------------
    // ASR Pro
    // --------------------------------------------------------

    Serial2.begin(
        9600,
        SERIAL_8N1,
        16,
        17
    );


    delay(1000);


    Serial.println(
        "串口和ASR Pro通信初始化完成"
    );
}


// ============================================================
// 原始CSV录制
//
// 使用:
//   record
//   MARK,walk_start
//   ...
//   END
//
// 输出:
//   seq,time_us,ax,ay,az,gx,gy,gz
//
// 录制期间不修改阈值/监测状态，只读最近一次采样快照。
// ============================================================

void runRawRecord() {
    Serial.println("RECORD_READY");

    Serial.setTimeout(1000);

    uint32_t lastSequence = 0;
    bool hasLast = false;

    for (;;) {
        const float aiProb = fallDetector.serviceCnnOnce();

        if (Serial.available()) {
            String line = Serial.readStringUntil('\n');
            line.trim();

            if (line == "END") {
                break;
            }

            if (line.startsWith("MARK")) {
                Serial.println(line);
                continue;
            }
        }

        uint32_t sequence = 0;
        int64_t timeUs = 0;

        int16_t ax = 0;
        int16_t ay = 0;
        int16_t az = 0;
        int16_t gx = 0;
        int16_t gy = 0;
        int16_t gz = 0;

        if (
            fallDetector.getLatestRawSample(
                sequence,
                timeUs,
                ax,
                ay,
                az,
                gx,
                gy,
                gz
            )
        ) {
            if (
                !hasLast ||
                sequence < lastSequence ||
                sequence - lastSequence >= 2
            ) {
                Serial.printf(
                    "%lu,%lld,%d,%d,%d,%d,%d,%d,%.4f,%d,%.4f\n",
                    (unsigned long)sequence,
                    (long long)timeUs,
                    (int)ax,
                    (int)ay,
                    (int)az,
                    (int)gx,
                    (int)gy,
                    (int)gz,
                    aiProb,
                    fallDetector.getLastAiDirection(),
                    fallDetector.getLastAiDirectionConfidence()
                );

                lastSequence = sequence;
                hasLast = true;
            }
        }

        delay(1);
    }

    Serial.println("RECORD_DONE");
}


// ============================================================
// 板端阈值回放
//
// PC发送:
//   replay
//   seq,time_us,ax,ay,az,gx,gy,gz
//   ...
//   END
//
// 回放使用旧数据raw，不扣当前新模块零偏。
// ============================================================

void runThresholdReplay() {
    Serial.println("REPLAY_READY");

    fallDetector.stopMonitoring();
    fallDetector.beginThresholdReplay();

    Serial.setTimeout(5000);

    uint32_t sampleCount = 0;
    uint32_t totalSamples = 0;
    uint32_t firstSeq = 0;

    bool foundTrigger = false;
    uint8_t firstMode = 0xFF;

    float firstA = 0.0f;
    float firstG = 0.0f;

    auto printTrial = [&]() {
        Serial.printf(
            "REPLAY_TRIAL samples=%lu first_seq=%lu mode=%u A=%.4f G=%.4f\n",
            (unsigned long)sampleCount,
            (unsigned long)firstSeq,
            (unsigned int)firstMode,
            firstA,
            firstG
        );
    };

    for (;;) {
        if (!Serial.available()) {
            delay(1);
            continue;
        }

        String line = Serial.readStringUntil('\n');
        line.trim();

        if (line == "END") {
            printTrial();
            break;
        }

        if (line == "RESET") {
            printTrial();

            fallDetector.beginThresholdReplay();

            sampleCount = 0;
            firstSeq = 0;
            foundTrigger = false;
            firstMode = 0xFF;
            firstA = 0.0f;
            firstG = 0.0f;
            continue;
        }

        if (line.length() == 0 || line[0] == '#') {
            continue;
        }

        unsigned long seq = 0;
        long long timeUs = 0;
        int ax = 0;
        int ay = 0;
        int az = 0;
        int gx = 0;
        int gy = 0;
        int gz = 0;

        int parsed = sscanf(
            line.c_str(),
            "%lu,%lld,%d,%d,%d,%d,%d,%d",
            &seq,
            &timeUs,
            &ax,
            &ay,
            &az,
            &gx,
            &gy,
            &gz
        );

        if (parsed != 8) {
            continue;
        }

        ++sampleCount;
        ++totalSamples;

        uint8_t mode = 0;
        float accMag = 0.0f;
        float gyroMag = 0.0f;

        bool trigger = fallDetector.replayThresholdSample(
            (uint32_t)seq,
            (int64_t)timeUs,
            (int16_t)ax,
            (int16_t)ay,
            (int16_t)az,
            (int16_t)gx,
            (int16_t)gy,
            (int16_t)gz,
            mode,
            accMag,
            gyroMag
        );

        if (trigger && !foundTrigger) {
            foundTrigger = true;
            firstSeq = (uint32_t)seq;
            firstMode = mode;
            firstA = accMag;
            firstG = gyroMag;

            Serial.printf(
                "REPLAY_TRIGGER seq=%lu mode=%u A=%.4f G=%.4f\n",
                (unsigned long)firstSeq,
                (unsigned int)firstMode,
                firstA,
                firstG
            );
        }
    }

    Serial.printf(
        "REPLAY_DONE total_samples=%lu\n",
        (unsigned long)totalSamples
    );

    fallDetector.startMonitoring();
}


// ============================================================
// 4G GNSS 坐标转换：ddmm.mmmm,N -> 十进制
// ============================================================

double nmeaToDecimalCoordinate(const String& raw) {
    const int comma = raw.indexOf(',');
    if (comma <= 0) return 0.0;

    String value = raw.substring(0, comma);
    value.trim();
    String hemisphere = raw.substring(comma + 1);
    hemisphere.trim();
    hemisphere.toUpperCase();

    const double nmea = value.toFloat();
    const int degrees = (int)(nmea / 100.0);
    const double minutes = nmea - degrees * 100.0;
    double decimal = degrees + minutes / 60.0;

    if (hemisphere == "S" || hemisphere == "W") {
        decimal = -decimal;
    }
    return decimal;
}


// ============================================================
// 跌倒检测回调
// ============================================================

void handleFallEvent(
    float currentPitch,
    float currentRoll
) {
    bool bleOk = bleManager.isConnected();
    bool wifiOk = (WiFi.status() == WL_CONNECTED);

    Serial.printf(
        "[事件处理] FALL WiFi=%d BLE=%d 4Gawake=%d\n",
        wifiOk ? 1 : 0,
        bleOk ? 1 : 0,
        module4gIsAwake ? 1 : 0
    );

    const String offlineJson = buildOfflineFallJson(currentPitch, currentRoll);
    bool cachedThisEvent = false;

    // 优先级1：WiFi
    if (wifiOk) {
        print4GLog("[联动] WiFi在线，通过WiFi上传云端");

        float axG = 0.0f, ayG = 0.0f, azG = 0.0f;
        float gxDps = 0.0f, gyDps = 0.0f, gzDps = 0.0f;

        fallDetector.getLatestMotionG(
            axG, ayG, azG,
            gxDps, gyDps, gzDps
        );

        bool cloudOk = postAlarmViaWiFi(
            String(CLOUD_SERVER_BASE_URL),
            true,
            currentPitch,
            currentRoll,
            fallDetector.getFallProbability(),
            fallDetector.getLastAiDirection(),
            fallDetector.getLastAiDirectionConfidence(),
            axG,
            ayG,
            azG,
            gxDps,
            gyDps,
            gzDps,
            !fallDetector.isOrientationAbnormal(),
            "wifi",
            nullptr,
            nullptr,
            fallDetector.getFallEventAiProbability(),
            fallDetector.getFallEventAiDirection(),
            fallDetector.getFallEventAiTime()
        );

        Serial.println(
            cloudOk
                ? "已上传云端(WiFi)"
                : "云端上传失败(WiFi)，尝试下一通道"
        );

        if (cloudOk) {
            lastUploadedFallenState = true;
            return;
        }
    }

    // 优先级2：BLE
    if (bleOk) {
        bleManager.sendFallAlert(
            true,
            currentPitch,
            currentRoll
        );

        print4GLog("[联动] 蓝牙在线，已发送F帧");
        lastUploadedFallenState = true;
        return;
    }

    // 优先级3：4G
    unsigned long now = millis();

    if (hasEverAlerted &&
        now - lastAlertSentTime < ALERT_COOLDOWN) {
        unsigned long remainMs =
            ALERT_COOLDOWN - (now - lastAlertSentTime);

        print4GLog(
            "[联动] 4G报警冷却中，剩余" +
            String((remainMs + 999UL) / 1000UL) +
            "秒，跳过"
        );

        if (!cachedThisEvent) {
            cachedThisEvent = cacheAlarmJson(offlineJson);
            if (cachedThisEvent) {
                Serial.println("[缓存] 跌倒事件已写入 SPIFFS");
            }
        }
        return;
    }

    lastAlertSentTime = now;
    hasEverAlerted = true;
    g4AlarmActive = true;

    const unsigned long alarmStartMs = millis();

    if (!module4gIsAwake) {
        module4g.wakeUp();
        module4gIsAwake = true;
        waitingFor4GActivation = false;
    }

    // ========================================================
    // 第一条报警：不等待GPS，先发云端，家属端立刻能看到
    // ========================================================

    String firstCloudJson =
        "{\"device_id\":\"ESP32_001\"," 
        "\"fallen\":true," 
        "\"pitch\":" + String(currentPitch, 2) +
        ",\"roll\":" + String(currentRoll, 2) +
        ",\"location_source\":\"4g\"," 
        "\"gps_pending\":true" +
        telemetryJsonSuffix() + "}";

    bool firstCloudOk = module4g.postJsonToCloud(
        "http://101.43.88.85:5000/api/alarm",
        firstCloudJson,
        false
    );

    print4GLog(
        firstCloudOk
            ? "[4G] 首条报警已上传云端"
            : "[4G] 首条报警上传失败"
    );

    if (firstCloudOk) {
        lastUploadedFallenState = true;
    }

    module4g.sendPushNotification(
        "老人跌倒警报",
        "检测到跌倒，位置正在获取中，请立即查看！"
    );

    print4GLog(
        "[4G] 首条报警完成，耗时" +
        String(millis() - alarmStartMs) + "ms"
    );

    // ========================================================
    // 第二条：GPS定位后补发位置更新
    // ========================================================

    String lat;
    String lon;
    bool gotLocation = module4g.getLocation(lat, lon, 45000);
    bool cloudUploaded = firstCloudOk;

    if (gotLocation) {
        String locationMsg =
            "定位信息：纬度" + lat + " 经度" + lon;

        bool pushOk = module4g.sendPushNotification(
            "老人跌倒-位置更新",
            locationMsg
        );

        print4GLog(
            pushOk
                ? "[4G] 位置推送发送成功"
                : "[4G] 位置推送发送失败"
        );

        const double latDecimal = nmeaToDecimalCoordinate(lat);
        const double lonDecimal = nmeaToDecimalCoordinate(lon);

        String locationJson =
            "{\"device_id\":\"ESP32_001\"," 
            "\"fallen\":true," 
            "\"pitch\":" + String(currentPitch, 2) +
            ",\"roll\":" + String(currentRoll, 2) +
            ",\"location_source\":\"4g\"," 
            "\"gps_pending\":false," 
            "\"latitude\":\"" + String(latDecimal, 6) +
            "\",\"longitude\":\"" + String(lonDecimal, 6) + "\"" +
            telemetryJsonSuffix() + "}";

        bool locationCloudOk = module4g.postJsonToCloud(
            "http://101.43.88.85:5000/api/alarm",
            locationJson,
            false
        );

        print4GLog(
            locationCloudOk
                ? "[4G] 位置更新已上传云端"
                : "[4G] 位置更新上传失败"
        );

        if (locationCloudOk) {
            cloudUploaded = true;
            lastUploadedFallenState = true;
        }
    } else {
        print4GLog(
            "[4G] GPS未定位，首条报警已保留，不再等待"
        );
    }

    if (!cloudUploaded && !cachedThisEvent) {
        cachedThisEvent = cacheAlarmJson(offlineJson);
        Serial.println(
            cachedThisEvent
                ? "[缓存] 云端不可达，跌倒事件已缓存"
                : "[缓存] 写入失败"
        );
    }

    g4AlarmActive = false;
}


void onFallDetected(
    const char* message
) {
    if (
        message != nullptr &&
        strcmp(
            message,
            "老人已恢复正常姿态"
        ) == 0
    ) {
        Serial.println(
            "\033[1;32m[联动] 跌倒状态已恢复正常\033[0m"
        );

        wifiManager.updateStatus(message);

        FallEvent event;
        event.type = FallEventType::RECOVERY;
        event.pitch = 0.0f;
        event.roll = 0.0f;
        event.timestamp = millis();

        if (
            fallEventQueue == nullptr ||
            xQueueSend(fallEventQueue, &event, 0) != pdPASS
        ) {
            Serial.println("[事件] 恢复事件入队失败");
        } else {
            Serial.println("[事件] 恢复事件已入队");
        }

        return;
    }

    Serial.print("\033[1;33m警告：\033[0m");
    Serial.println(message);

    asrComm.sendMessage("FALL");

    wifiManager.updateStatus(message);

    Quaternion q;
    VectorFloat gravity;
    float ypr[3];

    float currentPitch = 0.0f;
    float currentRoll = 0.0f;

    if (
        fallDetector.getCurrentData(
            q,
            gravity,
            ypr
        )
    ) {
        currentPitch =
            ypr[1] * 180.0f / M_PI;

        currentRoll =
            ypr[2] * 180.0f / M_PI;
    }

    FallEvent event;
    event.type = FallEventType::FALL;
    event.pitch = currentPitch;
    event.roll = currentRoll;
    event.timestamp = millis();

    if (
        fallEventQueue == nullptr ||
        xQueueSend(fallEventQueue, &event, 0) != pdPASS
    ) {
        Serial.println("[事件] 跌倒事件入队失败");
    } else {
        Serial.println("[事件] 跌倒事件已入队");
    }
}

// ============================================================
// ASR Pro命令
// ============================================================

void notifyCloudHelp(const String& helpEventId) {
    const bool wearingOk = !fallDetector.isOrientationAbnormal();
    const float helpAiProb = wearingOk ? fallDetector.getFallProbability() : 0.0f;
    const int helpAiDir = wearingOk ? fallDetector.getLastAiDirection() : 3;
    const float helpAiConf = wearingOk ? fallDetector.getLastAiDirectionConfidence() : 0.0f;

    bool ok = postAlarmViaWiFi(
        String(CLOUD_SERVER_BASE_URL),
        false,
        0.0f,
        0.0f,
        helpAiProb,
        helpAiDir,
        helpAiConf,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        wearingOk,
        "help",
        "help",
        helpEventId.length() ? helpEventId.c_str() : nullptr
    );

    if (!ok && bleManager.isConnected()) {
        // 保持 WiFi→BLE→4G 优先级。BLE 帧发出但未收到 ACK，
        // 仍不能确认 APP 已转发，必须继续唤醒 4G。
        for (int attempt = 1; attempt <= 3 && !ok; ++attempt) {
            Serial.printf("[语音求助] 发送 BLE 求助帧 attempt=%d\n", attempt);
            bleManager.sendHelpAlert();
            const unsigned long start = millis();
            while (millis() - start < 500UL) {
                if (bleManager.consumeHelpAck()) {
                    ok = true;
                    break;
                }
                delay(10);
            }
        }
        if (!ok) {
            Serial.println("[语音求助] BLE 未收到 APP ACK，转 4G");
        }
    }

    if (!ok) {
        if (!module4gIsAwake) {
            module4g.wakeUp();
            module4gIsAwake = true;
        }

        String helpJson =
            "{\"device_id\":\"ESP32_001\",\"fallen\":false,"
            "\"pitch\":0,\"roll\":0,"
            "\"location_source\":\"help\",\"event\":\"help\","
            "\"message\":\"老人语音求助\"";
        if (helpEventId.length()) {
            helpJson += ",\"event_id\":\"" + helpEventId + "\"";
        }
        helpJson += "}";

        ok = module4g.postJsonToCloud(
            String(CLOUD_SERVER_BASE_URL) + "/api/alarm",
            helpJson,
            false
        );
    }

    Serial.println(
        ok
            ? "[语音求助] 已上传云端"
            : "[语音求助] 云端上传失败"
    );
}


void onAsrCommand(
    const String& command
) {

    static String lastPrintedCommand;
    static unsigned long lastPrintedAt = 0;
    const unsigned long commandNow = millis();
    if (command != lastPrintedCommand || commandNow - lastPrintedAt >= 30000UL) {
        Serial.print("收到ASR命令: ");
        Serial.println(command);
        lastPrintedCommand = command;
        lastPrintedAt = commandNow;
    }


    wifiManager.updateCommand(
        command
    );


    // 云端 AI：ASR 只发固定意图，ESP32 取 voice_code 后让 ASR 播放预录音频。
    // 不在设备端做文字转语音。
    if (command.startsWith("AI:")) {
        String intent = command.substring(3);
        intent.trim();
        intent.toUpperCase();
        if (intent.length() == 0) intent = "HELLO";

        if (WiFi.status() != WL_CONNECTED) {
            asrComm.sendMessage("PLAY:AI_OFFLINE");
            Serial.println("[AI] WiFi未连接，播放离线提示");
            return;
        }

        String voiceCode;
        if (getAiVoiceCode(String(CLOUD_SERVER_BASE_URL), intent, voiceCode)) {
            asrComm.sendMessage("PLAY:" + voiceCode);
            Serial.println("[AI] 播放：" + voiceCode);
        } else {
            asrComm.sendMessage("PLAY:AI_ERROR");
            Serial.println("[AI] 云端请求失败");
        }
        return;
    }


    if (
        command.indexOf("HELP") >= 0 ||
        command.indexOf("help") >= 0 ||
        command.indexOf("紧急") >= 0
    ) {
        static unsigned long lastHelpMs = 0;
        static String lastHelpId = "";
        const unsigned long now = millis();

        int helpPos = command.indexOf("HELP");
        if (helpPos < 0) helpPos = command.indexOf("help");
        const int sep = helpPos >= 0 ? command.indexOf(':', helpPos) : -1;

        String helpId;
        if (sep >= 0 && sep + 1 < (int)command.length()) {
            helpId = command.substring(sep + 1);
            helpId.trim();
        } else {
            // 兼容天问Block无法输出数字变量的裸 HELP：由 ESP32 生成 ID。
            helpId = String(now);
        }

        if (helpId.length() == 0 || helpId.length() > 32) {
            return;
        }

        // 只接受 ASCII 事件 ID，防止串口乱码被当成求助。
        bool asciiOk = true;
        for (size_t i = 0; i < helpId.length(); ++i) {
            const uint8_t c = (uint8_t)helpId[i];
            if (c < 0x20 || c > 0x7E) {
                asciiOk = false;
                break;
            }
        }
        if (!asciiOk) return;

        // 带 ID 时按 ID 去重；裸 HELP 用 3 秒窗口吸收模块重发。
        if (sep >= 0) {
            if (helpId == lastHelpId) return;
            lastHelpId = helpId;
        } else {
            if (lastHelpMs != 0 && now - lastHelpMs < 3000UL) return;
        }
        lastHelpMs = now;

        notifyCloudHelp(helpId);
        Serial.println("✓ 已上报语音求助：" + helpId);
    }
    else if (
        command.indexOf("CALIB") >= 0
    ) {
        onCalibrate();
        asrComm.sendMessage("CALIB");
    }
    else if (
        command.indexOf("ON") >= 0 &&
        command.indexOf("OFF") < 0
    ) {

        fallDetector.startMonitoring();

        wifiManager.setMonitoringActive(
            true
        );

        asrComm.sendMessage(
            "ON"
        );

        Serial.println(
            "✓ 已开启跌倒监测"
        );
    }


    else if (
        command.indexOf("OFF") >= 0
    ) {

        fallDetector.stopMonitoring();

        wifiManager.setMonitoringActive(
            false
        );

        asrComm.sendMessage(
            "OFF"
        );

        Serial.println(
            "✓ 已关闭跌倒监测"
        );
    }
}


// ============================================================
// 监测开关
// ============================================================

void onMonitoringChanged(
    bool active
) {

    if (active) {

        fallDetector.startMonitoring();

        wifiManager.setMonitoringActive(
            true
        );


        Serial.println(
            "网页/云端控制：开启跌倒监测"
        );


        asrComm.sendMessage(
            "ESP32: 跌倒监测已开启"
        );

    } else {

        fallDetector.stopMonitoring();

        wifiManager.setMonitoringActive(
            false
        );


        Serial.println(
            "网页/云端控制：关闭跌倒监测"
        );


        asrComm.sendMessage(
            "ESP32: 跌倒监测已关闭"
        );
    }
}


// ============================================================
// 校准
// ============================================================

void onCalibrate() {

    Serial.println(
        "网页控制：开始校准传感器..."
    );


    fallDetector.calibrate();


    lastCalibrationTime =
        millis();


    wifiManager.setLastCalibrationTime(
        lastCalibrationTime
    );


    Serial.println(
        "校准完成"
    );


    asrComm.sendMessage(
        "ESP32: 传感器已校准"
    );
}


// ============================================================
// 紧急呼叫
// ============================================================

void onEmergency() {

    Serial.println(
        "⚠️ 紧急呼叫触发！"
    );


    asrComm.sendMessage(
        "紧急情况！请立即查看老人状态"
    );


    wifiManager.addFallHistory(
        "紧急呼叫",
        0
    );
}


// ============================================================
// 网页语音命令
// ============================================================

void onVoiceCommand(
    const String& command
) {

    Serial.println(
        "网页发送语音命令: " +
        command
    );


    onAsrCommand(
        command
    );
}


// ============================================================
// BLE确认断开
// ============================================================

void onBleConfirmedDisconnect() {

    print4GLog(
        "[联动] BLE确认断连"
    );
}


// ============================================================
// BLE重新连接
// ============================================================

void onBleReconnect() {

    print4GLog(
        "[联动] BLE已重新连接"
    );
}


// ============================================================
// 云端命令执行
// ============================================================

void executeCloudCommand(
    const String& command
) {

    Serial.println(
        "[云端] 收到控制命令：" +
        command
    );


    if (
        command == "start_monitoring" ||
        command == "start"
    ) {

        onMonitoringChanged(
            true
        );

        return;
    }


    if (
        command == "stop_monitoring" ||
        command == "stop"
    ) {

        onMonitoringChanged(
            false
        );

        return;
    }


    if (
        command == "emergency"
    ) {

        onEmergency();

        return;
    }


    Serial.println(
        "[云端] 未知命令"
    );
}


// ============================================================
// 云端命令
// ============================================================

void checkCloudCommand() {

    static bool cloudWasFailing = false;

    if (
        WiFi.status()
        !=
        WL_CONNECTED
    ) {

        return;
    }


    unsigned long now =
        millis();


    if (
        now -
        lastCloudCommandCheck
        <
        CLOUD_COMMAND_CHECK_INTERVAL
    ) {

        return;
    }


    lastCloudCommandCheck =
        now;


    HTTPClient http;


    String url =
        String(CLOUD_SERVER_BASE_URL) +
        "/api/command/pending?device_id=" +
        String(DEVICE_ID);


    http.begin(url);

    // 网络任务里也不能等太久；命令查询失败下一轮再试。
    http.setTimeout(2500);


    int httpCode =
        http.GET();

    if (httpCode <= 0) {

        if (!cloudWasFailing) {
            Serial.print(
                "[云端控制] 请求失败："
            );

            Serial.println(
                http.errorToString(
                    httpCode
                )
            );

            cloudWasFailing = true;
        }

        http.end();

        return;
    }

    if (httpCode != 200) {

        http.end();

        return;
    }

    if (cloudWasFailing) {
        Serial.println("[云端控制] 已恢复");
        cloudWasFailing = false;
    }


    String payload =
        http.getString();


    http.end();


    if (
        payload.length() == 0
    ) {

        return;
    }


    JsonDocument doc;


    DeserializationError error =
        deserializeJson(
            doc,
            payload
        );


    if (error) {

        Serial.print(
            "[云端控制] JSON解析失败："
        );

        Serial.println(
            error.c_str()
        );

        return;
    }


    String command = "";


    if (
        doc["command"].is<const char*>()
    ) {

        command =
            doc["command"].as<String>();
    }


    if (
        command.length() == 0 &&
        doc["data"]["command"]
            .is<const char*>()
    ) {

        command =
            doc["data"]["command"]
                .as<String>();
    }


    if (
        command.length() == 0
    ) {

        return;
    }


    executeCloudCommand(
        command
    );
}


// ============================================================
// 上传设备状态
// ============================================================

void uploadDeviceStatus() {

    if (g4AlarmActive) {
        return;
    }

    unsigned long now =
        millis();

    // 4G 是应急链路，不做 10 秒级实时状态上传；只在唤醒时立即补一次，
    // 之后按 60 秒阶段更新，避免被误认为实时数据。
    const unsigned long statusInterval =
        (WiFi.status() != WL_CONNECTED && module4gIsAwake)
            ? 60000UL
            : DEVICE_STATUS_UPLOAD_INTERVAL;

    if (
        now -
        lastDeviceStatusUpload
        <
        statusInterval
    ) {

        return;
    }


    lastDeviceStatusUpload =
        now;


    bool wifiOk =
        (
            WiFi.status()
            ==
            WL_CONNECTED
        );


    bool bleOk =
        bleManager.isConnected();


    // --------------------------------------------------------
    // 4G倒计时
    // --------------------------------------------------------

    int countdown = 0;


    if (
        waitingFor4GActivation
    ) {

        unsigned long elapsed =
            now -
            bothOfflineStartTime;


        if (
            elapsed <
            BOTH_OFFLINE_CONFIRM_DELAY
        ) {

            countdown =
                (
                    BOTH_OFFLINE_CONFIRM_DELAY -
                    elapsed
                ) / 1000;
        }
    }


    // --------------------------------------------------------
    // 当前姿态
    // --------------------------------------------------------

    float pitch = 0;

    float roll = 0;


    float statusAccMag = 0.0f, statusGyroMag = 0.0f;

    fallDetector.getLatestMagnitudes(
        statusAccMag, statusGyroMag
    );


    Quaternion q;

    VectorFloat gravity;

    float ypr[3];


    if (
        fallDetector.getCurrentData(
            q,
            gravity,
            ypr
        )
    ) {

        pitch =
            ypr[1] * 180 / M_PI;

        roll =
            ypr[2] * 180 / M_PI;
    }


    // --------------------------------------------------------
    // JSON
    // --------------------------------------------------------

    String json = "{";


    json +=
        "\"device_id\":\"ESP32_001\",";


    json +=
        "\"wifi\":" +
        String(
            wifiOk
            ? "true,"
            : "false,"
        );


    json +=
        "\"bluetooth\":" +
        String(
            bleOk
            ? "true,"
            : "false,"
        );


    json +=
        "\"4g\":" +
        String(
            module4gIsAwake
            ? "true,"
            : "false,"
        );


    json +=
        "\"pitch\":" +
        String(pitch, 2) +
        ",";


    json +=
        "\"roll\":" +
        String(roll, 2) +
        ",";

    json +=
        "\"ai_prob\":" +
        String(fallDetector.getFallProbability(), 4) +
        ",";

    json +=
        "\"ai_dir\":" +
        String(fallDetector.getLastAiDirection()) +
        ",";

    json +=
        "\"ai_conf\":" +
        String(fallDetector.getLastAiDirectionConfidence(), 4) +
        ",";

    json +=
        "\"ai_latency_ms\":" +
        String(fallDetector.getLastAiLatencyUs() / 1000.0f, 2) +
        ",";

    json +=
        "\"acc_mag\":" + String(statusAccMag, 3) +
        ",\"gyro_mag\":" + String(statusGyroMag, 2) +
        ",";

    json +=
        "\"wearing_ok\":" +
        String(fallDetector.isOrientationAbnormal() ? "false," : "true,");

    json +=
        "\"long_lie_seconds\":" +
        String(fallDetector.getFallDuration() / 1000UL) +
        ",";

    json +=
        "\"long_lie_level\":" +
        String(fallDetector.getLongLieLevel()) +
        ",";

    json +=
        "\"sedentary_seconds\":" +
        String(fallDetector.getSedentarySeconds()) +
        ",";

    json +=
        "\"battery_available\":" +
        String(batteryMonitor.isAvailable() ? "true," : "false,");

    json +=
        "\"battery_voltage\":" +
        String(batteryMonitor.getVoltage(), 2) +
        ",";

    json +=
        "\"battery_percent\":" +
        String(batteryMonitor.getPercent()) +
        ",";

    json +=
        "\"battery_low\":" +
        String(batteryMonitor.isLow() ? "true," : "false,");

    json +=
        "\"event_ai_prob\":" +
        String(fallDetector.getFallEventAiProbability(), 4) +
        ",";

    json +=
        "\"event_ai_dir\":" +
        String(fallDetector.getFallEventAiDirection()) +
        ",";

    json +=
        "\"event_ai_time\":" +
        String((unsigned long)fallDetector.getFallEventAiTime()) +
        ",";


    json +=
        "\"fallen\":" +
        String(
            fallDetector.isFallenState()
            ? "true,"
            : "false,"
        );


    json +=
        "\"monitoring\":" +
        String(
            fallDetector.isMonitoringActive()
            ? "true,"
            : "false,"
        );


    json +=
        "\"waiting_for_4g\":" +
        String(
            waitingFor4GActivation
            ? "true,"
            : "false,"
        );


    json +=
        "\"4g_countdown\":" +
        String(countdown);


    json += "}";


    // --------------------------------------------------------
    // WiFi上传
    // --------------------------------------------------------

    if (wifiOk) {

        HTTPClient http;


        String url =
            String(CLOUD_SERVER_BASE_URL) +
            "/api/device/status";


        http.begin(url);


        http.addHeader(
            "Content-Type",
            "application/json"
        );


        // 状态上传在独立任务里，短超时避免占住网络任务。
        http.setTimeout(1500);


        int code =
            http.POST(json);


        http.end();


        if (
            code !=
            lastDeviceStatusHttpCode
        ) {

            Serial.printf(
                "[设备状态] 云端HTTP=%d\n",
                code
            );


            lastDeviceStatusHttpCode =
                code;
        }


        return;
    }


    // --------------------------------------------------------
    // WiFi不可用 → 4G
    // --------------------------------------------------------

    if (
        module4gIsAwake
    ) {

        bool ok =
            module4g.postJsonToCloud(
                "http://101.43.88.85:5000/api/device/status",
                json,
                false
            );


        if (!ok) {

            if (
                millis() -
                lastCloudErrorLogTime
                >=
                CLOUD_ERROR_LOG_INTERVAL
            ) {

                lastCloudErrorLogTime =
                    millis();

                Serial.println(
                    "[设备状态] 4G上传失败"
                );
            }
        }
    }
}


// ============================================================
// 上传跌倒状态
// ============================================================

bool uploadFallenState(
    bool fallen
) {

    Quaternion q;

    VectorFloat gravity;

    float ypr[3];


    float pitch = 0;

    float roll = 0;


    if (
        fallDetector.getCurrentData(
            q,
            gravity,
            ypr
        )
    ) {

        pitch =
            ypr[1] * 180 / M_PI;

        roll =
            ypr[2] * 180 / M_PI;
    }


    // --------------------------------------------------------
    // WiFi
    // --------------------------------------------------------

    if (
        WiFi.status()
        ==
        WL_CONNECTED
    ) {

        String json =
            "{\"device_id\":\"ESP32_001\","
            "\"fallen\":" +
            String(
                fallen
                ? "true"
                : "false"
            ) +
            ",\"pitch\":" +
            String(pitch, 2) +
            ",\"roll\":" +
            String(roll, 2) +
            ",\"location_source\":\"wifi\""
            "}";


        HTTPClient http;


        String url =
            String(CLOUD_SERVER_BASE_URL) +
            "/api/alarm";


        http.begin(url);


        http.addHeader(
            "Content-Type",
            "application/json"
        );


        http.setTimeout(3000);


        int code =
            http.POST(json);


        http.end();


        Serial.print(
            "[跌倒状态] WiFi上传 HTTP："
        );

        Serial.println(code);


        return (
            code >= 200 &&
            code < 300
        );
    }


    // --------------------------------------------------------
    // 4G
    // --------------------------------------------------------

    if (
        module4gIsAwake
    ) {

        String json =
            "{\"device_id\":\"ESP32_001\","
            "\"fallen\":" +
            String(
                fallen
                ? "true"
                : "false"
            ) +
            ",\"pitch\":" +
            String(pitch, 2) +
            ",\"roll\":" +
            String(roll, 2) +
            ",\"location_source\":\"4g\""
            "}";


        bool ok =
            module4g.postJsonToCloud(
                "http://101.43.88.85:5000/api/alarm",
                json,
                false
            );


        Serial.println(
            ok
            ? "[跌倒状态] 4G上传成功"
            : "[跌倒状态] 4G上传失败"
        );


        return ok;
    }


    return false;
}


// ============================================================
// 网络任务
// 周期性HTTP/4G管理不再放在loopTask里，避免阻塞跌倒检测主循环。
// 注意：跌倒回调里的4G报警目前仍是独立同步路径，下一阶段再迁入队列。
// ============================================================

void networkTask(void* parameter) {
    (void)parameter;

    for (;;) {
        FallEvent event;

        if (
            fallEventQueue != nullptr &&
            xQueueReceive(
                fallEventQueue,
                &event,
                0
            ) == pdPASS
        ) {
            if (event.type == FallEventType::FALL) {
                handleFallEvent(
                    event.pitch,
                    event.roll
                );
            } else {
                pendingRecoveryUpload = true;

                // 一次跌倒事件结束后，下一次跌倒应重新拥有完整4G报警机会，
                // 而不是被上一次跌倒的60秒冷却误伤。
                hasEverAlerted = false;
                lastAlertSentTime = 0;
                g4AlarmActive = false;
            }
        }

        if (
            pendingRecoveryUpload &&
            millis() - lastRecoveryUploadAttempt >=
            RECOVERY_RETRY_INTERVAL
        ) {
            lastRecoveryUploadAttempt = millis();

            if (uploadFallenState(false)) {
                lastUploadedFallenState = false;
                pendingRecoveryUpload = false;

                Serial.println(
                    "[云端] 跌倒状态已恢复：false"
                );
            }
        }

        if (WiFi.status() == WL_CONNECTED) {
            checkCloudCommand();

            static unsigned long lastOfflineFlush = 0;
            if (millis() - lastOfflineFlush >= 10000UL) {
                lastOfflineFlush = millis();
                int flushed = flushOfflineAlarms();
                if (flushed > 0) {
                    Serial.printf("[缓存] 已补传 %d 条离线跌倒事件\n", flushed);
                }
            }
        }

        // 状态上传按当前实际链路选择 WiFi 或 4G；不能只在 WiFi 在线时调用，
        // 否则 4G 应急期间 APP 会一直显示旧的 WiFi 状态。
        uploadDeviceStatus();

        checkConnectivityAndManage4G();

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}


// ============================================================
// setup
// ============================================================

void setup() {

    serial_init();


    // ========================================================
    // 4G
    // ========================================================

    module4g.begin(
        4,
        5,
        115200
    );


    module4g.setPushKey(
        "XZdaf19801895c6b6c810f2da648d56787"
    );


    // ========================================================
    // BLE
    // ========================================================

    bleManager.begin(
        "FallDetector"
    );


    // ========================================================
    // BLE回调
    // ========================================================

    bleManager.setOnConfirmedDisconnect(
        onBleConfirmedDisconnect
    );


    bleManager.setOnReconnect(
        onBleReconnect
    );


    startTime =
        millis();


    // ========================================================
    // MPU6050
    // ========================================================

    Serial.println(
        F("正在初始化传感器...")
    );


    if (
        !fallDetector.begin()
    ) {

        Serial.println(
            "MPU6050 初始化失败：继续启动网络/调试通道，跌倒监测不可用"
        );

        // 不再 while(true) 把设备变砖：保留BLE/网页/4G日志通道，
        // 便于现场确认是接线、地址还是供电问题。
    } else {

        Serial.println(
            "MPU6050 初始化成功"
        );

        // 用Python导出的标准样本验证C++推理，不依赖传感器。
        fallDetector.runSelfTest();
    }

    batteryMonitor.begin();


    // ========================================================
    // 回调
    // ========================================================

    fallDetector.setFallDetectionCallback(
        onFallDetected
    );


    asrComm.setCommandCallback(
        onAsrCommand
    );


    asrComm.begin(9600, 16, 17);


    // ========================================================
    // WiFi 管理
    // ========================================================

    wifiManager.begin();


    wifiManager.setMonitoringCallback(
        onMonitoringChanged
    );


    wifiManager.setCalibrateCallback(
        onCalibrate
    );


    wifiManager.setEmergencyCallback(
        onEmergency
    );


    wifiManager.setVoiceCommandCallback(
        onVoiceCommand
    );


    // ========================================================
    // 跌倒事件队列（必须在 startMonitoring 之前创建）
    // ========================================================

    fallEventQueue = xQueueCreate(
        8,
        sizeof(FallEvent)
    );

    if (fallEventQueue == nullptr) {
        Serial.println("[事件] 跌倒事件队列创建失败");
    }


    // ========================================================
    // 默认开启跌倒监测（必须在网络任务之前）
    // ========================================================

    fallDetector.startMonitoring();

    wifiManager.setMonitoringActive(
        true
    );


    // ========================================================
    // 网络任务：HTTP/4G管理移出loopTask
    // ========================================================

    BaseType_t netTaskResult =
        xTaskCreatePinnedToCore(
            networkTask,
            "NetworkTask",
            12288,
            nullptr,
            1,
            &networkTaskHandle,
            0
        );

    if (netTaskResult != pdPASS) {
        networkTaskHandle = nullptr;
        Serial.println("[网络] 任务创建失败，HTTP仍在主循环外不可用");
    } else {
        Serial.println("[网络] 独立任务已启动（core 0）");
    }


    Serial.println(
        "系统初始化完成，开始运行..."
    );

    Serial.printf(
        "[BOOT] free_heap=%lu, min_free_heap=%lu\n",
        (unsigned long)ESP.getFreeHeap(),
        (unsigned long)ESP.getMinFreeHeap()
    );
}


// ============================================================
// BLE / WiFi / 4G统一联动
// ============================================================

void checkConnectivityAndManage4G() {

    // 4G正在发送跌倒报警时，不要同时做CFUN休眠/唤醒。
    if (g4AlarmActive) {
        return;
    }

    bool bleOk =
        bleManager.isConnected();


    bool wifiOk =
        (
            WiFi.status()
            ==
            WL_CONNECTED
        );


    bool anyOnline =
        bleOk ||
        wifiOk;


    // ========================================================
    // 至少一个在线
    // ========================================================

    if (anyOnline) {

        waitingFor4GActivation =
            false;


        if (
            module4gIsAwake
        ) {

            print4GLog(
                "[联动] 蓝牙/WiFi恢复在线，4G重新休眠"
            );


            module4g.sleep();


            module4gIsAwake =
                false;
        }
    }


    // ========================================================
    // WiFi + BLE都离线
    // ========================================================

    else {

        // 恢复原来的4G待机策略：
        // WiFi和BLE都离线后，等15秒确认，然后提前唤醒4G待机，
        // 这样真跌倒时不需要再等4G开机/注册。待机功耗远低于发射。
        if (
            !waitingFor4GActivation &&
            !module4gIsAwake
        ) {
            waitingFor4GActivation = true;
            bothOfflineStartTime = millis();

            print4GLog(
                "[联动] 蓝牙和WiFi均离线，开始15秒确认计时..."
            );
        } else if (
            waitingFor4GActivation &&
            millis() - bothOfflineStartTime >=
            BOTH_OFFLINE_CONFIRM_DELAY
        ) {
            print4GLog(
                "[联动] 确认双离线，唤醒4G作为兜底"
            );

            module4g.wakeUp();
            module4gIsAwake = true;
            waitingFor4GActivation = false;

            // 4G刚唤醒就补一次状态，让家属端尽快看到“应急模式”，
            // 不必等下一轮 10 秒状态周期。强制绕过上传节流。
            lastDeviceStatusUpload = 0;
            uploadDeviceStatus();
        }
    }
}


// ============================================================
// 主循环
// ============================================================

void loop() {
    fallDetector.update();
    batteryMonitor.update();

    if (fallDetector.consumeSedentaryReminder()) {
        asrComm.sendMessage("久坐提醒：您已经静止较久，请起身活动");
        Serial.println("[久坐] 已触发静止/久坐提醒");
    }
    unsigned long currentTime =
        millis();


    // ========================================================
    // WiFi 管理
    // ========================================================

    wifiManager.processDNS();


    // ========================================================
    // WiFi
    // ========================================================

    wifiManager.checkWiFiStatus();


    // ========================================================
    // 云端控制/状态上传/4G联动已移到 networkTask（core 0）
    // ========================================================

    // BLE 连接状态确认与断连计时
    bleManager.update();


    // ========================================================
    // BLE实时帧：1Hz发送R帧，App用于实时显示
    // ========================================================

    if (
        bleManager.isConnected() &&
        millis() - lastBleRealtimeSend >=
        BLE_REALTIME_INTERVAL
    ) {
        lastBleRealtimeSend = millis();

        Quaternion bleQ;
        VectorFloat bleGravity;
        float bleYpr[3] = {0.0f, 0.0f, 0.0f};

        float blePitch = 0.0f;
        float bleRoll = 0.0f;

        if (
            fallDetector.getCurrentData(
                bleQ,
                bleGravity,
                bleYpr
            )
        ) {
            blePitch = bleYpr[1] * 180.0f / M_PI;
            bleRoll = bleYpr[2] * 180.0f / M_PI;
        }

        int fourGCountdown = 0;

        if (waitingFor4GActivation) {
            unsigned long elapsed =
                millis() - bothOfflineStartTime;

            if (elapsed < BOTH_OFFLINE_CONFIRM_DELAY) {
                fourGCountdown =
                    (int)(
                        (BOTH_OFFLINE_CONFIRM_DELAY - elapsed + 999UL)
                        / 1000UL
                    );
            }
        }

        float bleAccMag = 0.0f;
        float bleGyroMag = 0.0f;
        fallDetector.getLatestMagnitudes(bleAccMag, bleGyroMag);

        const bool bleFallen = fallDetector.isFallenState();
        const float bleAiProb = bleFallen
            ? fallDetector.getFallEventAiProbability()
            : fallDetector.getFallProbability();
        const int bleAiDir = bleFallen
            ? fallDetector.getFallEventAiDirection()
            : fallDetector.getLastAiDirection();
        const float bleAiConf = fallDetector.getLastAiDirectionConfidence();

        bleManager.sendRealtimePacket(
            WiFi.status() == WL_CONNECTED,
            true,
            module4gIsAwake,
            waitingFor4GActivation,
            fourGCountdown,
            module4gIsAwake,
            bleAccMag,
            bleGyroMag,
            bleFallen,
            fallDetector.isMonitoringActive(),
            !fallDetector.isOrientationAbnormal(),
            bleAiProb,
            bleAiDir,
            bleAiConf
        );
    }


    // ========================================================
    // 电脑串口命令
    // ========================================================

    if (Serial.available()) {

        // 不能用 5 秒 readString 阻塞主循环。
        Serial.setTimeout(50);

        String readUART =
            Serial.readStringUntil('\n');


        readUART.trim();


        if (
            readUART.startsWith("CNN,")
        ) {

            const String payload = readUART.substring(4);

            char* buf = new char[payload.length() + 1];

            if (buf != nullptr) {
                payload.toCharArray(buf, payload.length() + 1);

                static float vals[INPUT_TIMESTEPS * INPUT_FEATURES];
                int n = 0;
                char* token = strtok(buf, ",");

                while (token != nullptr && n < INPUT_TIMESTEPS * INPUT_FEATURES) {
                    vals[n++] = static_cast<float>(atof(token));
                    token = strtok(nullptr, ",");
                }

                int dir = 3;
                float conf = 0.0f;
                const float p = fallDetector.runCnnExternal(vals, n, dir, conf);

                Serial.printf(
                    "[CNN_TEST] prob=%.4f dir=%d conf=%.4f n=%d\n",
                    p,
                    dir,
                    conf,
                    n
                );

                delete[] buf;
            }
        }

        else if (
            readUART == "record"
        ) {

            runRawRecord();
        }


        else if (
            readUART == "replay"
        ) {

            runThresholdReplay();
        }


        else if (
            readUART == "test"
        ) {

            Serial.println(
                "发送测试信号到ASR Pro..."
            );


            asrComm.sendMessage(
                "ESP32测试信号"
            );


            Serial.println(
                "测试信号已发送"
            );
        }


        else if (
            readUART == "c"
        ) {

            Serial.println(
                "开始校准传感器..."
            );


            fallDetector.calibrate();


            lastCalibrationTime =
                millis();


            wifiManager.setLastCalibrationTime(
                lastCalibrationTime
            );


            Serial.println(
                "校准完成"
            );
        }


        else if (
            readUART == "r"
        ) {

            fallDetector.readSensorData();
        }


        else if (
            readUART == "s"
        ) {

            fallDetector.startMonitoring();


            wifiManager.setMonitoringActive(
                true
            );


            Serial.println(
                "开始跌倒监测..."
            );
        }


        else if (
            readUART == "p"
        ) {

            fallDetector.stopMonitoring();


            wifiManager.setMonitoringActive(
                false
            );


            Serial.println(
                "暂停跌倒监测..."
            );
        }


        else {

            asrComm.sendMessage(
                readUART
            );


            Serial.println(
                "已发送到ASR Pro: " +
                readUART
            );
        }


        readUART = "";
    }


    // ========================================================
    // ASR通信
    // ========================================================

    asrComm.update();


    // ========================================================
    // 跌倒检测
    // ========================================================

    


    // ========================================================
    // 获取当前传感器数据
    // ========================================================

    Quaternion q;

    VectorFloat gravity;

    float ypr[3];


    if (
        fallDetector.getCurrentData(
            q,
            gravity,
            ypr
        )
    ) {

        float pitch =
            ypr[1] * 180 / M_PI;


        float roll =
            ypr[2] * 180 / M_PI;


        bool fallenState =
            fallDetector.isFallenState();


        unsigned long fallDuration =
            fallDetector.getFallDuration();


        bool monitoringActive =
            fallDetector.isMonitoringActive();


        // ----------------------------------------------------
        // 更新 WiFi 状态
        // ----------------------------------------------------

        wifiManager.update(
            pitch,
            roll,
            fallenState,
            fallDuration,
            monitoringActive
        );
        // 通过 BLE 向 APP 发送状态变化（F 帧只在状态变化或跌倒保持时低频发送）
        static bool lastBleFallFlag = false;
        static unsigned long lastBleFallFrameAt = 0;
        if (bleManager.isConnected()) {
            const bool needFallFrame =
                fallenState != lastBleFallFlag ||
                (fallenState && millis() - lastBleFallFrameAt >= 5000UL);
            if (needFallFrame) {
                bleManager.sendFallAlert(
                    fallenState,
                    pitch,
                    roll
                );
                lastBleFallFlag = fallenState;
                lastBleFallFrameAt = millis();
            }
        }


        // ----------------------------------------------------
        // 跌倒状态变化
        // ----------------------------------------------------

        // 跌倒状态变化由 onFallDetected() 入队、
        // networkTask统一处理，loopTask不再直接POST。
    }


    // ========================================================
    // 主循环心跳
    // ========================================================

    static unsigned long lastLoopHeartbeat = 0;

    if (millis() - lastLoopHeartbeat >= 5000UL) {
        lastLoopHeartbeat = millis();

        Serial.printf(
            "[主循环心跳] heap=%lu fallen=%d monitoring=%d\n",
            (unsigned long)ESP.getFreeHeap(),
            fallDetector.isFallenState() ? 1 : 0,
            fallDetector.isMonitoringActive() ? 1 : 0
        );
    }


    // ========================================================
    // 动态延迟
    // ========================================================

    unsigned long loopDuration =
        millis() -
        currentTime;


    if (
        loopDuration < 10
    ) {

        delay(
            10 -
            loopDuration
        );
    }
}
