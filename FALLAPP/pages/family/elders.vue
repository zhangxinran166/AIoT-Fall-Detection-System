<template>
	<view class="container">
		<view class="top">
			<view class="top-text">
				<view class="h1">我的老人</view>
				<view class="sub">点击选择要查看的老人，长按可解除绑定</view>
			</view>
			<view class="top-btn" @click="goUser"></view>
		</view>

		<view v-if="loading && list.length === 0" class="empty">正在加载...</view>
		<view v-else-if="errorMsg" class="empty">{{ errorMsg }}</view>
		<view v-else-if="list.length === 0" class="empty">
			还没有绑定老人，点击下方按钮添加
		</view>

		<view
			v-for="item in list"
			:key="item.device_id"
			class="elder-card"
			@click="selectElder(item)"
			@longpress="confirmUnbind(item)"
		>
			<view class="avatar">👴</view>
			<view class="info">
				<view class="name">{{ item.elder_name }}</view>
				<view class="meta">设备号：{{ item.device_id }}</view>
				<view class="meta">当前链路：{{ item.link || '—' }}</view>
			</view>
			<view :class="['state', 's-' + item.status]">
				{{ statusText(item.status) }}
			</view>
		</view>

		<button type="primary" @click="openBind">＋ 绑定新老人</button>
		<button type="default" @click="logout">🚪 退出登录</button>

		<!-- 绑定弹窗 -->
		<view v-if="showBind" class="modal-mask" @click="showBind = false">
			<view class="modal-box" @click.stop>
				<view class="modal-title">绑定新老人</view>
				<input
					class="bind-input"
					v-model="bindDeviceId"
					placeholder="请输入老人的设备编号"
					maxlength="64"
				/>
				<view class="btn-row">
					<button class="btn-cancel" @click="showBind = false">取消</button>
					<button class="btn-confirm" :disabled="binding" @click="submitBind">
						绑定
					</button>
				</view>
			</view>
		</view>
	</view>
</template>

<script>
export default {
	data() {
		return {
			cloudBaseUrl: 'http://101.43.88.85:5000',
			list: [],
			loading: false,
			errorMsg: '',
			showBind: false,
			bindDeviceId: '',
			binding: false,
			timer: null
		}
	},

	onShow() {
		if (!uni.getStorageSync('token')) {
			uni.reLaunch({ url: '/pages/login/login' })
			return
		}
		this.loadElders()
		this.stopTimer()
		this.timer = setInterval(() => this.loadElders(), 10000)
	},

	onHide() {
		this.stopTimer()
	},

	onUnload() {
		this.stopTimer()
	},

	methods: {
		stopTimer() {
			if (this.timer) {
				clearInterval(this.timer)
				this.timer = null
			}
		},

		// 统一的云端请求，登录失效时自动回到登录页
		api(method, path, data) {
			const token = uni.getStorageSync('token') || ''
			return new Promise((resolve, reject) => {
				uni.request({
					url: this.cloudBaseUrl + path,
					method: method,
					header: {
						'Authorization': 'Bearer ' + token,
						'Content-Type': 'application/json'
					},
					data: data,
					timeout: 10000,
					success: (res) => {
						if (res.statusCode === 401) {
							this.stopTimer()
							uni.showToast({ title: '登录已失效，请重新登录', icon: 'none' })
							uni.removeStorageSync('token')
							setTimeout(() => {
								uni.reLaunch({ url: '/pages/login/login' })
							}, 800)
							reject(new Error('401'))
							return
						}
						resolve(res)
					},
					fail: reject
				})
			})
		},

		async loadElders() {
			if (this.loading) return
			this.loading = true
			try {
				const res = await this.api('GET', '/api/family/elders')
				if (res.statusCode === 200 && res.data && res.data.success === true) {
					this.list = Array.isArray(res.data.data) ? res.data.data : []
					this.errorMsg = ''
				} else {
					this.errorMsg = '获取老人列表失败，请稍后重试'
				}
			} catch (e) {
				if (!e || e.message !== '401') {
					this.errorMsg = '云端连接失败，请检查网络'
				}
			} finally {
				this.loading = false
			}
		},

		statusText(s) {
			const map = { normal: '正常', fallen: '跌倒！', offline: '离线', never: '未上线' }
			return map[s] || '未知'
		},

		// 选择一位老人，进入首页
		selectElder(item) {
			uni.setStorageSync('deviceId', item.device_id)
			uni.setStorageSync('elderName', item.elder_name || '')
			uni.navigateTo({ url: '/pages/family/index' })
		},

		// 右上角入口：家人端个人中心
		goUser() {
			uni.navigateTo({ url: '/pages/family/user' })
		},

		openBind() {
			this.bindDeviceId = ''
			this.showBind = true
		},

		async submitBind() {
			const id = this.bindDeviceId.trim()
			if (!id) {
				uni.showToast({ title: '请输入设备编号', icon: 'none' })
				return
			}
			if (this.binding) return
			this.binding = true
			try {
				const res = await this.api('POST', '/api/auth/bind', { device_id: id })
				if (res.statusCode === 200 && res.data && res.data.success === true) {
					uni.setStorageSync('devices', res.data.devices || [])
					this.showBind = false
					uni.showToast({ title: '绑定成功', icon: 'success' })
					this.loading = false
					await this.loadElders()
				} else {
					uni.showToast({
						title: (res.data && res.data.message) || '绑定失败',
						icon: 'none'
					})
				}
			} catch (e) {
				if (!e || e.message !== '401') {
					uni.showToast({ title: '网络异常，绑定失败', icon: 'none' })
				}
			} finally {
				this.binding = false
			}
		},

		confirmUnbind(item) {
			uni.showModal({
				title: '解除绑定',
				content: '确定不再查看「' + item.elder_name + '」（' + item.device_id + '）吗？',
				success: async (m) => {
					if (!m.confirm) return
					try {
						const res = await this.api('POST', '/api/auth/unbind', { device_id: item.device_id })
						if (res.statusCode === 200 && res.data && res.data.success === true) {
							uni.setStorageSync('devices', res.data.devices || [])
							if (uni.getStorageSync('deviceId') === item.device_id) {
								uni.removeStorageSync('deviceId')
								uni.removeStorageSync('elderName')
							}
							uni.showToast({ title: '已解除绑定', icon: 'success' })
							this.loading = false
							await this.loadElders()
						} else {
							uni.showToast({ title: '解除绑定失败', icon: 'none' })
						}
					} catch (e) {
						if (!e || e.message !== '401') {
							uni.showToast({ title: '网络异常', icon: 'none' })
						}
					}
				}
			})
		},

		logout() {
			uni.showModal({
				title: '提示',
				content: '确定退出登录吗？',
				success: (res) => {
					if (res.confirm) {
						this.stopTimer()
						;['isLogin', 'userRole', 'token', 'username', 'nickname', 'devices', 'deviceId', 'elderName']
							.forEach((k) => uni.removeStorageSync(k))
						uni.reLaunch({ url: '/pages/login/login' })
					}
				}
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

.top {
	display: flex;
	align-items: center;
	justify-content: space-between;
	margin: 0 0 32rpx;
	padding-left: 6rpx;
}

.top-text {
	flex: 1;
	min-width: 0;
}

/* 右上角个人中心入口（线条人像） */
.top-btn {
	width: 68rpx;
	height: 68rpx;
	border-radius: 50%;
	flex-shrink: 0;
	margin-left: 20rpx;
	background: #FFFFFF url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%231B2233' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'%3E%3Ccircle cx='12' cy='8.2' r='3.4'/%3E%3Cpath d='M4.8 20.2c0-3.7 3.2-6.1 7.2-6.1s7.2 2.4 7.2 6.1'/%3E%3C/svg%3E") no-repeat center;
	background-size: 38rpx 38rpx;
	box-shadow: 0 4rpx 14rpx rgba(31, 45, 74, 0.10);
}

.h1 {
	font-size: 42rpx;
	font-weight: 700;
	color: #1B2233;
}

.sub {
	margin-top: 10rpx;
	font-size: 24rpx;
	color: #98A0AE;
}

.empty {
	color: #98A0AE;
	font-size: 27rpx;
	text-align: center;
	padding: 120rpx 10rpx;
	line-height: 1.7;
}

.elder-card {
	display: flex;
	align-items: center;
	background: #FFFFFF;
	border-radius: 26rpx;
	padding: 30rpx;
	margin-bottom: 24rpx;
	box-shadow: 0 6rpx 24rpx rgba(31, 45, 74, 0.05);
}

.avatar {
	width: 96rpx;
	height: 96rpx;
	line-height: 96rpx;
	text-align: center;
	font-size: 50rpx;
	border-radius: 50%;
	background: #EAF1FF;
	margin-right: 24rpx;
	flex-shrink: 0;
}

.info {
	flex: 1;
}

.name {
	font-size: 33rpx;
	font-weight: 700;
	color: #1B2233;
}

.meta {
	margin-top: 8rpx;
	font-size: 23rpx;
	color: #98A0AE;
}

.state {
	padding: 8rpx 20rpx;
	border-radius: 24rpx;
	font-size: 22rpx;
	font-weight: 600;
	color: #FFFFFF;
	background: #B4BAC6;
	flex-shrink: 0;
}

.s-normal {
	background: #12B76A;
}

.s-fallen {
	background: #F04F4F;
}

.s-offline,
.s-never {
	background: #B4BAC6;
}

button {
	margin: 0 0 22rpx 0;
}

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

.bind-input {
	border: 2rpx solid #E9EDF4;
	background: #FAFBFD;
	border-radius: 16rpx;
	padding: 24rpx;
	font-size: 28rpx;
	min-height: 92rpx;
	color: #1B2233;
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
</style>