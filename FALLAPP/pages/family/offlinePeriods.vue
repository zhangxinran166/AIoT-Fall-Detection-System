<template>
	<view class="page">
		<view class="head">
			<view class="title">离线时间段</view>
			<view class="sub">{{ elderName || deviceId }} · 离线期间跌倒记录同步展示</view>
		</view>
		<view class="summary-box">
			<text class="summary-main">{{ periods.length ? '共 ' + periods.length + ' 条离线记录' : '无离线记录' }}</text>
			<text class="summary-sub">设备断网/断电历史</text>
		</view>
		<view v-if="loading" class="empty">加载中...</view>
		<view v-else-if="!periods.length" class="empty">暂无离线记录</view>
		<view v-for="(item, idx) in periods" :key="idx" class="card">
			<view class="falls-box">
				<view class="falls-head">
					<text class="falls-title">该时段跌倒记录</text>
					<text class="falls-count">{{ (item.alarms && item.alarms.length) ? item.alarms.length + ' 条' : '无' }}</text>
				</view>
				<view v-if="!item.alarms || !item.alarms.length" class="no-fall">该时段无跌倒记录</view>
				<view v-for="(a, i) in item.alarms" :key="i" class="alarm">
					<view class="alarm-time">{{ a.time || '—' }}</view>
					<view class="alarm-body">{{ a.fallen ? '跌倒' : '状态' }} · {{ a.location_source || '未知链路' }}</view>
				</view>
			</view>
			<view class="row"><text class="label">离线</text><text class="value">{{ item.offline_start || '—' }} → {{ item.offline_end || '—' }}</text></view>
			<view class="row"><text class="label">时长</text><text class="value">{{ durationText(item.duration_seconds) }}</text></view>
		</view>
	</view>
</template>
<script>
export default {
	data() {
		return {
			cloudBaseUrl: 'http://101.43.88.85:5000',
			deviceId: 'ESP32_001',
			elderName: '',
			loading: false,
			periods: []
		}
	},
	onLoad(options) {
		this.deviceId = options.deviceId || 'ESP32_001'
		this.elderName = options.elderName || ''
		this.loadPeriods()
	},
	methods: {
		async loadPeriods() {
			this.loading = true
			try {
				const [periodRes, alarmRes] = await Promise.all([
					this.request('/api/device/offline_periods?device_id=' + encodeURIComponent(this.deviceId)),
					this.request('/api/alarm/history?device_id=' + encodeURIComponent(this.deviceId))
				])
				const rawPeriods = periodRes && periodRes.success && Array.isArray(periodRes.data) ? periodRes.data : []
				const alarms = alarmRes && alarmRes.success && Array.isArray(alarmRes.data) ? alarmRes.data : []
				this.periods = rawPeriods.slice().reverse().map(item => {
					const start = String(item.offline_start || '')
					const end = String(item.offline_end || '')
					const inPeriod = alarms.filter(a => {
						if (!a || !a.fallen || !a.time) return false
						const t = String(a.time)
						if (start && t < start) return false
						if (end && t > end) return false
						return true
					})
					return Object.assign({}, item, { alarms: inPeriod })
				})
			} catch (e) {
				this.periods = []
			} finally {
				this.loading = false
			}
		},
		request(url) {
			return new Promise(resolve => {
				uni.request({
					url: this.cloudBaseUrl + url,
					method: 'GET',
					timeout: 8000,
					success: res => resolve(res && res.data && res.data.success ? res.data : null),
					fail: () => resolve(null)
				})
			})
		},
		durationText(sec) {
			const n = Number(sec) || 0
			if (n < 60) return n + ' 秒'
			if (n < 3600) return Math.floor(n / 60) + ' 分钟'
			return Math.floor(n / 3600) + ' 小时 ' + Math.floor((n % 3600) / 60) + ' 分钟'
		}
	}
}
</script>
<style scoped>
.page { min-height: 100vh; background: #f4f6fa; padding: 24rpx; box-sizing: border-box; }
.head { padding: 12rpx 8rpx 24rpx; }
.title { font-size: 40rpx; font-weight: 700; color: #1b2233; }
.sub { margin-top: 8rpx; color: #7b8494; font-size: 24rpx; }
.summary-box { background: #EFF6FF; border: 2rpx solid #BFDBFE; border-radius: 20rpx; padding: 22rpx 24rpx; margin-bottom: 20rpx; display: flex; flex-direction: column; }
.summary-main { color: #1D4ED8; font-size: 30rpx; font-weight: 700; }
.summary-sub { color: #5B7DB1; font-size: 22rpx; margin-top: 6rpx; }
.card { background: #fff; border-radius: 24rpx; padding: 24rpx; margin-bottom: 20rpx; }
.falls-box { background: #F8FAFF; border: 1rpx solid #E2EAF8; border-radius: 16rpx; padding: 18rpx 20rpx; margin-bottom: 18rpx; }
.falls-head { display: flex; align-items: baseline; justify-content: space-between; }
.falls-title { font-size: 27rpx; font-weight: 700; color: #1D4ED8; }
.falls-count { font-size: 22rpx; color: #6B7A90; }
.no-fall { color: #98A0AE; font-size: 24rpx; padding-top: 10rpx; }
.row { display: flex; justify-content: space-between; margin-bottom: 12rpx; font-size: 26rpx; }
.label { color: #7b8494; }
.value { color: #1b2233; flex: 1; text-align: right; margin-left: 20rpx; }
.empty { color: #98a0ae; text-align: center; padding: 80rpx 0; }
.alarm { padding: 12rpx 0 0; margin-top: 10rpx; border-top: 1rpx solid #E8EEF8; }
.alarm-time { font-size: 23rpx; color: #7b8494; }
.alarm-body { font-size: 25rpx; color: #1b2233; margin-top: 4rpx; }
</style>
