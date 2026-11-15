<script>
	import { sendHeartbeat } from './utils/heartbeat.js'

	export default {
		onLaunch(){
			const isLogin=uni.getStorageSync("isLogin")
			const userRole=uni.getStorageSync("userRole")

			// 已登录就先打一次心跳，让后台看到「在线」
			if (isLogin) {
				sendHeartbeat(true)
			}

			// 没有登录 → 跳登录页
			if(!isLogin){
				uni.reLaunch({
					url:"/pages/login/login"
				})
			} else {
				// 已登录，按角色跳转
				if (userRole === "elder") {
					uni.reLaunch({
						url: "/pages/index/index"
					})
				} else if (userRole === "family") {
					uni.reLaunch({
						url: "/pages/family/index"
					})
				}
			}
		},

		// 从后台切回前台时补一次（内部有 10 分钟节流）
		onShow() {
			sendHeartbeat(false)
		}
	}
</script>

<style>
	/* ==========================================================
	   全局基础样式
	   设计变量（此文件为纯 CSS，变量以注释形式记录，修改时请保持一致）：
	   背景 #F4F6FA   卡片 #FFFFFF
	   主色 #2F6FED   主色浅 #EAF1FF
	   危险 #F04F4F   成功 #12B76A   警告 #F79009
	   文本 #1B2233 / #5C6675 / #98A0AE
	   分割线 #EEF1F5   卡片圆角 24rpx
	   ========================================================== */

	page {
		background: #F4F6FA;
		color: #1B2233;
		font-size: 28rpx;
		font-family: -apple-system, BlinkMacSystemFont, "Helvetica Neue",
			"PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", sans-serif;
		-webkit-font-smoothing: antialiased;
	}

	view,
	text,
	button,
	input,
	textarea,
	image {
		box-sizing: border-box;
	}

	/* 统一按钮外形，各页面只需覆盖颜色 */
	button {
		height: 88rpx;
		line-height: 88rpx;
		font-size: 30rpx;
		font-weight: 600;
		border-radius: 16rpx;
		padding: 0 32rpx;
		margin: 0 0 22rpx 0;
		border: none;
	}

	button::after {
		border: none;
	}

	/* 小尺寸按钮保持紧凑 */
	button[size="mini"] {
		height: 60rpx;
		line-height: 60rpx;
		font-size: 24rpx;
		font-weight: 500;
		padding: 0 22rpx;
		border-radius: 30rpx;
	}
</style>
