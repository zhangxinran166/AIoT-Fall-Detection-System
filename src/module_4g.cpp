#include "module_4g.h"
#include "log_utils.h"


Module4G::Module4G() : serial(nullptr) {}

void Module4G::begin(int rxPin, int txPin, long baudRate) {
    // 使用Serial1，跟ASRPRO占用的Serial2区分开，避免冲突
    serial = &Serial1;
    serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
}

bool Module4G::sendATCommand(const String& cmd, const String& expected, unsigned long timeoutMs) {
    if (!serial) return false;

    // 清空一下之前残留在缓冲区里的旧数据，避免读到上一条指令的残留回复
    while (serial->available()) serial->read();

    serial->println(cmd);

    String response = "";
    unsigned long startTime = millis();
    while (millis() - startTime < timeoutMs) {
        while (serial->available()) {
            char c = serial->read();
            response += c;
            // 只要累计收到的内容里包含了期待的关键词，就算成功，提前返回
            if (response.indexOf(expected) != -1) {
                return true;
            }
        }
        // 让出CPU，避免在networkTask(core0)里忙等触发任务看门狗。
        delay(1);
    }
    // 超时了都没等到期待的内容
    print4GLog("[4G] 指令 " + cmd + " 超时，实际收到: " + response);
    return false;
}

bool Module4G::initNetwork() {
    // 这一整套顺序，是我们在SSCOM里一条条手动验证过、最终成功的流程
    bool ok = true;
    ok &= sendATCommand("AT", "OK");
    ok &= sendATCommand("AT+CTZU=1", "OK");           // 开启网络自动同步时间，避免1970年那个坑

    // 新增：先等待网络真正注册成功，最多等20秒，每秒查一次
    // 这一步是关键，避免模块还没准备好就被要求联网导致失败
    bool registered = false;
    for (int i = 0; i < 20; i++) {
        while (serial->available()) serial->read();
        serial->println("AT+CREG?");
        delay(300);
        String resp = "";
        unsigned long t = millis();
        while (millis() - t < 700) {
            while (serial->available()) resp += (char)serial->read();
            delay(1);
        }
        if (resp.indexOf("+CREG: 0,1") != -1 || resp.indexOf("+CREG: 0,5") != -1) {
            registered = true;
            break;
        }
        delay(1000);
    }

    if (!registered) {
        print4GLog("[4G] 等待网络注册超时，跳过后续联网步骤");
        return false;
    }

    ok &= sendATCommand("AT+CGDCONT=1,\"IP\",\"CMNET\"", "OK");
    ok &= sendATCommand("AT+CGATT=1", "OK");
    ok &= sendATCommand("AT+CGACT=1,1", "OK");

    if (!ok) {
        print4GLog("[4G] 网络初始化失败，检查SIM卡/信号");
    }else {
    print4GLog("[4G] 网络初始化成功");
    }
    return ok;
}

void Module4G::terminateHttpSession() {
    while (serial->available()) serial->read();  // 清空缓冲区
    serial->println("AT+HTTPTERM");

    unsigned long startTime = millis();
    String response = "";
    while (millis() - startTime < 1500) {
        while (serial->available()) response += (char)serial->read();
        if (response.indexOf("OK") != -1 || response.indexOf("ERROR") != -1) {
            return; // 不管OK还是ERROR，只要模块回应了就算处理完毕，不打印警告
        }
        delay(1);
    }
    print4GLog("[4G] HTTPTERM无响应，可能模块异常"); // 真正完全没反应才提示
}

bool Module4G::sendPushNotification(const String& title, const String& content) {
    // 每次发送前先清理一下可能残留的上一次HTTP会话状态
    terminateHttpSession();

    if (!sendATCommand("AT+HTTPINIT", "OK")) return false;
    if (!sendATCommand("AT+CSSLCFG=\"sslversion\",0,3", "OK")) return false;
    if (!sendATCommand("AT+CSSLCFG=\"authmode\",0,0", "OK")) return false;
    if (!sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1", "OK")) return false;
    if (!sendATCommand("AT+HTTPPARA=\"SSLCFG\",0", "OK")) return false;

    String url = "https://xizhi.qqoq.net/" + pushKey + ".send?title=" +
                 urlEncode(title) + "&content=" + urlEncode(content);
    if (!sendATCommand("AT+HTTPPARA=\"URL\",\"" + url + "\"", "OK")) return false;

    if (!sendATCommand("AT+HTTPACTION=0", "OK")) return false;

    // AT+HTTPACTION=0返回OK后，真正的结果是模块之后异步推送过来的一行
    // 格式类似 +HTTPACTION: 0,200,33，这里单独再等一下这行结果
    unsigned long startTime = millis();
    String response = "";
    bool success = false;
    while (millis() - startTime < 8000) {
        while (serial->available()) {
            char c = serial->read();
            response += c;
        }
        if (response.indexOf("+HTTPACTION: 0,200") != -1) {
            success = true;
            break;
        }
        if (response.indexOf("+HTTPACTION:") != -1) {
            // 收到结果了，但不是200，直接跳出不用继续等
            break;
        }
        delay(1);
    }

    terminateHttpSession();
    if (!success) {
        print4GLog("[4G] 推送失败，返回内容: " + response);
    }
    return success;
}

bool Module4G::getLocation(String& latitude, String& longitude, unsigned long timeoutMs) {
    sendATCommand("AT+CGNSSPWR=1", "READY!", 15000); // 每次用之前才开机

    String response = "";
    const unsigned long deadline = millis() + timeoutMs;

    // 不是只查一次，而是每 2 秒重新询问 +CGPSINFO。
    // GNSS 冷启动通常需要 30 秒以上，单次查询很容易在定位前就放弃。
    while (millis() < deadline) {
        while (serial->available()) response += (char)serial->read();
        serial->println("AT+CGPSINFO");

        const unsigned long waitStart = millis();
        while (millis() - waitStart < 1800UL) {
            while (serial->available()) response += (char)serial->read();

            const int latest = response.lastIndexOf("+CGPSINFO: ");
            if (latest != -1) {
                const int comma = response.indexOf(',', latest + 11);
                if (comma > latest + 11 && response.length() > comma + 5) {
                    break;
                }
            }
            delay(1);
        }

        const int latest = response.lastIndexOf("+CGPSINFO: ");
        if (latest != -1) {
            const int comma = response.indexOf(',', latest + 11);
            if (comma > latest + 11 && response.length() > comma + 5) {
                break;
            }
        }
        delay(500);
    }

    int dataStart = response.lastIndexOf("+CGPSINFO: ");
    bool success = false;

    if (dataStart != -1) {
        dataStart += 11;
        int firstComma = response.indexOf(',', dataStart);
        if (firstComma != dataStart) {
            int p1 = firstComma;
            int p2 = response.indexOf(',', p1 + 1);
            int p3 = response.indexOf(',', p2 + 1);
            int p4 = response.indexOf(',', p3 + 1);
            latitude = response.substring(dataStart, p1) + "," + response.substring(p1 + 1, p2);
            longitude = response.substring(p2 + 1, p3) + "," + response.substring(p3 + 1, p4);
            success = latitude.length() > 2;
        }
    }

    // 不管搜没搜到，用完立刻关掉GNSS，省电
    sendATCommand("AT+CGNSSPWR=0", "OK", 3000);

    return success;
}

String Module4G::urlEncode(const String& str) {
    String encoded = "";
    char c;
    char code[4];
    for (size_t i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            // 中文等特殊字符，按UTF-8字节逐个转成 %XX 格式
            // 这是之前遇到400错误（编码问题）的直接解决方案
            sprintf(code, "%%%02X", (unsigned char)c);
            encoded += code;
        }
    }
    return encoded;
}

void Module4G::sleep() {
    sendATCommand("AT+CFUN=0", "OK", 3000); // 关闭射频功能，进入低功耗
    print4GLog("[4G] 已进入休眠");
}

void Module4G::wakeUp() {
    sendATCommand("AT+CFUN=1", "OK", 5000); // 恢复射频功能
    print4GLog("[4G] 已唤醒，重新联网中...");
    initNetwork(); // 唤醒后需要重新走一遍联网流程
}

bool Module4G::postJsonToCloud(const String& url, const String& jsonBody, bool useSSL) {
    terminateHttpSession();
    if (!sendATCommand("AT+HTTPINIT", "OK")) return false;

    if (useSSL) {
        sendATCommand("AT+CSSLCFG=\"sslversion\",0,3", "OK");
        sendATCommand("AT+CSSLCFG=\"authmode\",0,0", "OK");
        sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1", "OK");
        sendATCommand("AT+HTTPPARA=\"SSLCFG\",0", "OK");
    }

    if (!sendATCommand("AT+HTTPPARA=\"URL\",\"" + url + "\"", "OK")) return false;
    if (!sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", "OK")) return false;

    // 告诉模块接下来要发多长的数据，给它10秒钟准备接收
    while (serial->available()) serial->read();
    serial->println("AT+HTTPDATA=" + String(jsonBody.length()) + ",10000");

    String resp = "";
    unsigned long t = millis();
    while (millis() - t < 3000) {
        while (serial->available()) resp += (char)serial->read();
        if (resp.indexOf("DOWNLOAD") != -1) break;
        delay(1);
    }
    if (resp.indexOf("DOWNLOAD") == -1) {
        Serial.println("[4G] 未收到DOWNLOAD提示，POST失败");
        terminateHttpSession();
        return false;
    }

    serial->print(jsonBody); // 真正把JSON数据发过去

    resp = "";
    t = millis();
    while (millis() - t < 3000) {
        while (serial->available()) resp += (char)serial->read();
        if (resp.indexOf("OK") != -1) break;
        delay(1);
    }

    if (!sendATCommand("AT+HTTPACTION=1", "OK")) { // 1 = POST
        terminateHttpSession();
        return false;
    }

    resp = "";
    t = millis();
    bool success = false;
    while (millis() - t < 8000) {
        while (serial->available()) resp += (char)serial->read();
        if (resp.indexOf("+HTTPACTION: 1,200") != -1) { success = true; break; }
        if (resp.indexOf("+HTTPACTION:") != -1) break;
        delay(1);
    }

    terminateHttpSession();
    if (!success) Serial.printf("[4G] POST失败，返回: %s\n", resp.c_str());
    return success;
}