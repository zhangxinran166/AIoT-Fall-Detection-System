<template>
	<view class="container">
		<!-- 小时概览 -->
		<view class="summary-card">
			<view class="summary-title">老人跌倒警报</view>
			<view class="summary-row">
				<text class="summary-label">时间段</text>
				<text class="summary-value">{{ rangeText }}</text>
			</view>
			<view class="summary-row">
				<text class="summary-label">设备号</text>
				<text class="summary-value">{{ deviceId || '-' }}</text>
			</view>
			<view class="summary-row">
				<text class="summary-label">记录数</text>
				<text class="summary-value">{{ records.length }} 条</text>
			</view>
		</view>

		<view class="section-head">
			<text class="section-title">具体跌倒时间</text>
			<button v-if="pendingCount > 0" size="mini" type="primary" class="ack-all-btn" @click="ackAllInHour">
				确认全部({{ pendingCount }})
			</button>
		</view>

		<view v-if="loading" class="empty">正在加载...</view>
		<view v-else-if="records.length === 0" class="empty">{{ error || '该时间段没有跌倒记录' }}</view>

		<view v-else class="timeline">
			<view v-for="(item, index) in records" :key="item.id || index" class="record">
				<view class="record-left">
					<view class="record-time">{{ formatTime(item.created_text) }}</view>
					<view class="record-seq">第 {{ index + 1 }} 次</view>
				</view>

				<view class="record-line">
					<view class="dot" :class="item.status === 'acked' ? 'dot-done' : 'dot-pending'"></view>
					<view class="line" v-if="index < records.length - 1"></view>
				</view>

				<view class="record-body">
					<view class="record-text">{{ item.body || '检测到跌倒' }}</view>
					<view class="record-foot">
						<text :class="item.status === 'acked' ? 'status-done' : 'status-pending'">
							{{ item.status === 'acked' ? '已确认' : '待确认' }}
						</text>
						<text v-if="item.status !== 'acked'" class="ack-link" @click="ackOne(item)">确认已知晓</text>
					</view>
				</view>
			</view>
		</view>
	</view>
</template>

<script>
	import {
		CLOUD_BASE_URL,
		HOUR_MS,
		fetchNotifications,
		fmtHms,
		fmtHourRange,
		normalizeNotifications,
		parseTime
	} from '../../utils/alarm.js'

	export default {
		data() {
			return {
				cloudBaseUrl: CLOUD_BASE_URL,
				deviceId: '',
				startMs: 0,
				loading: false,
				error: '',
				records: []
			}
		},
		computed: {
			rangeText() {
				return this.startMs ? fmtHourRange(this.startMs) : '-'
			},
			pendingCount() {
				return this.records.filter(i => i.status !== 'acked').length
			}
		},
		onLoad(options) {
			this.deviceId = decodeURIComponent(options.deviceId || '')
			this.startMs = Number(options.start) || 0
			this.load()
		},
		methods: {
			formatTime(text) {
				const t = parseTime(text)
				return isFinite(t) ? fmtHms(t) : String(text || '')
			},

			async load() {
				const token = uni.getStorageSync('token') || ''
				if (!token) {
					this.error = '登录信息不存在，请重新登录'
					return
				}
				if (this.loading) return
				this.loading = true
				this.error = ''

				try {
					const res = await fetchNotifications(this.cloudBaseUrl, token)
					const result = normalizeNotifications(res)
					if (!result.ok) {
						this.error = result.message
						this.records = []
						return
					}
					const endMs = this.startMs + HOUR_MS
					this.records = result.list
						.filter(item => {
							if (this.deviceId && item.device_id !== this.deviceId) return false
							const t = parseTime(item.created_text)
							return isFinite(t) && t >= this.startMs && t < endMs
						})
						.sort((a, b) => parseTime(a.created_text) - parseTime(b.created_text))
				} catch (err) {
					console.error('加载小时详情失败', err)
					this.error = '云端连接失败，请检查网络'
					this.records = []
				} finally {
					this.loading = false
				}
			},

			async ackOne(item) {
				await this.postAck([item.id])
			},

			async ackAllInHour() {
				const ids = this.records.filter(i => i.status !== 'acked').map(i => i.id)
				if (ids.length === 0) return
				await this.postAck(ids)
			},

			// 逐条调用确认接口，最后统一刷新
			async postAck(ids) {
				const token = uni.getStorageSync('token') || ''
				if (!token) {
					uni.showToast({
						title: '请重新登录',
						icon: 'none'
					})
					return
				}
				uni.showLoading({
					title: '正在确认'
				})
				try {
					for (const id of ids) {
						await new Promise((resolve, reject) => {
							uni.request({
								url: this.cloudBaseUrl + '/api/notifications/ack',
								method: 'POST',
								header: {
									'Authorization': 'Bearer ' + token,
									'Content-Type': 'application/json'
								},
								data: {
									id: id
								},
								timeout: 10000,
								success: resolve,
								fail: reject
							})
						})
					}
					uni.showToast({
						title: '已确认',
						icon: 'success'
					})
					await this.load()
				} catch (err) {
					uni.showToast({
						title: '网络异常，确认失败',
						icon: 'none'
					})
				} finally {
					uni.hideLoading()
				}
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

	.summary-card {
		background: #fff;
		border-radius: 16rpx;
		padding: 28rpx;
		margin-bottom: 30rpx;
	}

	.summary-title {
		font-size: 32rpx;
		font-weight: bold;
		color: #d93025;
		margin-bottom: 18rpx;
	}

	.summary-row {
		display: flex;
		font-size: 25rpx;
		line-height: 1.9;
	}

	.summary-label {
		width: 130rpx;
		color: #888;
	}

	.summary-value {
		flex: 1;
		color: #333;
	}

	.section-head {
		display: flex;
		align-items: center;
		justify-content: space-between;
		margin-bottom: 16rpx;
	}

	.section-title {
		font-size: 30rpx;
		font-weight: bold;
		color: #222;
	}

	.ack-all-btn {
		margin: 0;
	}

	.empty {
		color: #999;
		font-size: 27rpx;
		text-align: center;
		padding: 60rpx 20rpx;
		background: #fff;
		border-radius: 16rpx;
	}

	.timeline {
		background: #fff;
		border-radius: 16rpx;
		padding: 20rpx 24rpx;
	}

	.record {
		display: flex;
		padding: 18rpx 0;
	}

	.record-left {
		width: 130rpx;
		flex-shrink: 0;
	}

	.record-time {
		font-size: 27rpx;
		font-weight: bold;
		color: #222;
		font-family: monospace;
	}

	.record-seq {
		font-size: 21rpx;
		color: #aaa;
		margin-top: 4rpx;
	}

	.record-line {
		width: 40rpx;
		display: flex;
		flex-direction: column;
		align-items: center;
		padding-top: 10rpx;
	}

	.dot {
		width: 16rpx;
		height: 16rpx;
		border-radius: 50%;
		flex-shrink: 0;
	}

	.dot-pending {
		background: #f56c6c;
	}

	.dot-done {
		background: #07a35a;
	}

	.line {
		flex: 1;
		width: 2rpx;
		background: #e8eaf0;
		margin-top: 6rpx;
	}

	.record-body {
		flex: 1;
		padding-left: 10rpx;
		padding-bottom: 6rpx;
	}

	.record-text {
		font-size: 26rpx;
		color: #333;
		line-height: 1.6;
	}

	.record-foot {
		display: flex;
		align-items: center;
		gap: 20rpx;
		margin-top: 8rpx;
	}

	.status-pending {
		color: #e67e22;
		font-size: 22rpx;
	}

	.status-done {
		color: #07a35a;
		font-size: 22rpx;
	}

	.ack-link {
		color: #2f6fed;
		font-size: 22rpx;
	}
/* ==========================================================
   视觉升级：与全局卡片风格对齐
   ========================================================== */

.summary-card,
.timeline,
.empty {
	border-radius: 26rpx;
	box-shadow: 0 6rpx 24rpx rgba(31, 45, 74, 0.05);
}

.ack-link {
	color: #007AFF;
}
/* ==========================================================
   新版视觉：渐变概览卡 + 大圆角时间轴
   ========================================================== */
.container {
	padding: 30rpx 30rpx 90rpx;
	background: #F4F6FA;
}

.summary-card {
	background: linear-gradient(135deg, #4A7CFF 0%, #57C9F2 100%);
	border-radius: 34rpx;
	padding: 34rpx 32rpx;
	margin-bottom: 30rpx;
	box-shadow: 0 16rpx 36rpx rgba(74, 124, 255, 0.28);
}

.summary-title {
	color: #FFFFFF;
	font-size: 34rpx;
	font-weight: 700;
	margin-bottom: 20rpx;
}

.summary-label {
	color: rgba(255, 255, 255, 0.82);
}

.summary-value {
	color: #FFFFFF;
	font-weight: 600;
}

.section-title {
	font-size: 30rpx;
	font-weight: 700;
}

.ack-all-btn {
	border-radius: 999rpx;
	margin: 0;
}

.timeline {
	background: #FFFFFF;
	border-radius: 32rpx;
	padding: 26rpx 28rpx;
	box-shadow: 0 10rpx 30rpx rgba(31, 45, 74, 0.07);
}

.record-time {
	font-size: 28rpx;
	font-weight: 700;
	color: #1B2233;
}

.ack-link {
	color: #2F6FED;
	font-weight: 600;
}

.empty {
	background: #FFFFFF;
	border-radius: 32rpx;
	box-shadow: 0 10rpx 30rpx rgba(31, 45, 74, 0.07);
}
</style>
