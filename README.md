# 腰部穿戴式老人跌倒监测与紧急求助系统

面向独居、空巢老人的可穿戴跌倒检测与紧急求助系统。

终端基于 ESP32-S3，使用 MPU6050 采集六轴数据，通过 A/G 阈值与重力确认完成跌倒报警，并使用端侧 1D CNN 输出跌倒相似度和方向，作为辅助解释。系统支持 WiFi、BLE、4G 三链路降级，断网时本地缓存事件，恢复后补传；同时支持 ASRPRO 离线语音求助。

## 功能

- 六轴跌倒检测：A/G 候选 + 0.8 秒重力确认
- 端侧 1D CNN：跌倒相似度、前/后/侧方向
- WiFi / BLE / 4G 三链路自动降级
- 断网本地缓存，恢复后补传
- ASRPRO 离线语音求助
- 家属端与老人端 APP
- Flask + SQLite 云端
- 事件证据留存与告警记录

## 系统结构

```text
MPU6050 / MAX17048
        │
        ▼
   ESP32-S3
   ├─ A/G 阈值 + 重力确认
   ├─ 1D CNN 方向/概率
   ├─ WiFi / BLE / 4G
   └─ ASRPRO 语音
        │
        ▼
   Flask + SQLite
        │
        ▼
   uni-app 家属端/老人端
```

## 目录

```text
src/                       ESP32-S3 固件
include/                   头文件、协议、模型参数
cloud_server/fall_cloud/   Flask 云端服务
FALLAPP/                   uni-app 家属端/老人端
platformio.ini             PlatformIO 配置
merge_firmware.py          合并镜像脚本
部署与运行说明.md          部署与运行步骤
README.md
LICENSE
```

## 硬件接口

| 模块 | 接口 | 引脚 |
|---|---|---|
| MPU6050 | I2C | GPIO10 / GPIO11 |
| MAX17048 | I2C | GPIO10 / GPIO11 |
| ASRPRO | UART 9600 | GPIO16 / GPIO17 |
| A7670C | UART 115200 | GPIO4 / GPIO5 |

主控：ESP32-S3-N16R8  
电池：1S 5000mAh，PH2.0-2P

## 快速开始

### 1. 固件

```bash
git clone https://github.com/zhangxinran166/AIoT-Fall-Detection-System.git
cd AIoT-Fall-Detection-System
```

修改：

```text
include/wifi_config.h
```

```cpp
#define WIFI_SSID_DEFAULT     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD_DEFAULT "YOUR_WIFI_PASSWORD"
```

编译、烧录：

```bash
pio run
pio run -t upload --upload-port COMx
pio device monitor --port COMx
```

### 2. 云端

```bash
cd cloud_server/fall_cloud
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
copy .env.example .env
python app.py
```

在 `.env` 中按需填写 `AI_API_KEY`、`AI_API_URL` 和 `AI_MODEL`。  
留空也能启动基础功能；开放问答需要配置自己的 API Key。

### 3. APP

用 HBuilderX 导入 `FALLAPP/`，修改云端地址后打包 APK。

## 算法说明

跌倒报警由阈值规则完成：

```text
加速度 ≥ 1.25g
角速度 ≥ 310dps
两事件间隔 ≤ 20ms
0.8 秒后重力方向变化 ≥ 30°
恢复角 ≤ 40°，持续 2 秒
1.5 秒防重复
```

CNN 只输出方向与概率，作为辅助证据，不参与报警与或判断。

## 验证数据

阈值规则在自测、KFall、SisFall 上做过工程对比：

```text
自测：23 跌倒 / 193 ADL，召回 91.3%，ADL 误报 0
KFall：文件级召回 68.2%，FPR 0.77%
SisFall：跨传感器召回 64.9%，FPR 7.07%
```

CNN 在 KFall 同分布留出上：

```text
召回 100%，FPR 1.0%，方向准确率 94.1%
```

这些是开发验证结果，不代表真实老人临床泛化。

## 已知限制

- 公开数据集与设备在传感器、采样率、佩戴方式上不同。
- 自测样本量有限。
- 天气查询依赖外部接口，网络不可达时会返回不可用。
- CNN 仅作辅助解释，报警由规则完成。

## License

MIT，详见 `LICENSE`。
