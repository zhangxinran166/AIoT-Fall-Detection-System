<template>
	<view class="container">

		<!-- ============================================ -->
		<!-- 模式一：设置密保（需要已登录） -->
		<!-- ============================================ -->
		<block v-if="mode === 'set'">

			<view class="head">
				<view class="head-title">设置密保问题</view>
				<view class="head-sub">忘记密码时，答对问题就能自己重设密码</view>
			</view>

			<view class="card">
				<view class="field-label">选择一个密保问题</view>

				<view class="presets">
					<view
						v-for="q in presets"
						:key="q"
						class="preset"
						:class="{ on: question === q }"
						@click="question = q"
					>{{ q }}</view>
				</view>

				<input
					class="input"
					v-model="question"
					placeholder="也可以自己写一个问题"
					maxlength="60"
				/>
			</view>

			<view class="card">
				<view class="field-label">答案</view>
				<input
					class="input"
					v-model="answer"
					placeholder="答案不区分大小写和空格"
					maxlength="60"
				/>
				<view class="field-label mt">再输入一次</view>
				<input
					class="input"
					v-model="answer2"
					placeholder="确认答案"
					maxlength="60"
				/>
			</view>

			<button
				type="primary"
				class="submit"
				:disabled="saving"
				@click="saveQuestion"
			>{{ saving ? '保存中…' : '保存密保' }}</button>

			<view class="tip">
				提示：答案请设置成自己一定记得住的内容。设置后只有答对才能重置密码。
			</view>

		</block>

		<!-- ============================================ -->
		<!-- 模式二：忘记密码（未登录） -->
		<!-- ============================================ -->
		<block v-else>

			<view class="head">
				<view class="head-title">找回密码</view>
				<view class="head-sub">回答密保问题后，重设一个新密码</view>
			</view>

			<!-- 第 1 步：填账号 -->
			<block v-if="step === 1">
				<view class="card">
					<view class="field-label">你的账号</view>
					<input
						class="input"
						v-model="username"
						placeholder="请输入注册时的账号"
						maxlength="32"
					/>
				</view>

				<button
					type="primary"
					class="submit"
					:disabled="loading"
					@click="loadQuestion"
				>{{ loading ? '查询中…' : '下一步' }}</button>

				<view class="link-row" @click="backToLogin">返回登录</view>
			</block>

			<!-- 第 2 步：回答问题 + 设新密码 -->
			<block v-else>
				<view class="card question-card">
					<view class="q-label">密保问题</view>
					<view class="q-text">{{ question }}</view>
				</view>

				<view class="card">
					<view class="field-label">答案</view>
					<input
						class="input"
						v-model="answer"
						placeholder="请输入密保答案"
						maxlength="60"
					/>

					<view class="field-label mt">新密码</view>
					<input
						class="input"
						v-model="newPwd"
						:password="!showPwd"
						placeholder="至少 6 位"
						maxlength="128"
					/>

					<view class="field-label mt">确认新密码</view>
					<view class="pwd-wrap">
						<input
							class="input pwd-input"
							v-model="newPwd2"
							:password="!showPwd"
							placeholder="再输入一次"
							maxlength="128"
						/>
						<view
							class="eye"
							:class="{ on: showPwd }"
							@click="showPwd = !showPwd"
						></view>
					</view>
				</view>

				<button
					type="primary"
					class="submit"
					:disabled="submitting"
					@click="resetPassword"
				>{{ submitting ? '提交中…' : '重置密码' }}</button>

				<view class="link-row" @click="step = 1">换个账号</view>
			</block>

		</block>

	</view>
</template>

<script>
const CLOUD = 'http://101.43.88.85:5000'

export default {
	data() {
		return {
			mode: 'forgot',
			step: 1,
			loading: false,
			saving: false,
			submitting: false,

			username: '',
			question: '',
			answer: '',
			answer2: '',
			newPwd: '',
			newPwd2: '',
			showPwd: false,

			presets: [
				'我最喜欢的颜色是什么',
				'我小学的名字是什么',
				'我出生的城市是哪里',
				'我最好的朋友叫什么'
			]
		}
	},
	onLoad(options) {
		this.mode = options && options.mode === 'set' ? 'set' : 'forgot'
		if (this.mode === 'set') {
			// 设置密保需要登录态
			if (!uni.getStorageSync('token')) {
				uni.showToast({ title: '请先登录', icon: 'none' })
				setTimeout(() => uni.reLaunch({ url: '/pages/login/login' }), 800)
				return
			}
			this.username = uni.getStorageSync('username') || ''
			this.question = this.presets[0]
		}
	},
	methods: {
		backToLogin() {
			uni.reLaunch({ url: '/pages/login/login' })
		},

		toast(title) {
			uni.showToast({ title, icon: 'none', position: 'top' })
		},

		post(url, data, withToken) {
			const header = { 'Content-Type': 'application/json' }
			if (withToken) header['Authorization'] = 'Bearer ' + (uni.getStorageSync('token') || '')
			return new Promise((resolve, reject) => {
				uni.request({
					url: CLOUD + url,
					method: 'POST',
					header,
					data: data || {},
					timeout: 12000,
					success: resolve,
					fail: reject
				})
			})
		},

		// 保存密保（已登录）
		async saveQuestion() {
			const q = (this.question || '').trim()
			const a = (this.answer || '').trim()
			if (q.length < 2) return this.toast('请填写密保问题')
			if (!a) return this.toast('请填写答案')
			if (a !== (this.answer2 || '').trim()) return this.toast('两次输入的答案不一致')

			this.saving = true
			try {
				const r = await this.post('/api/auth/security/set', { question: q, answer: a }, true)
				if (r.statusCode === 200 && r.data && r.data.success) {
					uni.showToast({ title: '密保已保存', icon: 'success' })
					setTimeout(() => uni.navigateBack(), 800)
				} else if (r.statusCode === 401) {
					this.toast('登录已过期，请重新登录')
				} else {
					this.toast((r.data && r.data.message) || '保存失败')
				}
			} catch (e) {
				this.toast('网络异常，请重试')
			} finally {
				this.saving = false
			}
		},

		// 第 1 步：查这个账号的密保问题
		async loadQuestion() {
			const name = (this.username || '').trim()
			if (!name) return this.toast('请填写账号')

			this.loading = true
			try {
				const res = await new Promise((resolve, reject) => {
					uni.request({
						url: CLOUD + '/api/auth/security/question?username=' + encodeURIComponent(name),
						method: 'GET',
						timeout: 12000,
						success: resolve,
						fail: reject
					})
				})
				if (res.statusCode === 200 && res.data && res.data.success) {
					if (!res.data.has_question) {
						this.toast('该账号未设置密保问题，无法自助找回')
						return
					}
					this.question = res.data.question
					this.answer = ''
					this.newPwd = ''
					this.newPwd2 = ''
					this.step = 2
				} else {
					this.toast((res.data && res.data.message) || '查询失败')
				}
			} catch (e) {
				this.toast('网络异常，请重试')
			} finally {
				this.loading = false
			}
		},

		// 第 2 步：答对后重设密码
		async resetPassword() {
			if (!(this.answer || '').trim()) return this.toast('请填写密保答案')
			if ((this.newPwd || '').length < 6) return this.toast('新密码至少 6 位')
			if (this.newPwd !== this.newPwd2) return this.toast('两次输入的密码不一致')

			this.submitting = true
			try {
				const r = await this.post('/api/auth/security/reset', {
					username: (this.username || '').trim(),
					answer: (this.answer || '').trim(),
					new_password: this.newPwd
				}, false)

				if (r.statusCode === 200 && r.data && r.data.success) {
					uni.showToast({ title: '密码已重置', icon: 'success' })
					setTimeout(() => uni.reLaunch({ url: '/pages/login/login' }), 900)
				} else {
					this.toast((r.data && r.data.message) || '重置失败')
				}
			} catch (e) {
				this.toast('网络异常，请重试')
			} finally {
				this.submitting = false
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

.head {
	padding: 20rpx 10rpx 36rpx;
}

.head-title {
	font-size: 44rpx;
	font-weight: 700;
	color: #1B2233;
}

.head-sub {
	font-size: 25rpx;
	color: #8A93A3;
	margin-top: 12rpx;
}

.card {
	background: #FFFFFF;
	border-radius: 30rpx;
	padding: 28rpx 26rpx;
	margin-bottom: 26rpx;
	box-shadow: 0 8rpx 26rpx rgba(31, 45, 74, 0.06);
}

.field-label {
	font-size: 26rpx;
	color: #5C6675;
	font-weight: 600;
	margin-bottom: 14rpx;
}

.field-label.mt {
	margin-top: 26rpx;
}

.input {
	background: #FAFBFD;
	border: 2rpx solid #E7ECF5;
	border-radius: 16rpx;
	padding: 24rpx;
	font-size: 28rpx;
	color: #1B2233;
	min-height: 88rpx;
	box-sizing: border-box;
}

.presets {
	display: flex;
	flex-wrap: wrap;
	gap: 14rpx;
	margin-bottom: 20rpx;
}

.preset {
	padding: 14rpx 22rpx;
	border-radius: 999rpx;
	background: #F2F5FA;
	color: #5C6675;
	font-size: 24rpx;
}

.preset.on {
	background: #E8F0FF;
	color: #2F6FED;
	font-weight: 600;
}

.question-card {
	background: linear-gradient(135deg, #4A7CFF 0%, #57C9F2 100%);
	box-shadow: 0 14rpx 32rpx rgba(74, 124, 255, 0.26);
}

.q-label {
	font-size: 23rpx;
	color: rgba(255, 255, 255, 0.85);
}

.q-text {
	font-size: 34rpx;
	font-weight: 700;
	color: #FFFFFF;
	margin-top: 12rpx;
	line-height: 1.4;
}

.pwd-wrap {
	position: relative;
}

.pwd-input {
	padding-right: 100rpx;
}

.eye {
	position: absolute;
	right: 20rpx;
	top: 50%;
	margin-top: -26rpx;
	width: 52rpx;
	height: 52rpx;
	background-color: #A0A8B6;
	-webkit-mask-repeat: no-repeat;
	-webkit-mask-position: center;
	-webkit-mask-size: 42rpx 42rpx;
	mask-repeat: no-repeat;
	mask-position: center;
	mask-size: 42rpx 42rpx;
	-webkit-mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M2.5 12S6 5.8 12 5.8 21.5 12 21.5 12 18 18.2 12 18.2 2.5 12 2.5 12z'/%3E%3Ccircle cx='12' cy='12' r='3.2'/%3E%3C/svg%3E");
	mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M2.5 12S6 5.8 12 5.8 21.5 12 21.5 12 18 18.2 12 18.2 2.5 12 2.5 12z'/%3E%3Ccircle cx='12' cy='12' r='3.2'/%3E%3C/svg%3E");
}

.eye.on {
	background-color: #2F6FED;
	-webkit-mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M2.5 12S6 5.8 12 5.8 21.5 12 21.5 12 18 18.2 12 18.2 2.5 12 2.5 12z'/%3E%3Ccircle cx='12' cy='12' r='3.2'/%3E%3Cpath d='M4.5 4.5l15 15'/%3E%3C/svg%3E");
	mask-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M2.5 12S6 5.8 12 5.8 21.5 12 21.5 12 18 18.2 12 18.2 2.5 12 2.5 12z'/%3E%3Ccircle cx='12' cy='12' r='3.2'/%3E%3Cpath d='M4.5 4.5l15 15'/%3E%3C/svg%3E");
}

.submit {
	height: 96rpx;
	line-height: 96rpx;
	border-radius: 999rpx;
	background: linear-gradient(135deg, #4A7CFF 0%, #2F6FED 100%);
	font-size: 31rpx;
	font-weight: 700;
	box-shadow: 0 12rpx 28rpx rgba(47, 111, 237, 0.28);
	margin-top: 10rpx;
}

.link-row {
	text-align: center;
	font-size: 26rpx;
	color: #8A93A3;
	padding: 32rpx 0 10rpx;
}

.tip {
	font-size: 23rpx;
	color: #A0A8B6;
	line-height: 1.6;
	padding: 22rpx 12rpx 0;
}
</style>
