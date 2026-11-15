<template>
  <view class="login-container">
    <view class="title">老人跌倒预警系统</view>
    <view class="sub-title">账号登录与注册</view>

    <!-- 登录 / 注册切换 -->
    <view class="mode-wrap">
      <view
        class="mode-item"
        :class="{ active: mode === 'login' }"
        @click="switchMode('login')"
      >
        已有账号登录
      </view>
      <view
        class="mode-item"
        :class="{ active: mode === 'register' }"
        @click="switchMode('register')"
      >
        注册新账号
      </view>
    </view>

    <!-- 身份选择 -->
    <view class="section-title">请选择身份</view>

    <view class="role-wrap">
      <view
        class="role-card"
        :class="{ active: role === 'elder' }"
        @click="selectRole('elder')"
      >
        <view class="role-icon">👴</view>
        <view class="role-name">老人端</view>
        <view class="role-desc">一键监测、紧急呼救</view>
      </view>

      <view
        class="role-card"
        :class="{ active: role === 'family' }"
        @click="selectRole('family')"
      >
        <view class="role-icon">👨‍👩‍👧</view>
        <view class="role-name">家人端</view>
        <view class="role-desc">远程监护、查看告警</view>
      </view>
    </view>

    <!-- 账号表单 -->
    <view class="login-form">
      <input
        class="input"
        v-model="account"
        placeholder="请设置账号（3-32位字母、数字或下划线）"
        maxlength="32"
      />

      <view class="pwd-wrap">
        <input
          class="input pwd-input"
          v-model="pwd"
          :password="!showPwd"
          placeholder="请输入密码（至少6位）"
          maxlength="128"
        />
        <view
          class="eye"
          :class="{ on: showPwd }"
          @click="showPwd = !showPwd"
        ></view>
      </view>

      <input
        v-if="mode === 'register'"
        class="input"
        v-model="confirmPwd"
        type="password"
        placeholder="请再次输入密码"
        maxlength="128"
      />

      <!-- 老人注册时填写称呼，家人端会显示这个名字 -->
      <input
        v-if="mode === 'register' && role === 'elder'"
        class="input"
        v-model="nickname"
        placeholder="请输入老人称呼（例如：张爷爷）"
        maxlength="16"
      />

      <!-- 注册时填写绑定设备 -->
      <input
        v-if="mode === 'register'"
        class="input"
        v-model="deviceId"
        :placeholder="role === 'elder' ? '请输入设备编号' : '请输入要绑定的老人设备编号'"
        maxlength="64"
      />

      <!-- 注册时设置密保问题（忘记密码时用来找回） -->
      <view v-if="mode === 'register'" class="sec-block">
        <view class="sec-title">设置密保问题</view>
        <view class="sec-sub">忘记密码时，答对问题就能重设密码</view>

        <view class="sec-presets">
          <view
            v-for="q in secPresets"
            :key="q"
            class="sec-preset"
            :class="{ on: secQuestion === q }"
            @click="secQuestion = q"
          >{{ q }}</view>
        </view>

        <input
          class="input"
          v-model="secQuestion"
          placeholder="密保问题（可自己写）"
          maxlength="60"
        />

        <input
          class="input"
          v-model="secAnswer"
          placeholder="密保答案（不区分大小写）"
          maxlength="60"
        />
      </view>

      <button
        class="submit-btn"
        :disabled="loading"
        @click="submitForm"
      >
        {{
          loading
            ? '请稍候...'
            : mode === 'register'
              ? '注册并进入系统'
              : '登录'
        }}
      </button>

      <view class="forgot" v-if="mode === 'login'" @click="goForgot">
        忘记密码？
      </view>

      <view class="tip" v-if="mode === 'register'">
        注册时选择身份，并绑定要使用的设备。家人端注册后，还可以在“我的老人”页继续绑定更多老人。
      </view>
      <view class="tip" v-else>
        使用你自己注册的账号和密码登录。
      </view>
    </view>
  </view>
</template>

<script>
import { sendHeartbeat } from '../../utils/heartbeat.js';

export default {
  data() {
    return {
      mode: 'login',
      role: 'elder',
      account: '',
      pwd: '',
      confirmPwd: '',
      // 密码是否明文显示（小眼睛）
      showPwd: false,
      nickname: '',
      deviceId: 'ESP32_001',
      // 注册时的密保问题（忘记密码时用来找回）
      secQuestion: '我小学的名字是什么',
      secAnswer: '',
      secPresets: [
        '我小学的名字是什么',
        '我最喜欢的颜色是什么',
        '我出生的城市是哪里',
        '我最好的朋友叫什么'
      ],
      loading: false,
      cloudBaseUrl: 'http://101.43.88.85:5000'
    };
  },

  methods: {
    switchMode(newMode) {
      if (this.loading) return;
      this.mode = newMode;
    },

    selectRole(newRole) {
      this.role = newRole;
    },

    roleName(role) {
      return role === 'family' ? '家人端' : '老人端';
    },

    // 忘记密码：走密保问题找回
    goForgot() {
      uni.navigateTo({ url: '/pages/login/forgot' });
    },

    submitForm() {
      if (this.loading) return;

      const username = this.account.trim();
      const password = this.pwd;

      if (!this.role) {
        uni.showToast({
          title: '请先选择身份',
          icon: 'none'
        });
        return;
      }

      if (!/^[a-zA-Z0-9_]{3,32}$/.test(username)) {
        uni.showToast({
          title: '账号须为3-32位字母、数字或下划线',
          icon: 'none',
          duration: 2500
        });
        return;
      }

      if (password.length < 6) {
        uni.showToast({
          title: '密码至少需要6位',
          icon: 'none'
        });
        return;
      }

      if (this.mode === 'register') {
        if (password !== this.confirmPwd) {
          uni.showToast({
            title: '两次输入的密码不一致',
            icon: 'none'
          });
          return;
        }

        const device = this.deviceId.trim();

        if (!device) {
          uni.showToast({
            title: '请输入设备编号',
            icon: 'none'
          });
          return;
        }

        const nick = this.nickname.trim();

        if (this.role === 'elder' && !nick) {
          uni.showToast({
            title: '请输入老人称呼',
            icon: 'none'
          });
          return;
        }

        const secQ = (this.secQuestion || '').trim();
        const secA = (this.secAnswer || '').trim();

        if (secQ.length < 2) {
          uni.showToast({
            title: '请设置密保问题',
            icon: 'none'
          });
          return;
        }

        if (!secA) {
          uni.showToast({
            title: '请填写密保答案',
            icon: 'none'
          });
          return;
        }

        this.sendRequest(
          '/api/auth/register',
          {
            username: username,
            password: password,
            role: this.role,
            device_id: device,
            nickname: this.role === 'elder' ? nick : '',
            sec_question: secQ,
            sec_answer: secA
          },
          true
        );
      } else {
        this.sendRequest(
          '/api/auth/login',
          {
            username: username,
            password: password
          },
          false
        );
      }
    },

    sendRequest(path, requestData, isRegister) {
      this.loading = true;

      uni.showLoading({
        title: isRegister ? '正在注册...' : '正在登录...'
      });

      uni.request({
        url: this.cloudBaseUrl + path,
        method: 'POST',
        header: {
          'Content-Type': 'application/json'
        },
        data: requestData,

        success: (res) => {
          const data = res.data || {};

          if (
            res.statusCode === 200 &&
            data.success === true &&
            data.token &&
            (data.role === 'elder' || data.role === 'family')
          ) {
            // 服务器返回的身份才是最终身份
            const serverRole = data.role;
            const devices = Array.isArray(data.devices)
              ? data.devices
              : [];

            // 身份校验：登录接口只认账号密码，不校验上面选的身份。
            // 选了老人端却用家人端账号登录必须拦下，否则会直接跳到家人界面。
            if (serverRole !== this.role) {
              uni.hideLoading();
              this.loading = false;

              uni.showModal({
                title: '身份不匹配',
                content:
                  '这个账号是「' + this.roleName(serverRole) + '」账号，' +
                  '请在上方选择「' + this.roleName(serverRole) + '」后再登录。',
                showCancel: false,
                confirmText: '知道了',
                success: () => {
                  // 顺手把身份卡片切到正确的那个，用户再点一次登录即可
                  this.role = serverRole;
                }
              });
              return;
            }

            // 保存登录状态
            uni.setStorageSync('token', data.token);
            uni.setStorageSync('userRole', serverRole);
            uni.setStorageSync('isLogin', true);
            uni.setStorageSync('username', this.account.trim());
            uni.setStorageSync('nickname', data.nickname || '');
            uni.setStorageSync('devices', devices);

            // 登录成功立刻上报心跳，后台马上显示「在线」
            sendHeartbeat(true);

            if (serverRole === 'elder') {
              if (devices.length > 0) {
                uni.setStorageSync('deviceId', devices[0]);
              }
            } else {
              // 家人端：直接进首页，首页会自动使用第一个已绑定的老人
              uni.removeStorageSync('deviceId');
              uni.removeStorageSync('elderName');
            }

            uni.hideLoading();
            this.loading = false;

            // 家人端单点登录：这次登录把之前那台设备顶下线了
            if (!isRegister && data.kicked_previous) {
              uni.showToast({
                title: '已登录，原设备已下线',
                icon: 'none',
                duration: 2000
              });
            } else {
              uni.showToast({
                title: isRegister ? '注册成功' : '登录成功',
                icon: 'success'
              });
            }

            setTimeout(() => {
              if (serverRole === 'elder') {
                uni.reLaunch({
                  url: '/pages/index/index'
                });
              } else {
                uni.reLaunch({
                  url: '/pages/family/index'
                });
              }
            }, 300);
          } else {
            uni.hideLoading();
            this.loading = false;

            let message = '操作失败，请稍后重试';

            if (res.statusCode === 401) {
              message = '账号或密码错误';
            } else if (data.message) {
              message = data.message;
            } else if (data.error) {
              message = data.error;
            } else if (res.statusCode === 409) {
              message = '该账号可能已被注册';
            }

            uni.showToast({
              title: message,
              icon: 'none',
              duration: 2500
            });
          }
        },

        fail: (err) => {
          uni.hideLoading();
          this.loading = false;

          console.error('[账号请求失败]', err);

          uni.showModal({
            title: '连接服务器失败',
            content:
              '请检查手机网络和服务器地址。真机运行时，HTTP访问也可能受到系统网络安全设置限制。',
            showCancel: false
          });
        }
      });
    }
  }
};
</script>

<style scoped>
.login-container {
  padding: 70rpx 44rpx 90rpx;
  background: #F4F6FA;
  min-height: 100vh;
  box-sizing: border-box;
}

.title {
  font-size: 48rpx;
  font-weight: 700;
  text-align: center;
  color: #1B2233;
  letter-spacing: 1rpx;
}

.sub-title {
  text-align: center;
  font-size: 25rpx;
  color: #8A93A3;
  margin: 16rpx 0 48rpx;
  letter-spacing: 2rpx;
}

/* 登录 / 注册切换 */
.mode-wrap {
  display: flex;
  background: #E7ECF5;
  border-radius: 18rpx;
  padding: 8rpx;
  margin-bottom: 42rpx;
}

.mode-item {
  flex: 1;
  text-align: center;
  padding: 20rpx 8rpx;
  border-radius: 14rpx;
  color: #6B7385;
  font-size: 27rpx;
  transition: all 0.2s;
}

.mode-item.active {
  color: #007AFF;
  background: #FFFFFF;
  font-weight: 600;
  box-shadow: 0 4rpx 14rpx rgba(0, 122, 255, 0.14);
}

.section-title {
  font-size: 27rpx;
  color: #6B7385;
  margin-bottom: 18rpx;
  padding-left: 6rpx;
}

/* 身份卡片 */
.role-wrap {
  display: flex;
  gap: 24rpx;
}

.role-card {
  flex: 1;
  background: #FFFFFF;
  border-radius: 22rpx;
  padding: 34rpx 16rpx;
  text-align: center;
  border: 3rpx solid transparent;
  box-shadow: 0 4rpx 16rpx rgba(31, 45, 74, 0.05);
  transition: all 0.2s;
}

.role-card.active {
  border-color: #007AFF;
  background: #F5F9FF;
  box-shadow: 0 10rpx 26rpx rgba(0, 122, 255, 0.18);
}

.role-icon {
  font-size: 60rpx;
  line-height: 1.1;
  margin-bottom: 16rpx;
}

.role-name {
  font-size: 31rpx;
  font-weight: 600;
  color: #1B2233;
}

.role-card.active .role-name {
  color: #007AFF;
}

.role-desc {
  font-size: 22rpx;
  color: #98A0AE;
  margin-top: 10rpx;
}

/* 表单 */
.login-form {
  margin-top: 46rpx;
}

.input {
  width: 100%;
  background: #FFFFFF;
  border-radius: 18rpx;
  padding: 26rpx;
  font-size: 28rpx;
  margin-bottom: 22rpx;
  min-height: 92rpx;
  border: 2rpx solid #E9EDF4;
  color: #1B2233;
}

.submit-btn {
  background: #007AFF;
  color: #FFFFFF;
  border-radius: 18rpx;
  margin-top: 18rpx;
  box-shadow: 0 10rpx 26rpx rgba(0, 122, 255, 0.26);
}

.submit-btn[disabled] {
  opacity: 0.55;
  box-shadow: none;
}

.tip {
  text-align: center;
  margin-top: 28rpx;
  font-size: 23rpx;
  color: #98A0AE;
  line-height: 1.7;
}
/* ==========================================================
   新版视觉：浅蓝渐变背景 + 大圆角 + 胶囊按钮
   ========================================================== */
.login-container {
  padding: 90rpx 44rpx 100rpx;
  background: linear-gradient(180deg, #E8F0FF 0%, #F4F6FA 46%);
  min-height: 100vh;
  box-sizing: border-box;
}

.title {
  font-size: 54rpx;
  font-weight: 700;
  color: #1B2233;
  letter-spacing: 2rpx;
}

.sub-title {
  font-size: 26rpx;
  color: #8A93A3;
  margin: 18rpx 0 54rpx;
  letter-spacing: 3rpx;
}

.mode-wrap {
  background: #FFFFFF;
  border-radius: 999rpx;
  padding: 8rpx;
  margin-bottom: 46rpx;
  box-shadow: 0 8rpx 24rpx rgba(31, 45, 74, 0.06);
}

.mode-item {
  padding: 20rpx 8rpx;
  border-radius: 999rpx;
  color: #8A93A3;
  font-size: 27rpx;
}

.mode-item.active {
  color: #FFFFFF;
  background: linear-gradient(135deg, #4A7CFF 0%, #2F6FED 100%);
  font-weight: 600;
  box-shadow: 0 8rpx 20rpx rgba(47, 111, 237, 0.28);
}

.section-title {
  font-size: 27rpx;
  color: #5C6675;
  margin-bottom: 20rpx;
  padding-left: 6rpx;
}

.role-card {
  border-radius: 26rpx;
  padding: 38rpx 16rpx;
  box-shadow: 0 10rpx 28rpx rgba(31, 45, 74, 0.06);
}

.role-card.active {
  border-color: #2F6FED;
  background: #FFFFFF;
  box-shadow: 0 14rpx 32rpx rgba(47, 111, 237, 0.20);
}

.role-icon {
  font-size: 64rpx;
  margin-bottom: 18rpx;
}

.role-name {
  font-size: 32rpx;
}

.role-desc {
  font-size: 23rpx;
  margin-top: 12rpx;
}

.login-form {
  margin-top: 52rpx;
}

.input {
  border-radius: 20rpx;
  padding: 28rpx;
  min-height: 98rpx;
  border: 2rpx solid #E3E9F3;
  background: #FFFFFF;
}

.submit-btn {
  height: 100rpx;
  line-height: 100rpx;
  border-radius: 999rpx;
  margin-top: 24rpx;
  background: linear-gradient(135deg, #4A7CFF 0%, #2F6FED 100%);
  box-shadow: 0 14rpx 30rpx rgba(47, 111, 237, 0.30);
  font-size: 32rpx;
}

.tip {
  margin-top: 32rpx;
  font-size: 23rpx;
  color: #A0A8B6;
}

/* 忘记密码入口 */
.forgot {
  text-align: center;
  font-size: 26rpx;
  color: #2F6FED;
  padding: 28rpx 0 6rpx;
  font-weight: 500;
}

/* 注册时的密保问题 */
.sec-block {
  margin-top: 28rpx;
  padding: 26rpx 24rpx 20rpx;
  background: #FFFFFF;
  border-radius: 22rpx;
  border: 2rpx solid #E7ECF5;
}

.sec-title {
  font-size: 28rpx;
  font-weight: 700;
  color: #1B2233;
}

.sec-sub {
  font-size: 23rpx;
  color: #98A0AE;
  margin-top: 8rpx;
  margin-bottom: 20rpx;
}

.sec-presets {
  display: flex;
  flex-wrap: wrap;
  gap: 12rpx;
  margin-bottom: 6rpx;
}

.sec-preset {
  padding: 12rpx 20rpx;
  border-radius: 999rpx;
  background: #F2F5FA;
  color: #5C6675;
  font-size: 23rpx;
}

.sec-preset.on {
  background: #E8F0FF;
  color: #2F6FED;
  font-weight: 600;
}

.sec-block .input {
  margin-top: 16rpx;
}

/* 密码输入框 + 小眼睛 */
.pwd-wrap {
  position: relative;
}

.pwd-input {
  padding-right: 100rpx;
}

.eye {
  position: absolute;
  right: 24rpx;
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
</style>