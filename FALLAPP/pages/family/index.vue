<template>
	<view class="container">
		<!-- 顶部栏 -->
		<view class="app-header">
			<view class="app-header-spacer"></view>
			<view class="app-header-title">家人端</view>
			<view class="app-header-btn" @click="switchElder"></view>
		</view>

		<!-- 老人信息 + 设备状态（合并） -->
		<view class="hero">
			<view class="hero-top">
				<view class="hero-avatar">👴</view>
				<view class="hero-main">
					<view class="hero-name">{{ elderName || '未命名老人' }}</view>
					<view class="hero-sub">设备 {{ deviceId }}</view>
					<view class="hero-tag">{{ statusLabel }}</view>
				</view>
			</view>

			<view class="hero-metrics">
				<view class="metric">
					<text class="metric-k">链路</text>
					<text class="metric-v">{{ realtime.linkModeText }}</text>
				</view>
				<view class="metric">
					<text class="metric-k">佩戴</text>
					<text class="metric-v">{{ realtime.wearingText }}</text>
				</view>
				<view class="metric">
					<text class="metric-k">电量</text>
					<text class="metric-v" :class="{ 'battery-low-text': realtime.batteryLow, 'battery-charging-text': realtime.batteryCharging }">{{ realtime.batteryText }}</text>
				</view>
			</view>

			<view class="hero-battery">
				<view
					class="hero-battery-fill"
					:class="{ low: realtime.batteryLow, charging: realtime.batteryCharging }"
					:style="{ width: (Number(realtime.batteryPercent) || 0) + '%' }"
				></view>
			</view>
			<text v-if="realtime.batteryCharging" class="battery-state charging">正在充电</text>
			<text v-else-if="realtime.batteryLow" class="battery-state low">电量不足，请及时充电</text>
		</view>

		<!-- AI 分析 / 实时监测 -->
		<view class="grid-row">
			<view class="card half">
				<view class="card-head">
					<view class="head-icon"></view>
					<text class="card-title">AI 分析</text>
				</view>

				<view class="gauge" :class="{ 'gauge-offline': !deviceFresh && !realtime.fallen }">
					<view class="gauge-ring"></view>
					<view class="gauge-center">
						<text class="gauge-num" style="font-size: 30rpx;">{{ realtime.fallen ? '跌倒' : realtime.aiText }}</text>
						<text class="gauge-unit">{{ realtime.fallen ? 'AI 证据' : '参考' }}</text>
					</view>
				</view>

				<view class="kv">
					<text class="k">参考</text>
					<text class="v">{{ realtime.fallen ? realtime.eventAiText : realtime.aiText }}</text>
				</view>
				<view class="kv">
					<text class="k">方向</text>
					<text class="v">{{ realtime.fallen ? realtime.eventAiDirText : realtime.aiDirText }}</text>
				</view>
				<view class="kv">
					<text class="k">佩戴方向</text>
					<text class="v" :class="realtime.wearingClass">{{ realtime.wearingText }}</text>
				</view>
				<view class="kv">
					<text class="k">推理</text>
					<text class="v">{{ realtime.aiLatencyText }}</text>
				</view>
				<view class="card-foot">{{ realtime.time || '暂无数据' }}</view>
			</view>

			<view class="card half">
				<view class="card-head">
					<view class="head-icon alt"></view>
					<text class="card-title">{{ monitorTitle }}</text>
				</view>

				<view class="chip" :class="realtime.fallen ? 'chip-danger' : (deviceFresh ? 'chip-ok' : 'chip-offline')">
					{{ realtime.fallen ? '检测到跌倒' : (deviceFresh ? (realtime.monitoring ? '监测中' : '未监测') : realtime.connectionText) }}
				</view>
				<view v-if="!deviceFresh && !realtime.fallen" class="status-hint">{{ realtime.connectionHint }}</view>

				<view class="kv">
					<text class="k">加速度</text>
					<text class="v">{{ realtime.accMag }}</text>
				</view>
				<view class="kv">
					<text class="k">角速度</text>
					<text class="v">{{ realtime.gyroMag }}</text>
				</view>
				<view class="kv" v-if="showDataTime">
					<text class="k">本次上报</text>
					<text class="v">{{ realtime.time || '—' }}</text>
				</view>
				<view class="kv" v-if="realtime.fallen">
					<text class="k">跌倒持续</text>
					<text class="v">{{ realtime.longLieNoTimer ? '阶段性数据' : realtime.longLieSeconds + ' 秒' }}</text>
				</view>
				<view class="kv" v-if="realtime.fallen">
					<text class="k">长躺分级</text>
					<text class="v" :class="realtime.longLieClass">{{ realtime.longLieText }}</text>
				</view>
				<view class="kv" v-if="!realtime.fallen && realtime.sedentaryText">
					<text class="k">活动</text>
					<text class="v">{{ realtime.sedentaryText }}</text>
				</view>
			</view>
		</view>

		<!-- 当前位置 -->
		<view class="loc-card" @click="goLocation">
			<view class="loc-icon">📍</view>
			<view class="loc-body">
				<view class="loc-label">当前位置</view>
				<view class="loc-addr" :class="{ 'loc-none': !loc.ok }">{{ locationText }}</view>
				<view class="loc-src" v-if="loc.ok && linkText">{{ linkText }}</view>

				<view class="mini-map">
					<view class="map-line line-a"></view>
					<view class="map-line line-b"></view>
					<view class="map-line line-c"></view>
					<view class="map-ring" v-if="loc.ok"></view>
					<view class="map-pin" v-if="loc.ok"></view>
					<view class="map-empty-text" v-else>暂无位置</view>
				</view>
			</view>
			<view class="loc-arrow">›</view>
		</view>

		<!-- 底部胶囊按钮 -->
		<view class="action-row">
			<button class="pill pill-green" @click="sendAlert">发送提醒</button>
		</view>

		<tab-bar active="home"></tab-bar>

		<!-- 发送提醒弹窗 -->
		<view
			v-if="showRemindModal"
			class="modal-mask"
			@click="closeRemindModal"
		>
			<view class="modal-box" @click.stop>
				<view class="modal-title">发送提醒给老人</view>
				<view class="quick-list">
					<view class="quick-item" @click="fillText('记得按时吃药')">💊 按时吃药</view>
					<view class="quick-item" @click="fillText('记得多喝温水')">💧 多喝温水</view>
					<view class="quick-item" @click="fillText('记得按时休息')">😴 按时休息</view>
					<view class="quick-item" @click="fillText('下周去医院复诊')">🏥 医院复诊</view>
				</view>
				<textarea
					class="remind-input"
					v-model="remindContent"
					placeholder="或者手动输入提醒内容"
				></textarea>
				<view class="btn-row">
					<button class="btn-cancel" @click="closeRemindModal">取消</button>
					<button class="btn-confirm" @click="submitRemind">发送</button>
				</view>
			</view>
		</view>
	</view>
</template>

<script>
// 高德「Web服务」Key —— 把经纬度转成「市 / 区 / 街道」，留空则显示经纬度
const AMAP_KEY = '0aa5d7e746a27aebcfc54f1786c918f7'

import TabBar from '../../components/tab-bar/tab-bar.vue'

export default {
	components: { TabBar },
	data() {
		return {
			cloudBaseUrl: 'http://101.43.88.85:5000',
			deviceId: '',
			elderName: '',
			deviceStatus: '',
			realtime: {
				pitch: '—',
				roll: '—',
				fallen: false,
				monitoring: false,
				time: '',
				longLieSeconds: 0,
				longLieText: '无',
				longLieClass: 'chip-ok',
				sedentaryText: '',
				batteryText: '未检测',
				batteryPercent: 0,
				batteryVoltageText: '—',
				batteryLow: false,
				batteryClass: '',
				batteryCharging: false,
				lastBatteryVoltage: null,
				batteryChargingUntil: 0,
				batteryRiseCount: 0,
				lastBatteryPercent: null,
				batteryMissCount: 0,
				aiLatencyText: '—',
				accMag: '—',
				gyroMag: '—',
				aiText: '—',
				aiDirText: '—',
				wearingText: '未检测',
				wearingClass: '',
				connectionText: '在线监测中',
				connectionHint: '',
				linkModeText: '正在获取链路',
				longLieNoTimer: false,
				eventAiText: '—',
				eventAiDirText: '—'
			},
			alarm: {
				latitude: null,
				longitude: null,
				location_source: null
			},
			loc: {
				ok: false,
				lat: null,
				lng: null,
				time: '',
				address: ''
			},
			link: {
				wifi: false,
				ble: false,
				g4: false
			},
			lastStatusTime: '',
			lastStatusChangedAt: 0,
			statusInitialized: false,
			fallHold: {
				active: false,
				startedAt: 0,
				time: '',
				source: ''
			},
			fallTickTimer: null,
			lastRecoveredFallTime: '',
			statusTick: Date.now(),
			lastGeoKey: '',
			showRemindModal: false,
			remindContent: '',
			remindSending: false,
			helpDialogOpen: false,
			fallEventOpen: false,
			fallCheckInFlight: false,
			helpCheckInFlight: false,
			lastFallPopupAt: 0,
			sessionStartTime: '',
			helpAckedKeys: uni.getStorageSync('helpAckedKeys') || [],
			helpVibrateTimer: null,
			timer: null
		}
	},
	computed: {
		deviceFresh() {
			const tick = this.statusTick
			return this.lastStatusChangedAt > 0 && (tick - this.lastStatusChangedAt) <= 35000
		},
		monitorTitle() {
			if (this.fallHold.active && String(this.fallHold.source).toLowerCase() === '4g') {
				return '间断监测'
			}
			if (!this.deviceFresh && this.link.g4) return '间断监测'
			return '实时监测'
		},
		showDataTime() {
			return this.link.g4 || String(this.fallHold.source).toLowerCase() === '4g'
		},
		statusLabel() {
			if (this.deviceFresh) {
				return this.realtime.fallen ? '在线 · 检测到跌倒' : '在线 · 正常'
			}
			if (this.realtime.fallen) return '检测到跌倒！'
			if (this.link.g4) return '离线监测中'
			if (this.link.wifi || this.link.ble) return '连接异常 · 暂无数据'
			return '设备离线'
		},
		statusClass() {
			if (this.realtime.fallen) return 'danger'
			if (this.deviceFresh) return 'online'
			return 'offline'
		},
		realtimeText() {
			if (!this.realtime.time) return '暂无设备状态'
			if (this.realtime.fallen) return '⚠️ 检测到跌倒'
			if (this.realtime.monitoring) return '✓ 正常监测中'
			return '○ 监测已停止'
		},
		realtimeClass() {
			if (!this.realtime.time) return 'offline'
			if (this.realtime.fallen) return 'danger'
			return this.realtime.monitoring ? 'online' : 'offline'
		},
		locationText() {
			if (!this.loc.ok) return '暂无位置'
			if (this.loc.address) return this.loc.address
			return this.loc.lat.toFixed(5) + ', ' + this.loc.lng.toFixed(5)
		},
		linkText() {
			if (this.link.wifi) return '手机定位 · WiFi'
			if (this.link.ble) return '手机定位 · 蓝牙'
			if (this.link.g4) return '设备定位'
			return ''
		}
	},
	onLoad() {
		this.sessionStartTime = this.nowString()
	},
	onShow() {
		if (!uni.getStorageSync('token')) {
			uni.reLaunch({ url: '/pages/login/login' })
			return
		}
		let savedDeviceId = uni.getStorageSync('deviceId')
		if (!savedDeviceId) {
			// 登录后直接进首页：自动使用第一个已绑定的老人
			const devices = uni.getStorageSync('devices')
			const first = Array.isArray(devices) ? devices[0] : ''
			if (!first) {
				uni.reLaunch({ url: '/pages/family/elders' })
				return
			}
			savedDeviceId = first
			uni.setStorageSync('deviceId', first)
		}
		this.deviceId = savedDeviceId
		this.elderName = uni.getStorageSync('elderName') || ''
		this.refreshAll()
		this.stopTimer()
		this.timer = setInterval(() => {
			this.loadDeviceStatus()
			this.loadRealtime()
			this.loadLocation()
			// 求助/跌倒轮询不能依赖设备状态请求成功；
			// 4G/离线时设备状态可能失败，但云端报警历史仍然可取。
			this.checkHelpEvents()
			this.checkFallEvents()
		}, 5000)
		this.startFallTicker()
	},
	onHide() {
		this.stopTimer()
	},
	onUnload() {
		this.stopTimer()
	},
	onPullDownRefresh() {
		Promise.all([
			this.loadDeviceStatus(),
			this.loadRealtime(),
			this.loadLatestAlarm(),
			this.loadLocation()
		]).finally(() => {
			uni.stopPullDownRefresh()
		})
	},
	methods: {
		stopTimer() {
			if (this.timer) {
				clearInterval(this.timer)
				this.timer = null
			}
			this.stopFallTicker()
		},
		startFallTicker() {
			this.stopFallTicker()
			this.fallTickTimer = setInterval(() => {
				if (this.fallHold.active) this.updateFallElapsed()
			}, 1000)
		},
		stopFallTicker() {
			if (this.fallTickTimer) {
				clearInterval(this.fallTickTimer)
				this.fallTickTimer = null
			}
		},
		updateFallElapsed() {
			if (!this.fallHold.active || !this.fallHold.startedAt) return
			const seconds = Math.max(0, Math.floor((Date.now() - this.fallHold.startedAt) / 1000))
			if (String(this.fallHold.source).toLowerCase() === '4g') {
				// 4G只有阶段性上报，不能像 WiFi/BLE 一样实时计秒。
				this.realtime.longLieNoTimer = true
				this.realtime.longLieSeconds = 0
				this.realtime.longLieText = seconds >= 60 ? '长躺（阶段性）' : '跌倒（应急报警）'
				this.realtime.longLieClass = seconds >= 60 ? 'chip-danger' : 'chip-warn'
				return
			}
			this.realtime.longLieNoTimer = false
			this.realtime.longLieSeconds = seconds
			const level = seconds >= 300 ? 3 : (seconds >= 60 ? 2 : (seconds >= 30 ? 1 : 0))
			this.realtime.longLieText = ['无', '持续倒地', '长躺警告', '长躺严重'][level]
			this.realtime.longLieClass = level >= 2 ? 'chip-danger' : (level >= 1 ? 'chip-warn' : 'chip-ok')
		},
		refreshAll() {
			return Promise.all([
				this.loadDeviceStatus(),
				this.loadRealtime(),
				this.loadLatestAlarm(),
				this.loadLocation()
			])
		},
		getToken() {
			return uni.getStorageSync('token') || ''
		},
		async loadDeviceStatus() {
			const token = this.getToken()
			if (!token) return
			try {
				const res = await new Promise((resolve, reject) => {
					uni.request({
						url: this.cloudBaseUrl + '/api/family/elders',
						method: 'GET',
						header: { 'Authorization': 'Bearer ' + token },
						timeout: 10000,
						success: resolve,
						fail: reject
					})
				})
				if (res.statusCode === 200 && res.data && res.data.success === true) {
					const list = Array.isArray(res.data.data) ? res.data.data : []
					const me = list.find(x => x.device_id === this.deviceId)
					if (me) {
						this.deviceStatus = me.status || ''
						if (me.elder_name) {
							this.elderName = me.elder_name
							uni.setStorageSync('elderName', me.elder_name)
						}
					} else {
						this.deviceStatus = ''
					}
				}
			} catch (err) {
				console.error('[家属端]设备状态请求异常：', err)
			}
		},
		loadRealtime() {
			if (!this.deviceId) return Promise.resolve()
			this.statusTick = Date.now()
			return new Promise(resolve => {
				uni.request({
					url: this.cloudBaseUrl + '/api/device/status?device_id=' + encodeURIComponent(this.deviceId) + '&_ts=' + Date.now(),
					method: 'GET',
					header: { 'Cache-Control': 'no-cache' },
					timeout: 5000,
					success: (res) => {
						if (res.statusCode < 200 || res.statusCode >= 300) return resolve()
						if (!res.data || res.data.success !== true || !res.data.data) return resolve()
						const d = res.data.data
						const nowFallen = Boolean(d.fallen)
						this.realtime.pitch = d.pitch == null ? '—' : Number(d.pitch).toFixed(2)
						this.realtime.roll = d.roll == null ? '—' : Number(d.roll).toFixed(2)
						if (nowFallen) {
							if (!this.fallHold.active) {
								this.fallHold.active = true
								this.fallHold.startedAt = Date.now()
								this.fallHold.time = d.time || this.nowString()
								this.fallHold.source = d.location_source || (d['4g'] ? '4g' : 'wifi')
							}
							this.realtime.fallen = true
							this.updateEventAi(d.event_ai_prob, d.event_ai_dir)
						} else if (this.fallHold.active) {
							// 只有比本次跌倒更新的状态，才允许解除跌倒保持。
							// 4G 的旧状态/离线状态时间更早，不能把跌倒页面冲掉。
							const statusTime = d.time || ''
							const fallTime = this.fallHold.time || ''
							if (!fallTime || !statusTime || statusTime >= fallTime) {
								this.fallHold.active = false
								this.realtime.fallen = false
								this.realtime.longLieSeconds = 0
								this.realtime.longLieText = '无'
								this.realtime.longLieNoTimer = false
								this.realtime.eventAiText = '—'
								this.realtime.eventAiDirText = '—'
								this.lastRecoveredFallTime = statusTime || this.nowString()
							}
						} else if (!this.fallHold.active) {
							this.realtime.fallen = false
						}
						this.realtime.monitoring = Boolean(d.monitoring)

						const lieSec = Number(d.long_lie_seconds)
						const lieLevel = Number(d.long_lie_level)
						this.realtime.longLieSeconds = Number.isFinite(lieSec) ? Math.max(0, Math.round(lieSec)) : 0
						this.realtime.longLieText = ['无', '持续倒地', '长躺警告', '长躺严重'][Number.isFinite(lieLevel) ? Math.max(0, Math.min(3, lieLevel)) : 0]
						this.realtime.longLieClass = lieLevel >= 2 ? 'chip-danger' : (lieLevel >= 1 ? 'chip-warn' : 'chip-ok')
						if (this.fallHold.active) this.updateFallElapsed()

						const sedentarySec = Number(d.sedentary_seconds)
						this.realtime.sedentaryText = Number.isFinite(sedentarySec) && sedentarySec >= 300
							? `静止/久坐 ${Math.floor(sedentarySec / 60)} 分钟`
							: ''

						const batteryAvail = d.battery_available === true
						const batteryPercent = Number(d.battery_percent)
						if (batteryAvail && Number.isFinite(batteryPercent)) {
							this.realtime.batteryMissCount = 0
							this.realtime.batteryText = batteryPercent + '%'
							this.realtime.batteryPercent = Math.max(0, Math.min(100, batteryPercent))
							const bv = Number(d.battery_voltage)
							this.realtime.batteryVoltageText = Number.isFinite(bv) ? bv.toFixed(2) + ' V' : '—'

							let charging = d.battery_charging === true || d.charging === true
							const lastPercent = this.realtime.lastBatteryPercent
							if (!charging && Number.isFinite(batteryPercent) && lastPercent !== null && batteryPercent > lastPercent) {
								// 充电时电压可能阶跃上升后长时间持平，SOC 上升同样作为充电证据。
								this.realtime.batteryChargingUntil = Date.now() + 30 * 60 * 1000
							}
							this.realtime.lastBatteryPercent = batteryPercent
							if (!charging && Number.isFinite(bv)) {
								const lastVoltage = this.realtime.lastBatteryVoltage
								if (lastVoltage !== null) {
									const delta = bv - lastVoltage
									if (delta >= 0.02) {
										this.realtime.batteryRiseCount += 1
										if (this.realtime.batteryRiseCount >= 2) {
											this.realtime.batteryChargingUntil = Date.now() + 30 * 60 * 1000
										}
									} else if (delta <= -0.03) {
										this.realtime.batteryChargingUntil = 0
										this.realtime.batteryRiseCount = 0
									} else {
										this.realtime.batteryRiseCount = 0
									}
								}
								this.realtime.lastBatteryVoltage = bv
								charging = Date.now() < this.realtime.batteryChargingUntil
							}
							this.realtime.batteryCharging = charging
							this.realtime.batteryLow = !charging && (d.battery_low === true || batteryPercent <= 20)
							this.realtime.batteryClass = this.realtime.batteryLow ? 'battery-low' : ''
						} else {
							// MAX17048 偶发一次读取失败时保留上次值，连续 3 次失败才显示未检测。
							this.realtime.batteryMissCount += 1
							if (this.realtime.batteryMissCount >= 3) {
								this.realtime.batteryText = '未检测'
								this.realtime.batteryPercent = 0
								this.realtime.batteryVoltageText = '—'
								this.realtime.batteryLow = false
								this.realtime.batteryCharging = false
								this.realtime.lastBatteryVoltage = null
								this.realtime.lastBatteryPercent = null
								this.realtime.batteryRiseCount = 0
								this.realtime.batteryClass = ''
							}
						}

						const acc = Number(d.acc_mag)
						const gyro = Number(d.gyro_mag)
						this.realtime.accMag = Number.isFinite(acc) ? acc.toFixed(2) + ' g' : '—'
						this.realtime.gyroMag = Number.isFinite(gyro) ? gyro.toFixed(2) + ' dps' : '—'
						const aiP = Number(d.ai_prob)
						const aiLatency = Number(d.ai_latency_ms)
						this.realtime.aiLatencyText = Number.isFinite(aiLatency) && aiLatency > 0
							? aiLatency.toFixed(1) + ' ms'
							: '—'
						this.realtime.aiText = Number.isFinite(aiP)
							? (aiP <= 0.05 ? '待触发' : (aiP <= 1 ? aiP * 100 : aiP).toFixed(0) + '%')
							: '—'
						const aiD = Number(d.ai_dir)
						this.realtime.aiDirText = (Number.isFinite(aiP) && aiP >= 0.30 && Number.isFinite(aiD))
							? ['前倒倾向', '后倒倾向', '侧倒倾向', '未知'][Math.max(0, Math.min(3, aiD))]
							: '未判定'
						const wearing = d.wearing_ok
						if (wearing === true || wearing === 1 || wearing === 'normal') {
							this.realtime.wearingText = '正常'
							this.realtime.wearingClass = ''
						} else if (wearing === false || wearing === 0 || wearing === 'abnormal') {
							this.realtime.wearingText = '疑似戴反，请检查'
							this.realtime.wearingClass = 'battery-low'
						} else {
							this.realtime.wearingText = '未检测'
							this.realtime.wearingClass = ''
						}

						if (wearing === false || wearing === 0 || wearing === 'abnormal') {
							this.realtime.aiText = '—'
							this.realtime.aiDirText = '疑似戴反，AI暂停'
						}

						this.statusInitialized = true

						const t = d.time || ''
						this.realtime.time = t
						this.link.wifi = d.wifi === true || d.wifi_connected === true
						this.link.ble = d.bluetooth === true || d.bluetooth_connected === true
						this.link.g4 = d['4g'] === true || d.four_g_active === true
						if (t && t !== this.lastStatusTime) {
							this.lastStatusTime = t
							this.lastStatusChangedAt = Date.now()
						}
						this.updateConnectionState()
						resolve()
					},
					fail: () => {
						this.lastStatusChangedAt = 0
						this.updateConnectionState()
						resolve()
					}
				})
			})
		},

		updateConnectionState() {
			const ageSec = this.lastStatusChangedAt > 0
				? (Date.now() - this.lastStatusChangedAt) / 1000
				: 9999
			if (this.deviceFresh) {
				this.realtime.connectionText = '在线监测中'
				this.realtime.connectionHint = ''
				this.realtime.linkModeText = this.link.g4
					? '应急传输'
					: (this.link.wifi ? 'WiFi 实时传输' : (this.link.ble ? '蓝牙传输' : '在线传输'))
				return
			}
			if (this.realtime.fallen) {
				this.realtime.connectionText = '跌倒报警'
				this.realtime.connectionHint = '报警来自阶段性数据，暂无实时数据'
				this.realtime.linkModeText = String(this.fallHold.source).toLowerCase() === '4g'
					? '应急模式'
					: '跌倒报警链路'
				return
			}
			if (this.link.g4 && ageSec <= 120) {
				this.realtime.connectionText = '应急监测中'
				this.realtime.connectionHint = '阶段性上报，暂无实时数据'
				this.realtime.linkModeText = '应急模式'
			} else if ((this.link.wifi || this.link.ble) && ageSec <= 120) {
				this.realtime.connectionText = '连接异常'
				this.realtime.connectionHint = '出了点小问题，正在尝试连接'
				this.realtime.linkModeText = this.link.wifi ? 'WiFi 连接中' : '蓝牙连接中'
			} else if (ageSec > 300 && !this.link.wifi && !this.link.ble && !this.link.g4) {
				this.realtime.connectionText = '设备离线'
				this.realtime.connectionHint = '长时间无数据，可能断电'
				this.realtime.linkModeText = '设备离线'
			} else {
				this.realtime.connectionText = '设备离线'
				this.realtime.connectionHint = '正在尝试重新连接'
				this.realtime.linkModeText = '无可用链路'
			}
		},

		updateEventAi(prob, dir) {
			const p = Number(prob)
			if (!Number.isFinite(p)) {
				this.realtime.eventAiText = '—'
				this.realtime.eventAiDirText = '—'
				return
			}
			const level = p >= 0.70 ? '高置信' : (p >= 0.30 ? '中置信' : '低置信')
			this.realtime.eventAiText = `${level} · ${(p * 100).toFixed(0)}%`
			this.realtime.eventAiDirText = ['前倒', '后倒', '侧倒', '未知'][Number.isFinite(Number(dir)) ? Math.max(0, Math.min(3, Number(dir))) : 3]
		},

		formatEventAi(prob, dir) {
			const p = Number(prob)
			if (!Number.isFinite(p)) return '—'
			const level = p >= 0.70 ? '高置信' : (p >= 0.30 ? '中置信' : '低置信')
			const dirText = ['前倒', '后倒', '侧倒', '未知'][Number.isFinite(Number(dir)) ? Math.max(0, Math.min(3, Number(dir))) : 3]
			return `${level} · ${(p * 100).toFixed(0)}% · ${dirText}`
		},

		nowString() {
			const d = new Date()
			const p = (n) => (n < 10 ? '0' : '') + n
			return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds())
		},

		isRecentAlarmTime(time) {
			const parsed = Date.parse(String(time || '').replace(/-/g, '/'))
			if (!Number.isFinite(parsed)) return false
			const age = Date.now() - parsed
			return age >= 0 && age <= 10 * 60 * 1000
		},

		// ASR 语音求助：轮询报警历史里的 location_source=help
		isHelpAcked(item) {
			const key = String(item && (item.id || item.time) || '')
			return key && this.helpAckedKeys.indexOf(key) >= 0
		},

		markHelpAcked(item) {
			const key = String(item && (item.id || item.time) || '')
			if (!key || this.helpAckedKeys.indexOf(key) >= 0) return
			this.helpAckedKeys.push(key)
			if (this.helpAckedKeys.length > 100) this.helpAckedKeys.shift()
			uni.setStorageSync('helpAckedKeys', this.helpAckedKeys)
		},

		startHelpVibration() {
			this.stopHelpVibration()
			const vibrate = () => {
				if (uni.vibrateLong) uni.vibrateLong()
			}
			vibrate()
			this.helpVibrateTimer = setInterval(vibrate, 2500)
		},

		stopHelpVibration() {
			if (this.helpVibrateTimer) {
				clearInterval(this.helpVibrateTimer)
				this.helpVibrateTimer = null
			}
		},

		checkHelpEvents() {
			if (this.helpDialogOpen || this.helpCheckInFlight) return
			this.helpCheckInFlight = true
			uni.request({
				url: this.cloudBaseUrl + '/api/alarm/history?device_id=' + encodeURIComponent(this.deviceId),
				method: 'GET',
				timeout: 6000,
				success: (res) => {
					const list = res && res.data && res.data.success && Array.isArray(res.data.data) ? res.data.data : []
					const helps = list.filter(a => a && (a.location_source === 'help' || a.event === 'help') && a.time && !this.isHelpAcked(a) && (a.time >= this.sessionStartTime || this.isRecentAlarmTime(a.time)))
					if (helps.length) uni.setStorageSync('alarmUnread', 1)
					if (!helps.length) return
					let newest = helps[0]
					helps.forEach(a => { if (a.time > newest.time) newest = a })
					if (this.helpDialogOpen) return
					this.helpDialogOpen = true
					this.startHelpVibration()
					uni.showModal({
						title: '老人需要帮助',
						content: '请立即回应老人！\n时间：' + newest.time,
						showCancel: false,
						confirmText: '我已知晓',
						success: () => {
							this.stopHelpVibration()
							helps.forEach(a => this.markHelpAcked(a))
							this.helpDialogOpen = false
							uni.request({
								url: this.cloudBaseUrl + '/api/alarm/ack',
								method: 'POST',
								header: { 'Content-Type': 'application/json' },
								data: { device_id: this.deviceId }
							})
						},
						fail: () => { this.stopHelpVibration(); this.helpDialogOpen = false }
					})
				},
				complete: () => { this.helpCheckInFlight = false },
				fail: () => {}
			})
		},

		checkFallEvents() {
			if (this.fallEventOpen || this.fallCheckInFlight) return
			if (Date.now() - this.lastFallPopupAt < 60000) return
			this.fallCheckInFlight = true
			uni.request({
				url: this.cloudBaseUrl + '/api/alarm/history?device_id=' + encodeURIComponent(this.deviceId),
				method: 'GET',
				timeout: 6000,
				success: (res) => {
					const list = res && res.data && res.data.success && Array.isArray(res.data.data) ? res.data.data : []
					const falls = list.filter(a => a && a.fallen && a.time && a.acked !== true && a.location_source !== 'help' && a.event !== 'help' && (a.time >= this.sessionStartTime || this.isRecentAlarmTime(a.time)) && a.time > (this.lastRecoveredFallTime || ''))
					if (falls.length) uni.setStorageSync('alarmUnread', 1)
					if (!falls.length) return
					let newest = falls[0]
					falls.forEach(a => { if (a.time > newest.time) newest = a })
					this.realtime.fallen = true
					this.realtime.time = newest.time
					this.updateEventAi(newest.event_ai_prob, newest.event_ai_dir)
					if (!this.fallHold.active) {
						this.fallHold.active = true
						const parsed = Date.parse(String(newest.time || '').replace(/-/g, '/'))
						this.fallHold.startedAt = Number.isFinite(parsed) ? parsed : Date.now()
						this.fallHold.time = newest.time || this.nowString()
						this.fallHold.source = newest.location_source || ''
					}
					this.updateFallElapsed()
					this.updateConnectionState()
					this.fallEventOpen = true
					this.lastFallPopupAt = Date.now()
					if (uni.vibrateLong) uni.vibrateLong()
					uni.showModal({
						title: '收到跌倒报警',
						content: '时间：' + newest.time + '\n链路：' + (newest.location_source || '未知') +
							'\nAI证据：' + this.formatEventAi(newest.event_ai_prob, newest.event_ai_dir) +
							'\n请立即查看老人状态！',
						showCancel: false,
						confirmText: '我已知晓',
						success: () => {
							uni.request({
								url: this.cloudBaseUrl + '/api/alarm/ack',
								method: 'POST',
								header: { 'Content-Type': 'application/json' },
								data: { device_id: this.deviceId, all_fallen: true },
								complete: () => { this.fallEventOpen = false }
							})
						},
						fail: () => { this.fallEventOpen = false }
					})
				},
				complete: () => { this.fallCheckInFlight = false },
				fail: () => {}
			})
		},

		loadLatestAlarm() {
			if (!this.deviceId) return Promise.resolve()
			return new Promise(resolve => {
				uni.request({
					url: this.cloudBaseUrl + '/api/alarm/latest?device_id=' + encodeURIComponent(this.deviceId) + '&_ts=' + Date.now(),
					method: 'GET',
					header: { 'Cache-Control': 'no-cache' },
					timeout: 5000,
					success: (res) => {
						if (!res.data || res.data.success !== true || !res.data.data) return resolve()
						const d = res.data.data
						this.alarm.latitude = d.latitude == null ? null : d.latitude
						this.alarm.longitude = d.longitude == null ? null : d.longitude
						this.alarm.location_source = d.location_source || null
						resolve()
					},
					fail: () => resolve()
				})
			})
		},
		loadLocation() {
			if (!this.deviceId) return Promise.resolve()
			return new Promise(resolve => {
				uni.request({
					url: this.cloudBaseUrl + '/api/location?device_id=' +
						encodeURIComponent(this.deviceId) + '&_ts=' + Date.now(),
					method: 'GET',
					header: {
						'Authorization': 'Bearer ' + this.getToken(),
						'Cache-Control': 'no-cache'
					},
					timeout: 8000,
					success: (res) => {
						if (!res || res.statusCode !== 200 || !res.data || res.data.success !== true) return resolve()
						const d = res.data.data || {}
						const lat = Number(d.latitude)
						const lng = Number(d.longitude)
						const valid = Number.isFinite(lat) && Number.isFinite(lng) &&
							!(lat === 0 && lng === 0) &&
							lat >= -90 && lat <= 90 &&
							lng >= -180 && lng <= 180
						this.loc.ok = valid
						if (valid) {
							this.loc.lat = lat
							this.loc.lng = lng
							this.loc.time = d.time || ''
							const direct = typeof d.address === 'string' ? d.address : ''
							if (direct) {
								this.loc.address = direct
							} else {
								this.reverseGeocode(lat, lng)
							}
						} else {
							this.loc.address = ''
						}
						resolve()
					},
					fail: () => resolve()
				})
			})
		},
		reverseGeocode(lat, lng) {
			if (!AMAP_KEY) return
			const key = lat.toFixed(3) + ',' + lng.toFixed(3)
			if (key === this.lastGeoKey) return
			this.lastGeoKey = key

			uni.request({
				url: 'https://restapi.amap.com/v3/geocode/regeo',
				method: 'GET',
				data: {
					key: AMAP_KEY,
					location: lng.toFixed(6) + ',' + lat.toFixed(6),
					extensions: 'base'
				},
				timeout: 8000,
				success: (res) => {
					if (!res || res.statusCode !== 200 || !res.data) return
					const data = res.data
					if (data.status !== '1' || !data.regeocode) {
						this.lastGeoKey = ''
						return
					}
					const ac = data.regeocode.addressComponent || {}
					const pick = (v) => (typeof v === 'string' ? v : '')
					const city = pick(ac.city) || pick(ac.province)
					const district = pick(ac.district)
					const street = pick(ac.township)
					const addr = [city, district, street].filter(Boolean).join('')
					if (addr) this.loc.address = addr
				},
				fail: () => {
					this.lastGeoKey = ''
				}
			})
		},
		goLocation() {
			this.getLocation()
		},
		goUser() {
			uni.navigateTo({ url: '/pages/family/user' })
		},
		goAlarmList() {
			uni.navigateTo({
				url: `/pages/family/alarmList?deviceId=${encodeURIComponent(this.deviceId)}&elderName=${encodeURIComponent(this.elderName || '')}`
			})
		},
		switchElder() {
			uni.navigateTo({ url: '/pages/family/elders' })
		},
		goOfflinePeriods() {
			uni.navigateTo({
				url: `/pages/family/offlinePeriods?deviceId=${encodeURIComponent(this.deviceId)}&elderName=${encodeURIComponent(this.elderName || '')}`
			})
		},
		getLocation() {
			if (!this.deviceId) {
				uni.showToast({ title: '请先绑定老人设备', icon: 'none' })
				return
			}
			uni.navigateTo({
				url: `/pages/familyLocation/familyLocation?deviceId=${encodeURIComponent(this.deviceId)}&elderName=${encodeURIComponent(this.elderName || '')}`
			})
		},
		sendAlert() {
			this.showRemindModal = true
			this.remindContent = ''
		},
		fillText(text) {
			this.remindContent = text
		},
		closeRemindModal() {
			this.showRemindModal = false
		},
		submitRemind() {
			const content = this.remindContent.trim()
			if (!content) {
				uni.showToast({ title: '请输入提醒内容', icon: 'none', position: 'top' })
				return
			}
			if (this.remindSending) return
			this.remindSending = true

			uni.request({
				url: this.cloudBaseUrl + '/api/remind',
				method: 'POST',
				header: {
					'Authorization': 'Bearer ' + this.getToken(),
					'Content-Type': 'application/json'
				},
				data: { device_id: this.deviceId, content },
				timeout: 10000,
				success: (res) => {
					if (res.statusCode === 200 && res.data && res.data.success) {
						this.showRemindModal = false
						uni.showToast({
							title: res.data.sent > 1 ? '已发送给 ' + res.data.sent + ' 位老人' : '提醒已发送',
							icon: 'success'
						})
					} else if (res.statusCode === 404) {
						uni.showToast({
							title: (res.data && res.data.message) || '该设备还没有绑定老人端账号',
							icon: 'none',
							duration: 2500
						})
					} else if (res.statusCode === 401) {
						uni.showToast({ title: '登录已失效，请重新登录', icon: 'none' })
					} else {
						uni.showToast({
							title: (res.data && res.data.message) || '发送失败，请重试',
							icon: 'none'
						})
					}
				},
				fail: () => {
					uni.showToast({ title: '网络异常，发送失败', icon: 'none' })
				},
				complete: () => {
					this.remindSending = false
				}
			})
		}
	}
}
</script>

<style scoped>
.container {
	padding: 0 32rpx 180rpx;
	background: #F4F6FA;
	min-height: 100vh;
	box-sizing: border-box;
}

/* ================= 顶部栏 ================= */
.app-header {
	box-sizing: content-box;
	padding-top: var(--status-bar-height);
	height: 150rpx;
	margin-bottom: 18rpx;
	display: flex;
	align-items: center;
	justify-content: space-between;
}

.app-header-title {
	flex: 1;
	text-align: center;
	font-size: 50rpx;
	line-height: 150rpx;
	font-weight: 700;
	color: #1B2233;
}

.app-header-spacer {
	width: 68rpx;
	height: 68rpx;
	flex-shrink: 0;
}

.app-header-btn {
	width: 68rpx;
	height: 68rpx;
	border-radius: 50%;
	background: #FFFFFF url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%232F6FED' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M3.5 8.5h13.5'/%3E%3Cpath d='M13.5 5l3.5 3.5-3.5 3.5'/%3E%3Cpath d='M20.5 15.5H7'/%3E%3Cpath d='M10.5 12L7 15.5l3.5 3.5'/%3E%3C/svg%3E") no-repeat center;
	background-size: 38rpx 38rpx;
	box-shadow: 0 4rpx 14rpx rgba(31, 45, 74, 0.10);
	flex-shrink: 0;
}

/* ================= hero ================= */
.hero {
	display: flex;
	flex-direction: column;
	padding: 34rpx 30rpx 28rpx;
	border-radius: 34rpx;
	margin-bottom: 30rpx;
	background: linear-gradient(135deg, #4A7CFF 0%, #57C9F2 100%);
	box-shadow: 0 16rpx 36rpx rgba(74, 124, 255, 0.28);
}

/* 上半部分：头像 + 姓名 */
.hero-top {
	display: flex;
	align-items: center;
}

/* 下半部分：链路 / 佩戴 / 电量 */
.hero-metrics {
	display: flex;
	margin-top: 26rpx;
	padding-top: 22rpx;
	border-top: 1rpx solid rgba(255, 255, 255, 0.24);
}

.metric {
	flex: 1;
	display: flex;
	flex-direction: column;
	align-items: center;
	min-width: 0;
}

.metric-k {
	font-size: 22rpx;
	color: rgba(255, 255, 255, 0.78);
}

.metric-v {
	font-size: 27rpx;
	font-weight: 600;
	color: #FFFFFF;
	margin-top: 8rpx;
	text-align: center;
	white-space: nowrap;
	overflow: hidden;
	text-overflow: ellipsis;
	max-width: 100%;
}

.hero-battery {
	height: 10rpx;
	margin-top: 18rpx;
	border-radius: 999rpx;
	background: rgba(255, 255, 255, 0.24);
	overflow: hidden;
}

.hero-battery-fill {
	height: 100%;
	border-radius: 999rpx;
	background: #FFFFFF;
	transition: width 0.3s ease;
}

.hero-battery-fill.low {
	background: #FF4D4F;
}

.hero-battery-fill.charging {
	background: #22C55E;
}

.battery-low-text {
	color: #FFB4B4 !important;
}

.battery-charging-text {
	color: #7CF7A8 !important;
}

.battery-state {
	display: block;
	margin-top: 8rpx;
	font-size: 22rpx;
}

.battery-state.low {
	color: #FFB4B4;
}

.battery-state.charging {
	color: #7CF7A8;
}

.hero-avatar {
	width: 120rpx;
	height: 120rpx;
	line-height: 120rpx;
	text-align: center;
	font-size: 64rpx;
	border-radius: 50%;
	background: rgba(255, 255, 255, 0.28);
	margin-right: 24rpx;
	flex-shrink: 0;
}

.hero-main {
	flex: 1;
	min-width: 0;
	color: #FFFFFF;
}

.hero-name {
	font-size: 40rpx;
	font-weight: 700;
	line-height: 1.3;
}

.hero-sub {
	font-size: 26rpx;
	opacity: 0.9;
	margin-top: 6rpx;
}

.hero-tag {
	display: inline-block;
	margin-top: 12rpx;
	font-size: 24rpx;
	padding: 4rpx 18rpx;
	border-radius: 999rpx;
	background: rgba(255, 255, 255, 0.24);
}

/* 旧的 .hero-arrow 已废弃：切换入口移到顶部栏 */

/* ================= 两列卡片 ================= */
.grid-row {
	display: flex;
	gap: 26rpx;
	margin-bottom: 40rpx;
}

.card {
	background: #FFFFFF;
	border-radius: 32rpx;
	padding: 30rpx 26rpx;
	box-shadow: 0 10rpx 30rpx rgba(31, 45, 74, 0.07);
}

.card.half {
	flex: 1;
	min-width: 0;
}

.card-head {
	display: flex;
	align-items: center;
	justify-content: center;
	margin-bottom: 24rpx;
}

.head-icon {
	width: 44rpx;
	height: 44rpx;
	border-radius: 14rpx;
	margin-right: 12rpx;
	background: #E8F0FF url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%232F6FED' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 3.5 20 8v8l-8 4.5L4 16V8z'/%3E%3Cpath d='M4 8l8 4.5 8-4.5'/%3E%3Cpath d='M12 12.5V20.5'/%3E%3C/svg%3E") no-repeat center;
	background-size: 26rpx 26rpx;
}

.head-icon.alt {
	background: #E9FBF1 url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2312B76A' stroke-width='1.9' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M3 12.5h4l3-7 4 14 3-7h4'/%3E%3C/svg%3E") no-repeat center;
	background-size: 26rpx 26rpx;
}

.card-title {
	font-size: 32rpx;
	font-weight: 700;
	color: #1B2233;
}

.card-foot {
	text-align: center;
	font-size: 24rpx;
	color: #98A0AE;
}

.gauge {
	position: relative;
	width: 176rpx;
	height: 176rpx;
	margin: 4rpx auto 20rpx;
}

.gauge-ring {
	position: absolute;
	top: 0;
	left: 0;
	right: 0;
	bottom: 0;
	border-radius: 50%;
	border: 14rpx solid #EAF0FA;
	border-top-color: #3D7BFF;
	border-right-color: #3D7BFF;
	transform: rotate(45deg);
}

.gauge-center {
	position: absolute;
	top: 0;
	left: 0;
	right: 0;
	bottom: 0;
	display: flex;
	flex-direction: column;
	align-items: center;
	justify-content: center;
}

.gauge-num {
	font-size: 46rpx;
	font-weight: 700;
	color: #1B2233;
	line-height: 1.1;
}

.gauge-unit {
	font-size: 20rpx;
	color: #98A0AE;
	margin-top: 4rpx;
}

.chip {
	border-radius: 999rpx;
	padding: 14rpx 18rpx;
	font-size: 25rpx;
	font-weight: 600;
	text-align: center;
	margin-bottom: 18rpx;
}

.chip-ok {
	background: #E9FBF1;
	color: #0E9F6E;
}

.chip-danger {
	background: #FFECEC;
	color: #E02B2B;
}

.chip-offline {
	background: #EEF1F5;
	color: #7B8494;
}

.gauge-offline .gauge-ring {
	border-color: #D5DAE2;
	box-shadow: none;
}

.gauge-offline .gauge-num {
	color: #8B94A3;
	font-size: 28rpx !important;
}

.status-hint {
	margin-top: 12rpx;
	font-size: 22rpx;
	color: #8B94A3;
	line-height: 1.5;
}

.link-value {
	color: #7B8494 !important;
	font-size: 22rpx !important;
	font-weight: 400 !important;
}

.kv {
	display: flex;
	align-items: center;
	justify-content: space-between;
	padding: 12rpx 0;
	border-bottom: 1rpx solid #F2F5FA;
}

.kv:last-child {
	border-bottom: none;
}

.k {
	font-size: 26rpx;
	color: #8A93A3;
}

.v {
	font-size: 28rpx;
	font-weight: 600;
	color: #1B2233;
}

/* ================= 当前位置 ================= */
.loc-card {
	display: flex;
	align-items: flex-start;
	background: #FFFFFF;
	border-radius: 32rpx;
	padding: 30rpx;
	margin-bottom: 34rpx;
	box-shadow: 0 10rpx 30rpx rgba(31, 45, 74, 0.07);
}

.loc-icon {
	width: 72rpx;
	height: 72rpx;
	line-height: 72rpx;
	text-align: center;
	font-size: 34rpx;
	border-radius: 50%;
	background: #E8F0FF;
	margin-right: 20rpx;
	flex-shrink: 0;
}

.loc-body {
	flex: 1;
	min-width: 0;
}

.loc-label {
	font-size: 24rpx;
	color: #8A93A3;
}

.loc-addr {
	font-size: 30rpx;
	font-weight: 700;
	color: #1B2233;
	line-height: 1.4;
	margin-top: 8rpx;
	text-align: left;
}

.loc-addr.loc-none {
	color: #B4BAC6;
	font-weight: 500;
}

.loc-src {
	font-size: 23rpx;
	color: #98A0AE;
	margin-top: 8rpx;
}

.loc-arrow {
	font-size: 40rpx;
	color: #C6CCD8;
	margin-left: 12rpx;
	line-height: 1;
}

.mini-map {
	position: relative;
	height: 176rpx;
	border-radius: 20rpx;
	margin-top: 18rpx;
	overflow: hidden;
	background: linear-gradient(135deg, #E9EFF8 0%, #F7F9FC 100%);
}

.map-line {
	position: absolute;
	background: #DCE4EF;
}

.line-a {
	left: -10%;
	top: 44%;
	width: 120%;
	height: 10rpx;
}

.line-b {
	left: 34%;
	top: -10%;
	width: 10rpx;
	height: 120%;
}

.line-c {
	left: -10%;
	top: 76%;
	width: 120%;
	height: 8rpx;
	transform: rotate(-7deg);
}

.map-ring {
	position: absolute;
	left: 50%;
	top: 50%;
	width: 76rpx;
	height: 76rpx;
	margin: -38rpx 0 0 -38rpx;
	border: 2rpx solid rgba(240, 79, 79, 0.35);
	border-radius: 50%;
	background: rgba(240, 79, 79, 0.08);
}

.map-pin {
	position: absolute;
	left: 50%;
	top: 50%;
	width: 16rpx;
	height: 16rpx;
	margin: -8rpx 0 0 -8rpx;
	border-radius: 50%;
	background: #F04F4F;
	box-shadow: 0 0 0 6rpx rgba(240, 79, 79, 0.18);
}

.map-empty-text {
	position: absolute;
	left: 0;
	right: 0;
	top: 50%;
	transform: translateY(-50%);
	text-align: center;
	font-size: 22rpx;
	color: #B4BAC6;
}

/* ================= 胶囊按钮 ================= */
.action-row {
	display: flex;
	gap: 24rpx;
}

.pill {
	flex: 1;
	height: 96rpx;
	line-height: 96rpx;
	padding: 0;
	margin: 0;
	border-radius: 999rpx;
	font-size: 31rpx;
	font-weight: 700;
	color: #FFFFFF;
}

.pill::after {
	border: none;
}

.pill-green {
	background: linear-gradient(135deg, #3ED598 0%, #12B76A 100%);
	box-shadow: 0 10rpx 24rpx rgba(18, 183, 106, 0.28);
}

/* ================= 弹窗 ================= */
.modal-mask {
	position: fixed;
	top: 0;
	left: 0;
	right: 0;
	bottom: 0;
	background: rgba(17, 24, 39, 0.55);
	z-index: 999;
	display: flex;
	align-items: center;
	justify-content: center;
}

.modal-box {
	width: 86%;
	background: #FFFFFF;
	border-radius: 28rpx;
	padding: 36rpx 30rpx 30rpx;
	box-sizing: border-box;
}

.modal-title {
	text-align: center;
	font-size: 33rpx;
	font-weight: 700;
	color: #1B2233;
	margin-bottom: 28rpx;
}

.quick-list {
	display: flex;
	flex-wrap: wrap;
	gap: 16rpx;
	margin-bottom: 26rpx;
}

.quick-item {
	padding: 16rpx 24rpx;
	background: #EAF1FF;
	border-radius: 14rpx;
	font-size: 25rpx;
	color: #007AFF;
}

.remind-input {
	width: 100%;
	border: 2rpx solid #E9EDF4;
	background: #FAFBFD;
	min-height: 140rpx;
	padding: 22rpx;
	border-radius: 16rpx;
	font-size: 27rpx;
	color: #1B2233;
	box-sizing: border-box;
}

.btn-row {
	display: flex;
	gap: 20rpx;
	margin-top: 30rpx;
}

.btn-cancel {
	flex: 1;
	background: #F2F4F8;
	color: #4A5364;
	font-weight: 500;
}

.btn-confirm {
	flex: 1;
	background: #007AFF;
	color: #FFFFFF;
}

.battery-card { margin-bottom: 24rpx; }
/* 设备状态卡片已经合并进 hero，这里只保留间距 */
.battery-bar {
	height: 16rpx;
	background: #EEF1F5;
	border-radius: 999rpx;
	overflow: hidden;
	margin: 14rpx 0 8rpx;
}
.battery-fill {
	height: 100%;
	background: linear-gradient(90deg, #22C55E, #16A34A);
	border-radius: 999rpx;
	transition: width 0.3s ease;
}
.battery-main { display: flex; align-items: baseline; gap: 14rpx; margin: 10rpx 0 14rpx; }
.battery-value { font-size: 46rpx; font-weight: 700; color: #1B2233; }
.battery-sub { font-size: 23rpx; color: #7B8494; }
.battery-low { color: #F04F4F !important; }

.help-history-card { margin-bottom: 24rpx; }
.help-empty { color: #98A0AE; font-size: 24rpx; padding: 10rpx 0; }
.help-row { display: flex; justify-content: space-between; gap: 16rpx; padding: 12rpx 0; border-top: 1rpx solid #EEF1F5; }
.help-time { color: #5C6675; font-size: 23rpx; }
.help-meta { color: #7B8494; font-size: 22rpx; text-align: right; }
</style>
