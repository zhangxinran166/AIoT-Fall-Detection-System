import { CLOUD_BASE_URL } from './alarm.js'

// 心跳节流：同一个 App 生命周期内最多每 10 分钟上报一次
let lastSentAt = 0
const MIN_INTERVAL = 10 * 60 * 1000

function readSystemInfo() {
	try {
		return uni.getSystemInfoSync() || {}
	} catch (e) {
		return {}
	}
}

/**
 * 上报一次「App 活着」的心跳。
 * 管理后台的「用户与 APP」页面靠它判断在线 / 离线 / 未安装。
 *
 * @param {boolean} force 为 true 时忽略 10 分钟节流
 */
export function sendHeartbeat(force) {
	const token = uni.getStorageSync('token') || ''
	if (!token) return Promise.resolve()

	const now = Date.now()
	if (!force && now - lastSentAt < MIN_INTERVAL) return Promise.resolve()
	lastSentAt = now

	const sys = readSystemInfo()

	return new Promise((resolve) => {
		uni.request({
			url: CLOUD_BASE_URL + '/api/client/heartbeat',
			method: 'POST',
			header: {
				'Authorization': 'Bearer ' + token,
				'Content-Type': 'application/json'
			},
			data: {
				version: '1.0.0',
				model: sys.model || sys.deviceModel || '',
				platform: sys.platform || sys.system || ''
			},
			timeout: 8000,
			success: resolve,
			fail: resolve
		})
	})
}

export default sendHeartbeat

/**
 * 退出登录前调用：告诉服务器「我下线了」，
 * 管理后台会立刻显示「已退出」，不用等心跳过期。
 */
export function sendOffline() {
	const token = uni.getStorageSync('token') || ''
	if (!token) return Promise.resolve()

	return new Promise((resolve) => {
		uni.request({
			url: CLOUD_BASE_URL + '/api/client/offline',
			method: 'POST',
			header: {
				'Authorization': 'Bearer ' + token,
				'Content-Type': 'application/json'
			},
			data: {},
			timeout: 5000,
			success: resolve,
			fail: resolve
		})
	})
}
