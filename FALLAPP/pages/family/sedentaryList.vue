<template>
	<view class="page">
		<view class="head"><view class="title">久坐记录</view><view class="sub">连续低活动约 45 分钟自动记录</view></view>
		<view class="summary-box">
			<text class="summary-main">{{ list.length ? '共 ' + list.length + ' 条久坐记录' : '无久坐记录' }}</text>
			<text class="summary-sub">长时间低活动历史</text>
		</view>
		<view v-if="loading" class="empty">加载中...</view>
		<view v-else-if="!list.length" class="empty">暂无久坐记录</view>
		<view v-for="(item, i) in list" :key="i" class="card">
			<view class="row"><text class="time">{{ item.time || '—' }}</text><text class="tag">久坐</text></view>
			<view class="meta">持续约 {{ Math.floor((item.sedentary_seconds || 0) / 60) }} 分钟</view>
		</view>
	</view>
</template>
<script>
export default {
	data() { return { cloudBaseUrl: 'http://101.43.88.85:5000', deviceId: 'ESP32_001', loading: false, list: [] } },
	onLoad(options) { this.deviceId = options.deviceId || 'ESP32_001'; this.load() },
	onShow() { this.load() },
	methods: {
		load() {
			this.loading = true
			uni.request({
				url: this.cloudBaseUrl + '/api/device/sedentary_records?device_id=' + encodeURIComponent(this.deviceId),
				method: 'GET', timeout: 8000,
				success: (res) => {
					this.list = res && res.data && res.data.success && Array.isArray(res.data.data) ? res.data.data : []
				},
				complete: () => { this.loading = false }
			})
		}
	}
}
</script>
<style scoped>
.page { min-height: 100vh; background: #f4f6fa; padding: 24rpx; box-sizing: border-box; }
.head { padding: 10rpx 8rpx 24rpx; } .title { font-size: 38rpx; font-weight: 700; color: #1b2233; } .sub { margin-top: 8rpx; color: #7b8494; font-size: 24rpx; }
.summary-box { background: #F5F3FF; border: 2rpx solid #DDD6FE; border-radius: 20rpx; padding: 22rpx 24rpx; margin-bottom: 20rpx; display: flex; flex-direction: column; }
.summary-main { color: #6D28D9; font-size: 30rpx; font-weight: 700; }
.summary-sub { color: #7C6AAE; font-size: 22rpx; margin-top: 6rpx; }
.card { background: #fff; border-radius: 24rpx; padding: 24rpx; margin-bottom: 18rpx; }
.row { display: flex; justify-content: space-between; align-items: center; }
.time { color: #1b2233; font-size: 28rpx; font-weight: 600; }
.tag { color: #7c3aed; background: #f3e8ff; border-radius: 10rpx; padding: 4rpx 14rpx; font-size: 22rpx; }
.meta { color: #7b8494; font-size: 24rpx; margin-top: 10rpx; }
.empty { text-align: center; color: #98a0ae; padding: 100rpx 0; }
</style>
