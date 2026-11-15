#ifndef LOG_UTILS_H
#define LOG_UTILS_H
#include <Arduino.h>

// 4G/GNSS相关事件，用这个方框
inline void print4GLog(const String& msg) {
  Serial.println("┌──────────── [4G/GNSS] ────────────");
  Serial.println("│ " + msg);
  Serial.println("└────────────────────────────────────");
}

// 蓝牙/WiFi连接状态变化，用这个方框（样式不一样，一眼能区分是哪一类）
inline void printConnLog(const String& msg) {
  Serial.println("═══════════ [连接状态] ═══════════");
  Serial.println("║ " + msg);
  Serial.println("═══════════════════════════════════");
}

#endif