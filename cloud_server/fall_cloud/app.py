from flask import Flask, request, jsonify
from flask_cors import CORS
from datetime import datetime
from threading import Lock
import json
import os
import sqlite3
import time
import urllib.error
import urllib.parse
import urllib.request
from copy import deepcopy
from admin_module import init_module, notify_family

app = Flask(__name__)
CORS(app)

# ============================================================
# 基本配置
# ============================================================

DEFAULT_DEVICE_ID = "ESP32_001"
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.environ.get("FALL_DB_PATH", os.path.join(BASE_DIR, "fall.db"))

# 保存最多多少条报警历史
MAX_ALARM_HISTORY = 100

# ============================================================
# 线程锁
# ============================================================

data_lock = Lock()


# ============================================================
# 持久化：状态/报警/离线/久坐/位置写 SQLite
#
# 只持久化低容量状态，不把高频原始六轴写入数据库。
# 写操作在请求结束后执行，避免持锁写盘阻塞接口。
# ============================================================

def _state_conn():
    conn = sqlite3.connect(DB_PATH, timeout=10)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA busy_timeout=5000")
    return conn


def init_state_db():
    conn = _state_conn()
    try:
        with conn:
            conn.execute(
                "CREATE TABLE IF NOT EXISTS cloud_state("
                "key TEXT PRIMARY KEY,"
                "value TEXT NOT NULL,"
                "updated_at TEXT NOT NULL)"
            )
    finally:
        conn.close()


def save_cloud_state():
    with data_lock:
        payload = deepcopy({
            "latest_alarm": latest_alarm,
            "alarm_history": alarm_history,
            "offline_periods": offline_periods,
            "sedentary_history": sedentary_history,
            "sedentary_last_seconds": sedentary_last_seconds,
            "device_last_seen": device_last_seen,
            "device_states": device_states,
            "phone_locations": phone_locations,
            "g4_locations": g4_locations,
        })
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    conn = _state_conn()
    try:
        with conn:
            for key, value in payload.items():
                conn.execute(
                    "INSERT INTO cloud_state(key, value, updated_at) VALUES(?,?,?) "
                    "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at",
                    (key, json.dumps(value, ensure_ascii=False, default=str), now),
                )
    finally:
        conn.close()


def load_cloud_state():
    global latest_alarm, alarm_history, offline_periods, sedentary_history
    global sedentary_last_seconds, device_last_seen, device_states
    global phone_locations, g4_locations

    conn = _state_conn()
    try:
        rows = conn.execute("SELECT key, value FROM cloud_state").fetchall()
    except sqlite3.Error as exc:
        print("[持久化] 读取失败:", exc)
        return
    finally:
        conn.close()

    loaded = {}
    for key, value in rows:
        try:
            loaded[key] = json.loads(value)
        except (TypeError, ValueError):
            print("[持久化] 忽略损坏记录:", key)

    with data_lock:
        if isinstance(loaded.get("latest_alarm"), dict):
            latest_alarm = loaded["latest_alarm"]
        if isinstance(loaded.get("alarm_history"), list):
            alarm_history[:] = loaded["alarm_history"][:MAX_ALARM_HISTORY]
        if isinstance(loaded.get("offline_periods"), list):
            offline_periods[:] = loaded["offline_periods"][-100:]
        if isinstance(loaded.get("sedentary_history"), list):
            sedentary_history[:] = loaded["sedentary_history"][-200:]
        if isinstance(loaded.get("sedentary_last_seconds"), dict):
            sedentary_last_seconds.update(loaded["sedentary_last_seconds"])
        if isinstance(loaded.get("device_last_seen"), dict):
            device_last_seen.update(loaded["device_last_seen"])
        if isinstance(loaded.get("device_states"), dict):
            device_states.update(loaded["device_states"])
        if isinstance(loaded.get("phone_locations"), dict):
            phone_locations.update(loaded["phone_locations"])
        if isinstance(loaded.get("g4_locations"), dict):
            g4_locations.update(loaded["g4_locations"])

    print("[持久化] 已加载:",
          {k: (len(v) if isinstance(v, (list, dict)) else 1) for k, v in loaded.items()})



# ============================================================
# 最新报警数据
#
# APP 获取：
# GET /api/alarm/latest
#
# 目前支持：
# WiFi
# Bluetooth
# 4G
#
# location_source:
#   wifi
#   bluetooth
#   4g
# ============================================================

latest_alarm = {
    "device_id": DEFAULT_DEVICE_ID,

    "fallen": False,

    "pitch": 0,
    "roll": 0,

    # AI 辅助观察与阈值模长（仅展示，不参与云端判断）
    "ai_prob": None,
    "ai_dir": None,
    "ai_conf": None,
    "ai_latency_ms": None,
    "acc_mag": None,
    "gyro_mag": None,
    "wearing_ok": None,
    "long_lie_seconds": 0,
    "long_lie_level": 0,
    "sedentary_seconds": 0,
    "battery_available": False,
    "battery_voltage": None,
    "battery_percent": None,
    "battery_low": False,
    "event_ai_prob": None,
    "event_ai_dir": None,
    "event_ai_time": None,

    "location_source": None,

    "latitude": None,
    "longitude": None,

    "time": None
}


# ============================================================
# 报警历史
# ============================================================

alarm_history = []

# 离线时间段（当前为内存存储；重启会丢失，后续可迁 SQLite）
offline_periods = []
OFFLINE_GAP_SECONDS = 30
sedentary_history = []
sedentary_last_seconds = {}
device_last_seen = {}


# ============================================================
# 设备状态
#
# 后面 APP 可以直接读取这个接口显示：
#
# WiFi：已连接 / 未连接
# Bluetooth：已连接 / 未连接
# 4G：休眠 / 工作
#
# 当前 ESP32 如果还没有主动上报这些状态，
# 就保持 unknown。
# ============================================================

device_states = {}


# ============================================================
# 云端控制命令
#
# APP → 云服务器 → ESP32
#
# 每个设备只保存一个等待执行的命令。
#
# ESP32 获取一次以后立即删除。
# ============================================================

pending_commands = {}


# ============================================================
# 工具函数
# ============================================================

def now_string():
    """
    返回当前服务器时间。
    """

    return datetime.now().strftime(
        "%Y-%m-%d %H:%M:%S"
    )


def get_device_id(data):
    """
    从请求数据中获取设备ID。
    """

    if not data:
        return DEFAULT_DEVICE_ID

    return data.get(
        "device_id",
        DEFAULT_DEVICE_ID
    )


# ============================================================
# 首页
# ============================================================

@app.route("/", methods=["GET"])
def index():

    return jsonify({
        "success": True,
        "service": "Fall Detection Cloud Server",
        "version": "1.0",
        "message": "Fall Cloud Server is running."
    })


# ============================================================
# 测试接口
# ============================================================

@app.route("/api/test", methods=["GET"])
def api_test():

    return jsonify({
        "success": True,
        "message": "Cloud server OK",
        "time": now_string()
    })


# ============================================================
# ============================================================
#                    报警系统
# ============================================================
# ============================================================


# ============================================================
# ESP32 上传报警
#
# POST /api/alarm
#
# WiFi：
#
# {
#   "device_id": "ESP32_001",
#   "fallen": true,
#   "pitch": -166.16,
#   "roll": 99.86
# }
#
# 4G：
#
# {
#   "device_id": "ESP32_001",
#   "fallen": true,
#   "pitch": -166.16,
#   "roll": 99.86,
#   "location_source": "4g",
#   "latitude": "...",
#   "longitude": "..."
# }
# ============================================================

@app.route("/api/alarm", methods=["POST"])
def receive_alarm():

    global latest_alarm

    data = request.get_json(
        silent=True
    )

    if data is None:

        return jsonify({
            "success": False,
            "message": "Invalid JSON"
        }), 400


    device_id = get_device_id(data)

    fallen = data.get(
        "fallen",
        False
    )

    pitch = data.get(
        "pitch",
        0
    )

    roll = data.get(
        "roll",
        0
    )

    ai_prob = data.get("ai_prob", None)
    ai_dir = data.get("ai_dir", None)
    ai_conf = data.get("ai_conf", None)
    ai_latency_ms = data.get("ai_latency_ms", None)
    acc_mag = data.get("acc_mag", None)
    gyro_mag = data.get("gyro_mag", None)
    wearing_ok = data.get("wearing_ok", None)
    event_ai_prob = data.get("event_ai_prob", None)
    event_ai_dir = data.get("event_ai_dir", None)
    event_ai_time = data.get("event_ai_time", None)
    event_type = data.get("event", None)
    event_message = data.get("message", None)
    event_id = data.get("event_id", None)

    location_source = data.get(
        "location_source",
        None
    )

    latitude = data.get(
        "latitude",
        None
    )

    longitude = data.get(
        "longitude",
        None
    )

    alarm_time = now_string()
    now_epoch = int(time.time())

    event_epoch = data.get("event_epoch", None)
    try:
        if event_epoch is not None and int(event_epoch) > 0:
            alarm_time = datetime.fromtimestamp(int(event_epoch)).strftime("%Y-%m-%d %H:%M:%S")
    except (TypeError, ValueError, OSError):
        pass

    # 有 event_id 时按 ID 去重；没有 ID 的旧 ASR 才按 10 分钟未确认去重。
    if location_source == "help":
        with data_lock:
            for item in alarm_history:
                if item.get("device_id") != device_id:
                    continue
                if item.get("location_source") != "help":
                    continue
                if event_id:
                    if item.get("event_id") == event_id:
                        return jsonify({
                            "success": True,
                            "duplicate": True,
                            "data": item
                        })
                    continue
                age = now_epoch - int(item.get("event_epoch") or 0)
                if item.get("acked") is not True and age < 600:
                    return jsonify({
                        "success": True,
                        "duplicate": True,
                        "data": item
                    })



    # --------------------------------------------------------
    # 保存报警
    # --------------------------------------------------------

    alarm = {

        "device_id": device_id,

        "fallen": fallen,

        "pitch": pitch,

        "roll": roll,

        "ai_prob": ai_prob,
        "ai_dir": ai_dir,
        "ai_conf": ai_conf,
        "ai_latency_ms": ai_latency_ms,
        "acc_mag": acc_mag,
        "gyro_mag": gyro_mag,
        "wearing_ok": wearing_ok,
        "event_ai_prob": event_ai_prob,
        "event_ai_dir": event_ai_dir,
        "event_ai_time": event_ai_time,
        "event": event_type,
        "message": event_message,
        "event_id": event_id,
        "acked": False,
        "id": int(time.time() * 1000),
        "event_epoch": now_epoch,

        "location_source": location_source,

        "latitude": latitude,

        "longitude": longitude,

        "time": alarm_time
    }


    with data_lock:

        latest_alarm = alarm.copy()

        alarm_history.insert(
            0,
            alarm.copy()
        )

        if len(alarm_history) > MAX_ALARM_HISTORY:

            alarm_history.pop()


    if fallen or location_source == "help" or event_type == "help":
        try:
            notify_family(device_id, alarm)
        except Exception as e:
            print("[通知] 生成家属通知失败：", e)

    # 4G 只在跌倒事件里定位；把这次坐标同步到位置接口，
    # 让家属端跌倒后能直接在地图上看到 4G 位置。
    if location_source == "4g":
        try:
            lat_ok = float(latitude)
            lon_ok = float(longitude)
            if abs(lat_ok) <= 90 and abs(lon_ok) <= 180 and not (lat_ok == 0 and lon_ok == 0):
                _save_location(device_id, lat_ok, lon_ok, None, "4g", "wgs84")
        except (TypeError, ValueError):
            pass

    print()
    print("========================================")
    print("           收到ESP32报警")
    print("========================================")
    print("设备：", device_id)
    print("跌倒：", fallen)
    print("Pitch：", pitch)
    print("Roll：", roll)
    print("通信来源：", location_source)
    print("纬度：", latitude)
    print("经度：", longitude)
    print("时间：", alarm_time)
    print("========================================")
    print()


    return jsonify({

        "success": True,

        "message": "Alarm received",

        "data": alarm

    })


# ============================================================
# APP 获取最新报警
#
# GET /api/alarm/latest
#
# APP 页面主要使用这个接口。
# ============================================================

@app.route("/api/alarm/latest", methods=["GET"])
def get_latest_alarm():

    # 支持 ?device_id=ESP32_002
    # 不传或者就是默认设备，返回全局那一份；
    # 传了别的设备号，从报警历史里找这台设备最近的一条。
    device_id = request.args.get("device_id")

    with data_lock:

        if device_id and device_id != DEFAULT_DEVICE_ID:
            data = None
            for item in alarm_history:
                if item.get("device_id") == device_id:
                    data = item.copy()
                    break
            if data is None:
                data = {
                    "device_id": device_id,
                    "fallen": False,
                    "pitch": None,
                    "roll": None,
                    "location_source": None,
                    "latitude": None,
                    "longitude": None,
                    "time": None,
                }
        else:
            data = latest_alarm.copy()


    return jsonify({

        "success": True,

        "data": data

    })


# ============================================================
# APP 获取报警历史
#
# GET /api/alarm/history
#
# 后面 APP 可以做：
#
# 今天报警
# 最近三天
# 报警记录
# 历史位置
# ============================================================

@app.route("/api/alarm/history", methods=["GET"])
def get_alarm_history():

    device_id = request.args.get(
        "device_id"
    )

    time_from = request.args.get("from")
    time_to = request.args.get("to")


    with data_lock:

        if device_id:

            history = [

                item.copy()

                for item in alarm_history

                if item.get("device_id")
                == device_id

            ]

        else:

            history = [

                item.copy()

                for item in alarm_history

            ]

        if time_from:
            history = [item for item in history if (item.get("time") or "") >= time_from]

        if time_to:
            history = [item for item in history if (item.get("time") or "") <= time_to]


    return jsonify({

        "success": True,

        "count": len(history),

        "data": history

    })


# ============================================================
# APP 确认求助/报警
# POST /api/alarm/ack
# ============================================================

@app.route("/api/alarm/ack", methods=["POST"])
def ack_alarm():

    data = request.get_json(silent=True) or {}
    device_id = get_device_id(data)
    alarm_id = data.get("id")
    alarm_time = data.get("time")

    changed = 0
    ack_all_help = alarm_id is None and alarm_time is None
    ack_all_fallen = bool(data.get("all_fallen"))
    with data_lock:
        for item in alarm_history:
            if item.get("device_id") != device_id:
                continue
            if ack_all_fallen:
                if not item.get("fallen") or item.get("location_source") == "help":
                    continue
            elif ack_all_help:
                if item.get("location_source") != "help":
                    continue
            else:
                if alarm_id is not None and str(item.get("id")) != str(alarm_id):
                    continue
                if alarm_time is not None and item.get("time") != alarm_time:
                    continue
            item["acked"] = True
            changed += 1

        if latest_alarm and latest_alarm.get("device_id") == device_id:
            matched_latest = False
            if ack_all_fallen and latest_alarm.get("fallen"):
                matched_latest = True
            elif ack_all_help and latest_alarm.get("location_source") == "help":
                matched_latest = True
            elif not ack_all_help and not ack_all_fallen:
                if alarm_id is not None and str(latest_alarm.get("id")) == str(alarm_id):
                    matched_latest = True
                elif alarm_time is not None and latest_alarm.get("time") == alarm_time:
                    matched_latest = True
            if matched_latest:
                latest_alarm["acked"] = True
                changed += 1

    return jsonify({"success": True, "changed": changed})


# ============================================================
# APP 清除最新报警状态
#
# 后面如果需要“已处理报警”，APP可以调用。
#
# POST /api/alarm/clear
# ============================================================

@app.route("/api/alarm/clear", methods=["POST"])
def clear_alarm():

    global latest_alarm

    data = request.get_json(
        silent=True
    ) or {}


    device_id = get_device_id(data)


    with data_lock:

        latest_alarm = {

            "device_id": device_id,

            "fallen": False,

            "pitch": 0,

            "roll": 0,

            "ai_prob": None,
            "ai_dir": None,
            "ai_conf": None,
    "ai_latency_ms": None,
            "acc_mag": None,
            "gyro_mag": None,
            "wearing_ok": None,
    "long_lie_seconds": 0,
    "long_lie_level": 0,
    "sedentary_seconds": 0,
    "battery_available": False,
    "battery_voltage": None,
    "battery_percent": None,
    "battery_low": False,
    "event_ai_prob": None,
    "event_ai_dir": None,
    "event_ai_time": None,

            "location_source": None,

            "latitude": None,

            "longitude": None,

            "time": now_string()

        }


    print(
        "[云端] 报警状态已清除：",
        device_id
    )


    return jsonify({

        "success": True,

        "message": "Alarm cleared",

        "data": latest_alarm

    })


# ============================================================
# ============================================================
#                    云端控制
# ============================================================
# ============================================================


# ============================================================
# APP → 云端
#
# POST /api/command
#
# APP发送：
#
# {
#   "device_id": "ESP32_001",
#   "command": "start_monitoring"
# }
#
# 支持：
#
# start_monitoring
# stop_monitoring
# emergency
# calibrate
# ============================================================

@app.route("/api/command", methods=["POST"])
def receive_command():

    data = request.get_json(
        silent=True
    )

    if data is None:

        return jsonify({

            "success": False,

            "message": "Invalid JSON"

        }), 400


    device_id = get_device_id(data)

    command = data.get(
        "command"
    )


    if not command:

        return jsonify({

            "success": False,

            "message": "Missing command"

        }), 400


    allowed_commands = [

        "start_monitoring",

        "stop_monitoring",

        "emergency",

        "calibrate"

    ]


    if command not in allowed_commands:

        return jsonify({

            "success": False,

            "message": "Unknown command",

            "allowed_commands":
                allowed_commands

        }), 400


    with data_lock:

        pending_commands[device_id] = {

            "command": command,

            "time": now_string()

        }


    print()
    print("========================================")
    print("          收到APP云端控制命令")
    print("========================================")
    print("设备：", device_id)
    print("命令：", command)
    print("========================================")
    print()


    return jsonify({

        "success": True,

        "message": "Command queued",

        "device_id": device_id,

        "command": command

    })


# ============================================================
# ESP32 获取等待执行的命令
#
# GET /api/command/pending?device_id=ESP32_001
#
# ESP32目前就是访问这个接口。
# ============================================================

@app.route("/api/command/pending", methods=["GET"])
def get_pending_command():

    device_id = request.args.get(

        "device_id",

        DEFAULT_DEVICE_ID

    )


    with data_lock:

        pending = pending_commands.get(

            device_id

        )


        if pending is None:

            return jsonify({

                "success": True,

                "command": None

            })


        command = pending.get(

            "command"

        )


        command_time = pending.get(

            "time"

        )


        # ----------------------------------------------------
        # ESP32取走以后立即删除
        #
        # 防止同一个命令重复执行
        # ----------------------------------------------------

        del pending_commands[device_id]


    print()
    print("========================================")
    print("        ESP32获取到云端控制命令")
    print("========================================")
    print("设备：", device_id)
    print("命令：", command)
    print("命令时间：", command_time)
    print("========================================")
    print()


    return jsonify({

        "success": True,

        "command": command,

        "time": command_time

    })


# ============================================================
# 查询当前是否有等待命令
#
# APP以后如果需要调试，可以使用。
# ============================================================

@app.route("/api/command/status", methods=["GET"])
def command_status():

    device_id = request.args.get(

        "device_id",

        DEFAULT_DEVICE_ID

    )


    with data_lock:

        pending = pending_commands.get(

            device_id
        )


    return jsonify({

        "success": True,

        "device_id": device_id,

        "pending": pending is not None,

        "command": (
            pending.get("command")
            if pending
            else None
        )

    })


# ============================================================
# ============================================================
#                    设备通信状态
# ============================================================
# ============================================================


# ============================================================
# ESP32 上报通信状态
#
# POST /api/device/status
#
# 以后可以发送：
#
# {
#   "device_id": "ESP32_001",
#   "wifi": true,
#   "bluetooth": true,
#   "4g": false
# }
#
# 当前ESP32还没接这个接口也没关系。
# ============================================================

@app.route("/api/device/status", methods=["POST"])
def update_device_status():

    data = request.get_json(
        silent=True
    )

    if data is None:

        return jsonify({

            "success": False,

            "message": "Invalid JSON"

        }), 400


    device_id = get_device_id(data)


    status = {
    "device_id": device_id,

    "wifi": data.get("wifi", None),
    "bluetooth": data.get("bluetooth", None),
    "4g": data.get("4g", None),

    "wifi_status": data.get("wifi_status", None),
    "bluetooth_status": data.get("bluetooth_status", None),
    "4g_status": data.get("4g_status", None),

    "pitch": data.get("pitch", None),
    "roll": data.get("roll", None),
    "fallen": data.get("fallen", False),
    "monitoring": data.get("monitoring", None),

    "ai_prob": data.get("ai_prob", None),
    "ai_dir": data.get("ai_dir", None),
    "ai_conf": data.get("ai_conf", None),
    "ai_latency_ms": data.get("ai_latency_ms", None),
    "acc_mag": data.get("acc_mag", None),
    "gyro_mag": data.get("gyro_mag", None),
    "wearing_ok": data.get("wearing_ok", None),
    "long_lie_seconds": data.get("long_lie_seconds", 0),
    "long_lie_level": data.get("long_lie_level", 0),
    "sedentary_seconds": data.get("sedentary_seconds", 0),
    "battery_available": data.get("battery_available", False),
    "battery_voltage": data.get("battery_voltage", None),
    "battery_percent": data.get("battery_percent", None),
    "battery_low": data.get("battery_low", False),
    "event_ai_prob": data.get("event_ai_prob", None),
    "event_ai_dir": data.get("event_ai_dir", None),
    "event_ai_time": data.get("event_ai_time", None),

    "link": data.get("link", None),

    "waiting_for_4g": data.get("waiting_for_4g", False),
    "4g_countdown": data.get("4g_countdown", 0),

    "time": now_string()
}


    with data_lock:

        old_status = device_states.get(device_id)
        now_epoch = int(time.time())
        old_epoch = device_last_seen.get(device_id)

        # 手机通过 BLE 代传状态时只带链路/姿态/AI 字段，
        # 不能把 ESP32 刚上传的电量、长躺、事件 AI 等字段覆盖成 null。
        if old_status and data.get("link") == "ble":
            preserve_fields = (
                "battery_available", "battery_voltage", "battery_percent", "battery_low",
                "long_lie_seconds", "long_lie_level", "sedentary_seconds",
                "event_ai_prob", "event_ai_dir", "event_ai_time",
                "ai_latency_ms",
            )
            for field in preserve_fields:
                if field not in data:
                    status[field] = old_status.get(field, status.get(field))

        if old_status and old_epoch is not None:
            gap = now_epoch - old_epoch
            if gap >= OFFLINE_GAP_SECONDS:
                offline_periods.append({
                    "device_id": device_id,
                    "offline_start": old_status.get("time"),
                    "offline_end": status.get("time"),
                    "duration_seconds": gap
                })
                if len(offline_periods) > 100:
                    offline_periods.pop(0)

        device_last_seen[device_id] = now_epoch

        sedentary_seconds = int(status.get("sedentary_seconds") or 0)
        previous_sedentary = sedentary_last_seconds.get(device_id, 0)
        if sedentary_seconds >= 45 * 60 and previous_sedentary < 45 * 60:
            sedentary_history.append({
                "device_id": device_id,
                "time": status.get("time"),
                "sedentary_seconds": sedentary_seconds,
                "id": int(time.time() * 1000)
            })
            if len(sedentary_history) > 200:
                sedentary_history.pop(0)
        sedentary_last_seconds[device_id] = sedentary_seconds

        device_states[device_id] = status


    print(
        "[设备状态]",
        status
    )


    return jsonify({

        "success": True,

        "data": status

    })


# ============================================================
# APP 获取设备通信状态
#
# GET /api/device/status?device_id=ESP32_001
# ============================================================

@app.route("/api/device/status", methods=["GET"])
def get_device_status():

    device_id = request.args.get(
        "device_id",
        DEFAULT_DEVICE_ID
    )

    with data_lock:

        status = device_states.get(
            device_id
        )

    if status is None:

        status = {
    "device_id": device_id,

    "wifi": None,
    "bluetooth": None,
    "4g": None,

    "wifi_status": "unknown",
    "bluetooth_status": "unknown",
    "4g_status": "unknown",

    "pitch": None,
    "roll": None,
    "fallen": False,
    "monitoring": None,

    "ai_prob": None,
    "ai_dir": None,
    "ai_conf": None,
    "ai_latency_ms": None,
    "acc_mag": None,
    "gyro_mag": None,
    "wearing_ok": None,
    "long_lie_seconds": 0,
    "long_lie_level": 0,
    "sedentary_seconds": 0,
    "battery_available": False,
    "battery_voltage": None,
    "battery_percent": None,
    "battery_low": False,
    "event_ai_prob": None,
    "event_ai_dir": None,
    "event_ai_time": None,

    "link": None,

    "waiting_for_4g": False,
    "4g_countdown": 0,

    "time": None
}

    else:

        wifi = status.get("wifi")
        bluetooth = status.get("bluetooth")
        four_g = status.get("4g")

        monitoring = status.get(
            "monitoring",
            None
        )

        status["wifi_connected"] = wifi

        status["bluetooth_connected"] = bluetooth

        status["four_g_active"] = four_g

        status["monitoring_active"] = monitoring


    return jsonify({

        "success": True,

        "data": status

    })


# ============================================================
# APP 查询离线时间段
# GET /api/device/offline_periods?device_id=ESP32_001
# ============================================================

@app.route("/api/device/offline_periods", methods=["GET"])
def get_offline_periods():

    device_id = request.args.get(
        "device_id",
        DEFAULT_DEVICE_ID
    )

    with data_lock:

        periods = [
            item.copy()
            for item in offline_periods
            if item.get("device_id") == device_id
        ]

    return jsonify({
        "success": True,
        "count": len(periods),
        "data": periods
    })


# ============================================================
# APP 查询久坐记录
# GET /api/device/sedentary_records?device_id=ESP32_001
# ============================================================

@app.route("/api/device/sedentary_records", methods=["GET"])
def get_sedentary_records():

    device_id = request.args.get(
        "device_id",
        DEFAULT_DEVICE_ID
    )

    with data_lock:

        records = [
            item.copy()
            for item in sedentary_history
            if item.get("device_id") == device_id
        ]

    records.reverse()

    return jsonify({
        "success": True,
        "count": len(records),
        "data": records
    })


# 保存每台设备对应的老人手机最新位置。
# 位置数据与报警历史分开保存，避免普通定位覆盖报警记录。
# ============================================================
#                    定位（老人手机 GPS + 4G 芯片）
#
# 两路位置分开保存，互不覆盖：
#   phone_locations：老人手机 APP 上报（POST /api/location，source=phone）
#   g4_locations   ：ESP32 的 4G 芯片 GNSS 上报
#                    （POST /api/location，source=4g；
#                      或 /api/alarm 里 location_source=4g 且带经纬度）
#
# 家人端 GET /api/location 时，按设备当前链路选一路返回：
#   WiFi 或 蓝牙在线  → 老人手机位置
#   WiFi 和 蓝牙都断开 → 4G 芯片位置（没有就退回手机最后一次位置）
#   设备状态未知/过期  → 两路里最新的一条
#
# 每条位置带 coord 字段：wgs84 或 gcj02，家人端据此决定要不要转换坐标。
# ============================================================

phone_locations = {}
g4_locations = {}

PHONE_LOCATION_OFFLINE_SECONDS = 30
# 天气查询允许使用更早的最后已知位置；仅用于天气，不影响在线判断。
LOCATION_WEATHER_FALLBACK_SECONDS = 6 * 3600
DEVICE_STATUS_FRESH_SECONDS = 90


def _valid_coordinate(value, low, high):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if low <= number <= high:
        return number
    return None


def _valid_accuracy(value):
    if value is None:
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if 0 <= number <= 100000:
        return number
    return None


def _phone_location_online(record):
    if not record:
        return False
    updated_at = int(record.get("updated_at", 0) or 0)
    return updated_at > 0 and (
        datetime.now().timestamp() * 1000 - updated_at
    ) <= PHONE_LOCATION_OFFLINE_SECONDS * 1000


def _location_fresh(record, seconds):
    if not record:
        return False
    try:
        updated_at = int(record.get("updated_at", 0) or 0)
    except (TypeError, ValueError):
        return False
    return updated_at > 0 and (
        datetime.now().timestamp() * 1000 - updated_at
    ) <= seconds * 1000


def _device_link_state(device_id):
    """
    根据 ESP32 最近一次上报的状态判断链路（需持有 data_lock）。
    返回：home_wifi / outside_ble / device_far / unknown
    """
    status = device_states.get(device_id)
    if not status:
        return "unknown"

    try:
        reported = datetime.strptime(status.get("time"), "%Y-%m-%d %H:%M:%S")
        age = (datetime.now() - reported).total_seconds()
    except (TypeError, ValueError):
        return "unknown"

    if age > DEVICE_STATUS_FRESH_SECONDS:
        return "unknown"

    wifi = status.get("wifi")
    bluetooth = status.get("bluetooth")

    if wifi is None and bluetooth is None:
        return "unknown"
    if wifi is True:
        return "home_wifi"
    if bluetooth is True:
        return "outside_ble"
    return "device_far"


def _save_location(device_id, latitude, longitude, accuracy, source, coord="wgs84"):
    """
    保存一条位置，source 为 phone 或 4g。
    注意：调用时不能已经持有 data_lock。
    """
    if source not in ("phone", "4g"):
        source = "phone"
    if coord not in ("wgs84", "gcj02"):
        coord = "wgs84"

    record = {
        "device_id": device_id,
        "latitude": latitude,
        "longitude": longitude,
        "accuracy": accuracy,
        "source": source,
        "location_source": source,
        "coord": coord,
        "updated_at": int(datetime.now().timestamp() * 1000),
        "time": now_string()
    }

    with data_lock:
        if source == "4g":
            g4_locations[device_id] = record
        else:
            phone_locations[device_id] = record
        link_state = _device_link_state(device_id)

    result = record.copy()
    result["link_state"] = link_state
    return result


@app.route("/api/location", methods=["POST"])
def upload_location():
    """老人手机 / ESP32 4G 芯片上传位置。"""
    data = request.get_json(silent=True) or {}

    device_id = get_device_id(data)
    latitude = _valid_coordinate(data.get("latitude"), -90, 90)
    longitude = _valid_coordinate(data.get("longitude"), -180, 180)

    if latitude is None or longitude is None:
        return jsonify({
            "success": False,
            "message": "latitude/longitude无效"
        }), 400

    source = str(data.get("source") or "phone").lower()
    coord = str(data.get("coord") or "wgs84").lower()

    record = _save_location(
        device_id,
        latitude,
        longitude,
        _valid_accuracy(data.get("accuracy")),
        source,
        coord
    )

    print(
        "[定位上报]",
        device_id,
        "来源：", record["source"],
        "坐标系：", record["coord"],
        "纬度：", latitude,
        "经度：", longitude,
        "精度：", record["accuracy"],
        "链路：", record["link_state"]
    )

    return jsonify({
        "success": True,
        "message": "位置上传成功",
        "data": record
    })


@app.route("/api/location", methods=["GET"])
def get_location():
    """家人端获取老人当前应显示的位置。"""
    device_id = request.args.get(
        "device_id",
        DEFAULT_DEVICE_ID
    )

    with data_lock:
        phone = phone_locations.get(device_id)
        g4 = g4_locations.get(device_id)
        phone = phone.copy() if phone else None
        g4 = g4.copy() if g4 else None
        link_state = _device_link_state(device_id)

    if link_state in ("home_wifi", "outside_ble"):
        # 主链路在线时以手机位置为主；手机暂时没数据就返回空，
        # 不为了一条旧 4G 坐标绕过主链路。
        chosen = phone
    elif link_state == "device_far":
        # 双离线时才使用 4G；4G 过期或没有坐标则回退手机最后位置。
        chosen = g4 if _location_fresh(g4, 300) else (phone or g4)
    else:
        candidates = [r for r in (phone, g4) if r]
        chosen = max(
            candidates,
            key=lambda r: r.get("updated_at", 0)
        ) if candidates else None

    if chosen is None:
        data = {
            "device_id": device_id,
            "latitude": None,
            "longitude": None,
            "accuracy": None,
            "source": None,
            "location_source": None,
            "coord": "wgs84",
            "link_state": link_state,
            "phone_online": False,
            "updated_at": 0,
            "time": "",
            "stale": True
        }
    else:
        data = chosen
        data["link_state"] = link_state
        data["phone_online"] = _phone_location_online(phone)
        try:
            age = max(
                0,
                int(datetime.now().timestamp() * 1000)
                - int(data.get("updated_at", 0) or 0)
            ) / 1000.0
        except (TypeError, ValueError):
            age = None
        data["age_seconds"] = round(age, 1) if age is not None else None
        data["stale"] = (
            not _location_fresh(data, 30)
            if data.get("source") == "phone"
            else not _location_fresh(data, 300)
        )

    return jsonify({
        "success": True,
        "data": data,
        "phone": phone,
        "g4": g4
    })

# ============================================================
# ============================================================
#                    系统信息
# ============================================================
# ============================================================


@app.route("/api/system/status", methods=["GET"])
def system_status():

    with data_lock:

        alarm_count = len(
            alarm_history
        )

        devices = list(
            device_states.keys()
        )


    return jsonify({

        "success": True,

        "server": {

            "running": True,

            "time": now_string()

        },

        "alarm_count":
            alarm_count,

        "devices":
            devices

    })


# ============================================================
# 错误处理
# ============================================================

@app.errorhandler(404)
def page_not_found(error):

    return jsonify({

        "success": False,

        "message": "API endpoint not found",

        "path": request.path

    }), 404


# ============================================================
# 云端 AI 助手（不生成语音，只返回文字 + 预录语音代码）
#
# ESP32 / ASR 只播放 voice_code 对应的固定音频，
# 不在云端做 TTS，也不在设备端做文字转语音。
# ============================================================

AI_API_URL = os.environ.get(
    "AI_API_URL",
    "https://api.deepseek.com/chat/completions"
)
AI_API_KEY = os.environ.get("AI_API_KEY", "")
AI_MODEL = os.environ.get("AI_MODEL", "deepseek-chat")
AI_TIMEOUT = float(os.environ.get("AI_TIMEOUT", "12"))

# 天气接口：Open-Meteo 免 Key，位置必须来自设备/手机实时上报。
# 不设置固定城市坐标，避免老人换城市后天气还停留在旧位置。
WEATHER_TIMEOUT = float(os.environ.get("WEATHER_TIMEOUT", "2"))

AI_SYSTEM_PROMPT = (
    "你是家属端的老人看护助手。提问者通常是家属，不是老人本人。"
    "称呼提问者用“您”；称呼老人用“老人”“他/她”或老人姓名，"
    "绝不能用“您”指代老人。例如问“设备怎么戴”，应回答“老人应佩戴在……”，"
    "不要说“您应佩戴在……”。"
    "回答必须简短、口语化、一次不超过两句话。"
    "只能根据提供的设备数据回答，不能编造实时天气、温度、用药记录或位置。"
    "数据里没有的信息，直接说暂时没有数据，不要猜。"
    "不要做疾病诊断，不要开具处方。"
    "遇到胸痛、呼吸困难、意识不清、跌倒后无法起身等情况，"
    "提醒立即联系家属或拨打急救电话。"
)


def _battery_percent(context):
    status = (context or {}).get("status") or {}
    if status.get("battery_available") is not True:
        return None
    value = status.get("battery_percent")
    if not isinstance(value, (int, float)):
        return None
    return max(0, min(100, int(value)))


def _battery_voice_code(context):
    p = _battery_percent(context)
    if p is None:
        return "BATTERY_UNKNOWN"
    if p > 60:
        return "BATTERY_HIGH"
    if p >= 20:
        return "BATTERY_MEDIUM"
    return "BATTERY_LOW"


def _status_age_seconds(context):
    status = (context or {}).get("status") or {}
    value = status.get("time")
    if not value:
        return None
    try:
        updated = datetime.strptime(str(value), "%Y-%m-%d %H:%M:%S")
    except ValueError:
        return None
    return max(0, (datetime.now() - updated).total_seconds())


def _status_is_fresh(context):
    age = _status_age_seconds(context)
    return age is not None and age <= DEVICE_STATUS_FRESH_SECONDS


def _status_voice_code(context):
    status = (context or {}).get("status") or {}
    if not _status_is_fresh(context):
        return "STATUS_OFFLINE"
    if status.get("fallen"):
        return "STATUS_FALL"
    if status.get("monitoring") is False:
        return "STATUS_OFFLINE"
    if status.get("wifi") is True or status.get("wifi_connected") is True:
        return "STATUS_WIFI"
    if status.get("bluetooth") is True or status.get("bluetooth_connected") is True:
        return "STATUS_BLE"
    if status.get("4g") is True or status.get("four_g_active") is True:
        return "STATUS_4G"
    if status.get("time"):
        return "STATUS_RECONNECTING"
    return "STATUS_OFFLINE"


def _context_coordinates(context):
    now_ms = datetime.now().timestamp() * 1000
    candidates = []
    fallback = []

    phone = (context or {}).get("phone_location") or {}
    try:
        updated = float(phone.get("updated_at", 0) or 0)
    except (TypeError, ValueError):
        updated = 0
    if updated > 0 and now_ms - updated <= PHONE_LOCATION_OFFLINE_SECONDS * 1000:
        candidates.append(phone)
    if updated > 0 and now_ms - updated <= LOCATION_WEATHER_FALLBACK_SECONDS * 1000:
        fallback.append(phone)

    g4 = (context or {}).get("g4_location") or {}
    try:
        updated = float(g4.get("updated_at", 0) or 0)
    except (TypeError, ValueError):
        updated = 0
    # 4G GNSS 允许稍旧一些，但不能拿几天前的坐标查天气。
    if updated > 0 and now_ms - updated <= 5 * 60 * 1000:
        candidates.append(g4)
    if updated > 0 and now_ms - updated <= LOCATION_WEATHER_FALLBACK_SECONDS * 1000:
        fallback.append(g4)

    # 方案 B：没有新鲜坐标时，允许使用 6 小时内的最后已知位置，
    # 仍然只使用真实手机/4G 坐标，不使用固定城市。
    if not candidates:
        candidates = fallback

    if not candidates:
        return None

    chosen = max(candidates, key=lambda item: float(item.get("updated_at", 0) or 0))
    try:
        lat = float(chosen.get("latitude"))
        lon = float(chosen.get("longitude"))
    except (TypeError, ValueError):
        return None
    if abs(lat) <= 90 and abs(lon) <= 180 and not (lat == 0 and lon == 0):
        return lat, lon
    return None


def _temperature_band(temp):
    if temp < 10:
        return "COLD"
    if temp < 18:
        return "COOL"
    if temp < 26:
        return "COMFORT"
    return "HOT"


def _weather_kind(code):
    if code in (0, 1):
        return "CLEAR"
    if code in (2, 3, 45, 48):
        return "CLOUDY"
    if code in (71, 73, 75, 77, 85, 86):
        return "SNOW"
    if 51 <= code <= 67 or 80 <= code <= 82 or code >= 95:
        return "RAIN"
    return "CLOUDY"


def _weather_code_refresh(context):
    coords = _context_coordinates(context)
    if not coords:
        return {
            "voice_code": "WEATHER_UNKNOWN",
            "reply": "暂时没有位置，无法查询天气。",
            "weather": None,
        }

    lat, lon = coords
    query = urllib.parse.urlencode({
        "latitude": f"{lat:.4f}",
        "longitude": f"{lon:.4f}",
        "current": "temperature_2m,weather_code",
        "hourly": "precipitation_probability,weather_code",
        "forecast_hours": "6",
        "timezone": "auto",
    })
    url = "https://api.open-meteo.com/v1/forecast?" + query
    try:
        with urllib.request.urlopen(url, timeout=WEATHER_TIMEOUT) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, TimeoutError, ValueError):
        return {
            "voice_code": "WEATHER_UNKNOWN",
            "reply": "暂时查不到天气，请以手机天气为准。",
            "weather": None,
        }

    current = body.get("current") or {}
    temp = current.get("temperature_2m")
    code = current.get("weather_code")
    if not isinstance(temp, (int, float)):
        return {
            "voice_code": "WEATHER_UNKNOWN",
            "reply": "暂时没有实时天气数据。",
            "weather": None,
        }

    kind = _weather_kind(int(code or 0))
    band = _temperature_band(float(temp))

    hourly = body.get("hourly") or {}
    probs = hourly.get("precipitation_probability") or []
    codes = hourly.get("weather_code") or []
    next3_rain = False
    for i in range(1, min(4, len(probs), len(codes))):
        p = probs[i]
        c = codes[i]
        if (isinstance(p, (int, float)) and p >= 40) or (
            isinstance(c, (int, float)) and _weather_kind(int(c)) in ("RAIN", "SNOW")
        ):
            next3_rain = True
            break

    umbrella = kind in ("RAIN", "SNOW") or next3_rain
    if kind == "SNOW":
        voice_code = "WEATHER_SNOW"
        kind_text = "有小雪"
    elif umbrella:
        voice_code = f"WEATHER_{band}_RAIN"
        kind_text = "有雨"
    elif kind == "CLEAR":
        voice_code = f"WEATHER_{band}_CLEAR"
        kind_text = "天气晴"
    else:
        voice_code = f"WEATHER_{band}_CLOUDY"
        kind_text = "天气阴"

    range_text = {
        "COLD": "10℃以下",
        "COOL": "10到18℃",
        "COMFORT": "18到26℃",
        "HOT": "26℃以上",
    }[band]
    umbrella_text = "建议带伞" if umbrella else "不用带伞"
    rain_text = "未来三小时可能有雨" if next3_rain else "未来三小时没有雨"
    reply = f"现在{range_text}，{kind_text}，{umbrella_text}，{rain_text}。"

    return {
        "voice_code": voice_code,
        "reply": reply,
        "weather": {
            "temperature": round(float(temp), 1),
            "band": band,
            "kind": kind,
            "umbrella": umbrella,
            "next3_rain": next3_rain,
        },
    }


def ai_fallback_reply(intent, context=None):
    status = (context or {}).get("status") or {}
    if intent == "STATUS":
        if not _status_is_fresh(context):
            last_time = status.get("time") or "未知"
            return f"设备当前离线，最后数据是 {last_time}，请家属检查设备。"
        if status.get("fallen"):
            return "当前检测到跌倒状态，请先确认老人是否安全，并联系家属。"
        if status.get("monitoring") is False:
            return "设备当前没有在监测，请让家属检查设备。"
        battery = _battery_percent(context)
        battery_text = (
            f"电量约 {battery}%。"
            if battery is not None
            else "电量暂时没有数据。"
        )
        return "设备正在监测中，目前没有跌倒报警。" + battery_text
    if intent == "BATTERY":
        p = _battery_percent(context)
        if p is None:
            return "暂时没有电量数据。"
        if p > 60:
            return "电量充足，不需要充电。"
        if p >= 20:
            return "电量适中，记得留意电量。"
        return "电量不足，请及时充电。"
    if intent == "WEATHER":
        return _weather_code_refresh(context)["reply"]

    replies = {
        "HELLO": "我在，有什么可以帮您？",
        "MEDICATION": "请按家人或医生的安排按时服药，不要自行加量。",
        "HELP": "已收到求助，正在通知家属。",
        "FALL": "检测到跌倒风险，请先确认老人状态并联系家属。",
        "CHAT": "我还没学会回答这个问题，可以换一种说法。",
    }
    return replies.get(intent, replies["CHAT"])


def build_ai_context(device_id):
    """只把服务器上真实存在的数据交给大模型，避免客户端伪造。"""
    with data_lock:
        status = dict(device_states.get(device_id) or {})
        alarms = [
            dict(item)
            for item in alarm_history
            if item.get("device_id") == device_id
        ][-5:]
        phone = phone_locations.get(device_id)
        g4 = g4_locations.get(device_id)
        phone = dict(phone) if phone else None
        g4 = dict(g4) if g4 else None

    return {
        "status": status,
        "recent_alarms": alarms,
        "phone_location": phone,
        "g4_location": g4,
        "sources": [
            "device_status",
            "alarm_history",
            "phone_location",
            "g4_location",
        ],
    }


def ai_voice_code(intent):
    codes = {
        "HELLO": "AI_HELLO",
        "MEDICATION": "AI_MEDICATION",
        "HELP": "AI_HELP",
        "FALL": "AI_FALL",
        "CHAT": "AI_CHAT",
        "STATUS": "STATUS_OFFLINE",
        "BATTERY": "BATTERY_UNKNOWN",
    }
    return codes.get(intent, "AI_CHAT")


def ai_call_llm(text, context=None):
    if not AI_API_KEY:
        return None

    system_prompt = AI_SYSTEM_PROMPT
    if context:
        system_prompt += (
            "\n以下是设备当前数据，回答时可以参考，但不要编造：\n"
            + json.dumps(context, ensure_ascii=False)[:2000]
        )

    payload = {
        "model": AI_MODEL,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": text},
        ],
        "temperature": 0.4,
        "max_tokens": 160,
    }
    req = urllib.request.Request(
        AI_API_URL,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + AI_API_KEY,
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=AI_TIMEOUT) as resp:
            body = json.loads(resp.read().decode("utf-8"))
        choices = body.get("choices") or []
        if choices:
            content = choices[0].get("message", {}).get("content", "")
            content = str(content).strip()
            return content[:300] if content else None
    except (urllib.error.URLError, TimeoutError, ValueError, KeyError):
        return None
    return None


@app.route("/api/ai/chat", methods=["POST"])
def ai_chat():
    data = request.get_json(silent=True) or {}

    intent = str(data.get("intent") or "").strip().upper()
    text = str(data.get("text") or "").strip()
    device_id = str(data.get("device_id") or DEFAULT_DEVICE_ID)

    if not intent:
        if any(k in text for k in ("天气", "下雨", "温度")):
            intent = "WEATHER"
        elif any(k in text for k in ("电量", "电池", "充电")):
            intent = "BATTERY"
        elif any(k in text for k in ("药", "吃药")):
            intent = "MEDICATION"
        elif any(k in text for k in ("救命", "求助", "不舒服")):
            intent = "HELP"
        elif any(k in text for k in ("状态", "怎么样")):
            intent = "STATUS"
        elif text:
            intent = "CHAT"
        else:
            intent = "HELLO"

    context = build_ai_context(device_id)
    voice_code = ai_voice_code(intent)

    if intent == "WEATHER":
        weather = _weather_code_refresh(context)
        reply = weather["reply"]
        voice_code = weather["voice_code"]
        source = "weather"
    elif intent == "STATUS":
        reply = ai_fallback_reply("STATUS", context)
        voice_code = _status_voice_code(context)
        source = "fixed"
    elif intent == "BATTERY":
        reply = ai_fallback_reply("BATTERY", context)
        voice_code = _battery_voice_code(context)
        source = "fixed"
    elif intent in ("HELLO", "MEDICATION", "HELP", "FALL"):
        reply = ai_fallback_reply(intent, context)
        source = "fixed"
    else:
        reply = ai_call_llm(text or intent, context)
        source = "llm" if reply else "fallback"
        if not reply:
            reply = ai_fallback_reply("CHAT", context)

    return jsonify({
        "success": True,
        "device_id": device_id,
        "intent": intent,
        "reply": reply,
        "voice_code": voice_code,
        "source": source,
        "tts": False,
        "data_sources": context.get("sources", []),
        "data_time": (context.get("status") or {}).get("time"),
    })


@app.errorhandler(500)
def internal_server_error(error):

    return jsonify({

        "success": False,

        "message": "Internal server error"

    }), 500


# ============================================================
# 启动服务器
# ============================================================


# ============================================================
#                    云端监控网页
# ============================================================

import os
from flask import send_from_directory

BASE_DIR = os.path.dirname(os.path.abspath(__file__))


@app.route("/dashboard", methods=["GET"])
def dashboard():
    return send_from_directory(BASE_DIR, "dashboard.html")


@app.after_request
def persist_after_mutation(response):
    if request.method in ("POST", "PUT", "PATCH", "DELETE") and request.path.startswith("/api/"):
        try:
            save_cloud_state()
        except Exception as exc:
            print("[持久化] 保存失败:", exc)
    return response


init_state_db()
load_cloud_state()

init_module(app, device_states, alarm_history, data_lock)


if __name__ == "__main__":

    print()
    print("========================================")
    print("       老人跌倒云端服务器")
    print("========================================")
    print("服务器地址：0.0.0.0:5000")
    print()
    print("报警接口：")
    print("POST /api/alarm")
    print("GET  /api/alarm/latest")
    print("GET  /api/alarm/history")
    print()
    print("云端控制：")
    print("POST /api/command")
    print("GET  /api/command/pending")
    print()
    print("设备状态：")
    print("POST /api/device/status")
    print("GET  /api/device/status")
    print()
    print("手机定位：")
    print("POST /api/location")
    print("GET  /api/location")
    print()
    print("测试接口：")
    print("GET /api/test")
    print("========================================")
    print()


    app.run(

        host="0.0.0.0",

        port=5000,

        debug=False,
        threaded=True

    )
