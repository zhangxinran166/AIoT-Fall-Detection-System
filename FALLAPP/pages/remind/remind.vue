<template>
	<view class="container">
		<view class="page-head">
			<view class="title">家人提醒</view>
			<view class="sub">来自家人的叮嘱</view>
		</view>

		<view v-if="loading" class="empty">
			<view class="empty-text">正在加载…</view>
		</view>

		<view v-else-if="remindList.length === 0" class="empty">
			<view class="empty-icon">💬</view>
			<view class="empty-text">暂无家人提醒消息</view>
		</view>

		<view
			v-for="(item, index) in remindList"
			:key="index"
			class="remind-item"
		>
			<view class="item-head">
				<text class="time">{{ item.time_text }}</text>
				<text v-if="item.from_user" class="from">来自 {{ item.from_user }}</text>
				<text v-if="!item.is_read" class="red-tag">未读</text>
			</view>
			<view class="content">
				<text class="quote">💬</text>
				<text class="msg">{{ item.content }}</text>
			</view>
		</view>

		<view v-if="errorText" class="err">{{ errorText }}</view>
	</view>
</template>

<script>
import { CLOUD_BASE_URL } from '../../utils/alarm.js'

export default {
	data() {
		return {
			cloudBaseUrl: CLOUD_BASE_URL,
			remindList: [],
			loading: true,
			errorText: ''
		}
	},
	onLoad() {
		this.loadRemind()
	},
	onPullDownRefresh() {
		this.loadRemind().finally(() => uni.stopPullDownRefresh())
	},
	methods: {
		loadRemind() {
			const token = uni.getStorageSync('token') || ''
			if (!token) {
				this.loading = false
				this.errorText = '登录已失效，请重新登录'
				return Promise.resolve()
			}

			this.loading = true
			this.errorText = ''

			return new Promise((resolve) => {
				uni.request({
					url: this.cloudBaseUrl + '/api/remind?mark_read=1&_ts=' + Date.now(),
					method: 'GET',
					header: {
						'Authorization': 'Bearer ' + token,
						'Cache-Control': 'no-cache'
					},
					timeout: 12000,
					success: (res) => {
						if (res.statusCode === 200 && res.data && res.data.success) {
							const list = Array.isArray(res.data.data) ? res.data.data : []
							// 服务端字段 -> 模板字段
							this.remindList = list.map((x) => ({
								id: x.id,
								content: x.content,
								time_text: x.time_text || '',
								is_read: Number(x.is_read) === 1,
								from_user: x.from_user || ''
							}))
						} else if (res.statusCode === 401) {
							this.errorText = '登录已失效，请重新登录'
						} else {
							this.errorText = (res.data && res.data.message) || '获取提醒失败'
						}
					},
					fail: () => {
						this.errorText = '网络异常，下拉可重试'
					},
					complete: () => {
						this.loading = false
						resolve()
					}
				})
			})
		}
	}
}
</script>

<style scoped>
.container {
	padding: 40rpx 30rpx 80rpx;
	background: #F4F6FA;
	min-height: 100vh;
	box-sizing: border-box;
}

.page-head {
	margin-bottom: 30rpx;
	padding-left: 6rpx;
}

.title {
	font-size: 40rpx;
	font-weight: 700;
	color: #1B2233;
}

.sub {
	margin-top: 8rpx;
	font-size: 24rpx;
	color: #98A0AE;
}

.empty {
	padding: 120rpx 20rpx;
	text-align: center;
}

.empty-icon {
	font-size: 80rpx;
	line-height: 1;
	margin-bottom: 20rpx;
}

.empty-text {
	color: #98A0AE;
	font-size: 27rpx;
}

.remind-item {
	background: #FFFFFF;
	border-radius: 22rpx;
	padding: 28rpx 30rpx;
	margin-bottom: 22rpx;
	box-shadow: 0 4rpx 18rpx rgba(31, 45, 74, 0.05);
}

.item-head {
	display: flex;
	align-items: center;
	justify-content: space-between;
	margin-bottom: 14rpx;
}

.time {
	color: #8A93A3;
	font-size: 24rpx;
}

.from {
	color: #8A93A3;
	font-size: 22rpx;
	margin-left: auto;
	margin-right: 12rpx;
}

.red-tag {
	color: #F04F4F;
	background: #FFECEC;
	font-size: 21rpx;
	font-weight: 600;
	padding: 4rpx 16rpx;
	border-radius: 20rpx;
}

.content {
	display: flex;
	align-items: flex-start;
}

.quote {
	font-size: 28rpx;
	margin-right: 12rpx;
	line-height: 1.6;
}

.msg {
	flex: 1;
	color: #1B2233;
	font-size: 29rpx;
	line-height: 1.6;
}

.err {
	margin-top: 20rpx;
	text-align: center;
	font-size: 24rpx;
	color: #E6A23C;
}
</style>
