// 告警数据处理工具
// 供「告警记录」列表页与「小时详情」页共用

export const CLOUD_BASE_URL = 'http://101.43.88.85:5000'

export const HOUR_MS = 60 * 60 * 1000

export function pad2(n) {
	return n < 10 ? '0' + n : '' + n
}

// 兼容 'YYYY-MM-DD HH:mm:ss' / ISO / 时间戳，避免各端 Date 解析差异
export function parseTime(value) {
	if (value == null || value === '') return NaN
	if (typeof value === 'number') return value
	if (value instanceof Date) return value.getTime()
	const text = String(value)
	const m = text.match(/(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2})(?::(\d{2}))?/)
	if (m) {
		return new Date(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +(m[6] || 0)).getTime()
	}
	const t = Date.parse(text)
	return isNaN(t) ? NaN : t
}

export function fmtDate(ms) {
	const d = new Date(ms)
	return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' + pad2(d.getDate())
}

export function fmtHm(ms) {
	const d = new Date(ms)
	return pad2(d.getHours()) + ':' + pad2(d.getMinutes())
}

export function fmtHms(ms) {
	const d = new Date(ms)
	return pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds())
}

// 某一小时的起止文案，如 2026-10-29 17:00 – 18:00
export function fmtHourRange(startMs) {
	const start = new Date(startMs)
	const endHour = (start.getHours() + 1) % 24
	return fmtDate(startMs) + ' ' + pad2(start.getHours()) + ':00 – ' + pad2(endHour) + ':00'
}

// 把整点时间戳转成小时桶的 key
export function toHourStart(ms) {
	const d = new Date(ms)
	d.setMinutes(0, 0, 0)
	return d.getTime()
}

// 本周起始（周一 00:00），返回毫秒
export function startOfWeekMs(now) {
	const base = now == null ? Date.now() : parseTime(now)
	const d = new Date(base)
	d.setHours(0, 0, 0, 0)
	const day = d.getDay() // 0=周日
	const offset = day === 0 ? 6 : day - 1
	d.setDate(d.getDate() - offset)
	return d.getTime()
}

// 24 小时分布：下标 0-23 表示当天 0-24 点收到的告警条数
export function buildHourCounts(list) {
	const counts = new Array(24).fill(0)
	if (!Array.isArray(list)) return counts
	list.forEach(item => {
		const t = parseTime(item.created_text)
		if (!isFinite(t)) return
		counts[new Date(t).getHours()] += 1
	})
	return counts
}

// 按整点小时分组：同一小时内的跌倒记录归到一张卡片
export function groupByHour(list, fallbackDeviceId) {
	if (!Array.isArray(list) || list.length === 0) return []
	const buckets = {}
	list.forEach(item => {
		const t = parseTime(item.created_text)
		if (!isFinite(t)) return
		const startMs = toHourStart(t)
		const key = String(startMs)
		if (!buckets[key]) {
			buckets[key] = {
				key: key,
				startMs: startMs,
				endMs: startMs + HOUR_MS,
				items: [],
				ackedCount: 0
			}
		}
		buckets[key].items.push(item)
		if (item.status === 'acked') buckets[key].ackedCount += 1
	})

	return Object.keys(buckets).map(key => {
		const bucket = buckets[key]
		bucket.items.sort((a, b) => parseTime(a.created_text) - parseTime(b.created_text))
		bucket.count = bucket.items.length
		bucket.allAcked = bucket.ackedCount === bucket.count
		bucket.deviceId = fallbackDeviceId ||
			(bucket.items[0] && bucket.items[0].device_id) || ''
		bucket.title = (bucket.items[0] && bucket.items[0].title) || '老人跌倒警报'
		bucket.rangeText = fmtHourRange(bucket.startMs)
		return bucket
	}).sort((a, b) => b.startMs - a.startMs)
}

// 拉取云端告警。
// 不传 limit 表示「有多少拉多少」，服务端会返回该账号的全部记录。
export function fetchNotifications(baseUrl, token, limit) {
	const size = parseInt(limit, 10)
	const query = size > 0 ? '?limit=' + size : ''
	return new Promise((resolve, reject) => {
		uni.request({
			url: baseUrl + '/api/notifications' + query,
			method: 'GET',
			header: {
				'Authorization': 'Bearer ' + token
			},
			timeout: 20000,
			success: resolve,
			fail: reject
		})
	})
}

// 把接口响应统一成 { ok, list, statusCode, message }
export function normalizeNotifications(res) {
	const out = {
		ok: false,
		list: [],
		statusCode: res && res.statusCode,
		message: ''
	}
	if (!res) return out
	const body = res.data || {}
	if (res.statusCode === 200 && body.success) {
		out.ok = true
		out.list = Array.isArray(body.data) ? body.data : []
		return out
	}
	if (res.statusCode === 401) {
		out.message = '登录已失效，请重新登录'
	} else {
		out.message = body.message || '获取告警失败，请刷新重试'
	}
	return out
}
