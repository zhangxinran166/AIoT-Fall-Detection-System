# -*- coding: utf-8 -*-
"""
管理后台模块（放在 app.py 同一目录）

功能：
  1. 用户注册 / 登录 / 绑定设备（老人端 elder、家属端 family）
  2. 报警时给绑定的家属生成通知记录，家属 APP 来取、来确认
  3. APP 心跳、错误和闪退上报
  4. 管理后台总览接口 + /admin 页面

管理员密码：写在同目录的 admin_password.txt 里（一行文字即可）
"""

import hmac
import os
import re
import secrets
import sqlite3
import time
from datetime import datetime

from flask import Blueprint, jsonify, request, send_from_directory
from werkzeug.security import check_password_hash, generate_password_hash

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.environ.get("FALL_DB_PATH", os.path.join(BASE_DIR, "fall.db"))
ADMIN_PW_FILE = os.path.join(BASE_DIR, "admin_password.txt")

DEVICE_ONLINE_SECONDS = 90      # 与 /api/device/status 的 90 秒口径一致
APP_ONLINE_SECONDS = 90         # APP 心跳每 30 秒一次，超过这个时间没心跳，算已退出/可能崩溃
NOTIFY_COOLDOWN_SECONDS = 10    # 同一设备、同一事件类型 10 秒内只通知一次

bp = Blueprint("admin_module", __name__)
_ctx = {"device_states": {}, "alarm_history": [], "lock": None}


# ============================================================
# 数据库
# ============================================================

def _conn():
    conn = sqlite3.connect(DB_PATH, timeout=10)
    conn.row_factory = sqlite3.Row
    return conn


def run(sql, args=()):
    conn = _conn()
    try:
        cur = conn.execute(sql, args)
        conn.commit()
        return cur.lastrowid
    finally:
        conn.close()


def run_rc(sql, args=()):
    """执行写操作并返回受影响的行数。"""
    conn = _conn()
    try:
        cur = conn.execute(sql, args)
        conn.commit()
        return cur.rowcount
    finally:
        conn.close()


def rows(sql, args=()):
    conn = _conn()
    try:
        return [dict(r) for r in conn.execute(sql, args).fetchall()]
    finally:
        conn.close()


def init_db():
    conn = _conn()
    try:
        conn.executescript("""
        CREATE TABLE IF NOT EXISTS users(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            pw_hash TEXT NOT NULL,
            role TEXT NOT NULL,
            created INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS bindings(
            user_id INTEGER NOT NULL,
            device_id TEXT NOT NULL,
            PRIMARY KEY(user_id, device_id));
        CREATE TABLE IF NOT EXISTS sessions(
            token TEXT PRIMARY KEY,
            user_id INTEGER NOT NULL,
            created INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS notifications(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT,
            to_user_id INTEGER,
            title TEXT,
            body TEXT,
            status TEXT,
            created INTEGER,
            delivered INTEGER,
            acked INTEGER);
        CREATE TABLE IF NOT EXISTS app_clients(
            user_id INTEGER PRIMARY KEY,
            last_seen INTEGER,
            version TEXT,
            model TEXT,
            platform TEXT);
        CREATE TABLE IF NOT EXISTS app_errors(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT,
            kind TEXT,
            message TEXT,
            page TEXT,
            version TEXT,
            model TEXT,
            created INTEGER);
        CREATE TABLE IF NOT EXISTS reminders(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT,
            from_user_id INTEGER,
            to_user_id INTEGER,
            content TEXT,
            created INTEGER,
            is_read INTEGER DEFAULT 0);
        """)
        cols = [r["name"] for r in conn.execute("PRAGMA table_info(users)").fetchall()]
        if "nickname" not in cols:
            conn.execute("ALTER TABLE users ADD COLUMN nickname TEXT")
        # 找回密码用的密保问题（答案用哈希存储，不存明文）
        if "sec_question" not in cols:
            conn.execute("ALTER TABLE users ADD COLUMN sec_question TEXT")
        if "sec_answer" not in cols:
            conn.execute("ALTER TABLE users ADD COLUMN sec_answer TEXT")
        ncols = [r["name"] for r in conn.execute("PRAGMA table_info(notifications)").fetchall()]
        if "cleared" not in ncols:
            conn.execute("ALTER TABLE notifications ADD COLUMN cleared INTEGER DEFAULT 0")
        conn.commit()
    finally:
        conn.close()


def init_module(app, device_states, alarm_history, lock):
    """在 app.py 里调用一次，把现有的设备状态和报警历史接进来。"""
    _ctx["device_states"] = device_states
    _ctx["alarm_history"] = alarm_history
    _ctx["lock"] = lock
    init_db()
    app.register_blueprint(bp)


# ============================================================
# 工具
# ============================================================

def ts_text(t):
    if not t:
        return None
    return datetime.fromtimestamp(t).strftime("%Y-%m-%d %H:%M:%S")


def age_seconds(time_str):
    try:
        t = datetime.strptime(time_str, "%Y-%m-%d %H:%M:%S")
        return (datetime.now() - t).total_seconds()
    except (TypeError, ValueError):
        return None


def device_summary(state):
    """根据设备最后一次上传的状态，判断 状态 / 当前链路 / 最后上传时间。"""
    age = age_seconds(state.get("time"))
    online = age is not None and age <= DEVICE_ONLINE_SECONDS
    if not state:
        status = "never"
    elif not online:
        status = "offline"
    elif state.get("fallen"):
        status = "fallen"
    else:
        status = "normal"
    if online and state.get("wifi"):
        link = "WiFi"
    elif online and state.get("bluetooth"):
        link = "蓝牙"
    elif online and state.get("4g"):
        link = "4G"
    else:
        link = "—"
    return {"status": status, "link": link, "last_upload": state.get("time")}


def current_user():
    header = request.headers.get("Authorization", "")
    token = header[7:] if header.startswith("Bearer ") else ""
    if not token:
        return None
    r = rows(
        "SELECT u.id, u.username, u.role FROM sessions s "
        "JOIN users u ON u.id = s.user_id WHERE s.token = ?", (token,))
    return r[0] if r else None


def admin_state():
    """返回 True 通过 / False 密码错 / None 服务器还没设置管理员密码"""
    try:
        with open(ADMIN_PW_FILE, encoding="utf-8") as f:
            pw = f.read().strip()
    except OSError:
        return None
    if not pw:
        return None
    given = request.headers.get("X-Admin-Password", "")
    return hmac.compare_digest(pw.encode("utf-8"), given.encode("utf-8"))


def bad(msg, code=400):
    return jsonify({"success": False, "message": msg}), code


def new_session(user_id):
    token = secrets.token_urlsafe(24)
    run("INSERT INTO sessions(token, user_id, created) VALUES(?,?,?)",
        (token, user_id, int(time.time())))
    return token


def user_devices(user_id):
    return [r["device_id"] for r in rows(
        "SELECT device_id FROM bindings WHERE user_id = ?", (user_id,))]


# ============================================================
# 注册 / 登录 / 绑定
# ============================================================

@bp.route("/api/auth/register", methods=["POST"])
def register():
    d = request.get_json(silent=True) or {}
    username = str(d.get("username", "")).strip()
    password = str(d.get("password", ""))
    role = d.get("role")
    device_id = str(d.get("device_id", "")).strip()

    if not re.fullmatch(r"[A-Za-z0-9_]{3,32}", username):
        return bad("账号需为3~32位字母、数字或下划线")
    if len(password) < 6:
        return bad("密码至少6位")
    if role not in ("elder", "family"):
        return bad("role 必须是 elder 或 family")
    if not device_id:
        return bad("请填写设备ID")
    if rows("SELECT 1 FROM users WHERE username = ?", (username,)):
        return bad("账号已存在", 409)

    nickname = str(d.get("nickname", "")).strip()[:16] or username

    # 密保问题：注册时一起设置，忘记密码时用来找回
    sec_question = str(d.get("sec_question", "")).strip()
    sec_answer = str(d.get("sec_answer", "")).strip()
    if bool(sec_question) != bool(sec_answer):
        return bad("密保问题和答案需要一起填写")
    if sec_question:
        if not (2 <= len(sec_question) <= 60):
            return bad("密保问题请填 2~60 个字")
        if len(sec_answer) > 60:
            return bad("密保答案最多 60 个字")

    uid = run("INSERT INTO users(username, pw_hash, role, created, nickname, "
              "sec_question, sec_answer) VALUES(?,?,?,?,?,?,?)",
              (username, generate_password_hash(password), role, int(time.time()), nickname,
               sec_question or None,
               generate_password_hash(norm_answer(sec_answer)) if sec_answer else None))
    run("INSERT OR IGNORE INTO bindings(user_id, device_id) VALUES(?,?)", (uid, device_id))
    return jsonify({"success": True, "token": new_session(uid), "role": role,
                    "devices": [device_id]})


@bp.route("/api/auth/login", methods=["POST"])
def login():
    d = request.get_json(silent=True) or {}
    r = rows("SELECT id, pw_hash, role, nickname FROM users WHERE username = ?",
             (str(d.get("username", "")).strip(),))
    if not r or not check_password_hash(r[0]["pw_hash"], str(d.get("password", ""))):
        return bad("账号或密码错误", 401)
    uid = r[0]["id"]
    return jsonify({"success": True, "token": new_session(uid), "role": r[0]["role"],
                    "nickname": r[0]["nickname"] or str(d.get("username", "")).strip(),
                    "devices": user_devices(uid)})


@bp.route("/api/auth/bind", methods=["POST"])
def bind_device():
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    device_id = str((request.get_json(silent=True) or {}).get("device_id", "")).strip()
    if not device_id:
        return bad("请填写设备ID")
    run("INSERT OR IGNORE INTO bindings(user_id, device_id) VALUES(?,?)",
        (user["id"], device_id))
    return jsonify({"success": True, "devices": user_devices(user["id"])})


@bp.route("/api/auth/profile", methods=["POST"])
def update_profile():
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    nickname = str((request.get_json(silent=True) or {}).get("nickname", "")).strip()[:16]
    if not nickname:
        return bad("昵称不能为空")
    run("UPDATE users SET nickname = ? WHERE id = ?", (nickname, user["id"]))
    return jsonify({"success": True, "nickname": nickname})


@bp.route("/api/auth/unbind", methods=["POST"])
def unbind_device():
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    device_id = str((request.get_json(silent=True) or {}).get("device_id", "")).strip()
    run("DELETE FROM bindings WHERE user_id = ? AND device_id = ?", (user["id"], device_id))
    return jsonify({"success": True, "devices": user_devices(user["id"])})


# ============================================================
# 密保问题 / 忘记密码
#
# 设计：
#   1. 登录状态下 → /api/auth/security/set 设置问题和答案
#   2. 忘记密码时 → /api/auth/security/question 取回问题
#   3. 答对后     → /api/auth/security/reset 重设密码
#
# 答案做归一化（去首尾空格、合并空白、转小写）后哈希存储，
# 不会明文入库。
# ============================================================

# 重置密码的失败次数限制，防止有人拿密保问题暴力猜答案
_reset_attempts = {}
RESET_MAX_TRY = 5
RESET_WINDOW = 600  # 秒


def norm_answer(s):
    """密保答案归一化：合并空白 + 转小写，避免大小写/多敲空格导致对不上。"""
    return " ".join(str(s or "").split()).lower()


def _reset_allowed(username):
    now = time.time()
    lst = [t for t in _reset_attempts.get(username, []) if now - t < RESET_WINDOW]
    _reset_attempts[username] = lst
    return len(lst) < RESET_MAX_TRY


@bp.route("/api/auth/security/set", methods=["POST"])
def set_security_question():
    user = current_user()
    if not user:
        return bad("请先登录", 401)

    d = request.get_json(silent=True) or {}
    question = str(d.get("question", "")).strip()
    answer = str(d.get("answer", "")).strip()

    if not (2 <= len(question) <= 60):
        return bad("密保问题请填 2~60 个字")
    if not (1 <= len(answer) <= 60):
        return bad("密保答案请填 1~60 个字")

    run("UPDATE users SET sec_question = ?, sec_answer = ? WHERE id = ?",
        (question, generate_password_hash(norm_answer(answer)), user["id"]))
    return jsonify({"success": True, "question": question})


@bp.route("/api/auth/security/question", methods=["GET"])
def get_security_question():
    username = (request.args.get("username") or "").strip()
    if not username:
        return bad("请填写账号")

    r = rows("SELECT sec_question FROM users WHERE username = ?", (username,))
    # 账号不存在 / 没设过密保，都统一返回 has_question=false，
    # 不暴露账号是否存在
    if not r or not r[0]["sec_question"]:
        return jsonify({"success": True, "has_question": False})
    return jsonify({"success": True, "has_question": True,
                    "question": r[0]["sec_question"]})


@bp.route("/api/auth/security/reset", methods=["POST"])
def reset_password_by_answer():
    d = request.get_json(silent=True) or {}
    username = str(d.get("username", "")).strip()
    answer = str(d.get("answer", ""))
    new_password = str(d.get("new_password", ""))

    if not username:
        return bad("请填写账号")
    if len(new_password) < 6:
        return bad("新密码至少6位")
    if not _reset_allowed(username):
        return bad("尝试次数过多，请 10 分钟后再试", 429)

    r = rows("SELECT id, sec_answer FROM users WHERE username = ?", (username,))
    if not r or not r[0]["sec_answer"]:
        return bad("该账号未设置密保问题，无法自助找回", 404)

    ok = False
    try:
        ok = check_password_hash(r[0]["sec_answer"], norm_answer(answer))
    except Exception:
        ok = False

    if not ok:
        _reset_attempts.setdefault(username, []).append(time.time())
        return bad("密保答案不正确", 403)

    uid = r[0]["id"]
    run("UPDATE users SET pw_hash = ? WHERE id = ?",
        (generate_password_hash(new_password), uid))
    # 旧会话全部失效，避免别人拿着旧 token 继续用
    run("DELETE FROM sessions WHERE user_id = ?", (uid,))
    _reset_attempts.pop(username, None)
    return jsonify({"success": True, "message": "密码已重置，请用新密码登录"})


@bp.route("/api/family/elders", methods=["GET"])
def family_elders():
    """家属端“选择老人”页面用：返回这个家属绑定的所有老人（按设备）。"""
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    lock = _ctx["lock"]
    with lock:
        states = {k: dict(v) for k, v in _ctx["device_states"].items()}

    result = []
    for did in user_devices(user["id"]):
        elders = rows(
            "SELECT u.username, u.nickname FROM bindings b JOIN users u ON u.id = b.user_id "
            "WHERE b.device_id = ? AND u.role = 'elder'", (did,))
        info = device_summary(states.get(did) or {})
        result.append({
            "device_id": did,
            "elder_name": (elders[0]["nickname"] or elders[0]["username"]) if elders else "未命名老人",
            "elder_username": elders[0]["username"] if elders else None,
            "status": info["status"],
            "link": info["link"],
            "last_upload": info["last_upload"],
            "wearing_ok": (states.get(did) or {}).get("wearing_ok"),
        })
    return jsonify({"success": True, "data": result})


# ============================================================
# 家属通知
# ============================================================

def notify_family(device_id, alarm):
    """在 receive_alarm 里调用：给绑定了该设备的所有家属生成一条通知。"""
    now = int(time.time())
    src = {"wifi": "WiFi", "bluetooth": "蓝牙", "4g": "4G", "help": "语音求助"}.get(
        alarm.get("location_source"), "未知链路")
    is_help = alarm.get("location_source") == "help" or alarm.get("event") == "help"
    if is_help:
        title = "老人语音求助"
        body = "设备 %s 收到老人语音求助（%s，%s），请立即回应。" % (
            device_id, src, alarm.get("time", ""))
    else:
        title = "老人跌倒警报"
        body = "设备 %s 检测到老人跌倒（%s，%s），请尽快确认。" % (
            device_id, src, alarm.get("time", ""))
    # 同一设备、同一事件类型短时去重；跌倒和求助互不阻塞。
    if rows("SELECT 1 FROM notifications WHERE device_id = ? AND title = ? AND created > ?",
            (device_id, title, now - NOTIFY_COOLDOWN_SECONDS)):
        return
    family = rows(
        "SELECT u.id FROM bindings b JOIN users u ON u.id = b.user_id "
        "WHERE b.device_id = ? AND u.role = 'family'", (device_id,))
    for f in family:
        run("INSERT INTO notifications(device_id, to_user_id, title, body, status, created) "
            "VALUES(?,?,?,?,?,?)",
            (device_id, f["id"], title, body, "pending", now))


@bp.route("/api/notifications", methods=["GET"])
def my_notifications():
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    now = int(time.time())
    run("UPDATE notifications SET status='delivered', delivered=? "
        "WHERE to_user_id=? AND status='pending'", (now, user["id"]))

    # limit 不传就返回全部，传了就按传的条数截断
    # （老的 LIMIT 30 是历史遗留，会导致 App 只能看到最近 30 条）
    limit_raw = (request.args.get("limit") or "").strip()
    sql = ("SELECT id, device_id, title, body, status, created FROM notifications "
           "WHERE to_user_id=? AND COALESCE(cleared, 0) = 0 ORDER BY id DESC")
    args = [user["id"]]
    if limit_raw.isdigit() and int(limit_raw) > 0:
        sql += " LIMIT ?"
        args.append(int(limit_raw))

    data = rows(sql, tuple(args))
    for r in data:
        r["created_text"] = ts_text(r["created"])
    return jsonify({"success": True, "count": len(data), "data": data})


@bp.route("/api/notifications/ack", methods=["POST"])
def ack_notification():
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    nid = (request.get_json(silent=True) or {}).get("id")
    run("UPDATE notifications SET status='acked', acked=? WHERE id=? AND to_user_id=?",
        (int(time.time()), nid, user["id"]))
    return jsonify({"success": True})


@bp.route("/api/notifications/ack_all", methods=["POST"])
def ack_all_notifications():
    """一键确认：把当前家属名下（可限定某台设备）所有未确认通知标为已确认。"""
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    device_id = str((request.get_json(silent=True) or {}).get("device_id", "")).strip()
    sql = ("UPDATE notifications SET status='acked', acked=? "
           "WHERE to_user_id=? AND status != 'acked' AND COALESCE(cleared, 0) = 0")
    args = [int(time.time()), user["id"]]
    if device_id:
        sql += " AND device_id=?"
        args.append(device_id)
    return jsonify({"success": True, "count": run_rc(sql, tuple(args))})


@bp.route("/api/notifications/clear", methods=["POST"])
def clear_notifications():
    """清除：只隐藏【已确认】的通知；未确认的不会被清除。管理后台仍保留记录。"""
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    device_id = str((request.get_json(silent=True) or {}).get("device_id", "")).strip()
    sql = ("UPDATE notifications SET cleared=1 "
           "WHERE to_user_id=? AND status='acked' AND COALESCE(cleared, 0) = 0")
    args = [user["id"]]
    if device_id:
        sql += " AND device_id=?"
        args.append(device_id)
    return jsonify({"success": True, "count": run_rc(sql, tuple(args))})


# ============================================================
# APP 心跳 / 错误上报
# ============================================================

@bp.route("/api/client/heartbeat", methods=["POST"])
def heartbeat():
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    d = request.get_json(silent=True) or {}
    run("INSERT INTO app_clients(user_id, last_seen, version, model, platform) VALUES(?,?,?,?,?) "
        "ON CONFLICT(user_id) DO UPDATE SET last_seen=excluded.last_seen, "
        "version=excluded.version, model=excluded.model, platform=excluded.platform",
        (user["id"], int(time.time()), str(d.get("version", ""))[:32],
         str(d.get("model", ""))[:64], str(d.get("platform", ""))[:32]))
    return jsonify({"success": True})


# ============================================================
# 家人提醒：家人端发给老人端（存数据库，跨设备可达）
# ============================================================

@bp.route("/api/remind", methods=["POST"])
def send_remind():
    user = current_user()
    if not user:
        return bad("请先登录", 401)

    d = request.get_json(silent=True) or {}
    device_id = str(d.get("device_id", "")).strip()
    content = str(d.get("content", "")).strip()

    if not content:
        return bad("提醒内容不能为空")
    if len(content) > 200:
        return bad("提醒内容最多 200 字")
    if not device_id:
        return bad("缺少设备号")

    targets = rows(
        "SELECT u.id FROM users u JOIN bindings b ON b.user_id = u.id "
        "WHERE b.device_id = ? AND u.role = 'elder'", (device_id,))
    if not targets:
        return bad("这台设备还没有绑定老人端账号，绑定后才能收到提醒", 404)

    now = int(time.time())
    for t in targets:
        run("INSERT INTO reminders(device_id, from_user_id, to_user_id, content, created, is_read) "
            "VALUES(?,?,?,?,?,0)", (device_id, user["id"], t["id"], content, now))

    return jsonify({"success": True, "sent": len(targets)})


@bp.route("/api/remind", methods=["GET"])
def list_remind():
    user = current_user()
    if not user:
        return bad("请先登录", 401)

    data = rows(
        "SELECT r.id, r.content, r.created, r.is_read, u.username AS from_user "
        "FROM reminders r LEFT JOIN users u ON u.id = r.from_user_id "
        "WHERE r.to_user_id = ? ORDER BY r.id DESC LIMIT 100", (user["id"],))
    for r in data:
        r["time_text"] = ts_text(r["created"])

    # 拉取即视为已读；先把未读数返回给老人端
    unread = sum(1 for r in data if not r.get("is_read"))
    run("UPDATE reminders SET is_read = 1 WHERE to_user_id = ? AND is_read = 0", (user["id"],))

    return jsonify({"success": True, "count": len(data), "unread": unread, "data": data})


@bp.route("/api/client/offline", methods=["POST"])
def client_offline():
    """退出登录时调用：把 last_seen 置 0，后台立刻显示「已退出」。"""
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    run("INSERT INTO app_clients(user_id, last_seen) VALUES(?, 0) "
        "ON CONFLICT(user_id) DO UPDATE SET last_seen = 0", (user["id"],))
    return jsonify({"success": True})


@bp.route("/api/client/error", methods=["POST"])
def client_error():
    user = current_user()
    d = request.get_json(silent=True) or {}
    kind = d.get("kind") if d.get("kind") in ("js_error", "last_run_crash", "other") else "other"
    run("INSERT INTO app_errors(username, kind, message, page, version, model, created) "
        "VALUES(?,?,?,?,?,?,?)",
        (user["username"] if user else "未登录", kind,
         str(d.get("message", ""))[:500], str(d.get("page", ""))[:64],
         str(d.get("version", ""))[:32], str(d.get("model", ""))[:64], int(time.time())))
    return jsonify({"success": True})


# ============================================================
# 管理后台
# ============================================================

@bp.route("/admin", methods=["GET"])
def admin_page():
    return send_from_directory(BASE_DIR, "admin.html")


@bp.route("/api/admin/overview", methods=["GET"])
def admin_overview():
    ok = admin_state()
    if ok is None:
        return bad("服务器还没有设置管理员密码（admin_password.txt）", 503)
    if not ok:
        return bad("管理员密码错误", 401)

    now = int(time.time())
    lock = _ctx["lock"]
    with lock:
        states = {k: dict(v) for k, v in _ctx["device_states"].items()}
        history = [dict(a) for a in _ctx["alarm_history"][:200]]

    bound = rows("SELECT b.device_id, u.role, COUNT(*) AS n FROM bindings b "
                 "JOIN users u ON u.id = b.user_id GROUP BY b.device_id, u.role")
    device_ids = set(states.keys()) | {b["device_id"] for b in bound}

    unacked = {r["device_id"]: r["n"] for r in rows(
        "SELECT device_id, COUNT(*) AS n FROM notifications WHERE status != 'acked' GROUP BY device_id")}

    devices = []
    for did in sorted(device_ids):
        s = states.get(did) or {}
        info = device_summary(s)
        status, link = info["status"], info["link"]
        last_alarm = next((a for a in history if a.get("device_id") == did), None)
        devices.append({
            "device_id": did, "status": status, "link": link,
            "pitch": s.get("pitch"), "roll": s.get("roll"),
            "last_upload": s.get("time"), "monitoring": s.get("monitoring"),
            "waiting_for_4g": bool(s.get("waiting_for_4g")),
            "elder_count": sum(b["n"] for b in bound if b["device_id"] == did and b["role"] == "elder"),
            "family_count": sum(b["n"] for b in bound if b["device_id"] == did and b["role"] == "family"),
            "unacked": unacked.get(did, 0),
            "last_alarm_time": last_alarm.get("time") if last_alarm else None,
        })

    users = rows(
        "SELECT u.id, u.username, u.role, u.created, c.last_seen, c.version, c.model, c.platform "
        "FROM users u LEFT JOIN app_clients c ON c.user_id = u.id ORDER BY u.id DESC")

    # 每个账号收到的通知统计（总数 / 待确认 / 最近一条）
    notif_agg = {}
    for r in rows(
            "SELECT to_user_id AS uid, COUNT(*) AS total, "
            "SUM(CASE WHEN status != 'acked' THEN 1 ELSE 0 END) AS pending, "
            "MAX(created) AS last_created "
            "FROM notifications WHERE to_user_id IS NOT NULL GROUP BY to_user_id"):
        notif_agg[r["uid"]] = r

    # 每个账号最近一次登录时间（取会话表里最新的一条）
    login_agg = {}
    for r in rows("SELECT user_id, MAX(created) AS last_login FROM sessions GROUP BY user_id"):
        login_agg[r["user_id"]] = r["last_login"]

    for u in users:
        u["devices"] = user_devices(u["id"])
        u["created_text"] = ts_text(u["created"])
        u["last_seen_text"] = ts_text(u["last_seen"])
        u["last_login_text"] = ts_text(login_agg.get(u["id"]))
        agg = notif_agg.get(u["id"])
        u["notif_total"] = int(agg["total"]) if agg else 0
        u["notif_pending"] = int(agg["pending"] or 0) if agg else 0
        u["notif_last_text"] = ts_text(agg["last_created"]) if agg else None
        if u["last_seen"] is None:
            u["app_status"] = "never"
        elif u["last_seen"] == 0:
            # 主动退出登录过（心跳被置 0）
            u["app_status"] = "logout"
        elif now - u["last_seen"] <= APP_ONLINE_SECONDS:
            u["app_status"] = "online"
        else:
            u["app_status"] = "lost"

    notes = rows(
        "SELECT n.id, n.device_id, n.status, n.created, n.delivered, n.acked, u.username "
        "FROM notifications n LEFT JOIN users u ON u.id = n.to_user_id ORDER BY n.id DESC LIMIT 30")
    for n in notes:
        n["created_text"] = ts_text(n["created"])
        n["acked_text"] = ts_text(n["acked"])

    errors = rows("SELECT * FROM app_errors ORDER BY id DESC LIMIT 30")
    for e in errors:
        e["created_text"] = ts_text(e["created"])

    # ---- 图表统计（只读查询，不影响任何现有接口）----
    # 最近 24 小时，按整点分桶
    hour_base = (now // 3600) * 3600 - 23 * 3600
    hours = [0] * 24
    for r in rows("SELECT created FROM notifications WHERE created >= ?", (hour_base,)):
        idx = int((r["created"] - hour_base) // 3600)
        if 0 <= idx < 24:
            hours[idx] += 1

    # 最近 7 天，按自然日分桶
    day_base = int(datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp())
    day_base -= 6 * 86400
    days = [0] * 7
    for r in rows("SELECT created FROM notifications WHERE created >= ?", (day_base,)):
        idx = int((r["created"] - day_base) // 86400)
        if 0 <= idx < 7:
            days[idx] += 1
    day_labels = [datetime.fromtimestamp(day_base + i * 86400).strftime("%m-%d") for i in range(7)]

    # 通知状态分布
    st_rows = {r["status"]: r["n"] for r in rows(
        "SELECT status, COUNT(*) AS n FROM notifications GROUP BY status")}
    status_counts = {
        "pending": st_rows.get("pending", 0),
        "delivered": st_rows.get("delivered", 0),
        "acked": st_rows.get("acked", 0),
    }

    return jsonify({
        "success": True,
        "server_time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "devices": devices, "users": users,
        "notifications": notes, "errors": errors,
        "chart": {
            "hours": hours,
            "days": days,
            "day_labels": day_labels,
            "status": status_counts,
            "today": days[6],
            "week": sum(days),
        },
    })


# ============================================================
# 家属通知：按设备 / 家属筛选 + 图表聚合
# ============================================================

@bp.route("/api/admin/notifications", methods=["GET"])
def admin_notifications():
    ok = admin_state()
    if ok is None:
        return bad("服务器还没有设置管理员密码（admin_password.txt）", 503)
    if not ok:
        return bad("管理员密码错误", 401)

    device_id = (request.args.get("device_id") or "").strip()
    username = (request.args.get("username") or "").strip()
    try:
        limit = int(request.args.get("limit") or 200)
    except ValueError:
        limit = 200
    limit = max(1, min(limit, 500))

    where, args = [], []
    if device_id:
        where.append("n.device_id = ?")
        args.append(device_id)
    if username:
        where.append("u.username = ?")
        args.append(username)
    cond = ("WHERE " + " AND ".join(where)) if where else ""
    base = "FROM notifications n LEFT JOIN users u ON u.id = n.to_user_id " + cond

    s = rows("SELECT COUNT(*) AS total,"
             " SUM(CASE WHEN n.status='pending' THEN 1 ELSE 0 END) AS pending,"
             " SUM(CASE WHEN n.status='delivered' THEN 1 ELSE 0 END) AS delivered,"
             " SUM(CASE WHEN n.status='acked' THEN 1 ELSE 0 END) AS acked " + base, tuple(args))[0]
    pending = s["pending"] or 0
    delivered = s["delivered"] or 0
    acked = s["acked"] or 0

    day_base = int(datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp())
    day_base -= 6 * 86400
    days = [0] * 7
    time_cond = " AND n.created >= ?" if cond else "WHERE n.created >= ?"
    for r in rows("SELECT n.created " + base + time_cond, tuple(args + [day_base])):
        idx = int((r["created"] - day_base) // 86400)
        if 0 <= idx < 7:
            days[idx] += 1
    day_labels = [datetime.fromtimestamp(day_base + i * 86400).strftime("%m-%d") for i in range(7)]

    items = []
    for r in rows("SELECT n.id, n.device_id, n.title, n.body, n.status, n.created, n.acked, u.username "
                  + base + " ORDER BY n.id DESC LIMIT ?", tuple(args + [limit])):
        items.append({
            "id": r["id"],
            "device_id": r["device_id"],
            "username": r["username"] or "—",
            "title": r["title"],
            "body": r["body"],
            "status": r["status"],
            "created_text": ts_text(r["created"]),
            "acked_text": ts_text(r["acked"]),
        })

    devs = [r["device_id"] for r in rows(
        "SELECT DISTINCT device_id FROM notifications "
        "WHERE device_id IS NOT NULL AND device_id != '' ORDER BY device_id")]
    usrs = [{"id": r["id"], "username": r["username"], "role": r["role"]}
            for r in rows("SELECT id, username, role FROM users ORDER BY id DESC")]

    return jsonify({
        "success": True,
        "server_time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "filter": {"device_id": device_id, "username": username},
        "devices": devs,
        "users": usrs,
        "summary": {"total": s["total"] or 0, "pending": pending,
                    "delivered": delivered, "acked": acked},
        "chart": {
            "days": days,
            "day_labels": day_labels,
            "status": {"pending": pending, "delivered": delivered, "acked": acked},
        },
        "items": items,
    })


# ============================================================
# 注销账号：删除用户及其绑定 / 通知 / 会话
# ============================================================

@bp.route("/api/auth/delete", methods=["POST"])
def delete_account():
    user = current_user()
    if not user:
        return bad("请先登录", 401)
    uid = user["id"]
    run("DELETE FROM notifications WHERE to_user_id = ?", (uid,))
    run("DELETE FROM bindings WHERE user_id = ?", (uid,))
    run("DELETE FROM app_clients WHERE user_id = ?", (uid,))
    run("DELETE FROM sessions WHERE user_id = ?", (uid,))
    run("DELETE FROM users WHERE id = ?", (uid,))
    return jsonify({"success": True, "message": "账号已注销"})