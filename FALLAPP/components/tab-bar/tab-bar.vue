<template>
	<view class="tabbar">
		<view
			v-for="item in items"
			:key="item.key"
			class="tab"
			@click="go(item)"
		>
			<view class="tab-icon-wrap">
				<view
					class="tab-icon"
					:class="[item.key, active === item.key ? 'on' : '']"
				></view>
				<view v-if="item.key === 'alarm' && alarmUnread > 0" class="tab-dot"></view>
			</view>
			<text
				class="tab-text"
				:class="{ on: active === item.key }"
			>{{ item.name }}</text>
		</view>
	</view>
</template>

<script>
export default {
	name: 'tab-bar',
	props: {
		active: {
			type: String,
			default: 'home'
		}
	},
	data() {
		return {
			alarmUnread: 0,
			badgeTimer: null,
			items: [
				{ key: 'home', name: '首页', url: '/pages/family/index' },
				{ key: 'ai', name: 'AI', url: '/pages/family/ai' },
				{ key: 'alarm', name: '告警', url: '/pages/family/alarmList' },
				{ key: 'location', name: '位置', url: '/pages/familyLocation/familyLocation' },
				{ key: 'mine', name: '我的', url: '/pages/family/user' }
			]
		}
	},
	mounted() {
		this.refreshBadge()
		this.badgeTimer = setInterval(() => this.refreshBadge(), 2000)
	},
	beforeDestroy() {
		if (this.badgeTimer) clearInterval(this.badgeTimer)
	},
	methods: {
		refreshBadge() {
			this.alarmUnread = Number(uni.getStorageSync('alarmUnread') || 0)
		},
		go(item) {
			if (item.key === this.active) return
			const deviceId = uni.getStorageSync('deviceId') || ''
			const elderName = uni.getStorageSync('elderName') || ''
			const query = '?deviceId=' + encodeURIComponent(deviceId) +
				'&elderName=' + encodeURIComponent(elderName)
			uni.reLaunch({ url: item.url + query })
		}
	}
}
</script>

<style scoped>
.tabbar {
	position: fixed;
	left: 0;
	right: 0;
	bottom: 0;
	height: 108rpx;
	padding-bottom: env(safe-area-inset-bottom);
	background: #FFFFFF;
	display: flex;
	align-items: center;
	box-shadow: 0 -4rpx 24rpx rgba(31, 45, 74, 0.06);
	z-index: 999;
}

.tab {
	flex: 1;
	height: 108rpx;
	display: flex;
	flex-direction: column;
	align-items: center;
	justify-content: center;
}

.tab-icon {
	width: 44rpx;
	height: 44rpx;
	background-color: #9AA3B2;
	-webkit-mask-repeat: no-repeat;
	-webkit-mask-position: center;
	-webkit-mask-size: 44rpx 44rpx;
	mask-repeat: no-repeat;
	mask-position: center;
	mask-size: 44rpx 44rpx;
}

.tab-icon-wrap {
	position: relative;
	width: 44rpx;
	height: 44rpx;
}

.tab-dot {
	position: absolute;
	right: -6rpx;
	top: -4rpx;
	width: 16rpx;
	height: 16rpx;
	border-radius: 50%;
	background: #F04438;
	border: 2rpx solid #FFFFFF;
}

.tab-icon.on {
	background-color: #2F6FED;
}

.tab-icon.home {
	-webkit-mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M3.5 10.5 12 3.5l8.5 7'/%3E%3Cpath d='M5.8 9.8V20h12.4V9.8'/%3E%3C/svg%3E");
	mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M3.5 10.5 12 3.5l8.5 7'/%3E%3Cpath d='M5.8 9.8V20h12.4V9.8'/%3E%3C/svg%3E");
}

.tab-icon.ai {
	-webkit-mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 3.2l1.9 4.6 4.6 1.9-4.6 1.9L12 16.2l-1.9-4.6-4.6-1.9 4.6-1.9z'/%3E%3Cpath d='M18.4 15.2l.9 2.1 2.1.9-2.1.9-.9 2.1-.9-2.1-2.1-.9 2.1-.9z'/%3E%3C/svg%3E");
	mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 3.2l1.9 4.6 4.6 1.9-4.6 1.9L12 16.2l-1.9-4.6-4.6-1.9 4.6-1.9z'/%3E%3Cpath d='M18.4 15.2l.9 2.1 2.1.9-2.1.9-.9 2.1-.9-2.1-2.1-.9 2.1-.9z'/%3E%3C/svg%3E");
}

.tab-icon.alarm {	-webkit-mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 3.8a5.2 5.2 0 0 0-5.2 5.2v4L5.2 17h13.6l-1.6-4V9a5.2 5.2 0 0 0-5.2-5.2z'/%3E%3Cpath d='M10 20a2 2 0 0 0 4 0'/%3E%3C/svg%3E");
	mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 3.8a5.2 5.2 0 0 0-5.2 5.2v4L5.2 17h13.6l-1.6-4V9a5.2 5.2 0 0 0-5.2-5.2z'/%3E%3Cpath d='M10 20a2 2 0 0 0 4 0'/%3E%3C/svg%3E");
}

.tab-icon.location {
	-webkit-mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 21c4-4.2 6-7.4 6-10a6 6 0 1 0-12 0c0 2.6 2 5.8 6 10z'/%3E%3Ccircle cx='12' cy='10.8' r='2.2'/%3E%3C/svg%3E");
	mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 21c4-4.2 6-7.4 6-10a6 6 0 1 0-12 0c0 2.6 2 5.8 6 10z'/%3E%3Ccircle cx='12' cy='10.8' r='2.2'/%3E%3C/svg%3E");
}

.tab-icon.mine {
	-webkit-mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Ccircle cx='12' cy='8' r='3.6'/%3E%3Cpath d='M4.8 20.5c0-3.6 3.2-5.8 7.2-5.8s7.2 2.2 7.2 5.8'/%3E%3C/svg%3E");
	mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Ccircle cx='12' cy='8' r='3.6'/%3E%3Cpath d='M4.8 20.5c0-3.6 3.2-5.8 7.2-5.8s7.2 2.2 7.2 5.8'/%3E%3C/svg%3E");
}

.tab-text {
	font-size: 21rpx;
	color: #9AA3B2;
	margin-top: 6rpx;
}

.tab-text.on {
	color: #2F6FED;
	font-weight: 600;
}
</style>
