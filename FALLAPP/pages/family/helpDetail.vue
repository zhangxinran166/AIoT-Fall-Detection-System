<template>
	<view class="page">
		<view class="card">
			<view class="title">老人主动求助警报</view>
			<view class="row"><text class="k">时间</text><text class="v">{{ time || '—' }}</text></view>
			<view class="row"><text class="k">链路</text><text class="v">{{ source || '—' }}</text></view>
			<view class="row"><text class="k">状态</text><text class="v">{{ acked ? '已确认' : '待确认' }}</text></view>
			<view class="desc">老人通过语音发起主动求助，请及时联系并确认老人安全。</view>
			<button class="ack" v-if="!acked" @click="ack">确认已处理</button>
			<button class="ack done" v-else disabled>已确认</button>
		</view>
	</view>
</template>
<script>
export default {
	data() { return { cloudBaseUrl: 'http://101.43.88.85:5000', deviceId: 'ESP32_001', id: '', time: '', source: '', acked: false } },
	onLoad(options) {
		this.deviceId = options.deviceId || 'ESP32_001'
		this.id = options.id || ''
		this.time = decodeURIComponent(options.time || '')
		this.source = decodeURIComponent(options.source || '')
		this.acked = options.acked === '1'
	},
	methods: {
		ack() {
			uni.request({
				url: this.cloudBaseUrl + '/api/alarm/ack',
				method: 'POST',
				header: { 'Content-Type': 'application/json' },
				data: { device_id: this.deviceId, id: this.id, time: this.time },
				success: () => {
					this.acked = true
					uni.showToast({ title: '已确认', icon: 'success' })
				},
				fail: () => uni.showToast({ title: '确认失败', icon: 'none' })
			})
		}
	}
}
</script>
<style scoped>
.page { min-height: 100vh; background: #f4f6fa; padding: 28rpx; box-sizing: border-box; }
.card { background: #fff; border-radius: 28rpx; padding: 32rpx; }
.title { font-size: 38rpx; font-weight: 700; color: #1b2233; margin-bottom: 24rpx; }
.row { display: flex; justify-content: space-between; padding: 16rpx 0; border-bottom: 1rpx solid #eef1f5; font-size: 27rpx; }
.k { color: #7b8494; } .v { color: #1b2233; }
.desc { margin: 24rpx 0; color: #5c6675; line-height: 1.7; font-size: 25rpx; }
.ack { background: #2f6fed; color: #fff; border-radius: 16rpx; height: 84rpx; line-height: 84rpx; }
.ack.done { background: #e9fbf1; color: #0e9f6e; }
</style>
