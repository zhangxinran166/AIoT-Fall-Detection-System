#ifndef ASR_PROTOCOL_H
#define ASR_PROTOCOL_H

// ============================================================
// ESP32 <-> ASR Pro 语音模块 串口协议
// 物理连接：Serial2，9600 8N1，ESP32 RX=GPIO16，TX=GPIO17
// 每条消息都是一行 ASCII 文本，以 '\n' 结尾（不带 '\r'）。
//
// 【ESP32 -> 语音模块】语音模块收到以下字符串会自动播报对应语音
//   FALL      检测到跌倒，立即报警（核心功能）
//   RECOVER   老人恢复正常姿态，播报“已恢复”
//   MON_ON    开启跌倒监测
//   MON_OFF   关闭跌倒监测
//   CALIB     传感器校准完成
//   HELP      紧急呼救（网页/云端触发）
//   HEARTBEAT 心跳
//
// 【语音模块 -> ESP32】语音模块识别到关键词后发出的命令
//   ON / OPEN / 开启 / 打开   开启监测
//   OFF / CLOSE / 关闭        关闭监测
//   HELP / 紧急               紧急呼叫
//
// 注意：不要用中文长句当命令。语音模块内部是按“固定字符串”匹配的，
// 收到“ESP32: 跌倒监测已开启”这种句子不会触发播报，
// 所以所有下行指令统一使用上面的短码。
// ============================================================

namespace AsrCmd {

constexpr const char* FALL      = "FALL";
constexpr const char* RECOVER   = "RECOVER";
constexpr const char* MON_ON    = "MON_ON";
constexpr const char* MON_OFF   = "MON_OFF";
constexpr const char* CALIB     = "CALIB";
constexpr const char* HELP      = "HELP";
constexpr const char* HEARTBEAT = "HEARTBEAT";

// 同一次跌倒期间重复回调的语音播报最小间隔（毫秒）
constexpr unsigned long FALL_NOTIFY_INTERVAL = 30000;

}  // namespace AsrCmd

#endif  // ASR_PROTOCOL_H
