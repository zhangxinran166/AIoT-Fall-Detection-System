<template>
	<view class="page">
		<view class="head"><view class="title">老人主动求助警报</view><view class="sub">与跌倒警报分开记录</view></view>
		<view class="summary-box">
			<text class="summary-main">{{ list.length ? '共 ' + list.length + ' 条求助记录' : '无求助记录' }}</text>
			<text class="summary-sub">语音求助历史</text>
		</view>
		<view v-if="loading" class="empty">加载中...</view>
		<view v-else-if="!list.length" class="empty">暂无求助记录</view>
		<view v-for="(item, i) in list" :key="i" class="card" @click="open(item)">
			<view class="row"><text class="time">{{ item.time || '—' }}</text><text :class="item.acked ? 'done' : 'pending'">{{ item.acked ? '已确认' : '待确认' }}</text></view>
			<view class="meta">{{ item.location_source || '未知链路' }} · 语音求助</view>
			<view class="hint">查看详情 ›</view>
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
				url: this.cloudBaseUrl + '/api/alarm/history?device_id=' + encodeURIComponent(this.deviceId),
				method: 'GET', timeout: 8000,
				success: (res) => {
					const data = res && res.data && res.data.success ? res.data.data : []
					this.list = (Array.isArray(data) ? data : []).filter(a => a && (a.location_source === 'help' || a.event === 'help'))
				},
				complete: () => { this.loading = false }
			})
		},
		open(item) {
			uni.navigateTo({ url: '/pages/family/helpDetail?deviceId=' + encodeURIComponent(this.deviceId) + '&id=' + encodeURIComponent(item.id || '') + '&time=' + encodeURIComponent(item.time || '') + '&source=' + encodeURIComponent(item.location_source || '') + '&acked=' + (item.acked ? '1' : '0') })
		}
	}
}
</script>
<style scoped>
.page { min-height: 100vh; background: #f4f6fa; padding: 24rpx; box-sizing: border-box; }
.head { padding: 10rpx 8rpx 24rpx; } .title { font-size: 38rpx; font-weight: 700; color: #1b2233; } .sub { margin-top: 8rpx; color: #7b8494; font-size: 24rpx; }
.summary-box { background: #FFF7ED; border: 2rpx solid #FFE0B2; border-radius: 20rpx; padding: 22rpx 24rpx; margin-bottom: 20rpx; display: flex; flex-direction: column; }
.summary-main { color: #B54708; font-size: 30rpx; font-weight: 700; }
.summary-sub { color: #9A6A3A; font-size: 22rpx; margin-top: 6rpx; }
.card { background: #fff; border-radius: 24rpx; padding: 24rpx; margin-bottom: 18rpx; }
.row { display: flex; justify-content: space-between; font-size: 26rpx; } .time { color: #1b2233; font-weight: 600; }
.done { color: #0e9f6e; } .pending { color: #f04f4f; }
.meta { color: #7b8494; font-size: 23rpx; margin-top: 10rpx; } .hint { color: #2f6fed; font-size: 23rpx; margin-top: 12rpx; }
.empty { text-align: center; color: #98a0ae; padding: 100rpx 0; }
</style>
