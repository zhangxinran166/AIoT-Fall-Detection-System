#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


class BLEManager {

public:

    BLEManager();

    // ========================================================
    // 初始化BLE
    // ========================================================

    void begin(
        const char* deviceName = "FallDetector"
    );


    // ========================================================
    // BLE状态更新
    //
    // 主循环中必须持续调用
    //
    // 用于：
    // BLE断开
    //     ↓
    // 等待15秒
    //     ↓
    // 确认真正断开
    // ========================================================

    void update();


    // ========================================================
    // 发送跌倒数据
    // ========================================================

    void sendFallAlert(
        bool fallen,
        float pitch,
        float roll
    );

    void sendHelpAlert();
    bool consumeHelpAck();


    // ========================================================
    // 发送12字节R实时帧（App实时显示用）
    // ========================================================

    void sendRealtimePacket(
        bool wifiConnected,
        bool bleConnected,
        bool fourGOnline,
        bool waitingFor4G,
        int fourGCountdown,
        bool fourGAwake,
        float accMag,
        float gyroMag,
        bool fallen,
        bool monitoring,
        bool wearingOk,
        float aiProb,
        int aiDir,
        float aiConf
    );


    // ========================================================
    // 获取BLE连接状态
    // ========================================================

    bool isConnected() const {

        return connected;
    }


    // ========================================================
    // BLE确认断开回调
    // ========================================================

    using StateChangeCallback =
        void (*)();


    void setOnConfirmedDisconnect(
        StateChangeCallback cb
    ) {

        onConfirmedDisconnect = cb;
    }


    // ========================================================
    // BLE重新连接回调
    // ========================================================

    void setOnReconnect(
        StateChangeCallback cb
    ) {

        onReconnect = cb;
    }


    // ========================================================
    // 以下函数供BLE内部回调使用
    // ========================================================

    void handleConnect();

    void handleDisconnect();

    // ACK 回调：APP 写 0x41 + seq_low + seq_high
    void handleAck(const uint8_t* data, size_t len);


private:

    // ========================================================
    // BLE断开确认时间
    // ========================================================

    static const unsigned long
        DISCONNECT_CONFIRM_DELAY = 15000;


    // ========================================================
    // BLE状态
    // ========================================================

    bool connected;

    bool confirmedDisconnected;

    unsigned long disconnectStartTime;

    bool waitingForConfirm;


    // ========================================================
    // 回调
    // ========================================================

    StateChangeCallback
        onConfirmedDisconnect;

    StateChangeCallback
        onReconnect;


    // ========================================================
    // BLE对象
    // ========================================================

    BLEServer* pServer;

    BLECharacteristic* pCharacteristic;

    BLECharacteristic* pAckCharacteristic;

    // 最近一帧跌倒 R 帧，等待 APP ACK
    uint8_t pendingAlarmPacket[12];
    uint16_t pendingAlarmSeq;
    bool pendingAlarmAck;
    uint8_t alarmRetryCount;
    unsigned long lastAlarmNotifyMs;

    bool helpAckPending;
    bool helpAckReceived;
};


#endif