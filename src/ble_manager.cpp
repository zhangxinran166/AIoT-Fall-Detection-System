#include "ble_manager.h"
#include "log_utils.h"

#include <math.h>


// ============================================================
// BLE UUID
// ============================================================

#define SERVICE_UUID \
    "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

#define CHARACTERISTIC_UUID \
    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define ACK_CHARACTERISTIC_UUID \
    "beb5483e-36e1-4688-b7f5-ea07361b26a9"


// ============================================================
// BLE Server 回调
// ============================================================

class ServerCallbacks
    : public BLEServerCallbacks {

public:

    ServerCallbacks(
        BLEManager* mgr
    )
        : manager(mgr) {
    }


    // ========================================================
    // 手机连接ESP32
    // ========================================================

    void onConnect(
        BLEServer* pServer
    ) override {

        manager->handleConnect();
    }


    // ========================================================
    // 手机断开ESP32
    // ========================================================

    void onDisconnect(
        BLEServer* pServer
    ) override {

        manager->handleDisconnect();


        // ====================================================
        // 重新开始广播
        //
        // 这样APP断开后，可以再次搜索到ESP32
        // ====================================================

        pServer
            ->getAdvertising()
            ->start();
    }


private:

    BLEManager* manager;
};


// ============================================================
// APP ACK 回调
// 写 3 字节：0x41 + seq_low + seq_high
// ============================================================

class AckCallbacks
    : public BLECharacteristicCallbacks {

public:

    AckCallbacks(BLEManager* mgr)
        : manager(mgr) {
    }

    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        manager->handleAck(
            reinterpret_cast<const uint8_t*>(value.data()),
            value.length()
        );
    }

private:

    BLEManager* manager;
};


// ============================================================
// 构造函数
// ============================================================

BLEManager::BLEManager()
    :
      connected(false),

      confirmedDisconnected(true),

      disconnectStartTime(0),

      waitingForConfirm(false),

      onConfirmedDisconnect(nullptr),

      onReconnect(nullptr),

      pServer(nullptr),

      pCharacteristic(nullptr),

      pAckCharacteristic(nullptr),

      pendingAlarmSeq(0),

      pendingAlarmAck(false),

      alarmRetryCount(0),

      lastAlarmNotifyMs(0),

      helpAckPending(false),

      helpAckReceived(false) {
}


// ============================================================
// BLE初始化
// ============================================================

void BLEManager::begin(
    const char* deviceName
) {

    // ========================================================
    // 初始化BLE
    // ========================================================

    BLEDevice::init(
        deviceName
    );


    // ========================================================
    // 创建BLE Server
    // ========================================================

    pServer =
        BLEDevice::createServer();


    pServer->setCallbacks(
        new ServerCallbacks(this)
    );


    // ========================================================
    // 创建Service
    // ========================================================

    BLEService* pService =
        pServer->createService(
            SERVICE_UUID
        );


    // ========================================================
    // 创建Characteristic
    //
    // READ
    //      APP可以读取
    //
    // NOTIFY
    //      ESP32可以主动向APP发送数据
    // ========================================================

    pCharacteristic =
        pService->createCharacteristic(
            CHARACTERISTIC_UUID,

            BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR
        );

    // ACK 兜底：APP 若未发现独立 ACK 特征，也可把 0x49 写到主特征。
    pCharacteristic->setCallbacks(
        new AckCallbacks(this)
    );


    // ========================================================
    // 添加Notify描述符
    // ========================================================

    pCharacteristic->addDescriptor(
        new BLE2902()
    );


    // ========================================================
    // ACK 特征：APP 写 3 字节确认收到跌倒帧
    // 不改变原有 12 字节 R 帧
    // ========================================================

    pAckCharacteristic =
        pService->createCharacteristic(
            ACK_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR
        );

    pAckCharacteristic->setCallbacks(
        new AckCallbacks(this)
    );


    // ========================================================
    // 启动Service
    // ========================================================

    pService->start();


    // ========================================================
    // 开始广播
    // ========================================================

    BLEAdvertising* pAdvertising =
        BLEDevice::getAdvertising();


    pAdvertising->addServiceUUID(
        SERVICE_UUID
    );


    pAdvertising->setScanResponse(
        true
    );


    pAdvertising->start();


    // ========================================================
    // 日志
    // ========================================================

    printConnLog(
        "[BLE] 蓝牙服务已启动，等待手机连接..."
    );

    printConnLog(
        "[BLE] 设备名称：FallDetector"
    );
}


// ============================================================
// 手机连接
// ============================================================

void BLEManager::handleConnect()
{
    static uint32_t connectCount = 0;
    connectCount++;

    connected = true;

    waitingForConfirm = false;


    // ========================================================
    // 如果之前已经确认断开
    // 现在重新连接
    // ========================================================

    if (
        confirmedDisconnected &&
        onReconnect
    ) {

        onReconnect();
    }


    confirmedDisconnected = false;


    printConnLog(
        "[BLE] 手机已连接 count=" +
        String(connectCount)
    );
}


// ============================================================
// 手机断开
// ============================================================

void BLEManager::handleDisconnect()
{
    static uint32_t disconnectCount = 0;
    disconnectCount++;

    connected = false;

    pendingAlarmAck = false;

    waitingForConfirm = true;

    disconnectStartTime =
        millis();


    printConnLog(
        "[BLE] 手机已断开 count=" +
        String(disconnectCount) +
        "，开始15秒确认计时..."
    );
}


// ============================================================
// 发送跌倒数据
// ============================================================


void BLEManager::sendFallAlert(
    bool fallen,
    float pitch,
    float roll
) {
    if (!connected || !pCharacteristic) {
        printConnLog(
            "[BLE] 当前未连接，无法发送跌倒数据"
        );
        return;
    }

    // 6字节F帧，和最新App兼容：
    // [0]=0x46 'F'
    // [1]=fallen
    // [2..3]=pitch*10，int16小端
    // [4..5]=roll*10，int16小端
    // 不再发JSON：JSON超过默认MTU会被App分包缓存，
    // 一帧丢失后App会一直停在JSON缓冲模式，后续R帧全部被当成JSON，
    // 表现就是“跌倒后BLE实时数据一直是空，直到WiFi恢复”。
    long pitch10 = lroundf(pitch * 10.0f);
    long roll10 = lroundf(roll * 10.0f);

    if (pitch10 > 32767) pitch10 = 32767;
    if (pitch10 < -32768) pitch10 = -32768;
    if (roll10 > 32767) roll10 = 32767;
    if (roll10 < -32768) roll10 = -32768;

    uint8_t packet[6];
    packet[0] = 0x46;
    packet[1] = fallen ? 1 : 0;
    packet[2] = (uint8_t)(pitch10 & 0xFF);
    packet[3] = (uint8_t)((pitch10 >> 8) & 0xFF);
    packet[4] = (uint8_t)(roll10 & 0xFF);
    packet[5] = (uint8_t)((roll10 >> 8) & 0xFF);

    pCharacteristic->setValue(packet, sizeof(packet));
    pCharacteristic->notify();
}


void BLEManager::sendHelpAlert() {
    if (!connected || !pCharacteristic) {
        return;
    }
    helpAckPending = true;
    helpAckReceived = false;
    uint8_t packet[2] = {0x48, 0x01};
    pCharacteristic->setValue(packet, sizeof(packet));
    pCharacteristic->notify();
}

bool BLEManager::consumeHelpAck() {
    const bool received = helpAckReceived;
    helpAckReceived = false;
    helpAckPending = false;
    return received;
}


// ============================================================
// 12字节R实时帧
//
// 0: 0x52
// 1: flags bit0 WiFi, bit1 BLE, bit2 4G在线, bit3 4G唤醒,
//    bit4 等待4G, bit5 跌倒, bit6 监测中
// 2: 4G倒计时
// 3-4: pitch*10，int16 little-endian
// 5-6: roll*10，int16 little-endian
// 7-10: 32位sequence
// 11: 保留
// ============================================================

void BLEManager::sendRealtimePacket(
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
) {
    if (!connected || !pCharacteristic) {
        return;
    }

    if (fourGCountdown < 0) fourGCountdown = 0;
    if (fourGCountdown > 99) fourGCountdown = 99;

    const uint16_t acc100 = (uint16_t)max(0, min(65535, (int)lroundf(accMag * 100.0f)));
    const uint16_t gyro10 = (uint16_t)max(0, min(65535, (int)lroundf(gyroMag * 10.0f)));

    uint8_t flags = 0;
    if (wifiConnected) flags |= 0x01;
    if (bleConnected) flags |= 0x02;
    if (fourGOnline) flags |= 0x04;
    if (fourGAwake) flags |= 0x08;
    if (waitingFor4G) flags |= 0x10;
    if (fallen) flags |= 0x20;
    if (monitoring) flags |= 0x40;
    if (wearingOk) flags |= 0x80;

    static uint32_t sequence = 0;
    ++sequence;

    uint8_t packet[12];
    packet[0] = 0x52;
    packet[1] = flags;
    packet[2] = (uint8_t)fourGCountdown;
    packet[3] = (uint8_t)(acc100 & 0xFF);
    packet[4] = (uint8_t)((acc100 >> 8) & 0xFF);
    packet[5] = (uint8_t)(gyro10 & 0xFF);
    packet[6] = (uint8_t)((gyro10 >> 8) & 0xFF);
    packet[7] = (uint8_t)(sequence & 0xFF);
    packet[8] = (uint8_t)((sequence >> 8) & 0xFF);
    packet[9] = (uint8_t)max(0, min(100, (int)lroundf(aiProb * 100.0f)));
    const uint8_t aiDirBits = (uint8_t)max(0, min(3, aiDir)) << 6;
    const uint8_t aiConfBits = (uint8_t)max(0, min(63, (int)lroundf(aiConf * 63.0f)));
    packet[10] = (uint8_t)(aiDirBits | aiConfBits);
    packet[11] = 0;

    pCharacteristic->setValue(packet, sizeof(packet));
    pCharacteristic->notify();

    // 跌倒 R 帧需要 APP ACK；正常实时帧不重传，避免耗电。
    if (fallen && !pendingAlarmAck) {
        memcpy(pendingAlarmPacket, packet, sizeof(packet));
        pendingAlarmSeq = (uint16_t)(sequence & 0xFFFF);
        pendingAlarmAck = true;
        alarmRetryCount = 0;
        lastAlarmNotifyMs = millis();
    }
}






void BLEManager::handleAck(const uint8_t* data, size_t len)
{
    if (len < 1) {
        return;
    }

    if (data[0] == 0x49) {
        if (helpAckPending) {
            helpAckReceived = true;
            printConnLog("[BLE] 收到APP求助ACK");
        }
        return;
    }

    if (len < 3 || data[0] != 0x41) {
        return;
    }

    const uint16_t seq = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    if (pendingAlarmAck && seq == pendingAlarmSeq) {
        pendingAlarmAck = false;
        printConnLog(
            "[BLE] 收到APP ACK seq=" +
            String(seq)
        );
    }
}


// ============================================================
// BLE状态更新
//
// 必须在主循环中调用
// ============================================================

void BLEManager::update()
{

    // ========================================================
    // 跌倒 R 帧未收到 ACK，1 秒后重发，最多 3 次
    // ========================================================

    if (
        pendingAlarmAck &&
        connected &&
        pCharacteristic != nullptr &&
        millis() - lastAlarmNotifyMs >= 1000UL
    ) {
        if (alarmRetryCount >= 3) {
            pendingAlarmAck = false;
            printConnLog(
                "[BLE] 跌倒帧重传 3 次仍未 ACK，停止重发 seq=" +
                String(pendingAlarmSeq)
            );
        } else {
            pCharacteristic->setValue(
                pendingAlarmPacket,
                sizeof(pendingAlarmPacket)
            );
            pCharacteristic->notify();
            alarmRetryCount++;
            lastAlarmNotifyMs = millis();
            printConnLog(
                "[BLE] 重发跌倒帧 seq=" +
                String(pendingAlarmSeq) +
                " retry=" +
                String(alarmRetryCount)
            );
        }
    }


    // ========================================================
    // 当前正在等待15秒确认
    // ========================================================

    if (
        waitingForConfirm &&
        !connected
    ) {

        unsigned long now =
            millis();


        // ====================================================
        // 15秒已经过去
        // ====================================================

        if (
            now -
            disconnectStartTime
            >=
            DISCONNECT_CONFIRM_DELAY
        ) {

            waitingForConfirm = false;

            confirmedDisconnected = true;


            printConnLog(
                "[BLE] 确认断连"
            );


            // =================================================
            // 通知主程序
            //
            // 注意：
            // 这里只通知，不直接启动4G
            //
            // 4G最终状态仍然由
            // checkConnectivityAndManage4G()
            // 统一管理
            // =================================================

            if (
                onConfirmedDisconnect
            ) {

                onConfirmedDisconnect();
            }
        }
    }
}