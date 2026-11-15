<template>
	<view class="container">
		<view class="page-head">
			<view class="title">告警记录列表</view>
			<view class="sub">最近收到的跌倒告警</view>
		</view>

		<!-- 没有告警记录时显示 -->
		<view v-if="alertList.length === 0" class="empty">
			<view class="empty-icon">📭</view>
			<view class="empty-text">暂无跌倒告警消息</view>
		</view>

		<!-- 未读项用高亮样式区分 -->
		<view
			v-for="(item, index) in alertList"
			:key="index"
			class="alert-item"
			:class="{ unread: !item.isRead }"
		>
			<view class="item-head">
				<text class="time">{{ item.time }}</text>
				<text v-if="!item.isRead" class="red-tag">未读</text>
			</view>
			<view class="content">⚠️ 检测到老人跌倒！设备号：{{ item.deviceId }}</view>
		</view>
	</view>
</template>

<script>
export default {
	data() {
		return {
			alertList: []
		}
	},
	onLoad() {
		//页面加载时读取本地保存的告警
		let data = uni.getStorageSync("alertMsg")
		if(data){
			this.alertList = data
			//把所有未读消息，全部改成已读
			this.alertList.forEach(item=>{
				item.isRead = true
			})
			//写回存储
			uni.setStorageSync("alertMsg",this.alertList)
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

.alert-item {
	background: #FFFFFF;
	border-radius: 22rpx;
	padding: 28rpx 30rpx;
	margin-bottom: 22rpx;
	box-shadow: 0 4rpx 18rpx rgba(31, 45, 74, 0.05);
	border-left: 8rpx solid #F04F4F;
}

.item-head {
	display: flex;
	align-items: center;
	justify-content: space-between;
	margin-bottom: 12rpx;
}

.time {
	color: #8A93A3;
	font-size: 24rpx;
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
	color: #1B2233;
	font-size: 29rpx;
	font-weight: 600;
	line-height: 1.6;
}

.unread {
	background: #FFF7F7;
}
</style>
