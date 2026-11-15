# 腰部穿戴式老人跌倒监测与紧急求助系统

AIoT 嵌入式系统作品运行源码包。作品介绍文档、训练与测试材料在上一级目录，不参与运行。

## 目录

```text
src/                       ESP32-S3 固件
include/                   固件头文件、协议、模型参数
cloud_server/fall_cloud/   Flask 云端服务
FALLAPP/                   uni-app 老人端/家属端
platformio.ini             PlatformIO 构建配置
merge_firmware.py          合并烧录镜像脚本
部署与运行说明.md          固件/云端/APP 的运行步骤
README.md
LICENSE
```

上一级目录另有：

```text
作品介绍文档.docx
硬件设计\BOM 表.xlsx
测试与复现材料\research\
测试与复现材料\docs\
```

## 快速开始

### 1. 固件

先编辑：

```text
include/wifi_config.h
```

填入实际 WiFi：

```cpp
#define WIFI_SSID_DEFAULT     "你的WiFi名称"
#define WIFI_PASSWORD_DEFAULT "你的WiFi密码"
```

然后：

```bash
pio run
pio run -t upload --upload-port COMx
pio device monitor --port COMx
```

### 2. 云端

```bash
cd cloud_server/fall_cloud
python -m venv .venv
# Windows
.venv\Scripts\activate
pip install -r requirements.txt
copy .env.example .env
python app.py
```

在 `.env` 中填写 AI 接口配置，不要提交真实 `.env`。

### 3. APP

用 HBuilderX 导入 `FALLAPP/`，按 `部署与运行说明.md` 修改云端地址并打包 APK。

## 说明

- 本包不包含本地网页配网界面；WiFi 通过 `include/wifi_config.h` 配置。
- `SPIFFS` 仅用于断网告警缓存，不再存放网页资源。
- 作品介绍文档在上一级目录，训练与测试证据在上一级的 `测试与复现材料\research\`、`测试与复现材料\docs\`，不参与固件运行。
- 旧网页界面 `legacy_webui/` 不随本包提供，当前固件也没有本地网页服务。
- 不包含真实 WiFi 密码、云端 `.env`、数据库、编译缓存。
- 详细硬件接线、端口和常见问题见 `部署与运行说明.md`。

## License

MIT，详见 `LICENSE`。

## 路径要求

`platformio.ini` 已将构建目录指向系统临时目录，避免中文路径触发 Xtensa 链接错误，可直接执行 `pio run`。

源目录：

```text
D:\ESP32-Fall-Detection\AIoT嵌入式系统创新大赛-腰部穿戴式老人跌倒监测与紧急求助系统\源代码
```
编译后如生成 `.pio`、`.vscode`、`__pycache__`，提交前删除；它们是可重建的编译缓存。

