<template>

<view class="container">

	<!-- ================================================= -->
	<!-- 标题 -->
	<!-- ================================================= -->

	<view class="app-header">
		<view class="app-header-spacer"></view>
		<view class="app-header-title">老人跌倒预警系统</view>
		<view class="app-header-btn" @click="goProfile"></view>
	</view>


	<!-- ================================================= -->
	<!-- 主状态卡 -->
	<!-- ================================================= -->

	<view class="hero">

		<view class="hero-top">

			<view class="hero-main">

				<view class="hero-name">
					{{ elderNickname || '老人端' }}
				</view>

				<view class="hero-status">
					{{ cloudConnected ? '在线 · 设备正常' : '云端未连接' }}
				</view>

			</view>

			<view
				class="hero-badge"
				:class="monitoringActive ? 'badge-on' : 'badge-off'"
				@click="toggleMonitor"
			>
				{{ monitoringActive ? '监测中' : '已停止' }}
			</view>

		</view>

		<!-- 设备电量（合并进主卡） -->
		<view class="hero-battery">
			<view class="hb-head">
				<text class="hb-label">设备电量</text>
				<text class="hb-value" :class="{ low: batteryLow, charging: batteryCharging }">
					{{ batteryText }}
				</text>
			</view>
			<view class="hb-bar">
				<view
					class="hb-fill"
					:class="{ low: batteryLow, charging: batteryCharging }"
					:style="{ width: batteryPercent + '%' }"
				></view>
			</view>
			<text v-if="batteryCharging" class="hb-tip charging">正在充电</text>
			<text v-else-if="batteryLow" class="hb-tip low">电量不足，请及时充电</text>
		</view>

	</view>


	<!-- ================================================= -->
	<!-- 四联状态 -->
	<!-- ================================================= -->

	<view class="chips">

		<view class="chip-item">
			<view
				class="chip-dot"
				:class="effectiveWifiConnected ? 'dot-on' : 'dot-off'"
			></view>
			<text class="chip-name">WiFi</text>
			<text class="chip-state">
				{{ effectiveWifiConnected ? '已连接' : '未连接' }}
			</text>
		</view>

		<view class="chip-item">
			<view
				class="chip-dot"
				:class="bluetoothConnected ? 'dot-on' : 'dot-off'"
			></view>
			<text class="chip-name">蓝牙</text>
			<text class="chip-state">
				{{ bluetoothConnected ? '已连接' : '未连接' }}
			</text>
		</view>

		<view class="chip-item">
			<view
				class="chip-dot"
				:class="cloudConnected ? 'dot-on' : 'dot-off'"
			></view>
			<text class="chip-name">云端</text>
			<text class="chip-state">
				{{ cloudConnected ? '已同步' : '未连接' }}
			</text>
		</view>

		<view class="chip-item">
			<view
				class="chip-dot"
				:class="effectiveFourGActive ? 'dot-on' : 'dot-off'"
			></view>
			<text class="chip-name">4G</text>
			<text class="chip-state">
				{{ effectiveFourGActive
					? '应急中'
					: (effectiveFourGAwake ? '待机' : '未启用')
				}}
			</text>
		</view>

	</view>


	<!-- 4G启动倒计时 -->

	<view
		v-if="showWaitingCountdown"
		class="countdown"
	>

		4G正在启动

		<view class="countdown-number">
			{{ effectiveFourGCountdown }} 秒
		</view>

	</view>


	<!-- 设备电量已合并到上方主卡，这里不再单独占一块 -->

	<view class="route-info">

		当前通信方式：

		<strong>
			{{ communicationRoute }}
		</strong>

	</view>


	<!-- ================================================= -->
	<!-- 当前姿态 -->
	<!-- ================================================= -->

	<view class="card posture-card">

		<view class="card-head">
			<view class="head-icon"></view>
			<text class="card-title">实时监测</text>
		</view>

		<view class="posture-row">

			<view class="gauge">
				<view class="gauge-ring"></view>
				<view class="gauge-center">
					<text class="gauge-num">{{ displayAccMag }}</text>
					<text class="gauge-unit">加速度</text>
				</view>
			</view>

			<view class="kv-list">

				<view class="kv">
					<text class="k">角速度</text>
					<text class="v">{{ displayGyroMag }}</text>
				</view>

				<view class="kv">
					<text class="k">AI 参考</text>
					<text class="v">{{ displayAiProb }}</text>
				</view>

				<view class="kv">
					<text class="k">状态</text>
					<text class="v">{{ currentStatusText }}</text>
				</view>

			</view>

		</view>

	</view>


	<!-- ================================================= -->
	<!-- BLE连接 -->
	<!-- ================================================= -->

	<view class="card">

		<view class="section-title">
			蓝牙连接
		</view>


		<!-- 蓝牙状态 -->

		<view
			class="ble-status"
			:class="
				bluetoothConnected
					? 'ble-on'
					: 'ble-off'
			"
		>

			{{ bluetoothConnected ? '● ESP32蓝牙已连接' : '○ ESP32蓝牙未连接' }}

		</view>


		<!-- 当前设备 -->

		<view class="ble-device">

			设备：

			{{ bleDeviceName || '未连接' }}

		</view>


		<!-- 未连接 -->

		<button
			v-if="!bluetoothConnected"
			type="primary"
			@click="startBluetooth"
			:disabled="bleConnecting"
		>

			{{ bleConnecting ? '正在连接...' : '连接ESP32蓝牙' }}

		</button>

		<button
			v-if="!bluetoothConnected"
			type="default"
			@click="restartBluetooth"
		>重新连接蓝牙</button>


		<!-- 已连接 -->

		<button
			v-else
			type="warn"
			@click="disconnectBluetooth"
			:disabled="bleConnecting"
		>

			断开蓝牙

		</button>

		<view class="ble-auto-tip">
			WiFi 断开后会自动搜索并连接 ESP32 蓝牙；蓝牙也断开 15 秒后，设备会自动切到 4G。
		</view>

		<view class="ble-device">通知状态：{{ bleNotifyReady ? '已开启' : '未开启' }}</view>

		<!-- BLE提示 -->
		<view
			v-if="bleMessage"
			class="ble-message"
		>

			{{ bleMessage }}

		</view>

	</view>


	<!-- ================================================= -->
	<!-- 查看家人提醒 -->
	<!-- ================================================= -->

	<view class="remind-wrap">

		<button type="primary" @click="goRemind">📩 查看家人提醒</button>

		<view
			v-if="remindUnreadCount > 0"
			class="badge"
		>
			{{ remindUnreadCount }}
		</view>

	</view>


	<!-- ================================================= -->
	<!-- 设备 -->
	<!-- ================================================= -->

	<view class="device-info">

		设备：{{ deviceId }}

	</view>

</view>

</template>


<script>
// BLE界面刷新限流：记录上次刷新界面的时间（非响应式，避免自身触发更新）
let bleLastUiAt = 0
let bleForwardLastStatusAt = 0   // 上次把蓝牙状态转发云端的时间
let bleForwardLastAlarmAt = 0    // 上次把蓝牙报警转发云端的时间
// 转发报警时附带手机GPS，家人端可以直接看到报警位置
const BLE_FORWARD_USE_PHONE_GPS = true

// ============================================================
// 老人手机定位上报
// 只要老人端APP在前台，就周期把手机GPS上报云端。
// 家人端“获取老人位置”优先读取这份位置。
// ============================================================
const PHONE_LOCATION_INTERVAL = 20000
let phoneLocationTimer = null
let lastPhoneLocationAt = 0
let phoneLocationWarned = false
let phoneLocationFailCount = 0

export default {
	data() {
		return {
			cloudBaseUrl: 'http://101.43.88.85:5000',
			cloudConnected: false,

			cloudFallen: false,
			cloudPitch: '0.00',
			cloudRoll: '0.00',
			cloudTime: '',
			cloudLatitude: null,
			cloudLongitude: null,
			cloudLocationSource: null,

			// 当前页面显示的设备状态。只由当前选中的数据源同步，
			// 不允许WiFi、BLE、历史报警直接互相覆盖。
			currentFallen: false,
			currentPitch: '—',
			currentRoll: '—',
			currentStatusTime: '',
			currentDataSource: '',
			bleJsonBuffer: '',
			// 两条实时数据分别缓存。
			wifiRealtime: {
				pitch: '—',
				roll: '—',
				accMag: '—',
				gyroMag: '—',
				aiProb: null,
				wearingOk: null,
				fallen: false,
				monitoring: false,
				updatedAt: 0,
				updatedAtText: ''
			},
			bleRealtime: {
				pitch: '—',
				roll: '—',
				accMag: '—',
				gyroMag: '—',
				aiProb: null,
				aiDir: null,
				aiConf: null,
				wearingOk: null,
				fallen: false,
				monitoring: false,
				updatedAt: 0,
				updatedAtText: ''
			},

			// WiFi状态只表示ESP32设备WiFi本身。
			// BLE可以携带ESP32的WiFi状态，但绝不能因为BLE连接/断开就人为修改它。
			wifiConnected: false,
			wifiStateUpdatedAt: 0,
			bleWifiReported: false,
			bleWifiReportedValid: false,
			deviceStatusFresh: false,
			hasReceivedDeviceStatus: false,
			deviceStatusLastChangedAt: 0,
			lastStatusSeq: null,
			lastStatusServerTime: '',
			statusSeqAvailable: false,

			// BLE
			bluetoothConnected: false,
			bleDeviceId: '',
			bleDeviceName: '',
			bleConnecting: false,
			bleNotifyReady: false,
			bleConnectStartedAt: 0,
			bleWatchdogTimer: null,
			bleMessage: '',
			bleServiceUUID: '4fafc201-1fb5-459e-8fcc-c5c9c331914b',
			bleCharacteristicUUID: 'beb5483e-36e1-4688-b7f5-ea07361b26a8',
			bleServiceId: '',
			bleCharacteristicId: '',
			bleAckCharacteristicUUID: 'beb5483e-36e1-4688-b7f5-ea07361b26a9',
			bleAckCharacteristicId: '',
			lastBleHelpAt: 0,
			bleRealtimeValid: false,
			lastBleStatusAt: 0,
			blePacketCount: 0,
			lastBleSeq: null,
			bleDuplicateCount: 0,
			bleOutOfOrderCount: 0,
			bleReconnectAttempt: 0,
			bleReconnectTotal: 0,
			bleMtu: 0,
			bleRssi: null,
			bleRssiText: '—',
			bleRssiTimer: null,
			batteryText: '未检测',
			batteryPercent: 0,
			batteryLow: false,
			batteryCharging: false,
			lastBatteryVoltage: null,
			batteryChargingUntil: 0,
			batteryRiseCount: 0,
			lastBatteryPercent: null,
			batteryMissCount: 0,
			cloudAiProb: null,
			cloudAlarmModalShowing: false,
			lastCloudAlarmModalAt: 0,
			bleDeviceFoundListenerRegistered: false,
			bleConnectionStateListenerRegistered: false,
			bleValueListenerRegistered: false,

			// WiFi断开后自动搜索/连接蓝牙
			// 手动点“断开蓝牙”后置 true，避免马上又自动连回去
			pageVisible: true,
			pageShownAt: Date.now(),
			bleAutoConnectSuppressed: false,
			lastBleAutoAttemptAt: 0,

			// 4G：平时不上传普通数据，只显示双离线后的阶段和真实报警。
			fourGActive: false,
			// 老人称呼（登录时存入 storage），主状态卡标题用
			elderNickname: '',
			// 本机绑定的设备号（登录时从 storage 取，多设备时用于区分）
			deviceId: 'ESP32_001',
			fourGAwake: false,
			waitingFor4G: false,
			fourGCountdown: 0,
			localBothOfflineStartedAt: 0,
			inferredFourGStandby: false,
			currentAlarmSource: '',
			lastAlarmSignature: '',
			alarmHistoryInitialized: false,

			// 监测
			monitoringActive: false,
			// 监测开关的乐观更新：按下立刻切换显示，
			// 等设备回传真实状态后再确认（monitorPending 期间不被轮询覆盖）
			monitorPending: false,
			monitorTarget: false,
			monitorConfirmTimer: null,

			// 定时器/请求互斥
			cloudTimer: null,
			statusTimer: null,
			displayRefreshTimer: null,
			cloudAlarmRequestInFlight: false,
			deviceStatusRequestInFlight: false,
			isPollingRunning: false,
			pollTimeoutId: null,
			// 控制
			commandSending: false,
			commandMessage: '',
			remindUnreadCount: 0,
			lastUnreadCount: -1,
			remindTimer: null
		}
	},

	computed: {
		communicationRoute() {
			if (this.effectiveWifiConnected) return 'WiFi → 云端'
			if (this.effectiveBluetoothConnected) return '蓝牙 → 老人手机 → 云端'
			if (this.effectiveFourGActive) return '4G → 云端（应急报警）'
			if (this.effectiveFourGAwake) return '4G已进入待机状态（等待报警）'
			if (this.effectiveWaitingFor4G) return '等待15秒 → 4G'
			return '暂无可用通信链路'
		},

		communicationStatusText() {
			if (this.effectiveWifiConnected) return 'WiFi已连接'
			if (this.effectiveBluetoothConnected) return '蓝牙已连接（WiFi不可用）'
			if (this.effectiveFourGActive) return '4G应急已触发'
			if (this.effectiveFourGAwake) return '4G已进入待机状态'
			if (this.effectiveWaitingFor4G) return 'WiFi、蓝牙均断开，等待4G接管'
			return '暂无可用通信链路'
		},

		// 三链路唯一优先级：WiFi > BLE > 4G。
		effectiveWifiConnected() {
			// BLE不修改wifiConnected；这里只读取“ESP32通过BLE刚刚报告的WiFi真实状态”。
			// 这样BLE连接本身不会把WiFi状态改来改去，但WiFi断开时又能及时反映。
			if (this.bluetoothConnected === true && this.bleWifiReportedValid === true) {
				return this.bleWifiReported === true
			}
			return this.wifiConnected === true && this.deviceStatusFresh === true
		},

		effectiveBluetoothConnected() {
			// BLE物理连接本身就是可用通信链路，不因为某一帧暂时未到就错误启动4G。
			return this.bluetoothConnected === true
		},

		effectiveFourGActive() {
			if (this.effectiveWifiConnected || this.effectiveBluetoothConnected) return false
			return this.fourGActive === true
		},
		effectiveFourGAwake() {
			if (this.effectiveWifiConnected || this.effectiveBluetoothConnected) return false
			return this.fourGAwake === true || this.inferredFourGStandby === true
		},

		effectiveWaitingFor4G() {
			if (this.effectiveWifiConnected || this.effectiveBluetoothConnected) return false
			return this.waitingFor4G === true
		},

		effectiveFourGCountdown() {
		if (!this.effectiveWaitingFor4G) return 0
		return Number(this.fourGCountdown) || 0
	},

		showWaitingCountdown() {
			return this.effectiveWaitingFor4G && this.effectiveFourGCountdown > 0
		},

		displayPitch() {
			if (this.currentDataSource === 'wifi') return this.wifiRealtime.pitch
			if (this.currentDataSource === 'ble') return this.bleRealtime.pitch
			if (this.currentDataSource === 'alarm4g') return this.cloudPitch
			return '—'
		},

		displayRoll() {
			if (this.currentDataSource === 'wifi') return this.wifiRealtime.roll
			if (this.currentDataSource === 'ble') return this.bleRealtime.roll
			if (this.currentDataSource === 'alarm4g') return this.cloudRoll
			return '—'
		},

		displayAccMag() {
			if (this.currentDataSource === 'wifi') return this.wifiRealtime.accMag
			if (this.currentDataSource === 'ble') return this.bleRealtime.accMag
			return '—'
		},

		displayGyroMag() {
			if (this.currentDataSource === 'wifi') return this.wifiRealtime.gyroMag
			if (this.currentDataSource === 'ble') return this.bleRealtime.gyroMag
			return '—'
		},

		displayAiProb() {
			if (this.currentDataSource === 'alarm4g') {
				const cp = this.cloudAiProb
				return cp == null || !Number.isFinite(Number(cp)) ? '—' : (Number(cp) * 100).toFixed(0) + '%'
			}
			const p = this.currentDataSource === 'wifi' ? this.wifiRealtime.aiProb : this.bleRealtime.aiProb
			return p == null ? '—' : (Number(p) * 100).toFixed(0) + '%'
		},

		currentStatusText() {
			if (this.currentDataSource === 'alarm4g') return '⚠️ 最近一次4G跌倒报警'
			if (this.currentDataSource === 'wifi' || this.currentDataSource === 'ble') {
				return this.currentFallen ? '⚠️ 检测到老人跌倒' : '✓ 当前状态正常'
			}
			return '— 暂无实时设备数据'
		},

		locationSourceText() {
			if (this.cloudLocationSource === '4g') return '4G/GNSS'
			if (this.cloudLocationSource === 'wifi') return 'WiFi'
			if (this.cloudLocationSource === 'bluetooth') return '蓝牙'
			return this.cloudLocationSource || ''
		}
	},

	onLoad() {
			console.log('[APP] 启动')
			// 登录时保存的老人称呼，用于主状态卡显示
			this.elderNickname = uni.getStorageSync('nickname') || ''
			// 本机绑定的设备号：多台设备时，所有请求都带上它
			this.deviceId = uni.getStorageSync('deviceId') || 'ESP32_001'
			this.isPollingRunning = true
			// 启动串行递归轮询
			this.runPollTask()
		},
	
	onUnload() {
		console.log('[APP] 关闭')
		this.stopBleRssiPolling()
		this.stopBleWatchdog()
		// 停止标记，打断递归循环
		this.isPollingRunning = false
		// 清除延时定时器
		if(this.pollTimeoutId){
			clearTimeout(this.pollTimeoutId)
			this.pollTimeoutId = null
		}
		// 原来的cloudTimer清除保留（虽然不再使用，防止残留）
		if(this.cloudTimer){
			clearInterval(this.cloudTimer)
			this.cloudTimer = null
		}
		this.stopPhoneLocationReport()
		this.clearMonitorConfirmTimer()
		this.pageVisible = false
	},
	onHide() {
		// 页面进入后台后停止定位，避免持续耗电
		this.stopPhoneLocationReport()
		this.stopRemindPolling()
		this.pageVisible = false
		this.stopBleAutoScan()
		this.stopBleWatchdog()
		this.isPollingRunning = false
		if (this.pollTimeoutId) {
			clearTimeout(this.pollTimeoutId)
			this.pollTimeoutId = null
		}
	},
	onShow(){
		this.countRemindUnread()
		this.startRemindPolling()
		// 页面回到前台立即定位一次，之后每20秒上报一次
		this.startPhoneLocationReport()

		// 回到页面后允许重新自动找蓝牙；给设备状态留8秒刷新时间
		this.pageVisible = true
		this.pageShownAt = Date.now()
		this.bleAutoConnectSuppressed = false
		this.lastBleAutoAttemptAt = 0
		if (!this.isPollingRunning) {
			this.isPollingRunning = true
			this.runPollTask()
		}
	},

	onPullDownRefresh() {
		this.fetchCloudAlarm()
		Promise.resolve(this.fetchDeviceStatus()).finally(() => {
			this.syncCurrentView()
			uni.stopPullDownRefresh()
		})
	},

	methods: {
		// ===================================================
		// 当前显示数据源：只读选择，不做跨来源写入。
		// ===================================================
		async runPollTask(){
			// 如果页面已经标记停止，直接退出，不再继续循环
			if (!this.isPollingRunning) return;

			console.log('轮询执行')
			try{
				console.log("开始请求云端报警fetchCloudAlarm")
				await this.fetchCloudAlarm()
				console.log("云端报警完成，开始请求设备状态fetchDeviceStatus")
				await this.fetchDeviceStatus()
				console.log("设备状态请求完成，同步视图syncCurrentView")
				this.syncCurrentView()
				this.updateLocal4GStage()
				this.maybeAutoConnectBle()
				console.log("本轮轮询全部完成")
			}catch(e){
				console.error("轮询外层捕获异常", e)
			}
			// 延时4秒，开启下一轮（和你原来间隔保持一致 4000ms）
			this.pollTimeoutId = setTimeout(()=>{
			this.runPollTask()
			},4000)
		},
		// 未读提醒数：从服务器取，这样家人从另一台手机发的也能看到
		countRemindUnread(){
			const token = uni.getStorageSync('token') || ''
			if (!token) {
				this.remindUnreadCount = 0
				return
			}
			uni.request({
				url: this.cloudBaseUrl + '/api/remind?_ts=' + Date.now(),
				method: 'GET',
				header: {
					'Authorization': 'Bearer ' + token,
					'Cache-Control': 'no-cache'
				},
				timeout: 8000,
				success: (res) => {
					if (res.statusCode === 200 && res.data && res.data.success) {
						const list = Array.isArray(res.data.data) ? res.data.data : []
						const now = Number(
							res.data.unread != null
								? res.data.unread
								: list.filter(r => !r.is_read).length
						)
						// 有新的提醒进来：弹提示 + 震动，老人不用一直盯着看
						if (now > this.lastUnreadCount && this.lastUnreadCount >= 0) {
							uni.showToast({
								title: '收到新的家人提醒',
								icon: 'none',
								duration: 2500,
								position: 'top'
							})
							uni.vibrateLong && uni.vibrateLong({})
						}
						this.lastUnreadCount = now
						this.remindUnreadCount = now
					}
				}
			})
		},

		// 每 30 秒查一次有没有新的家人提醒
		startRemindPolling() {
			this.stopRemindPolling()
			this.remindTimer = setInterval(() => {
				this.countRemindUnread()
			}, 30000)
		},

		stopRemindPolling() {
			if (this.remindTimer) {
				clearInterval(this.remindTimer)
				this.remindTimer = null
			}
		},

		// ===================================================
		// 老人手机定位上报
		// 家人端 /api/location 读取的就是这里上传的坐标
		// ===================================================
		startPhoneLocationReport() {
			if (phoneLocationTimer) {
				clearInterval(phoneLocationTimer)
				phoneLocationTimer = null
			}

			// 进入页面立即上报一次，不用等20秒
			this.reportPhoneLocation(true)

			phoneLocationTimer = setInterval(() => {
				this.reportPhoneLocation(false)
			}, PHONE_LOCATION_INTERVAL)
		},

		stopPhoneLocationReport() {
			if (phoneLocationTimer) {
				clearInterval(phoneLocationTimer)
				phoneLocationTimer = null
			}
		},

		reportPhoneLocation(force) {
			const now = Date.now()

			// 20秒内已经上报过就跳过，避免BLE每秒一帧时疯狂定位
			if (!force && now - lastPhoneLocationAt < PHONE_LOCATION_INTERVAL - 1000) {
				return
			}
			lastPhoneLocationAt = now

			uni.getLocation({
				// 直接向系统要高德坐标（GCJ02），地图端不再做二次转换。
				// 之前按 WGS84 再转一次，在华为等机型上会整体偏到河边/湖里。
				type: 'gcj02',
				isHighAccuracy: true,
				highAccuracyExpireTime: 8000,
				success: (res) => {
					this.uploadPhoneLocation(res)
				},
				fail: (err) => {
					console.log('[定位] 获取手机位置失败：', err && err.errMsg)

					if (!phoneLocationWarned) {
						phoneLocationWarned = true
						uni.showToast({
							title: '请允许定位权限，家人端才能看到位置',
							icon: 'none',
							duration: 2500
						})
					}
				}
			})
		},

		uploadPhoneLocation(res) {
			const lat = Number(res.latitude)
			const lng = Number(res.longitude)

			if (
				!Number.isFinite(lat) ||
				!Number.isFinite(lng) ||
				(lat === 0 && lng === 0)
			) {
				return
			}

			// 老人手机定位直接使用 GCJ02（高德/腾讯地图坐标），
			// 上报时带上 coord，家人端就不会再按 WGS84 转一次。
			uni.request({
				url: this.cloudBaseUrl + '/api/location',
				method: 'POST',
				header: { 'Content-Type': 'application/json' },
				data: {
					device_id: this.deviceId,
					latitude: lat,
					longitude: lng,
					accuracy: res.accuracy || null,
					coord: 'gcj02',
					source: 'phone'
				},
				timeout: 5000,
				success: (r) => {
					phoneLocationFailCount = 0
					if (r && r.statusCode === 200 && r.data && r.data.success === true) {
						console.log('[定位] 手机位置已上报云端：', lat.toFixed(6), lng.toFixed(6))
					} else {
						console.log('[定位] 手机位置上报被拒绝：', r && r.statusCode, r && r.data)
					}
				},
				fail: (e) => {
					phoneLocationFailCount++
					// 网络不好时不要每次都刷日志
					if (phoneLocationFailCount <= 3 || phoneLocationFailCount % 10 === 0) {
						console.log('[定位] 手机位置上报失败：', e && e.errMsg)
					}
				}
			})
		},
		selectDataSource() {
			const now = Date.now()

			// WiFi一旦在线，永远保持最高优先级。
			// 但刚恢复时必须等待新的WiFi状态包，绝不把断线前的旧数据重新显示。
			if (this.effectiveWifiConnected) {
				if (this.wifiRealtime.updatedAt > 0 && now - this.wifiRealtime.updatedAt < 20000) {
					return 'wifi'
				}
				return 'pending_wifi'
			}

			// WiFi确实不可用时，BLE成为第二优先级。
			// 仍然只接受最近的BLE新数据。
			if (this.effectiveBluetoothConnected) {
				if (this.bleRealtime.updatedAt > 0 && now - this.bleRealtime.updatedAt < 5000) {
					return 'ble'
				}
				return 'pending_ble'
			}

			// 4G不是普通实时数据源。只有进入4G应急阶段时，才展示最后一次4G报警快照。
			if ((this.effectiveFourGActive || this.effectiveFourGAwake) && this.currentAlarmSource === '4g' && this.cloudFallen) {
				return 'alarm4g'
			}

			return ''
		},

		syncCurrentView() {
			const source = this.selectDataSource()
			this.currentDataSource = source

			if (source === 'wifi') {
				this.currentFallen = this.wifiRealtime.fallen
				this.currentStatusTime = this.wifiRealtime.updatedAtText
				this.applyMonitoring(this.wifiRealtime.monitoring)
				return
			}

			if (source === 'ble') {
				this.currentFallen = this.bleRealtime.fallen
				this.currentStatusTime = this.bleRealtime.updatedAtText
				this.applyMonitoring(this.bleRealtime.monitoring)
				return
			}

			if (source === 'alarm4g') {
				this.currentFallen = true
				this.currentStatusTime = this.cloudTime || this.formatTime()
				return
			}

			// pending_wifi / pending_ble / 无数据：不把任何旧数据冒充当前实时数据。
			this.currentFallen = false
			this.currentStatusTime = ''
		},

		// 监测状态收口：命令下发期间以本地目标值为准，
		// 等设备回传的真实状态与目标一致时才算确认完成。
		applyMonitoring(value) {
			const actual = value === true
			if (!this.monitorPending) {
				this.monitoringActive = actual
				return
			}
			if (actual === (this.monitorTarget === true)) {
				this.monitorPending = false
				this.monitoringActive = actual
				this.clearMonitorConfirmTimer()
				this.commandMessage = actual ? '✓ 监测已开启' : '✓ 监测已关闭'
			}
		},

		// ===================================================
		// 4G显示阶段
		// ===================================================
		updateLocal4GStage() {
			const wifiOnline = this.effectiveWifiConnected
			const bleOnline = this.effectiveBluetoothConnected
			const now = Date.now()

			if (wifiOnline || bleOnline) {
				this.localBothOfflineStartedAt = 0
				this.inferredFourGStandby = false
				this.waitingFor4G = false
				this.fourGCountdown = 0
				// 更高优先级链路恢复后，4G不再是当前通信链路。
				// 不删除最近一次4G报警快照；历史报警仍保留给后续家属模式/历史记录使用。
				this.fourGActive = false
				this.fourGAwake = false
				return
			}

			if (this.localBothOfflineStartedAt === 0) {
				this.localBothOfflineStartedAt = now
			}

			const offlineFor = now - this.localBothOfflineStartedAt

			if (offlineFor < 15000) {
				this.waitingFor4G = true
				this.inferredFourGStandby = false
				this.fourGCountdown = Math.max(1, Math.ceil((15000 - offlineFor) / 1000))
			}
			else {
				this.waitingFor4G = false
				this.fourGCountdown = 0
				this.inferredFourGStandby = true
			}
		},

		// ===================================================
		// BLE初始化
		// 这里保持已经验证过的UUID和9字节R协议，不改协议。
		// ===================================================
		initBluetooth() {
			console.log('[BLE] 初始化蓝牙')
			uni.openBluetoothAdapter({
				success: () => {
					console.log('[BLE] 蓝牙适配器打开成功')
					this.bleMessage = '蓝牙已开启，可以连接ESP32'

					if (!this.bleConnectionStateListenerRegistered) {
						this.bleConnectionStateListenerRegistered = true
						uni.onBLEConnectionStateChange((res) => {
							if (!res) return
							if (res.deviceId !== this.bleDeviceId) return

							this.bluetoothConnected = res.connected === true

							if (res.connected) {
								this.bleRealtimeValid = false
								this.lastBleStatusAt = 0
								this.bleMessage = 'ESP32蓝牙连接成功，正在准备通信'
							}
							else {
								this.bleRealtimeValid = false
								this.bleNotifyReady = false
								this.lastBleStatusAt = 0
								this.bleServiceId = ''
								this.bleCharacteristicId = ''
								this.bleConnecting = false
								this.stopBleWatchdog()
								this.bleMessage = 'ESP32蓝牙已断开'
								console.log('[BLE] 已断开，保留WiFi独立状态')
							}
						})
					}
				},
				fail: (err) => {
					console.log('[BLE] 蓝牙初始化失败：', err)
					this.bleMessage = '无法打开手机蓝牙'
				}
			})
		},

		registerBleStateListener() {
			if (this.bleConnectionStateListenerRegistered) return
			this.bleConnectionStateListenerRegistered = true
			uni.onBLEConnectionStateChange((res) => {
				if (!res) return
				if (res.deviceId !== this.bleDeviceId) return
				this.bluetoothConnected = res.connected === true
				if (!res.connected) {
					this.stopBleRssiPolling()
					this.bleRssi = null
					this.bleRssiText = '—'
					this.bleMtu = 0
					this.bleRealtimeValid = false
					this.bleNotifyReady = false
					this.lastBleStatusAt = 0
					this.bleServiceId = ''
					this.bleCharacteristicId = ''
					this.bleMessage = 'ESP32蓝牙已断开'
				}
			})
		},

		// ===================================================
		// WiFi断开后自动找蓝牙
		//
		// ESP32是BLE从机，只能广播，不能主动连手机；
		// 所以“自动找蓝牙”必须由老人手机端扫描并连接。
		// 触发条件：设备WiFi不在线 + 蓝牙未连接 + 不在连接中。
		// ===================================================
		maybeAutoConnectBle() {
			if (!this.pageVisible) return
			if (this.bluetoothConnected || this.bleConnecting) return
			if (this.bleAutoConnectSuppressed) return

			const now = Date.now()

			// WiFi还在线时不动蓝牙；顺便解除“手动断开”的抑制
			if (this.effectiveWifiConnected) {
				this.bleAutoConnectSuppressed = false
				this.bleReconnectAttempt = 0
				return
			}

			// 刚进页面时给云端设备状态8秒刷新时间，避免还没拿到状态就误判
			if (now - this.pageShownAt < 8000) return

			// 断线重连退避：5/10/20/40/60 秒
			const backoffMs = Math.min(60000, 5000 * Math.pow(2, Math.min(this.bleReconnectAttempt, 4)))
			if (now - this.lastBleAutoAttemptAt < backoffMs) return

			this.bleReconnectAttempt++
			this.lastBleAutoAttemptAt = now
			this.bleMessage = `检测到设备WiFi断开，正在自动搜索ESP32蓝牙（第${this.bleReconnectAttempt}次）...`
			console.log('[BLE] WiFi离线，自动开始搜索ESP32')
			this.startBluetooth()
		},

		stopBleAutoScan() {
			if (!this.bleConnecting) return
			uni.stopBluetoothDevicesDiscovery({
				complete: () => {
					if (!this.bluetoothConnected) {
						this.bleConnecting = false
					}
				}
			})
		},

		startBleWatchdog() {
			this.stopBleWatchdog()
			this.bleConnectStartedAt = Date.now()
			this.bleWatchdogTimer = setInterval(() => {
				if (!this.bleConnecting || this.bluetoothConnected) return
				if (Date.now() - this.bleConnectStartedAt >= 15000) {
					this.resetBleAttempt('蓝牙连接超时，请点击“重新连接蓝牙”')
				}
			}, 2000)
		},

		stopBleWatchdog() {
			if (this.bleWatchdogTimer) {
				clearInterval(this.bleWatchdogTimer)
				this.bleWatchdogTimer = null
			}
		},

		resetBleAttempt(message) {
			this.stopBleWatchdog()
			this.bleConnecting = false
			this.bluetoothConnected = false
			this.bleNotifyReady = false
			this.bleRealtimeValid = false
			this.bleServiceId = ''
			this.bleCharacteristicId = ''
			this.bleMessage = message || '已重置蓝牙连接'
			uni.stopBluetoothDevicesDiscovery({ complete: () => {} })
			if (this.bleDeviceId) {
				uni.closeBLEConnection({ deviceId: this.bleDeviceId, complete: () => {} })
			}
			this.bleDeviceId = ''
			this.bleDeviceName = ''
		},

		restartBluetooth() {
			this.resetBleAttempt('正在重新连接蓝牙...')
			setTimeout(() => this.startBluetooth(), 300)
		},

		startBluetooth() {
		    if (this.bleConnecting) return
		    this.bleAutoConnectSuppressed = false
		    this.bleConnecting = true
		    this.startBleWatchdog()
		    this.bleMessage = '正在初始化手机蓝牙...'
		
		    // 每次搜索前先确认手机蓝牙适配器已经打开
		    uni.openBluetoothAdapter({
		        success: () => {
		            console.log('[BLE] 蓝牙适配器已就绪')
		            this.registerBleStateListener()
		            this.bleMessage = '蓝牙已开启，正在搜索 ESP32...'
		
		            // 如果之前有连接记录，先关闭旧连接
		            if (this.bleDeviceId) {
		                const oldId = this.bleDeviceId
		
		                uni.closeBLEConnection({
		                    deviceId: oldId,
		                    complete: () => {
		                        this.bleDeviceId = ''
		                        this.bleDeviceName = ''
		                        this.bluetoothConnected = false
		                        this.bleNotifyReady = false
		                        this.bleRealtimeValid = false
		                        this.bleServiceId = ''
		                        this.bleCharacteristicId = ''
		
		                        this.searchESP32()
		                    }
		                })
		            } else {
		                this.searchESP32()
		            }
		        },
		
		        fail: (err) => {
		            console.log('[BLE] 打开蓝牙适配器失败：', err)
		            this.bleConnecting = false
		            this.bleMessage = '手机蓝牙初始化失败，请检查手机蓝牙和应用权限'
		        }
		    })
		},

		searchESP32() {
		    console.log('[BLE] 准备搜索 ESP32')
		
		    // 先停止之前可能残留的搜索
		    uni.stopBluetoothDevicesDiscovery({
		        complete: () => {
		
		            // 注册设备发现监听器
		            if (!this.bleDeviceFoundListenerRegistered) {
		                uni.onBluetoothDeviceFound((res) => {
		                    this.onBluetoothDeviceFound(res)
		                })
		
		                this.bleDeviceFoundListenerRegistered = true
		            }
		
		            // 开始搜索附近的蓝牙设备
		            uni.startBluetoothDevicesDiscovery({
		                allowDuplicatesKey: false,
		
		                success: () => {
		                    console.log('[BLE] 开始搜索 ESP32')
		                    this.bleMessage = '正在搜索 ESP32，请稍候...'
		
		                    // 10秒后仍未连接，停止搜索
		                    setTimeout(() => {
		                        if (this.bleConnecting && !this.bluetoothConnected) {
		                            uni.stopBluetoothDevicesDiscovery({
		                                complete: () => {
		                                    this.bleConnecting = false
		                                    this.bleMessage = '暂未找到 ESP32，请确认设备已通电并正在广播'
		                                    console.log('[BLE] 搜索超时')
		                                }
		                            })
		                        }
		                    }, 10000)
		                },
		
		                fail: (err) => {
		                    console.log('[BLE] 搜索失败：', err)
		                    this.bleConnecting = false
		                    this.bleMessage = '蓝牙搜索失败，请查看控制台错误'
		                }
		            })
		        }
		    })
		},

		onBluetoothDeviceFound(res) {
			const devices = res && res.devices ? res.devices : []
			for (let i = 0; i < devices.length; i++) {
				const device = devices[i]
				const name = device.name || device.localName || ''
				if (name !== 'FallDetector') continue

				console.log('[BLE] 找到ESP32：', device)
				this.bleDeviceId = device.deviceId
				this.bleDeviceName = name

				uni.stopBluetoothDevicesDiscovery({
					complete: () => this.connectBluetooth()
				})
				return
			}
		},

		connectBluetooth() {
			if (!this.bleDeviceId) {
				this.bleConnecting = false
				this.bleMessage = '没有找到ESP32'
				return
			}

			this.bleMessage = '正在连接 ESP32...'
			uni.createBLEConnection({
				deviceId: this.bleDeviceId,
				timeout: 10000,
				success: () => {
					console.log('[BLE] ESP32连接成功')
					this.bluetoothConnected = true
					this.bleConnecting = false
					this.stopBleWatchdog()
					this.bleAutoConnectSuppressed = false
					this.lastBleAutoAttemptAt = 0
					this.bleRealtimeValid = false
					this.bleWifiReportedValid = false
					this.bleWifiReported = false
					this.lastBleStatusAt = 0
					this.bleServiceId = ''
					this.bleCharacteristicId = ''
					this.lastBleSeq = null
					this.bleDuplicateCount = 0
					this.bleOutOfOrderCount = 0
					this.bleReconnectAttempt = 0
					this.bleReconnectTotal++
					this.requestBleMtu()
					this.startBleRssiPolling()
					this.getBLEServices(0)
				},
				fail: (err) => {
					console.log('[BLE] 连接失败：', err)
					const failedId = this.bleDeviceId
					this.bluetoothConnected = false
					this.bleRealtimeValid = false
					this.bleConnecting = false
					this.stopBleWatchdog()
					if (failedId) uni.closeBLEConnection({ deviceId: failedId })
					this.bleDeviceId = ''
					this.bleDeviceName = ''
					this.bleMessage = 'ESP32蓝牙连接失败'
				}
			})
		},

		disconnectBluetooth() {
			// 手动断开后先不要自动连回去；等WiFi恢复或重新进页面再允许自动连接
			this.bleAutoConnectSuppressed = true

			if (!this.bleDeviceId) {
				this.bluetoothConnected = false
				this.bleRealtimeValid = false
				this.bleMessage = '当前没有蓝牙连接'
				return
			}

			this.bleConnecting = true
			const deviceId = this.bleDeviceId
			this.bleMessage = '正在断开蓝牙...'

			uni.closeBLEConnection({
				deviceId: deviceId,
				complete: () => {
					this.bluetoothConnected = false
					this.bleRealtimeValid = false
					this.bleWifiReportedValid = false
					this.bleWifiReported = false
					this.lastBleStatusAt = 0
					this.bleConnecting = false
					this.bleDeviceId = ''
					this.bleDeviceName = ''
					this.bleServiceId = ''
					this.bleCharacteristicId = ''
					this.bleMessage = 'ESP32蓝牙已断开'
				}
			})
		},

		// ===================================================
		// BLE服务发现：允许手机刚建立GATT时返回空列表，最多重试5次。
		// UUID本身不改变。
		// ===================================================
		getBLEServices(retryCount) {
			const count = Number(retryCount) || 0
			uni.getBLEDeviceServices({
				deviceId: this.bleDeviceId,
				success: (res) => {
					const services = res && res.services ? res.services : []
					console.log('[BLE] Service查询完整结果：', JSON.stringify(res))
					console.log('[BLE] 当前查询设备ID：', this.bleDeviceId)
					console.log('[BLE] 手机发现的Service：', services)

					const target = this.bleServiceUUID.toLowerCase()
					let service = null
					for (let i = 0; i < services.length; i++) {
						const uuid = String(services[i].uuid || '').toLowerCase()
						if (uuid === target) {
							service = services[i]
							break
						}
					}

					if (!service) {
						if (count < 5) {
							console.log('[BLE] 暂未发现目标Service，重试：', count + 1)
							setTimeout(() => this.getBLEServices(count + 1), 400)
							return
						}
						console.log('[BLE] 5次仍未发现目标Service：', services)
						this.bleConnecting = false
						this.bleMessage = 'ESP32服务未发现，请重新连接'
						return
					}

					this.bleServiceId = service.uuid
					console.log('[BLE] 使用实际Service ID：', this.bleServiceId)
					this.getBLECharacteristics()
				},
				fail: (err) => {
					if (count < 5) {
						setTimeout(() => this.getBLEServices(count + 1), 400)
						return
					}
					console.log('[BLE] 获取Service失败：', err)
					this.bleConnecting = false
					this.bleMessage = '获取ESP32服务失败'
				}
			})
		},

		getBLECharacteristics() {
			const serviceId = this.bleServiceId || this.bleServiceUUID
			uni.getBLEDeviceCharacteristics({
				deviceId: this.bleDeviceId,
				serviceId: serviceId,
				success: (res) => {
					const characteristics = res && res.characteristics ? res.characteristics : []
					console.log('[BLE] 实际Service下的Characteristic：', characteristics)
					const target = this.bleCharacteristicUUID.toLowerCase()
					const ackTarget = this.bleAckCharacteristicUUID.toLowerCase()
					let characteristic = null
					let ackCharacteristic = null
					for (let i = 0; i < characteristics.length; i++) {
						const uuid = String(characteristics[i].uuid || '').toLowerCase()
						if (uuid === target) characteristic = characteristics[i]
						if (uuid === ackTarget) ackCharacteristic = characteristics[i]
					}

					if (!characteristic) {
						this.bleConnecting = false
						this.bleMessage = 'ESP32特征值未发现，请重新连接'
						return
					}

					this.bleCharacteristicId = characteristic.uuid
					this.bleAckCharacteristicId = ackCharacteristic ? ackCharacteristic.uuid : ''
					console.log('[BLE] 使用实际Characteristic ID：', this.bleCharacteristicId, 'ACK=', this.bleAckCharacteristicId)
					this.enableBLENotify()
				},
				fail: (err) => {
					console.log('[BLE] 获取Characteristic失败：', err)
					this.bleConnecting = false
					this.bleMessage = '获取ESP32特征值失败'
				}
			})
		},

		enableBLENotify() {
			const serviceId = this.bleServiceId || this.bleServiceUUID
			const characteristicId = this.bleCharacteristicId || this.bleCharacteristicUUID

			// 先注册一次全局监听，再开启Notify。
			if (!this.bleValueListenerRegistered) {
				this.bleValueListenerRegistered = true
				uni.onBLECharacteristicValueChange((res) => this.onBLEValueChange(res))
			}

			uni.notifyBLECharacteristicValueChange({
				deviceId: this.bleDeviceId,
				serviceId: serviceId,
				characteristicId: characteristicId,
				state: true,
				success: () => {
					console.log('[BLE] Notify已开启，等待实时数据')
					this.bleNotifyReady = true
					this.bleConnecting = false
					this.bleMessage = 'ESP32蓝牙通信已准备完成'
				},
				fail: (err) => {
					console.log('[BLE] Notify开启失败：', err)
					this.bleNotifyReady = false
					this.bleConnecting = false
					this.bluetoothConnected = false
					this.bleMessage = '蓝牙通知开启失败，请点击“重新连接蓝牙”'
				}
			})
		},

		requestBleMtu() {
			if (typeof uni.setBLEMTU !== 'function') return
			uni.setBLEMTU({
				deviceId: this.bleDeviceId,
				mtu: 64,
				success: (res) => { this.bleMtu = Number(res && res.mtu) || 64 },
				fail: () => {}
			})
		},

		startBleRssiPolling() {
			this.stopBleRssiPolling()
			if (typeof uni.getBLEDeviceRSSI !== 'function') return
			const poll = () => {
				if (!this.bluetoothConnected || !this.bleDeviceId) return
				uni.getBLEDeviceRSSI({
					deviceId: this.bleDeviceId,
					success: (res) => {
						const rssi = Number(res && res.RSSI)
						if (Number.isFinite(rssi)) {
							this.bleRssi = rssi
							this.bleRssiText = rssi + ' dBm'
						}
					},
					fail: () => {}
				})
			}
			poll()
			this.bleRssiTimer = setInterval(poll, 5000)
		},

		stopBleRssiPolling() {
			if (this.bleRssiTimer) {
				clearInterval(this.bleRssiTimer)
				this.bleRssiTimer = null
			}
		},

		sendBleAck(seq) {
			const ackId = this.bleAckCharacteristicId || this.bleAckCharacteristicUUID
			if (!ackId) return
			const buffer = new ArrayBuffer(3)
			const view = new DataView(buffer)
			view.setUint8(0, 0x41)
			view.setUint16(1, seq & 0xFFFF, true)
			uni.writeBLECharacteristicValue({
				deviceId: this.bleDeviceId,
				serviceId: this.bleServiceId || this.bleServiceUUID,
				characteristicId: ackId,
				value: buffer,
				fail: (err) => console.log('[BLE] ACK写入失败：', err)
			})
		},

		// ===================================================
		// BLE数据
		// ===================================================
		bleShouldSkip(fallenNow) {
			// 跌倒状态变化必须立刻显示；其余数据最多每500ms刷新一次界面
			const now = Date.now()
			if (fallenNow === this.bleRealtime.fallen && now - bleLastUiAt < 500) return true
			bleLastUiAt = now
			return false
		},

		handleBLEJson(jsonText) {
			let data
			try {
				data = JSON.parse(jsonText)
			} catch (e) {
				console.log('[BLE] JSON解析失败：', jsonText, e)
				return
			}
			// 兼容 pitch/roll/fallen 与 p/r/f 两种写法
			const pitch = Number(data.pitch ?? data.p)
			const roll = Number(data.roll ?? data.r)
			if (!Number.isFinite(pitch) || !Number.isFinite(roll)) return
			if (Math.abs(pitch) > 360 || Math.abs(roll) > 360) return

			const fallenRaw = data.fallen ?? data.f
			if (this.bleShouldSkip(fallenRaw === true || fallenRaw === 1)) return
			const now = Date.now()

			this.bleRealtime.pitch = pitch.toFixed(2)
			this.bleRealtime.roll = roll.toFixed(2)
			const wasFallen = this.bleRealtime.fallen
			const isFallen = (fallenRaw === true || fallenRaw === 1)
			this.bleRealtime.fallen = isFallen
			if (data.monitoring !== undefined) this.bleRealtime.monitoring = Boolean(data.monitoring)
			this.bleRealtime.updatedAt = now
			this.bleRealtime.updatedAtText = this.formatTime()

			// 若 ESP32 在 JSON 里带了 wifi 状态（字段名 wifi），同步给通信状态显示
			if (data.wifi !== undefined) {
				this.bleWifiReported = Boolean(data.wifi)
				this.bleWifiReportedValid = true
			}

			this.lastBleStatusAt = now
			this.bleRealtimeValid = true

			// 设备走蓝牙时，由手机把数据中转到云端
			this.forwardBleToCloud(pitch, roll, isFallen, wasFallen, data.monitoring)
		},

		forwardBleToCloud(pitch, roll, fallen, wasFallen, monitoring, aiProb = null, aiDir = null, aiConf = null, accMag = null, gyroMag = null, wearingOk = null) {
			const now = Date.now()

			// 1) 报警：设备 WiFi 不在线时才由手机转发，避免重复
			if (fallen && !this.effectiveWifiConnected && (!wasFallen || now - bleForwardLastAlarmAt > 60000)) {
				bleForwardLastAlarmAt = now
				this.postBleAlarm(pitch, roll, 0, aiProb, aiDir, aiConf, accMag, gyroMag, wearingOk)
			}

			// 2) 状态：只有设备自己没有通过WiFi上传时，才由手机代传
			if (!this.effectiveWifiConnected && now - bleForwardLastStatusAt >= 5000) {
				bleForwardLastStatusAt = now
				this.postBleStatus(pitch, roll, fallen, monitoring, aiProb, aiDir, aiConf, accMag, gyroMag, wearingOk)
			}
		},

		postBleStatus(pitch, roll, fallen, monitoring, aiProb = null, aiDir = null, aiConf = null, accMag = null, gyroMag = null, wearingOk = null) {
			uni.request({
				url: this.cloudBaseUrl + '/api/device/status',
				method: 'POST',
				header: { 'Content-Type': 'application/json' },
				data: {
					device_id: this.deviceId,
					wifi: false,
					bluetooth: true,
					'4g': false,
					acc_mag: accMag != null ? accMag : pitch,
					gyro_mag: gyroMag != null ? gyroMag : roll,
					fallen: fallen,
					monitoring: monitoring,
					waiting_for_4g: false,
					'4g_countdown': 0,
					link: 'ble',
					ai_prob: aiProb,
					ai_dir: aiDir,
					ai_conf: aiConf,
					wearing_ok: wearingOk
				},
				timeout: 4000,
				fail: (e) => { console.log('[转发] 状态上传失败：', e && e.errMsg) }
			})
		},

		postBleAlarm(pitch, roll, attempt, aiProb = null, aiDir = null, aiConf = null, accMag = null, gyroMag = null, wearingOk = null) {
			const body = {
				device_id: this.deviceId,
				fallen: true,
				pitch: pitch,
				roll: roll,
				location_source: 'bluetooth',
				coord: 'gcj02',
				ai_prob: aiProb,
				ai_dir: aiDir,
				ai_conf: aiConf,
				acc_mag: accMag,
				gyro_mag: gyroMag,
				wearing_ok: wearingOk
			}
			let sent = false
			const retry = (why) => {
				console.log('[转发] 报警上传失败：', why, '第', attempt + 1, '次')
				if (attempt < 2) {
					setTimeout(() => this.postBleAlarm(pitch, roll, attempt + 1, aiProb, aiDir, aiConf, accMag, gyroMag, wearingOk), 3000)
				} else {
					uni.showToast({ title: '报警上传云端失败，请检查手机网络', icon: 'none' })
				}
			}
			const send = (extra) => {
				if (sent) return
				sent = true
				uni.request({
					url: this.cloudBaseUrl + '/api/alarm',
					method: 'POST',
					header: { 'Content-Type': 'application/json' },
					data: Object.assign({}, body, extra),
					timeout: 5000,
					success: (res) => {
						const ok = res && res.statusCode >= 200 && res.statusCode < 300 && res.data && res.data.success === true
						if (ok) {
							console.log('[转发] 报警已通过手机上传云端')
							uni.showToast({ title: '报警已上传云端', icon: 'none' })
						} else {
							retry('服务器返回异常')
						}
					},
					fail: (e) => retry(e && e.errMsg)
				})
			}

			if (!BLE_FORWARD_USE_PHONE_GPS) {
				send({})
				return
			}
			setTimeout(() => send({}), 6000)
			try {
				uni.getLocation({
					type: 'gcj02',
					success: (r) => send({ latitude: r.latitude, longitude: r.longitude }),
					fail: () => send({})
				})
			} catch (e) {
				send({})
			}
		},

		postBleHelp() {
			uni.request({
				url: this.cloudBaseUrl + '/api/alarm',
				method: 'POST',
				header: { 'Content-Type': 'application/json' },
				data: {
					device_id: this.deviceId,
					fallen: false,
					location_source: 'bluetooth',
					event: 'help',
					message: '老人语音求助（蓝牙转发）'
				},
				timeout: 5000,
				success: (res) => {
					const ok = res && res.statusCode >= 200 && res.statusCode < 300
					uni.showToast({ title: ok ? '求助已转发云端' : '求助转发失败', icon: 'none' })
				},
				fail: () => {
					uni.showToast({ title: '求助转发失败', icon: 'none' })
				}
			})
		},

		sendHelpAck() {
			const ackId = this.bleAckCharacteristicId || this.bleAckCharacteristicUUID
			const mainId = this.bleCharacteristicId || this.bleCharacteristicUUID
			const targets = []
			// 主特征已经由固件开放 WRITE，并注册了 ACK 回调；
			// 优先写主特征，避免独立 ACK 特征发现成功但句柄不对。
			if (mainId) targets.push(mainId)
			if (ackId && ackId !== mainId) targets.push(ackId)
			if (!targets.length) {
				this.bleMessage = '求助 ACK 失败：未找到可写特征'
				return
			}
			const buffer = new ArrayBuffer(2)
			const view = new DataView(buffer)
			view.setUint8(0, 0x49)
			view.setUint8(1, 1)
			const attemptWrite = (targetIndex, attempt) => {
				const characteristicId = targets[targetIndex]
				uni.writeBLECharacteristicValue({
					deviceId: this.bleDeviceId,
					serviceId: this.bleServiceId || this.bleServiceUUID,
					characteristicId,
					value: buffer,
					success: () => {
						this.bleMessage = '求助 ACK 已发送'
						console.log('[BLE] 求助ACK写入成功', characteristicId)
					},
					fail: (err) => {
						console.log('[BLE] 求助ACK失败：', targetIndex, attempt + 1, err)
						if (attempt < 1) {
							setTimeout(() => attemptWrite(targetIndex, attempt + 1), 120)
						} else if (targetIndex + 1 < targets.length) {
							attemptWrite(targetIndex + 1, 0)
						} else {
							this.bleMessage = '求助 ACK 写入失败：' + ((err && err.errMsg) || '未知错误')
						}
					}
				})
			}
			attemptWrite(0, 0)
		},

		onBLEValueChange(res) {
			this.parseBleValue(res)
		},

		formatTime() {
			// 手写格式化，避免在 weex 引擎里调用 toLocaleString
			const d = new Date()
			const p = (n) => (n < 10 ? '0' : '') + n
			return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds())
		},

		parseBleValue(res) {
			try {
				if (!res || !res.value) return
				const bytes = new Uint8Array(res.value)
				// 兼容 ESP32 当前发送的 JSON 文本格式
				const textData = Array.from(bytes)
					.map(b => String.fromCharCode(b))
					.join('')
					.trim()
				const binaryFrame =
					bytes[0] === 0x52 ||
					bytes[0] === 0x46 ||
					bytes[0] === 0x48
				if (binaryFrame) {
					// 上一次JSON分包没有收完整时，缓冲区不能吃掉后续二进制R帧。
					this.bleJsonBuffer = ''
				}
				//console.log('[BLE调试] 收到字节数：', bytes.length)
				//console.log('[BLE调试] 收到原始文本：', JSON.stringify(textData))
				
				// ---- JSON 文本格式（ESP32 通过 BLE notify 分包发送）----
				if (!binaryFrame && (textData.startsWith('{') || this.bleJsonBuffer.length > 0)) {
					this.bleJsonBuffer += textData
					if (this.bleJsonBuffer.length > 512) {
						console.log('[BLE] JSON缓冲区过长，清空')
						this.bleJsonBuffer = ''
						return
					}
					// 缓冲区里可能已有多条完整 JSON，逐条取出（要求 JSON 为单层，不含嵌套 }）
					let endIndex = this.bleJsonBuffer.indexOf('}')
					while (endIndex !== -1) {
						const startIndex = this.bleJsonBuffer.indexOf('{')
						if (startIndex === -1 || startIndex > endIndex) {
							this.bleJsonBuffer = this.bleJsonBuffer.substring(endIndex + 1)
						} else {
							const jsonText = this.bleJsonBuffer.substring(startIndex, endIndex + 1)
							this.bleJsonBuffer = this.bleJsonBuffer.substring(endIndex + 1)
							this.handleBLEJson(jsonText)
						}
						endIndex = this.bleJsonBuffer.indexOf('}')
					}
					return
				}

				if (!bytes.length) return

				if (bytes[0] === 0x52 && bytes.length >= 9) {
					const frameSeq = bytes[7] | ((bytes[8] || 0) << 8)
					const isFallenFrame = (bytes[1] & 0x20) !== 0
					if (this.lastBleSeq !== null) {
						if (frameSeq === this.lastBleSeq) {
							this.bleDuplicateCount++
							if (isFallenFrame) this.sendBleAck(frameSeq)
							return
						}
						const forwardDiff = (frameSeq - this.lastBleSeq) & 0xFFFF
						if (forwardDiff > 0x8000) {
							this.bleOutOfOrderCount++
							if (isFallenFrame) this.sendBleAck(frameSeq)
							return
						}
					}
					this.lastBleSeq = frameSeq
					if (isFallenFrame) this.sendBleAck(frameSeq)
					if (this.bleShouldSkip(isFallenFrame)) return

					const flags = bytes[1]
					const wifi = (flags & 0x01) !== 0
					const awake = (flags & 0x08) !== 0
					const waiting = (flags & 0x10) !== 0
					const fallen = (flags & 0x20) !== 0
					const monitoring = (flags & 0x40) !== 0
					const wearingOk = (flags & 0x80) !== 0

					const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
					const accMag = view.getInt16(3, true) / 100
					const gyroMag = view.getInt16(5, true) / 10
					const aiProb = bytes[9] / 100
					const aiDir = (bytes[10] >> 6) & 0x03
					const aiConf = (bytes[10] & 0x3F) / 63
					const now = Date.now()

					if (!Number.isFinite(accMag) || !Number.isFinite(gyroMag)) return
					if (accMag < 0 || accMag > 20 || gyroMag < 0 || gyroMag > 4000) {
						console.log('[BLE] A/G 数据异常，丢弃本帧：', accMag, gyroMag, Array.from(bytes))
						return
					}

					this.lastBleStatusAt = now
					this.bleRealtimeValid = true
					this.blePacketCount++
					this.bleWifiReported = wifi
					this.bleWifiReportedValid = true
					this.wifiStateUpdatedAt = now
					this.fourGActive = (flags & 0x04) !== 0
					this.fourGAwake = awake
					this.waitingFor4G = waiting
					this.fourGCountdown = Number(bytes[2]) || 0

					const wasFallen = this.bleRealtime.fallen
					this.bleRealtime.accMag = accMag.toFixed(2) + ' g'
					this.bleRealtime.gyroMag = gyroMag.toFixed(2) + ' dps'
					this.bleRealtime.aiProb = aiProb
					this.bleRealtime.aiDir = aiDir
					this.bleRealtime.aiConf = aiConf
					this.bleRealtime.wearingOk = wearingOk
					this.bleRealtime.fallen = fallen
					this.bleRealtime.monitoring = monitoring
					this.bleRealtime.updatedAt = now
					this.bleRealtime.updatedAtText = this.formatTime()

					if (this.blePacketCount % 5 === 1) {
						console.log('[BLE] R实时包 seq=', frameSeq, 'A=', accMag, 'G=', gyroMag, 'AI=', aiProb, 'wear=', wearingOk)
					}

					this.forwardBleToCloud(
						0, 0, fallen, wasFallen, monitoring,
						aiProb, aiDir, aiConf, accMag, gyroMag, wearingOk
					)
					return
				}

				if (bytes[0] === 0x48) {
					this.bleMessage = '收到求助帧，正在回 ACK'
					console.log('[BLE] 收到0x48求助帧，立即回ACK')
					// BLE 链路先确认收到求助帧，云端转发失败时再提示用户。
					this.sendHelpAck()
					const helpNow = Date.now()
					// ESP32 会重发 0x48，APP 只处理一次，避免重复云端报警。
					if (helpNow - this.lastBleHelpAt > 3000) {
						this.lastBleHelpAt = helpNow
						this.postBleHelp()
					}
					return
				}

				if (bytes[0] === 0x46 && bytes.length >= 6) {
					if (this.bleShouldSkip(bytes[1] === 1)) return
					const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
					const now = Date.now()
					const wasFallen = this.bleRealtime.fallen
					this.bleRealtime.fallen = bytes[1] === 1
					this.bleRealtime.pitch = (view.getInt16(2, true) / 10).toFixed(2)
					this.bleRealtime.roll = (view.getInt16(4, true) / 10).toFixed(2)
					this.bleRealtime.updatedAt = now
					this.bleRealtime.updatedAtText = this.formatTime()
					this.lastBleStatusAt = now
					this.bleRealtimeValid = true

					// 跌倒通知包同样要中转到云端，否则蓝牙链路下的报警家人端收不到
					this.forwardBleToCloud(
						view.getInt16(2, true) / 10,
						view.getInt16(4, true) / 10,
						this.bleRealtime.fallen,
						wasFallen,
						this.bleRealtime.monitoring
					)

					return
				}
			} catch (e) {
				console.log('[BLE] 数据解析失败：', e)
			}
		},

		// ===================================================
		// 云端报警：只识别新报警/最近刚发生的报警。
		// ===================================================
		fetchCloudAlarm() {
			if (this.cloudAlarmRequestInFlight) return
			this.cloudAlarmRequestInFlight = true

			uni.request({
				url: this.cloudBaseUrl + '/api/alarm/latest?device_id=' +
					encodeURIComponent(this.deviceId) + '&_ts=' + Date.now(),
				method: 'GET',
				timeout: 5000,
				header: { 'Cache-Control': 'no-cache' },
				success: (res) => {
					this.cloudConnected = res.statusCode >= 200 && res.statusCode < 300
					if (!this.cloudConnected) return
					if (!res.data || res.data.success !== true || !res.data.data) return

					const data = res.data.data
					this.cloudFallen = Boolean(data.fallen)
					this.cloudPitch = data.pitch == null ? '0.00' : Number(data.pitch).toFixed(2)
					this.cloudRoll = data.roll == null ? '0.00' : Number(data.roll).toFixed(2)
					this.cloudAiProb = data.event_ai_prob != null ? Number(data.event_ai_prob) : (data.ai_prob != null ? Number(data.ai_prob) : null)
					this.cloudTime = data.time || ''
					this.cloudLatitude = data.latitude == null ? null : data.latitude
					this.cloudLongitude = data.longitude == null ? null : data.longitude
					this.cloudLocationSource = data.location_source || null

					const signature = [
						data.id == null ? '' : data.id,
						data.time || '',
						data.location_source || '',
						data.latitude == null ? '' : data.latitude,
						data.longitude == null ? '' : data.longitude,
						data.pitch == null ? '' : data.pitch,
						data.roll == null ? '' : data.roll,
						data.fallen ? '1' : '0'
					].join('|')

					const firstLoad = !this.alarmHistoryInitialized
					const isNew = !firstLoad && signature !== this.lastAlarmSignature
					const recent = Boolean(data.fallen) && this.isRecentAlarmTime(data.time)

					this.lastAlarmSignature = signature
					this.alarmHistoryInitialized = true

					if ((isNew || (firstLoad && recent)) && data.fallen) {
						this.currentAlarmSource = data.location_source || ''
						if (this.currentAlarmSource === '4g') {
							this.fourGActive = true
							this.fourGAwake = true
						}
						console.log('[云端报警] 新报警：', this.currentAlarmSource, data.time)
					}
				},
				fail: (err) => {
					this.cloudConnected = false
					console.log('[云端报警] 获取失败：', err)
					uni.showToast({
						title:"❌无法连接云端",
						icon:"none",
						duration:1200
					})
				},
				complete: () => {
					this.cloudAlarmRequestInFlight = false
				}
			})
		},

		isRecentAlarmTime(value) {
			if (!value) return false
			const t = Date.parse(String(value).replace(' ', 'T'))
			if (!Number.isFinite(t)) return false
			const age = Date.now() - t
			return age >= -10000 && age <= 70000
		},

		// ===================================================
		// 云端设备状态：主要负责WiFi状态和WiFi实时数据。
		// BLE连接并不会阻止这里更新。
		// ===================================================

		async fetchDeviceStatus() {
		  if (this.deviceStatusRequestInFlight) return;
		  this.deviceStatusRequestInFlight = true;
		  try {
		    const rawRes = await uni.request({
		      url: this.cloudBaseUrl + '/api/device/status?device_id=' +
		        encodeURIComponent(this.deviceId) + '&_ts=' + Date.now(),
		      method: 'GET',
		      timeout: 5000,
		      header: { 'Cache-Control': 'no-cache' }
		    })
		    const res = Array.isArray(rawRes) ? rawRes[1] : rawRes
		    if (!res) { this.updateDeviceStatusFresh(null); return }
		    if (res.statusCode < 200 || res.statusCode >= 300) {
		      this.updateDeviceStatusFresh(null);
		      return;
		    }
		    if (!res?.data || res.data.success !== true || !res.data.data) {
		      this.updateDeviceStatusFresh(null);
		      return;
		    }
		    const data = res.data.data;
		    // ========== 设备WiFi状态、wifi实时数据赋值 ==========
		    this.wifiConnected = Boolean(data?.wifi_connected);
		    this.wifiRealtime.pitch = data?.pitch == null ? '—' : Number(data.pitch).toFixed(2);
		    this.wifiRealtime.roll = data?.roll == null ? '—' : Number(data.roll).toFixed(2);
		    this.wifiRealtime.accMag = data?.acc_mag == null ? '—' : Number(data.acc_mag).toFixed(2) + ' g';
		    this.wifiRealtime.gyroMag = data?.gyro_mag == null ? '—' : Number(data.gyro_mag).toFixed(2) + ' dps';
		    this.wifiRealtime.aiProb = data?.ai_prob == null ? null : Number(data.ai_prob);
		    this.wifiRealtime.wearingOk = data?.wearing_ok;
		    if (data?.battery_available === true && data?.battery_percent != null) {
		      const pct = Math.max(0, Math.min(100, Number(data.battery_percent)))
		      const voltage = Number(data.battery_voltage)
		      this.batteryMissCount = 0
		      let charging = data?.battery_charging === true || data?.charging === true
		      const lastPercent = this.lastBatteryPercent
		      if (!charging && Number.isFinite(pct) && lastPercent !== null && pct > lastPercent) {
		        this.batteryChargingUntil = Date.now() + 30 * 60 * 1000
		      }
		      this.lastBatteryPercent = pct
		      if (!charging && Number.isFinite(voltage)) {
		        if (this.lastBatteryVoltage !== null) {
		          const delta = voltage - this.lastBatteryVoltage
		          if (delta >= 0.02) {
		            this.batteryRiseCount += 1
		            if (this.batteryRiseCount >= 2) {
		              this.batteryChargingUntil = Date.now() + 30 * 60 * 1000
		            }
		          } else if (delta <= -0.03) {
		            this.batteryChargingUntil = 0
		            this.batteryRiseCount = 0
		          } else {
		            this.batteryRiseCount = 0
		          }
		        }
		        this.lastBatteryVoltage = voltage
		        charging = Date.now() < this.batteryChargingUntil
		      }
		      this.batteryText = charging ? '正在充电' : (pct <= 20 ? '电量不足' : pct + '%')
		      this.batteryPercent = pct
		      this.batteryCharging = charging
		      this.batteryLow = !charging && pct <= 20
		    } else {
		      // 偶发一次读取失败时保留上次值，连续 3 次失败才显示未检测。
		      this.batteryMissCount += 1
		      if (this.batteryMissCount >= 3) {
		        this.batteryText = '未检测'
		        this.batteryPercent = 0
		        this.batteryCharging = false
		        this.batteryLow = false
		        this.lastBatteryVoltage = null
		        this.lastBatteryPercent = null
		        this.batteryRiseCount = 0
		      }
		    }

		    this.wifiRealtime.fallen = Boolean(data?.fallen);
		    this.wifiRealtime.monitoring = Boolean(data?.monitoring);
		    const serverTs = data?.time || '';
		    this.wifiRealtime.updatedAt = 0; // 稍后由 updateDeviceStatusFresh 之后统一设置
		    this.wifiRealtime.updatedAtText = serverTs || '';
		
		    this.updateDeviceStatusFresh(data);
		    this.wifiRealtime.updatedAt = this.deviceStatusLastChangedAt;
		
		  } catch (err) {
		    console.error("fetchDeviceStatus异常：", err)
		    this.updateDeviceStatusFresh(null);
		  } finally {
		    this.deviceStatusRequestInFlight = false;
		  }
		},
		
		updateDeviceStatusFresh(data) {
		  const now = Date.now()
		  const rawSeq = data ? data.status_seq : null
		
		  if (rawSeq !== undefined && rawSeq !== null && rawSeq !== '') {
		    const seq = Number(rawSeq)
		    if (Number.isFinite(seq)) {
		      this.statusSeqAvailable = true
		      if (this.lastStatusSeq === null || seq !== this.lastStatusSeq) {
		        this.lastStatusSeq = seq
		        this.deviceStatusLastChangedAt = now
		      }
		      this.deviceStatusFresh = this.deviceStatusLastChangedAt > 0 && (now - this.deviceStatusLastChangedAt < 20000)
		      return
		    }
		  }
		
		  const serverTime = data ? (data.updated_at || data.timestamp || data.time) : ''
		  const parsed = serverTime ? Date.parse(String(serverTime).replace(' ', 'T')) : NaN
		  if (Number.isFinite(parsed)) {
		    const oldParsed = this.lastStatusServerTime ? Date.parse(String(this.lastStatusServerTime).replace(' ', 'T')) : NaN
		    if (!Number.isFinite(oldParsed) || parsed > oldParsed) {
		      this.lastStatusServerTime = String(serverTime)
		      this.deviceStatusLastChangedAt = now
		    }
		  }
		  this.deviceStatusFresh = this.deviceStatusLastChangedAt > 0 && (now - this.deviceStatusLastChangedAt < 20000)
		},

		sendCommand(action) {
			if (this.commandSending) return
			this.commandSending = true
			this.commandMessage = '正在发送云端控制命令...'

			uni.request({
				url: this.cloudBaseUrl + '/api/command',
				method: 'POST',
				header: { 'Content-Type': 'application/json' },
				data: { device_id: this.deviceId, command: action },
				timeout: 5000,
				success: (res) => {
					if (res.statusCode >= 200 && res.statusCode < 300 && res.data && res.data.success === true) {
						this.commandMessage = this.monitorPending
							? '✓ 命令已发送，等待设备响应'
							: '✓ 命令已发送到云端'
						uni.showToast({ title: '命令已发送', icon: 'success' })
					}
					else {
						this.commandMessage = '✗ 云端命令发送失败'
						uni.showToast({ title: '云端控制失败', icon: 'none' })
						this.abortMonitorChange()
					}
				},
				fail: (err) => {
					console.log('[云端控制] 请求失败：', err)
					this.commandMessage = '✗ 无法连接云端服务器'
					uni.showToast({ title: '云端连接失败', icon: 'none' })
					this.abortMonitorChange()
				},
				complete: () => {
					this.commandSending = false
				}
			})
		},

		openMonitor() {
			this.requestMonitorChange(true)
		},

		closeMonitor() {
			this.requestMonitorChange(false)
		},

		// 一个按钮切换：未监测时开启，监测中则关闭
		toggleMonitor() {
			this.requestMonitorChange(!this.monitoringActive)
		},

		// 下发监测开关：先在本地切换（按钮立刻响应），再等设备回传确认。
		// 之前完全依赖 4 秒轮询，加上设备取命令的间隔，要等十秒左右才变。
		requestMonitorChange(target) {
			if (this.commandSending || this.monitorPending) return

			this.monitorTarget = target === true
			this.monitorPending = true
			this.monitoringActive = this.monitorTarget
			this.commandMessage = this.monitorTarget ? '正在开启监测...' : '正在关闭监测...'

			this.sendCommand(this.monitorTarget ? 'start_monitoring' : 'stop_monitoring')
			this.startMonitorConfirm()
		},

		// 命令发出后高频拉几次设备状态，尽快拿到真实结果
		startMonitorConfirm() {
			this.clearMonitorConfirmTimer()
			let tries = 0
			const tick = async () => {
				if (!this.monitorPending) return
				tries += 1
				await this.fetchDeviceStatus()
				this.syncCurrentView()
				if (!this.monitorPending) return
				if (tries >= 15) {
					this.abortMonitorChange()
					uni.showToast({
						title: '设备未响应，请检查设备连接',
						icon: 'none'
					})
					return
				}
				this.monitorConfirmTimer = setTimeout(tick, 1000)
			}
			this.monitorConfirmTimer = setTimeout(tick, 800)
		},

		clearMonitorConfirmTimer() {
			if (this.monitorConfirmTimer) {
				clearTimeout(this.monitorConfirmTimer)
				this.monitorConfirmTimer = null
			}
		},

		// 失败或超时：回退到点击前的状态
		abortMonitorChange() {
			if (!this.monitorPending) return
			this.monitorPending = false
			this.monitoringActive = !this.monitorTarget
			this.clearMonitorConfirmTimer()
		},

		// 右上角小房子：进入老人端个人中心
		goProfile() {
			uni.navigateTo({ url: '/pages/index/profile' })
		},

		goRemind(){
			uni.navigateTo({
				url:"/pages/remind/remind"
			})
		},
		logout(){
			uni.showModal({
				title:"提示",
				content:"确定要退出登录吗？",
				success(res){
					if(res.confirm){
						// 注意！你登录页存的是 isLogin 和 userRole 吗？
						// 你登录页面保存的是 isLogin / userRole，和登录页保持一致
						uni.removeStorageSync("isLogin")
						uni.removeStorageSync("userRole")
						uni.reLaunch({
							url:"/pages/login/login"
						})
					}
				}
			})
		}
	}
}
</script>


<style scoped>

.container {

	padding:
		30rpx;

	background:
		#f5f6f8;

	min-height:
		100vh;

	box-sizing:
		border-box;

}


.title {

	font-size:
		40rpx;

	font-weight:
		bold;

	text-align:
		center;

	margin-bottom:
		30rpx;

	color:
		#222;

}


.connection-card {

	padding:
		28rpx;

	border-radius:
		18rpx;

	margin-bottom:
		22rpx;

}


.connection-card.connected {

	background:
		#e8f7ed;

}


.connection-card.disconnected {

	background:
		#fde8e8;

}


.connection-title {

	font-size:
		24rpx;

	color:
		#666;

	margin-bottom:
		12rpx;

}


.connection-main {

	font-size:
		34rpx;

	font-weight:
		bold;

}


.connection-detail {

	font-size:
		22rpx;

	color:
		#888;

	margin-top:
		10rpx;

}


.card {

	background:
		white;

	border-radius:
		18rpx;

	padding:
		28rpx;

	margin-bottom:
		22rpx;

}


.section-title {

	font-size:
		30rpx;

	font-weight:
		bold;

	margin-bottom:
		22rpx;

}


.status-row {

	display:
		flex;

	justify-content:
		space-between;

	font-size:
		28rpx;

	padding:
		14rpx 0;

	border-bottom:
		1rpx solid #eeeeee;

}


.online {

	color:
		#19a15f;

	font-weight:
		bold;

}


.offline {

	color:
		#999;

}


.route-info {

	margin-top:
		20rpx;

	padding:
		18rpx;

	background:
		#f4f6f8;

	border-radius:
		12rpx;

	font-size:
		26rpx;

}


.ble-status {

	padding:
		20rpx;

	border-radius:
		12rpx;

	text-align:
		center;

	font-size:
		30rpx;

	font-weight:
		bold;

	margin-bottom:
		15rpx;

}


.ble-on {

	background:
		#e8f7ed;

	color:
		#19a15f;

}


.ble-off {

	background:
		#eeeeee;

	color:
		#777;

}


.ble-device {

	font-size:
		24rpx;

	color:
		#888;

	margin-bottom:
		15rpx;

}


.ble-message {

	font-size:
		24rpx;

	color:
		#666;

	text-align:
		center;

	margin-top:
		15rpx;

}


.ble-auto-tip {

	font-size:
		22rpx;

	color:
		#999;

	line-height:
		1.6;

	margin-top:
		18rpx;

	text-align:
		center;

}


.countdown {

	margin-top:
		18rpx;

	padding:
		20rpx;

	background:
		#fff4df;

	border-radius:
		12rpx;

	text-align:
		center;

	font-size:
		26rpx;

	color:
		#d98500;

}


.countdown-number {

	font-size:
		42rpx;

	font-weight:
		bold;

	margin-top:
		8rpx;

}


.alarm-card {

	padding:
		30rpx;

	border-radius:
		18rpx;

	margin-bottom:
		22rpx;

}


.alarm-normal {

	background:
		#eaf7ee;

}


.alarm-danger {

	background:
		#fde8e6;

}


.alarm-title {

	font-size:
		24rpx;

	color:
		#666;

	margin-bottom:
		12rpx;

}


.alarm-main {

	font-size:
		36rpx;

	font-weight:
		bold;

	margin-bottom:
		18rpx;

}


.alarm-data {

	font-size:
		28rpx;

	line-height:
		1.8;

}


.location {

	font-size:
		26rpx;

	margin-top:
		14rpx;

	line-height:
		1.7;

}


.alarm-time {

	font-size:
		22rpx;

	color:
		#999;

	margin-top:
		18rpx;

}


.monitor-status {

	font-size:
		34rpx;

	font-weight:
		bold;

	text-align:
		center;

	padding:
		20rpx;

	border-radius:
		12rpx;

}


.monitor-on {

	background:
		#e8f7ed;

	color:
		#19a15f;

}


.monitor-off {

	background:
		#eeeeee;

	color:
		#777;

}


button {

	margin-top:
		20rpx;

}


.command-message {

	text-align:
		center;

	margin-top:
		25rpx;

	font-size:
		26rpx;

	color:
		#666;

}


.device-info {

	text-align:
		center;

	color:
		#999;

	font-size:
		23rpx;

	margin-top:
		30rpx;

}

.logout-box{
  margin-top: 60rpx;
}
.logout-btn{
  background-color: #ff4d4f;
  color: #ffffff;
}
.remind-wrap{
	position: relative;
	margin-bottom: 22rpx;
}

.status-key{
	color: #333;
}

.badge{
	position:absolute;
	top:-10rpx;
	right:10rpx;
	background:#f56c6c;
	color:#fff;
	font-size:20rpx;
	border-radius:50%;
	width:36rpx;
	height:36rpx;
	text-align:center;
	line-height:36rpx;
	z-index:999;  /* 改成999，提高层级，防止被遮挡 */
}
/* ==========================================================
   视觉升级：统一卡片、状态、按钮的观感
   （下方规则覆盖上面旧样式，功能不受影响）
   ========================================================== */

.container {
	padding: 40rpx 30rpx 80rpx;
	background: #F4F6FA;
	min-height: 100vh;
	box-sizing: border-box;
}

.title {
	font-size: 42rpx;
	font-weight: 700;
	color: #1B2233;
	text-align: left;
	margin: 0 0 30rpx 6rpx;
	letter-spacing: 1rpx;
}

.card {
	background: #FFFFFF;
	border-radius: 26rpx;
	padding: 30rpx;
	margin-bottom: 26rpx;
	box-shadow: 0 6rpx 24rpx rgba(31, 45, 74, 0.05);
}

.section-title {
	display: flex;
	align-items: center;
	font-size: 30rpx;
	font-weight: 700;
	color: #1B2233;
	margin-bottom: 6rpx;
}

.section-title::before {
	content: '';
	width: 8rpx;
	height: 30rpx;
	background: #007AFF;
	border-radius: 4rpx;
	margin-right: 14rpx;
}

.status-row {
	display: flex;
	align-items: center;
	justify-content: space-between;
	font-size: 27rpx;
	color: #4A5364;
	padding: 22rpx 0;
	border-bottom: 1rpx solid #F4F6FA;
}

.status-key {
	color: #4A5364;
}

.online {
	color: #12B76A;
	font-weight: 600;
}

.offline {
	color: #98A0AE;
}

.route-info {
	margin-top: 22rpx;
	padding: 20rpx 22rpx;
	background: #F5F8FF;
	border-radius: 16rpx;
	font-size: 26rpx;
	color: #4A5364;
}

.route-info strong {
	color: #007AFF;
	font-weight: 600;
}

.connection-card {
	padding: 28rpx 30rpx;
	border-radius: 26rpx;
	margin-bottom: 26rpx;
	box-shadow: 0 6rpx 24rpx rgba(31, 45, 74, 0.05);
}

.connection-card.connected {
	background: #ECFAF2;
}

.connection-card.disconnected {
	background: #FDEDED;
}

.connection-main {
	font-size: 31rpx;
	font-weight: 700;
	color: #1B2233;
}

.connection-detail {
	margin-top: 8rpx;
	font-size: 22rpx;
	color: #8A93A3;
}

.ble-status {
	padding: 24rpx;
	border-radius: 18rpx;
	text-align: center;
	font-size: 29rpx;
	font-weight: 600;
	margin-bottom: 18rpx;
}

.ble-on {
	background: #ECFAF2;
	color: #12B76A;
}

.ble-off {
	background: #F2F4F8;
	color: #8A93A3;
}

.ble-device {
	font-size: 26rpx;
	color: #4A5364;
	text-align: center;
	margin-bottom: 22rpx;
}

.ble-auto-tip {
	font-size: 22rpx;
	color: #98A0AE;
	line-height: 1.7;
	margin-top: 16rpx;
}

.ble-message {
	margin-top: 14rpx;
	font-size: 24rpx;
	color: #E6A23C;
}

.countdown {
	margin-top: 18rpx;
	text-align: center;
	font-size: 24rpx;
	color: #E6A23C;
}

.countdown-number {
	font-size: 34rpx;
	font-weight: 700;
}

.command-message {
	margin: 6rpx 0 20rpx;
	font-size: 24rpx;
	color: #8A93A3;
	text-align: center;
}

.device-info {
	text-align: center;
	color: #B4BAC6;
	font-size: 22rpx;
	margin-top: 26rpx;
}

.logout-box {
	margin-top: 50rpx;
}

.logout-btn {
	background: #FFFFFF;
	color: #F04F4F;
	border: 2rpx solid #FFE4E4;
	font-weight: 500;
}

.remind-wrap {
	position: relative;
	margin-bottom: 22rpx;
}

.badge {
	position: absolute;
	top: -12rpx;
	right: 12rpx;
	background: #F04F4F;
	color: #FFFFFF;
	font-size: 20rpx;
	border-radius: 50%;
	width: 36rpx;
	height: 36rpx;
	text-align: center;
	line-height: 36rpx;
	z-index: 999;
}
/* ==========================================================
   新版视觉：居中顶栏 + 渐变主卡 + 四联状态 + 姿态卡
   ========================================================== */
.container {
	padding: 0 30rpx 80rpx;
	background: #F4F6FA;
	min-height: 100vh;
	box-sizing: border-box;
}

.app-header {
	box-sizing: content-box;
	padding-top: var(--status-bar-height);
	height: 150rpx;
	margin-bottom: 34rpx;
	display: flex;
	align-items: center;
	justify-content: space-between;
}

.app-header-title {
	flex: 1;
	text-align: center;
	font-size: 52rpx;
	line-height: 150rpx;
	font-weight: 700;
	color: #1B2233;
	letter-spacing: 1rpx;
}

.app-header-spacer {
	width: 64rpx;
	height: 64rpx;
	flex-shrink: 0;
}

.app-header-btn {
	width: 64rpx;
	height: 64rpx;
	border-radius: 50%;
	background: #FFFFFF url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%231B2233' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M3.2 10.6 12 3.4l8.8 7.2'/%3E%3Cpath d='M5.6 9.6V20a.9.9 0 0 0 .9.9h11a.9.9 0 0 0 .9-.9V9.6'/%3E%3Cpath d='M9.8 20.9v-5.4h4.4v5.4'/%3E%3C/svg%3E") no-repeat center;
	background-size: 36rpx 36rpx;
	box-shadow: 0 4rpx 14rpx rgba(31, 45, 74, 0.10);
}

.hero {
	display: flex;
	flex-direction: column;
	padding: 34rpx 32rpx 28rpx;
	border-radius: 34rpx;
	margin-bottom: 26rpx;
	background: linear-gradient(135deg, #4A7CFF 0%, #57C9F2 100%);
	box-shadow: 0 16rpx 36rpx rgba(74, 124, 255, 0.28);
}

.hero-top {
	display: flex;
	align-items: center;
}

/* 主卡里的电量条 */
.hero-battery {
	margin-top: 22rpx;
	padding-top: 20rpx;
	border-top: 1rpx solid rgba(255, 255, 255, 0.24);
}

.hb-head {
	display: flex;
	align-items: baseline;
	justify-content: space-between;
	margin-bottom: 12rpx;
}

.hb-label {
	font-size: 23rpx;
	color: rgba(255, 255, 255, 0.82);
}

.hb-value {
	font-size: 30rpx;
	font-weight: 700;
	color: #FFFFFF;
}

.hb-value.low {
	color: #FFE08A;
}

.hb-bar {
	height: 14rpx;
	border-radius: 999rpx;
	background: rgba(255, 255, 255, 0.24);
	overflow: hidden;
}

.hb-fill {
	height: 100%;
	border-radius: 999rpx;
	background: #FFFFFF;
	transition: width 0.3s ease;
}

.hb-fill.low {
	background: #FF4D4F;
}

.hb-fill.charging {
	background: #22C55E;
}

.hb-value.low {
	color: #FFB4B4;
}

.hb-value.charging {
	color: #7CF7A8;
}

.hb-tip {
	display: block;
	margin-top: 8rpx;
	font-size: 22rpx;
}

.hb-tip.low {
	color: #FFB4B4;
}

.hb-tip.charging {
	color: #7CF7A8;
}

.hero-main {
	flex: 1;
	min-width: 0;
	color: #FFFFFF;
}

.hero-name {
	font-size: 44rpx;
	font-weight: 700;
	line-height: 1.3;
}

.hero-status {
	font-size: 28rpx;
	opacity: 0.92;
	margin-top: 10rpx;
}

.hero-badge {
	font-size: 24rpx;
	padding: 10rpx 24rpx;
	border-radius: 999rpx;
	background: rgba(255, 255, 255, 0.24);
	color: #FFFFFF;
	flex-shrink: 0;
	margin-left: 16rpx;
}

.chips {
	display: flex;
	background: #FFFFFF;
	border-radius: 32rpx;
	padding: 28rpx 8rpx;
	margin-bottom: 26rpx;
	box-shadow: 0 10rpx 30rpx rgba(31, 45, 74, 0.07);
}

.chip-item {
	flex: 1;
	display: flex;
	flex-direction: column;
	align-items: center;
}

.chip-dot {
	width: 16rpx;
	height: 16rpx;
	border-radius: 50%;
	margin-bottom: 12rpx;
}

.dot-on {
	background: #12B76A;
}

.dot-off {
	background: #C9D0DC;
}

.chip-name {
	font-size: 27rpx;
	font-weight: 600;
	color: #1B2233;
}

.chip-state {
	font-size: 21rpx;
	color: #98A0AE;
	margin-top: 6rpx;
}

.route-info {
	margin: 0 0 26rpx;
	padding: 22rpx 26rpx;
	background: #FFFFFF;
	border-radius: 24rpx;
	font-size: 26rpx;
	color: #5C6675;
	box-shadow: 0 6rpx 20rpx rgba(31, 45, 74, 0.05);
}

.route-info strong {
	color: #2F6FED;
	font-weight: 700;
}

.posture-card {
	padding: 30rpx 28rpx;
	margin-bottom: 26rpx;
}

.card-head {
	display: flex;
	align-items: center;
	margin-bottom: 26rpx;
}

.head-icon {
	width: 44rpx;
	height: 44rpx;
	border-radius: 14rpx;
	margin-right: 12rpx;
	background: #E8F0FF url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%232F6FED' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 3.5 20 8v8l-8 4.5L4 16V8z'/%3E%3Cpath d='M4 8l8 4.5 8-4.5'/%3E%3Cpath d='M12 12.5V20.5'/%3E%3C/svg%3E") no-repeat center;
	background-size: 26rpx 26rpx;
}

.card-title {
	font-size: 30rpx;
	font-weight: 700;
	color: #1B2233;
}

.posture-row {
	display: flex;
	align-items: center;
}

.gauge {
	position: relative;
	width: 180rpx;
	height: 180rpx;
	margin-right: 30rpx;
	flex-shrink: 0;
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

.kv-list {
	flex: 1;
	min-width: 0;
}

.kv {
	display: flex;
	align-items: center;
	justify-content: space-between;
	padding: 14rpx 0;
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

/* 老人端按钮：加大高度与字号 */
button {
	height: 104rpx;
	line-height: 104rpx;
	font-size: 34rpx;
	border-radius: 18rpx;
}

.remind-wrap button {
	border-radius: 999rpx;
	background: linear-gradient(135deg, #4A7CFF 0%, #2F6FED 100%);
	color: #FFFFFF;
	font-weight: 700;
	box-shadow: 0 10rpx 24rpx rgba(47, 111, 237, 0.28);
}

.device-info {
	text-align: center;
	color: #B4BAC6;
	font-size: 26rpx;
	margin-top: 34rpx;
}

/* 适老化：只放大可读文字，卡片间距和布局保持不变 */
.container { font-size: 34rpx; }
.hero-name { font-size: 54rpx; }
.hero-status { font-size: 34rpx; }
.hero-sub { font-size: 29rpx; }
.connection-main { font-size: 38rpx; }
.connection-detail { font-size: 28rpx; }
.card-title { font-size: 36rpx; }
.section-title { font-size: 34rpx; }
.chip-name { font-size: 32rpx; }
.chip-state { font-size: 28rpx; }
.k { font-size: 32rpx; }
.v { font-size: 34rpx; }
.ble-status { font-size: 34rpx; }
.ble-device { font-size: 32rpx; }
.hb-label { font-size: 27rpx; }
.hb-value { font-size: 34rpx; }
.hb-tip { font-size: 25rpx; }
.device-info { font-size: 29rpx; }

</style>
