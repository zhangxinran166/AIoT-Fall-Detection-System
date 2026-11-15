#include "asr_communication.h"
#include "asr_protocol.h"

AsrCommunication::AsrCommunication() : 
    lastCommunicationTime(0),
    commandCallback(nullptr) {
}

void AsrCommunication::begin(unsigned long baudRate, int rxPin, int txPin) {
    Serial2.begin(baudRate, SERIAL_8N1, rxPin, txPin);
}

// 我们的指令都是 ASCII（ON / OFF / FALL / ...）。
// 非可打印字符一律当噪声丢弃：否则串口噪声会被当成命令，
// 再被转发到网页，导致 WebSocket 报 "Invalid UTF-8" 反复断线。
static bool isPrintableCommand(const String& s) {
    if (s.length() < 2) return false;          // 单字节基本是噪声
    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// 处理 ASR Pro 发来的数据。
// 兼容两种情况：带换行（标准）和只发字符串不带换行（部分固件）。
// 逐字节打印很占串口带宽，默认关掉；需要时编译加 -DASR_DEBUG。
//
// 静默去重：天问Block 的“语音识别ID”识别后不会自动清零，
// 语音模块会把同一条命令（ON / OFF / HELP）每 10ms 重发一次。
// 直接在固件侧丢弃短时间内的重复命令，模块那边就不用再维护“上次ID”变量了。
static const unsigned long ASR_DUPLICATE_WINDOW_MS = 5000;

void AsrCommunication::update() {
    static String buffer = "";
    static unsigned long lastByteTime = 0;
    static String lastAccepted = "";
    static unsigned long lastAcceptedTime = 0;

    auto deliver = [&](String cmd) {
        cmd.trim();

        if (!isPrintableCommand(cmd)) {
            return;
        }

        const unsigned long now = millis();
        lastCommunicationTime = now;   // 收到任何合法命令都算“在线”

        // 同一条命令在窗口期内重复出现，直接丢弃（不打印、不回调）
        if (cmd == lastAccepted && (now - lastAcceptedTime) < ASR_DUPLICATE_WINDOW_MS) {
            return;
        }

        // 串口命令调试打印已关闭，避免 ASR 重复发送时刷屏。
        // 需要时在 platformio.ini 加 -DASR_DEBUG。

        lastAccepted = cmd;
        lastAcceptedTime = now;

        if (commandCallback) {
            commandCallback(cmd);
        }
    };

    while (Serial2.available()) {
        char c = Serial2.read();
        lastByteTime = millis();
#ifdef ASR_DEBUG
        Serial.printf("ASR HEX: 0x%02X\n", (uint8_t)c);
#endif
        if (c == '\n') {
            deliver(buffer);
            buffer = "";
        } else if (c != '\r') {
            buffer += c;
            // 防止乱码把缓冲区撑爆
            if (buffer.length() > 32) buffer = "";
        }
    }

    // 没有换行的固件：串口空闲 30ms 就把缓冲区里的一行当作完整命令
    if (buffer.length() > 0 && millis() - lastByteTime > 30) {
        deliver(buffer);
        buffer = "";
    }
}

void AsrCommunication::sendMessage(const String& message) {
    Serial.println("发送给ASR:");
    Serial.println(message);
    // 只补一个 '\n'：Serial2.println() 会额外带 '\r'，
    // 天问Block 里按行取到的字符串会多出不可见字符。
    Serial2.print(message);
    Serial2.print('\n');
    lastCommunicationTime = millis();
}

void AsrCommunication::sendHeartbeat() {
    sendMessage(AsrCmd::HEARTBEAT);
}

bool AsrCommunication::isConnected() const {
    return (millis() - lastCommunicationTime) < CONNECTION_TIMEOUT;
}

String AsrCommunication::getStatusMessage() const {
    if (lastCommunicationTime == 0) {
        return "尚未建立连接";
    }
    
    String status = "上次通信在 ";
    status += String((millis() - lastCommunicationTime) / 1000.0);
    status += " 秒前";
    return status;
}
