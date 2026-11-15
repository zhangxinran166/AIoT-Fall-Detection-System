#ifndef MODULE_4G_H
#define MODULE_4G_H

#include <Arduino.h>


class Module4G {
public:
    Module4G();

    bool postJsonToCloud(const String& url, const String& jsonBody, bool useSSL = true);

    // 初始化串口，rxPin/txPin按实际接线改，跟USB转TTL测试时的接线逻辑一样
    // ESP32这边的RX接4G模块的TXD，ESP32这边的TX接4G模块的RXD
    void begin(int rxPin = 4, int txPin = 5, long baudRate = 115200);

    // 开机之后调用一次，把网络、SSL相关的固定配置跑一遍
    // 对应我们在SSCOM里手动测试成功的那一整套流程
    bool initNetwork();

    // 发送推送通知，title/content支持中文（内部会自动做URL编码）
    // 返回true表示HTTP层面收到了200，不代表消息100%送达，但基本可信
    bool sendPushNotification(const String& title, const String& content);

    // 获取GPS定位，室内可能长期返回false（搜不到星是正常现象）
    // 成功时latitude/longitude会被填入形如"2234.26947,N"这样的原始字符串
    bool getLocation(String& latitude, String& longitude, unsigned long timeoutMs = 2000);

    void sleep();   // 让4G进入低功耗休眠
    void wakeUp();  // 从休眠中唤醒4G

private:
    HardwareSerial* serial;
    String pushKey;   // 息知的密钥，构造后需要通过setPushKey设置

public:
    void setPushKey(const String& key) { pushKey = key; }

private:
    // 通用的"发指令、等指定回复"函数，所有上层功能都基于这个
    bool sendATCommand(const String& cmd, const String& expected, unsigned long timeoutMs = 3000);

    // 把一段文本（含中文）转成URL能安全传输的编码格式
    // 这是之前踩过的坑：中文直接拼进URL会被判定成格式错误(400)
    String urlEncode(const String& str);

// module_4g.h 里加一个新方法
private:
    void terminateHttpSession(); // 专门处理关闭session，OK和ERROR都算正常c
};

#endif