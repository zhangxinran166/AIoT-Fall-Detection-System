<template>
	<view class="container">
		<!-- 地图区 -->
		<view class="map-wrap">

			<map
				v-if="hasLocation"
				:key="mapKey"
				class="map"
				:latitude="mapLat"
				:longitude="mapLng"
				:scale="16"
				:circles="circles"
				:show-location="false"
			></map>

			<view v-else class="map-empty">
				<view class="map-empty-icon">📍</view>
				<view class="map-empty-text">{{ emptyText }}</view>
				<button
					v-if="!loading"
					size="mini"
					type="primary"
					class="map-empty-btn"
					@click="refresh"
				>
					立即重试
				</button>
			</view>

			<!-- 悬浮信息卡 -->
			<view v-if="hasLocation" class="map-bubble">
				<view class="bubble-pin">📍</view>
				<view class="bubble-body">
					<view class="bubble-name">{{ elderName || '老人' }} 当前位置</view>
					<view v-if="accuracy !== null" class="bubble-sub">约 {{ accuracyText }}</view>
					<view class="bubble-sub">{{ freshText }}</view>
				</view>
			</view>

			<!-- 回到老人位置 -->
			<view v-if="hasLocation" class="map-fab" @click="recenter">◎</view>

			<view class="map-status" :class="statusClass">
				<view class="status-dot"></view>
				<text class="status-text">{{ sourceText }}</text>
			</view>
		</view>

		<!-- 底部信息卡 -->
		<view class="sheet">

			<view class="sheet-handle"></view>

			<view class="info-row">
				<text class="info-label">定位来源</text>
				<text class="info-value">{{ sourceText }}</text>
			</view>

			<view class="info-row">
				<text class="info-label">纬度</text>
				<text class="info-value">{{ hasLocation ? latText : '—' }}</text>
			</view>

			<view class="info-row">
				<text class="info-label">经度</text>
				<text class="info-value">{{ hasLocation ? lngText : '—' }}</text>
			</view>

			<view class="info-row">
				<text class="info-label">定位精度</text>
				<text class="info-value">{{ accuracy !== null ? '约 ' + accuracyText : '—' }}</text>
			</view>

			<view class="info-row">
				<text class="info-label">更新时间</text>
				<text class="info-value">{{ updateText }}</text>
			</view>

			<button class="pill pill-blue" :disabled="loading" @click="refresh">
				{{ loading ? '正在获取位置…' : '刷新位置' }}
			</button>

			<button class="pill pill-outline" :disabled="!hasLocation" @click="openInMap">
				用系统地图打开
			</button>

			<view v-if="tip" class="tip">{{ tip }}</view>
		</view>

		<tab-bar active="location"></tab-bar>
	</view>
</template>

<script>
const CLOUD_BASE_URL = 'http://101.43.88.85:5000'
const DEVICE_ID_FALLBACK = 'ESP32_001'

// 云端位置的坐标系。老人端现在直接上报 GCJ02（并带 coord:'gcj02'），
// 这个默认值只用于没有 coord 字段的旧数据兜底。
// 'wgs84' = 页面会转成高德坐标；'gcj02' = 已经是高德坐标，不再转换。
const UPLOAD_COORD_DEFAULT = 'gcj02'

// 位置超过这个时间就提示“可能已过期”，但仍然显示最后一次位置
const STALE_MS = 180000

// 刚进入页面时快速轮询，超过 1 分钟后放慢
const FAST_POLL_MS = 3000
const SLOW_POLL_MS = 12000
const FAST_POLL_WINDOW = 60000

// WGS84 -> GCJ02（国内地图组件需要火星坐标）
const PI = 3.1415926535897932384626
const A = 6378245.0
const EE = 0.00669342162296594323

function outOfChina(lat, lng) {
	return lng < 72.004 || lng > 137.8347 || lat < 0.8293 || lat > 55.8271
}

function transformLat(x, y) {
	let ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * Math.sqrt(Math.abs(x))
	ret += ((20.0 * Math.sin(6.0 * x * PI) + 20.0 * Math.sin(2.0 * x * PI)) * 2.0) / 3.0
	ret += ((20.0 * Math.sin(y * PI) + 40.0 * Math.sin((y / 3.0) * PI)) * 2.0) / 3.0
	ret += ((160.0 * Math.sin((y / 12.0) * PI) + 320 * Math.sin((y * PI) / 30.0)) * 2.0) / 3.0
	return ret
}

function transformLng(x, y) {
	let ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * Math.sqrt(Math.abs(x))
	ret += ((20.0 * Math.sin(6.0 * x * PI) + 20.0 * Math.sin(2.0 * x * PI)) * 2.0) / 3.0
	ret += ((20.0 * Math.sin(x * PI) + 40.0 * Math.sin((x / 3.0) * PI)) * 2.0) / 3.0
	ret += ((150.0 * Math.sin((x / 12.0) * PI) + 300.0 * Math.sin((x / 30.0) * PI)) * 2.0) / 3.0
	return ret
}

function wgs84ToGcj02(lat, lng) {
	if (outOfChina(lat, lng)) return { lat: lat, lng: lng }

	let dLat = transformLat(lng - 105.0, lat - 35.0)
	let dLng = transformLng(lng - 105.0, lat - 35.0)

	const radLat = (lat / 180.0) * PI
	let magic = Math.sin(radLat)
	magic = 1 - EE * magic * magic
	const sqrtMagic = Math.sqrt(magic)

	dLat = (dLat * 180.0) / (((A * (1 - EE)) / (magic * sqrtMagic)) * PI)
	dLng = (dLng * 180.0) / ((A / sqrtMagic) * Math.cos(radLat) * PI)

	return {
		lat: lat + dLat,
		lng: lng + dLng
	}
}

function formatRelative(ms) {
	if (!ms || ms < 0) return ''
	const sec = Math.floor(ms / 1000)
	if (sec < 5) return '刚刚更新'
	if (sec < 60) return sec + ' 秒前'
	const min = Math.floor(sec / 60)
	if (min < 60) return min + ' 分钟前'
	const hour = Math.floor(min / 60)
	if (hour < 24) return hour + ' 小时前'
	return Math.floor(hour / 24) + ' 天前'
}

import TabBar from '../../components/tab-bar/tab-bar.vue'

export default {
	components: { TabBar },
	data() {
		return {
			cloudBaseUrl: CLOUD_BASE_URL,

			deviceId: '',
			elderName: '',

			// 云端的原始坐标（WGS84）
			rawLat: null,
			rawLng: null,

			// 地图实际使用的坐标（GCJ02）
			mapLat: 0,
			mapLng: 0,
			hasLocation: false,

			accuracy: null,
			updatedAt: 0,
			serverTime: '',
			locationSource: '',
			locationStale: false,

			// 设备三链路状态，用来判断位置到底来自手机还是 4G 芯片
			wifiOnline: false,
			bluetoothOnline: false,
			fourGActive: false,
			hasDeviceStatus: false,

			loading: false,
			tip: '',
			emptyText: '正在向云端获取老人位置…',

			circles: [],
			mapKey: 0,

			nowTick: Date.now(),
			pollStartAt: 0,
			pollTimer: null,
			tickTimer: null,
			fetching: false,
			unloaded: false
		}
	},

	computed: {
		sourceKind() {
			if (!this.hasLocation) return 'unknown'
			if (this.locationSource === '4g') return 'device-4g'
			if (this.locationSource === 'phone') {
				if (this.wifiOnline) return 'phone-wifi'
				if (this.bluetoothOnline) return 'phone-ble'
				return 'phone-last'
			}
			if (!this.hasDeviceStatus) return 'unknown'
			if (this.wifiOnline) return 'phone-wifi'
			if (this.bluetoothOnline) return 'phone-ble'
			if (this.fourGActive) return 'device-4g'
			return 'unknown'
		},

		sourceText() {
			const map = {
				'phone-wifi': '老人手机定位（家庭 WiFi）',
				'phone-ble': '老人手机定位（蓝牙连接）',
				'phone-last': '手机最后位置（4G 暂不可用）',
				'device-4g': '4G 芯片定位（双离线兜底）',
				unknown: '定位来源未知'
			}
			return map[this.sourceKind] || '定位来源未知'
		},

		statusClass() {
			return 'src-' + this.sourceKind
		},

		accuracyText() {
			if (this.accuracy === null || this.accuracy === undefined) return ''
			const n = Number(this.accuracy)
			if (!Number.isFinite(n) || n <= 0) return '未知'
			return Math.round(n) + ' 米'
		},

		latText() {
			return this.rawLat === null ? '—' : Number(this.rawLat).toFixed(6)
		},

		lngText() {
			return this.rawLng === null ? '—' : Number(this.rawLng).toFixed(6)
		},

		updateText() {
			if (!this.hasLocation) return '暂无数据'
			const relative = formatRelative(this.nowTick - this.updatedAt)
			if (this.serverTime && relative) return this.serverTime + '（' + relative + '）'
			return this.serverTime || relative || '时间未知'
		},

		isStale() {
			if (!this.hasLocation || !this.updatedAt) return true
			return this.nowTick - this.updatedAt > STALE_MS
		},

		freshText() {
			if (!this.hasLocation) return '等待设备上报'
			if (!this.updatedAt) return '时间未知'
			if (this.isStale) {
				return '位置可能已过期 · ' + formatRelative(this.nowTick - this.updatedAt)
			}
			return formatRelative(this.nowTick - this.updatedAt)
		}
	},

	onLoad(options) {
		const query = options || {}
		this.deviceId =
			(query.deviceId && decodeURIComponent(query.deviceId)) ||
			uni.getStorageSync('deviceId') ||
			DEVICE_ID_FALLBACK
		this.elderName =
			(query.elderName && decodeURIComponent(query.elderName)) ||
			uni.getStorageSync('elderName') ||
			''
	},

	onShow() {
		this.startPolling()
	},

	onHide() {
		this.stopPolling()
	},

	onUnload() {
		this.unloaded = true
		this.stopPolling()
	},

	methods: {
		startPolling() {
			this.stopPolling()

			this.pollStartAt = Date.now()
			this.nowTick = Date.now()

			this.tickTimer = setInterval(() => {
				this.nowTick = Date.now()
			}, 1000)

			this.fetchAll()
			this.scheduleNext()
		},

		scheduleNext() {
			this.pollTimer = setTimeout(() => {
				if (this.unloaded) return
				this.fetchAll()
				this.scheduleNext()
			}, Date.now() - this.pollStartAt < FAST_POLL_WINDOW ? FAST_POLL_MS : SLOW_POLL_MS)
		},

		stopPolling() {
			if (this.pollTimer) {
				clearTimeout(this.pollTimer)
				this.pollTimer = null
			}
			if (this.tickTimer) {
				clearInterval(this.tickTimer)
				this.tickTimer = null
			}
		},

		requestHeader() {
			const token = uni.getStorageSync('token') || ''
			if (!token) return {}
			return { Authorization: 'Bearer ' + token }
		},

		request(path) {
			return new Promise((resolve) => {
				uni.request({
					url: this.cloudBaseUrl + path,
					method: 'GET',
					header: this.requestHeader(),
					timeout: 8000,
					success: resolve,
					// 不抛出异常，避免定时轮询被一个失败请求打断
					fail: (err) => resolve({ statusCode: 0, err: err })
				})
			})
		},

		async fetchAll() {
			if (this.fetching) return
			this.fetching = true

			try {
				const [locRes, statusRes] = await Promise.all([
					this.request('/api/location?device_id=' + encodeURIComponent(this.deviceId) + '&_ts=' + Date.now()),
					this.request('/api/device/status?device_id=' + encodeURIComponent(this.deviceId) + '&_ts=' + Date.now())
				])

				this.applyDeviceStatus(statusRes)
				this.applyLocation(locRes)
			} finally {
				this.fetching = false
			}
		},

		applyDeviceStatus(res) {
			if (!res || res.statusCode !== 200 || !res.data || res.data.success !== true) {
				return
			}
			const data = res.data.data || {}
			this.wifiOnline = data.wifi === true || data.wifi_connected === true
			this.bluetoothOnline = data.bluetooth === true || data.bluetooth_connected === true
			this.fourGActive = data['4g'] === true || data.four_g_active === true
			this.hasDeviceStatus = true
		},

		applyLocation(res) {
			if (!res || res.statusCode !== 200 || !res.data || res.data.success !== true) {
				this.emptyText = '云端连接失败，请检查网络后重试'
				this.tip = '获取位置失败：请确认手机网络正常。'
				return
			}

			const data = res.data.data || {}
			const lat = Number(data.latitude)
			const lng = Number(data.longitude)

			const valid =
				Number.isFinite(lat) &&
				Number.isFinite(lng) &&
				!(lat === 0 && lng === 0) &&
				lat >= -90 &&
				lat <= 90 &&
				lng >= -180 &&
				lng <= 180

			if (!valid) {
				this.hasLocation = false
				this.emptyText = '暂未收到老人位置'
				this.tip = '还没有可用的定位数据：请确认老人手机 APP 已打开并允许定位；若设备的 WiFi 也断开，15 秒后会自动启用 4G 定位（约 1 分钟内上报）。'
				return
			}

			this.rawLat = lat
			this.rawLng = lng
			this.accuracy = data.accuracy === null || data.accuracy === undefined ? null : Number(data.accuracy)
			this.updatedAt = Number(data.updated_at) || 0
			this.serverTime = data.time || ''
			this.locationSource = String(data.source || data.location_source || '')
			this.locationStale = data.stale === true

			// 手机上报默认按 gcj02 处理；ESP32 的 GNSS 一定是 wgs84，
			// 所以双离线时不套用手机端的默认值，避免 4G 定位偏几百米。
			const isDevice4G = !this.wifiOnline && !this.bluetoothOnline && this.fourGActive
			const coord = String(
				data.coord ||
				data.coord_type ||
				(isDevice4G ? 'wgs84' : UPLOAD_COORD_DEFAULT)
			).toLowerCase()
			const gcj = coord === 'gcj02' ? { lat: lat, lng: lng } : wgs84ToGcj02(lat, lng)
			this.mapLat = gcj.lat
			this.mapLng = gcj.lng
			this.hasLocation = true

			// 用圆圈代替图片标注：
			// 1) 红色小圆点标出老人位置；
			// 2) 蓝色精度圈表示误差范围。
			// 这样不依赖 /static 图片，避免 HBuilderX 在手机上创建 static 目录失败。
			const acc = this.accuracy
			const nextCircles = [{
				latitude: gcj.lat,
				longitude: gcj.lng,
				radius: 10,
				color: '#f56c6c',
				fillColor: '#f56c6ccc',
				strokeWidth: 2
			}]

			if (acc && acc > 0 && acc <= 1000) {
				nextCircles.push({
					latitude: gcj.lat,
					longitude: gcj.lng,
					radius: acc,
					color: '#007aff88',
					fillColor: '#007aff22',
					strokeWidth: 1
				})
			}

			this.circles = nextCircles

			this.emptyText = ''
			this.tip = ''
		},

		// 地图被拖动或缩小后，重新把地图定位到老人位置
		recenter() {
			if (!this.hasLocation) return
			this.mapKey += 1
		},

		async refresh() {
			if (this.loading) return
			this.loading = true
			this.tip = ''

			try {
				await this.fetchAll()
				if (this.hasLocation) {
					uni.showToast({ title: '位置已刷新', icon: 'success' })
				} else {
					uni.showToast({ title: '暂未获取到位置', icon: 'none' })
				}
			} finally {
				this.loading = false
			}
		},

		// 内置地图没配置key时，仍可交给系统/第三方地图打开
		openInMap() {
			if (!this.hasLocation) {
				uni.showToast({ title: '暂无有效位置', icon: 'none' })
				return
			}

			uni.openLocation({
				latitude: this.mapLat,
				longitude: this.mapLng,
				name: this.elderName || '老人位置',
				address: this.sourceText,
				scale: 18,
				fail: () => {
					uni.showToast({ title: '打开地图失败', icon: 'none' })
				}
			})
		}
	}
}
</script>

<style scoped>
.container {
	padding: 30rpx;
	background: #f5f6f8;
	min-height: 100vh;
	box-sizing: border-box;
}

.header {
	margin: 6rpx 0 24rpx;
}

.title {
	font-size: 40rpx;
	font-weight: bold;
}

.sub {
	margin-top: 8rpx;
	font-size: 24rpx;
	color: #888;
}

.status-card {
	display: flex;
	align-items: center;
	justify-content: space-between;
	padding: 22rpx 26rpx;
	border-radius: 14rpx;
	margin-bottom: 22rpx;
	background: #eef4ff;
}

.status-main {
	display: flex;
	align-items: center;
	flex: 1;
}

.status-dot {
	width: 14rpx;
	height: 14rpx;
	border-radius: 50%;
	margin-right: 14rpx;
	background: #999;
}

.status-text {
	font-size: 28rpx;
	font-weight: bold;
	color: #333;
}

.status-time {
	font-size: 23rpx;
	color: #888;
	margin-left: 16rpx;
}

.src-phone-wifi,
.src-phone-ble {
	background: #eaf8ef;
}

.src-phone-last {
	background: #f1f2f5;
}

.src-phone-wifi .status-dot,
.src-phone-ble .status-dot {
	background: #07c160;
}

.src-phone-last .status-dot {
	background: #8a93a3;
}

.src-device-4g {
	background: #fff4e5;
}

.src-device-4g .status-dot {
	background: #ff9500;
}

.src-unknown {
	background: #f1f2f5;
}

.map {
	width: 100%;
	height: 560rpx;
	border-radius: 16rpx;
	margin-bottom: 10rpx;
	overflow: hidden;
}

.map-legend {
	margin: 0 4rpx 22rpx;
	font-size: 23rpx;
	color: #888;
	line-height: 1.6;
}

.map-empty {
	height: 420rpx;
	background: #fff;
	border-radius: 16rpx;
	margin-bottom: 22rpx;
	display: flex;
	flex-direction: column;
	align-items: center;
	justify-content: center;
	padding: 30rpx;
	box-sizing: border-box;
}

.map-empty-icon {
	font-size: 80rpx;
	line-height: 1;
	margin-bottom: 20rpx;
}

.map-empty-text {
	font-size: 28rpx;
	color: #888;
	text-align: center;
	line-height: 1.7;
}

.map-empty-btn {
	margin-top: 26rpx;
}

.info-card {
	background: #fff;
	border-radius: 16rpx;
	padding: 10rpx 26rpx;
	margin-bottom: 20rpx;
}

.info-row {
	display: flex;
	align-items: center;
	justify-content: space-between;
	padding: 22rpx 0;
	border-bottom: 1rpx solid #f2f2f2;
}

.info-row:last-child {
	border-bottom: none;
}

.info-label {
	font-size: 27rpx;
	color: #888;
}

.info-value {
	font-size: 27rpx;
	color: #333;
	font-weight: 500;
	max-width: 70%;
	text-align: right;
	word-break: break-all;
}

.tip {
	font-size: 24rpx;
	color: #e67e22;
	line-height: 1.6;
	margin: 8rpx 4rpx 20rpx;
}

.note {
	margin-top: 24rpx;
	font-size: 23rpx;
	color: #999;
	line-height: 1.7;
}

button {
	margin: 10rpx 0 0;
}

.open-map-btn {
	margin-top: 16rpx;
}
/* ==========================================================
   视觉升级（覆盖上方旧样式，不改动任何逻辑）
   ========================================================== */

.container {
	padding: 40rpx 30rpx 80rpx;
	background: #F4F6FA;
}

.header {
	margin: 0 0 26rpx;
	padding-left: 6rpx;
}

.title {
	font-size: 42rpx;
	font-weight: 700;
	color: #1B2233;
}

.sub {
	margin-top: 10rpx;
	font-size: 24rpx;
	color: #98A0AE;
}

.status-card {
	padding: 24rpx 26rpx;
	border-radius: 18rpx;
	margin-bottom: 24rpx;
	box-shadow: 0 4rpx 16rpx rgba(31, 45, 74, 0.05);
}

.status-text {
	font-size: 27rpx;
	font-weight: 600;
	color: #1B2233;
}

.status-time {
	font-size: 22rpx;
	color: #8A93A3;
}

.map {
	height: 560rpx;
	border-radius: 24rpx;
	margin-bottom: 12rpx;
	box-shadow: 0 8rpx 28rpx rgba(31, 45, 74, 0.08);
}

.map-legend {
	font-size: 22rpx;
	color: #98A0AE;
}

.map-empty {
	background: #FFFFFF;
	border-radius: 24rpx;
}

.map-empty-text {
	color: #98A0AE;
}

.info-card {
	background: #FFFFFF;
	border-radius: 24rpx;
	padding: 6rpx 26rpx;
	margin-bottom: 22rpx;
	box-shadow: 0 6rpx 24rpx rgba(31, 45, 74, 0.05);
}

.info-row {
	padding: 24rpx 0;
	border-bottom: 1rpx solid #F4F6FA;
}

.info-label {
	font-size: 26rpx;
	color: #8A93A3;
}

.info-value {
	font-size: 26rpx;
	color: #1B2233;
	font-weight: 600;
}

.tip {
	font-size: 23rpx;
	color: #E6A23C;
}

.note {
	font-size: 22rpx;
	color: #B4BAC6;
}

button {
	margin: 0 0 22rpx 0;
}
/* ==========================================================
   新版视觉：全屏地图 + 底部信息卡
   ========================================================== */
.container {
	padding: 0 0 190rpx;
	background: #F4F6FA;
}

.map-wrap {
	position: relative;
	width: 100%;
	height: 54vh;
	background: #DFE6F1;
}

.map {
	width: 100%;
	height: 100%;
	border-radius: 0;
	margin: 0;
	box-shadow: none;
}

.map-wrap .map-empty {
	position: absolute;
	top: 0;
	left: 0;
	right: 0;
	bottom: 0;
	height: auto;
	margin: 0;
	border-radius: 0;
	background: #EEF2F8;
	display: flex;
	flex-direction: column;
	align-items: center;
	justify-content: center;
	padding: 40rpx;
	box-sizing: border-box;
}

.map-status {
	position: absolute;
	top: 28rpx;
	left: 28rpx;
	display: flex;
	align-items: center;
	padding: 12rpx 22rpx;
	border-radius: 999rpx;
	background: rgba(255, 255, 255, 0.94);
	box-shadow: 0 6rpx 20rpx rgba(31, 45, 74, 0.12);
}

.map-status .status-dot {
	width: 14rpx;
	height: 14rpx;
	border-radius: 50%;
	margin-right: 12rpx;
	background: #999;
}

.map-status .status-text {
	font-size: 24rpx;
	font-weight: 600;
	color: #1B2233;
}

.map-bubble {
	position: absolute;
	left: 32rpx;
	right: 32rpx;
	bottom: 36rpx;
	display: flex;
	align-items: center;
	padding: 24rpx 28rpx;
	border-radius: 28rpx;
	background: #FFFFFF;
	box-shadow: 0 14rpx 34rpx rgba(31, 45, 74, 0.16);
}

.bubble-pin {
	font-size: 44rpx;
	margin-right: 18rpx;
	line-height: 1;
}

.bubble-body {
	flex: 1;
	min-width: 0;
}

.bubble-name {
	font-size: 32rpx;
	font-weight: 700;
	color: #1B2233;
}

.bubble-sub {
	font-size: 23rpx;
	color: #8A93A3;
	margin-top: 6rpx;
}

.map-fab {
	position: absolute;
	right: 32rpx;
	top: 28rpx;
	width: 76rpx;
	height: 76rpx;
	line-height: 76rpx;
	text-align: center;
	font-size: 38rpx;
	color: #2F6FED;
	border-radius: 50%;
	background: rgba(255, 255, 255, 0.96);
	box-shadow: 0 8rpx 22rpx rgba(31, 45, 74, 0.14);
}

.sheet {
	position: relative;
	margin-top: -40rpx;
	background: #FFFFFF;
	border-radius: 40rpx 40rpx 0 0;
	padding: 20rpx 36rpx 40rpx;
	box-shadow: 0 -12rpx 34rpx rgba(31, 45, 74, 0.10);
}

.sheet-handle {
	width: 72rpx;
	height: 8rpx;
	border-radius: 8rpx;
	background: #E1E6EF;
	margin: 0 auto 22rpx;
}

.info-row {
	display: flex;
	align-items: center;
	justify-content: space-between;
	padding: 24rpx 0;
	border-bottom: 1rpx solid #F2F5FA;
}

.info-row:last-of-type {
	border-bottom: none;
}

.info-label {
	font-size: 27rpx;
	color: #8A93A3;
	flex-shrink: 0;
}

.info-value {
	font-size: 28rpx;
	color: #1B2233;
	font-weight: 600;
	max-width: 70%;
	text-align: right;
	word-break: break-all;
}

.pill {
	height: 96rpx;
	line-height: 96rpx;
	padding: 0;
	margin: 22rpx 0 0;
	border-radius: 999rpx;
	font-size: 31rpx;
	font-weight: 700;
}

.pill::after {
	border: none;
}

.pill-blue {
	background: linear-gradient(135deg, #4A7CFF 0%, #2F6FED 100%);
	color: #FFFFFF;
	box-shadow: 0 12rpx 28rpx rgba(47, 111, 237, 0.28);
}

.pill-outline {
	background: #FFFFFF;
	color: #2F6FED;
	border: 2rpx solid #BFD3FF;
}

.tip {
	font-size: 23rpx;
	color: #E6A23C;
	margin: 20rpx 4rpx 0;
}
</style>