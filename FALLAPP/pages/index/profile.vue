<template>
	<view class="container">
		<!-- 头像卡片 -->
		<view class="header">
			<view class="avatar">👴</view>
			<view class="name">{{ nickname || '老人账号' }}</view>
			<view class="role-tag">老人端</view>
		</view>

		<!-- 信息卡片 -->
		<view class="info-card">
			<view class="info-item">
				<text class="label">登录账号</text>
				<text class="value">{{ username || '—' }}</text>
			</view>
			<view class="info-item">
				<text class="label">绑定设备</text>
				<text class="value">{{ deviceId || '—' }}</text>
			</view>
			<view class="info-item">
				<text class="label">设备数量</text>
				<text class="value">{{ devices.length }} 台</text>
			</view>
		</view>

		<!-- 功能按钮 -->
		<view class="btn-group">
			<button type="default" class="btn-ghost" @click="backHome">🏠 返回首页</button>
			<button type="warn" class="btn-danger" @click="logout">🚪 退出登录</button>
		</view>

		<view class="del-wrap">
			<text class="del-link" @click="goSecurity">设置密保</text>
			<text class="del-sep">·</text>
			<text class="del-link" @click="deleteAccount">注销账号</text>
		</view>
	</view>
</template>

<script>
import { CLOUD_BASE_URL } from '../../utils/alarm.js'
import { sendOffline } from '../../utils/heartbeat.js'

export default {
	data() {
		return {
			cloudBaseUrl: CLOUD_BASE_URL,
			username: '',
			nickname: '',
			deviceId: '',
			devices: []
		}
	},
	onShow() {
		this.username = uni.getStorageSync('username') || ''
		this.nickname = uni.getStorageSync('nickname') || ''
		this.deviceId = uni.getStorageSync('deviceId') || ''
		const d = uni.getStorageSync('devices')
		this.devices = Array.isArray(d) ? d : []
	},
	methods: {
		backHome() {
			uni.navigateBack()
		},
		logout() {
			uni.showModal({
				title: '提示',
				content: '确定退出登录吗？',
				success: (res) => {
					if (res.confirm) {
						// 先告诉服务器已下线（要用到旧 token，所以放在清理之前）
						sendOffline()
						;['isLogin', 'userRole', 'token', 'username', 'nickname', 'devices', 'deviceId', 'elderName']
							.forEach((k) => uni.removeStorageSync(k))
						uni.reLaunch({
							url: '/pages/login/login'
						})
					}
				}
			})
		},

		// 设置密保问题（忘记密码时用）
		goSecurity() {
			uni.navigateTo({ url: '/pages/login/forgot?mode=set' })
		},

		// 注销账号：确认后调用云端删除
		deleteAccount() {
			uni.showModal({
				title: '注销账号',
				content: '注销后，该账号的绑定关系和告警通知记录都会被删除，且无法恢复。确定继续吗？',
				confirmText: '注销',
				confirmColor: '#F04F4F',
				success: (res) => {
					if (res.confirm) this.doDeleteAccount()
				}
			})
		},

		async doDeleteAccount() {
			const token = uni.getStorageSync('token') || ''
			if (!token) {
				uni.showToast({ title: '请重新登录', icon: 'none' })
				return
			}
			uni.showLoading({ title: '正在注销' })
			try {
				const r = await new Promise((resolve, reject) => {
					uni.request({
						url: this.cloudBaseUrl + '/api/auth/delete',
						method: 'POST',
						header: {
							'Authorization': 'Bearer ' + token,
							'Content-Type': 'application/json'
						},
						data: {},
						timeout: 10000,
						success: resolve,
						fail: reject
					})
				})
				uni.hideLoading()
				if (r.statusCode === 200 && r.data && r.data.success) {
					;['isLogin', 'userRole', 'token', 'username', 'nickname', 'devices', 'deviceId', 'elderName']
						.forEach((k) => uni.removeStorageSync(k))
					uni.showToast({ title: '账号已注销', icon: 'success' })
					setTimeout(() => uni.reLaunch({ url: '/pages/login/login' }), 900)
				} else if (r.statusCode === 404) {
					uni.showToast({ title: '服务器未更新，暂不支持注销', icon: 'none' })
				} else {
					uni.showToast({
						title: (r.data && r.data.message) || '注销失败',
						icon: 'none'
					})
				}
			} catch (e) {
				uni.hideLoading()
				uni.showToast({ title: '网络异常，注销失败', icon: 'none' })
			}
		}
	}
}
</script>

<style scoped>
.container {
	background: #F4F6FA;
	min-height: 100vh;
	padding: 40rpx 30rpx 80rpx;
	box-sizing: border-box;
}

/* 头像卡片 */
.header {
	background: #FFFFFF;
	border-radius: 26rpx;
	padding: 56rpx 30rpx 44rpx;
	display: flex;
	flex-direction: column;
	align-items: center;
	margin-bottom: 26rpx;
	box-shadow: 0 6rpx 24rpx rgba(31, 45, 74, 0.06);
}

.avatar {
	width: 150rpx;
	height: 150rpx;
	line-height: 150rpx;
	text-align: center;
	font-size: 76rpx;
	border-radius: 50%;
	background: #EAF1FF;
	margin-bottom: 22rpx;
}

.name {
	font-size: 37rpx;
	font-weight: 700;
	color: #1B2233;
}

.role-tag {
	margin-top: 14rpx;
	font-size: 22rpx;
	color: #007AFF;
	background: #EAF1FF;
	border-radius: 22rpx;
	padding: 6rpx 22rpx;
}

/* 信息卡片 */
.info-card {
	background: #FFFFFF;
	border-radius: 26rpx;
	padding: 6rpx 30rpx;
	margin-bottom: 40rpx;
	box-shadow: 0 6rpx 24rpx rgba(31, 45, 74, 0.05);
}

.info-item {
	display: flex;
	align-items: flex-start;
	padding: 28rpx 0;
	border-bottom: 1rpx solid #EEF1F5;
	font-size: 28rpx;
}

.info-item:last-child {
	border-bottom: none;
}

.label {
	color: #8A93A3;
	width: 170rpx;
	flex-shrink: 0;
}

.value {
	color: #1B2233;
	flex: 1;
	word-break: break-all;
	font-weight: 500;
}

/* 按钮组 */
.btn-group {
	margin-top: 10rpx;
}

.btn-ghost {
	background: #FFFFFF;
	color: #1B2233;
	border: 2rpx solid #E7ECF5;
	font-weight: 500;
}

.btn-danger {
	background: #FFFFFF;
	color: #F04F4F;
	border: 2rpx solid #FFE4E4;
	font-weight: 500;
}

/* 注销账号（弱化入口，避免误触） */
.del-wrap {
	margin-top: 16rpx;
	text-align: center;
}

.del-link {
	font-size: 24rpx;
	color: #98A0AE;
	padding: 16rpx 30rpx;
}

.del-link:active {
	color: #F04F4F;
}

.del-sep {
	font-size: 24rpx;
	color: #D5DAE3;
	margin: 0 10rpx;
}
</style>
