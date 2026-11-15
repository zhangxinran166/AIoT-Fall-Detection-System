<template>
	<view class="container">
		<!-- 顶部说明 -->
		<view class="hero">
			<view class="hero-icon">✦</view>
			<view class="hero-body">
				<view class="hero-title">AI 助手</view>
				<view class="hero-sub">{{ elderName || '老人' }} · 设备 {{ deviceId }}</view>
			</view>
			<view class="hero-refresh" :class="{ spin: loading }" @click="refreshData(true)">↻</view>
		</view>

		<!-- 对话区 -->
		<scroll-view
			class="chat"
			scroll-y
			:scroll-into-view="scrollTarget"
			scroll-with-animation
		>
			<view
				v-for="(m, i) in messages"
				:key="i"
				:id="'msg' + i"
				class="msg-wrap"
			>
				<view v-if="showMsgTime(i)" class="msg-time">{{ formatMsgTime(m.time) }}</view>
				<view class="row" :class="m.role">
					<view class="avatar" :class="m.role === 'ai' ? 'ai' : 'me'">
						{{ m.role === 'ai' ? '✦' : '我' }}
					</view>
					<view class="bubble" :class="m.role">
						<text class="bubble-text">{{ m.text }}</text>
					</view>
				</view>
			</view>

			<view v-if="thinking" class="row ai">
				<view class="avatar ai">✦</view>
				<view class="bubble ai thinking">
					<view class="dot"></view>
					<view class="dot"></view>
					<view class="dot"></view>
				</view>
			</view>
		</scroll-view>

		<!-- 快捷提问 -->
		<scroll-view class="chips" scroll-x>
			<view
				v-for="s in suggestions"
				:key="s"
				class="chip"
				@click="ask(s)"
			>{{ s }}</view>
		</scroll-view>

		<!-- 输入栏 -->
		<view class="input-bar">
			<input
				class="input"
				v-model="draft"
				placeholder="问问老人现在怎么样…"
				confirm-type="send"
				@confirm="ask()"
			/>
			<view class="send" :class="{ on: draft.trim() }" @click="ask()">发送</view>
		</view>

		<tab-bar active="ai"></tab-bar>
	</view>
</template>

<script>
import TabBar from '../../components/tab-bar/tab-bar.vue'
import { SUGGESTIONS, buildAnswer } from '../../utils/ai.js'
import { CLOUD_BASE_URL } from '../../utils/alarm.js'

const AMAP_KEY = '0aa5d7e746a27aebcfc54f1786c918f7'

export default {
	components: { TabBar },
	data() {
		return {
			cloudBaseUrl: CLOUD_BASE_URL,
			deviceId: '',
			elderName: '',
			draft: '',
			thinking: false,
			loading: false,
			scrollTarget: '',
			suggestions: SUGGESTIONS,
			messages: [],
			ctx: {
				status: {},
				location: {},
				alarms: {}
			}
		}
	},
	onLoad() {
		this.deviceId = uni.getStorageSync('deviceId') || ''
		this.elderName = uni.getStorageSync('elderName') || ''
		this.loadHistory()
		this.refreshData()
	},
	onUnload() {
		this.saveHistory()
	},
	methods: {
		historyKey() {
			return 'aiChatHistory_' + (this.deviceId || 'default')
		},

		loadHistory() {
			const saved = uni.getStorageSync(this.historyKey())
			if (Array.isArray(saved) && saved.length) {
				this.messages = saved.filter(m => m && m.role && m.text).slice(-40).map(m => ({
					...m,
					time: Number(m.time) || Date.now()
				}))
			} else {
				this.messages = [{
					role: 'ai',
					time: Date.now(),
					text: `你好，我是${this.elderName || '老人'}的监测助手。\n可以问我：老人现在怎么样、在哪里、最近有没有跌倒。`
				}]
			}
			this.scrollToBottom()
		},

		saveHistory() {
			if (!Array.isArray(this.messages) || !this.messages.length) return
			uni.setStorageSync(this.historyKey(), this.messages.slice(-40))
		},

		showMsgTime(index) {
			if (index === 0) return true
			const prev = this.messages[index - 1]
			const cur = this.messages[index]
			if (!prev || !cur) return false
			return Number(cur.time) - Number(prev.time) > 5 * 60 * 1000
		},

		formatMsgTime(ts) {
			const d = new Date(Number(ts) || Date.now())
			const pad = (n) => String(n).padStart(2, '0')
			const now = new Date()
			const sameDay = d.getFullYear() === now.getFullYear() &&
				d.getMonth() === now.getMonth() &&
				d.getDate() === now.getDate()
			const hm = pad(d.getHours()) + ':' + pad(d.getMinutes())
			if (sameDay) return hm
			const yesterday = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 1)
			const isYesterday = d.getFullYear() === yesterday.getFullYear() &&
				d.getMonth() === yesterday.getMonth() &&
				d.getDate() === yesterday.getDate()
			if (isYesterday) return '昨天 ' + hm
			return (d.getMonth() + 1) + '月' + d.getDate() + '日 ' + hm
		},

		token() {
			return uni.getStorageSync('token') || ''
		},

		request(url, method, data, timeout) {
			return new Promise((resolve) => {
				uni.request({
					url,
					method: method || 'GET',
					data: data || {},
					header: {
						'Authorization': 'Bearer ' + this.token(),
						'Content-Type': 'application/json'
					},
					timeout: timeout || 10000,
					success: resolve,
					fail: () => resolve(null)
				})
			})
		},

		// 拉取最新数据：设备状态 / 位置 / 告警
		async refreshData(showToast) {
			if (!this.deviceId) return
			this.loading = true
			const did = encodeURIComponent(this.deviceId)
			try {
				const [st, loc, alarm] = await Promise.all([
					this.request(this.cloudBaseUrl + '/api/device/status?device_id=' + did + '&_ts=' + Date.now()),
					this.request(this.cloudBaseUrl + '/api/device/status?device_id=' + did + '&_ts=' + Date.now()),
					this.request(this.cloudBaseUrl + '/api/alarm/history?device_id=' + did)
				])

				// 设备状态
				if (st && st.data && st.data.success && st.data.data) {
					const d = st.data.data
					this.ctx.status = {
						pitch: d.pitch == null ? null : Number(d.pitch).toFixed(2),
						roll: d.roll == null ? null : Number(d.roll).toFixed(2),
						accMag: d.acc_mag == null ? null : Number(d.acc_mag).toFixed(2),
						gyroMag: d.gyro_mag == null ? null : Number(d.gyro_mag).toFixed(2),
						wearingOk: d.wearing_ok === true,
						fallen: Boolean(d.fallen),
						monitoring: Boolean(d.monitoring),
						wifi: d.wifi === true,
						bluetooth: d.bluetooth === true,
						g4: d['4g'] === true,
						batteryAvailable: Boolean(d.battery_available),
						batteryPercent: d.battery_percent,
						batteryLow: Boolean(d.battery_low),
						time: d.time || null
					}
				}

				// 告警
				if (alarm && alarm.data && alarm.data.success) {
					const list = Array.isArray(alarm.data.data) ? alarm.data.data : []
					this.ctx.alarms = {
						total: list.length,
						lastTime: list.length ? (list[0].time || '') : '',
						pending: list.filter((x) => x.fallen).length
					}
				}

				// 位置（走家人端的定位接口）
				const lr = await this.request(
					this.cloudBaseUrl + '/api/location?device_id=' + did + '&_ts=' + Date.now())
				if (lr && lr.data && lr.data.success && lr.data.data) {
					const l = lr.data.data
					const lat = Number(l.latitude)
					const lng = Number(l.longitude)
					const ok = isFinite(lat) && isFinite(lng) && !(lat === 0 && lng === 0)
					this.ctx.location = {
						ok,
						lat,
						lng,
						accuracy: l.accuracy,
						time: l.time || '',
						address: l.address || '',
						sourceText: this.sourceLabel(l.location_source || l.source)
					}
					if (ok && !l.address) this.geocode(lat, lng)
				}
			} finally {
				this.loading = false
				if (showToast) uni.showToast({ title: '数据已更新', icon: 'none' })
			}
		},

		sourceLabel(src) {
			if (src === '4g') return '4G 芯片定位'
			if (src === 'phone') return '老人手机定位'
			if (src === 'wifi') return '手机定位（WiFi）'
			if (src === 'bluetooth') return '手机定位（蓝牙）'
			return '未知'
		},

		// 经纬度转地址（有 Key 才转）
		geocode(lat, lng) {
			if (!AMAP_KEY) return
			uni.request({
				url: 'https://restapi.amap.com/v3/geocode/regeo',
				data: { key: AMAP_KEY, location: lng.toFixed(6) + ',' + lat.toFixed(6), extensions: 'base' },
				timeout: 8000,
				success: (res) => {
					const d = res && res.data
					if (!d || d.status !== '1' || !d.regeocode) return
					const ac = d.regeocode.addressComponent || {}
					const s = (v) => (typeof v === 'string' ? v : '')
					const addr = [s(ac.city) || s(ac.province), s(ac.district), s(ac.township)]
						.filter(Boolean).join('')
					if (addr) this.ctx.location.address = addr
				}
			})
		},

		async ask(text) {
			const q = (text !== undefined ? text : this.draft).trim()
			if (!q || this.thinking) return

			this.draft = ''
			this.messages.push({ role: 'me', text: q, time: Date.now() })
			this.saveHistory()
			this.scrollToBottom()
			this.thinking = true

			// 每次提问都先拉最新数据，保证回答是实时的
			await this.refreshData()

			await new Promise((r) => setTimeout(r, 320))
			let answer = await this.askCloud(q)
			if (!answer) {
				answer = buildAnswer({
					elderName: this.elderName,
					deviceId: this.deviceId,
					status: this.ctx.status,
					location: this.ctx.location,
					alarms: this.ctx.alarms
				}, q)
			}

			this.thinking = false
			this.messages.push({ role: 'ai', text: answer, time: Date.now() })
			this.saveHistory()
			this.scrollToBottom()
		},

		async askCloud(q) {
			const res = await this.request(
				this.cloudBaseUrl + '/api/ai/chat',
				'POST',
				{
					device_id: this.deviceId,
					intent: 'CHAT',
					text: q,
					context: {
						status: this.ctx.status,
						location: this.ctx.location,
						alarms: this.ctx.alarms
					}
				},
				30000
			)
			const d = res && res.data
			if (d && d.success === true && d.source === 'llm' && d.reply) {
				return String(d.reply)
			}
			return ''
		},

		scrollToBottom() {
			this.$nextTick(() => {
				this.scrollTarget = 'msg' + (this.messages.length - 1)
			})
		}
	}
}
</script>

<style scoped>
.container {
	display: flex;
	flex-direction: column;
	height: 100vh;
	background: #F4F6FA;
	box-sizing: border-box;
	padding-bottom: 108rpx;
}

/* 顶部 */
.hero {
	display: flex;
	align-items: center;
	padding: 26rpx 30rpx;
	margin: 20rpx 30rpx 16rpx;
	border-radius: 30rpx;
	background: linear-gradient(135deg, #4A7CFF 0%, #57C9F2 100%);
	box-shadow: 0 14rpx 32rpx rgba(74, 124, 255, 0.26);
}

.hero-icon {
	width: 72rpx;
	height: 72rpx;
	line-height: 72rpx;
	text-align: center;
	font-size: 36rpx;
	border-radius: 50%;
	background: rgba(255, 255, 255, 0.26);
	color: #FFFFFF;
	margin-right: 18rpx;
	flex-shrink: 0;
}

.hero-body {
	flex: 1;
	min-width: 0;
}

.hero-title {
	font-size: 32rpx;
	font-weight: 700;
	color: #FFFFFF;
}

.hero-sub {
	font-size: 23rpx;
	color: rgba(255, 255, 255, 0.86);
	margin-top: 4rpx;
}

.hero-refresh {
	width: 60rpx;
	height: 60rpx;
	line-height: 60rpx;
	text-align: center;
	font-size: 32rpx;
	color: #FFFFFF;
	border-radius: 50%;
	background: rgba(255, 255, 255, 0.22);
}

.hero-refresh.spin {
	animation: spin 1s linear infinite;
}

@keyframes spin {
	from { transform: rotate(0deg); }
	to { transform: rotate(360deg); }
}

/* 对话 */
.chat {
	flex: 1;
	height: 0;
	padding: 6rpx 30rpx 20rpx;
	box-sizing: border-box;
}

.row {
	display: flex;
	margin-bottom: 26rpx;
	align-items: flex-start;
}

.msg-wrap {
	margin-bottom: 26rpx;
}

.msg-wrap .row {
	margin-bottom: 0;
}

.msg-time {
	text-align: center;
	font-size: 22rpx;
	color: #9AA3B2;
	margin: 6rpx 0 16rpx;
}

.row.me {
	flex-direction: row-reverse;
}

.avatar {
	width: 60rpx;
	height: 60rpx;
	line-height: 60rpx;
	text-align: center;
	font-size: 26rpx;
	border-radius: 50%;
	flex-shrink: 0;
	margin: 0 14rpx;
}

.avatar.ai {
	background: linear-gradient(135deg, #4A7CFF, #57C9F2);
	color: #FFFFFF;
}

.avatar.me {
	background: #E7ECF5;
	color: #5C6675;
}

.bubble {
	max-width: 74%;
	padding: 22rpx 26rpx;
	border-radius: 24rpx;
	background: #FFFFFF;
	box-shadow: 0 6rpx 20rpx rgba(31, 45, 74, 0.06);
}

.bubble.ai {
	border-top-left-radius: 8rpx;
}

.bubble.me {
	background: linear-gradient(135deg, #4A7CFF, #2F6FED);
	border-top-right-radius: 8rpx;
	box-shadow: 0 8rpx 22rpx rgba(47, 111, 237, 0.22);
}

.bubble-text {
	font-size: 28rpx;
	line-height: 1.65;
	color: #1B2233;
	white-space: pre-wrap;
	word-break: break-all;
}

.bubble.me .bubble-text {
	color: #FFFFFF;
}

/* 输入中动画 */
.thinking {
	display: flex;
	align-items: center;
	gap: 8rpx;
	padding: 26rpx;
}

.dot {
	width: 12rpx;
	height: 12rpx;
	border-radius: 50%;
	background: #B9C2D0;
	animation: blink 1.2s infinite;
}

.dot:nth-child(2) { animation-delay: .2s; }
.dot:nth-child(3) { animation-delay: .4s; }

@keyframes blink {
	0%, 80%, 100% { opacity: .3; }
	40% { opacity: 1; }
}

/* 快捷提问 */
.chips {
	white-space: nowrap;
	padding: 10rpx 30rpx 14rpx;
	box-sizing: border-box;
}

.chip {
	display: inline-block;
	padding: 14rpx 24rpx;
	margin-right: 14rpx;
	background: #FFFFFF;
	border: 2rpx solid #E3E9F3;
	border-radius: 999rpx;
	font-size: 24rpx;
	color: #2F6FED;
}

/* 输入栏 */
.input-bar {
	display: flex;
	align-items: center;
	padding: 16rpx 30rpx 20rpx;
	background: #F4F6FA;
}

.input {
	flex: 1;
	height: 84rpx;
	background: #FFFFFF;
	border-radius: 999rpx;
	padding: 0 30rpx;
	font-size: 27rpx;
	border: 2rpx solid #E3E9F3;
	box-sizing: border-box;
}

.send {
	margin-left: 16rpx;
	height: 84rpx;
	line-height: 84rpx;
	padding: 0 36rpx;
	border-radius: 999rpx;
	background: #E7ECF5;
	color: #98A0AE;
	font-size: 27rpx;
	font-weight: 600;
}

.send.on {
	background: linear-gradient(135deg, #4A7CFF, #2F6FED);
	color: #FFFFFF;
	box-shadow: 0 8rpx 20rpx rgba(47, 111, 237, 0.24);
}
</style>
