<template>
	<view class="container">

		<!-- ============================================ -->
		<!-- 概览：上排跌倒告警状态 / 下排四类构成 -->
		<!-- ============================================ -->
		<view class="overview">
			<view class="ov-caption">跌倒告警</view>

			<view class="stat-row">
				<view class="stat">
					<view class="stat-num">{{ statTotal }}</view>
					<view class="stat-label">总告警</view>
				</view>
				<view class="stat">
					<view class="stat-num week">{{ statWeek }}</view>
					<view class="stat-label">本周</view>
				</view>
				<view class="stat">
					<view class="stat-num" :class="pendingRecords > 0 ? 'pending' : 'zero'">
						{{ pendingRecords }}
					</view>
					<view class="stat-label">待确认</view>
				</view>
				<view class="stat">
					<view class="stat-num done">{{ ackedRecords }}</view>
					<view class="stat-label">已确认</view>
				</view>
			</view>

			<view class="ov-divider"></view>

			<view class="type-row">
				<view
					v-for="c in categories"
					:key="c.key"
					class="type-chip"
					:class="{ on: filterType === c.key }"
					@click="setFilter(c.key)"
				>
					<view class="ov-dot" :class="c.key"></view>
					<text class="type-name">{{ c.name }}</text>
					<text class="type-num" :class="c.key">{{ c.count }}</text>
					<view v-if="c.key === 'offline' && offlineUnread" class="unread-dot"></view>
				</view>
			</view>
		</view>

		<!-- ============================================ -->
		<!-- 筛选胶囊 -->
		<!-- ============================================ -->
		<scroll-view class="filters" scroll-x :show-scrollbar="false">
			<view
				class="filter"
				:class="{ on: filterType === 'all' }"
				@click="setFilter('all')"
			>全部 <text class="filter-num">{{ totalCount }}</text></view>

			<view
				v-for="c in categories"
				:key="c.key"
				class="filter"
				:class="{ on: filterType === c.key }"
				@click="setFilter(c.key)"
			>{{ c.name }} <text class="filter-num">{{ c.count }}</text></view>
		</scroll-view>

		<!-- ============================================ -->
		<!-- 24 小时图表：跟随筛选 -->
		<!-- ============================================ -->
		<view class="chart-card">
			<view class="chart-head">
				<text class="chart-title">最近 24 小时{{ filterType === 'all' ? '' : ' · ' + typeName }}</text>
				<text class="chart-max">{{ chart.max > 0 ? '峰值 ' + chart.max + ' 条' : '暂无数据' }}</text>
			</view>

			<view v-if="filterType === 'all'" class="legend">
				<view class="lg"><view class="sw fall"></view><text>跌倒</text></view>
				<view class="lg"><view class="sw help"></view><text>求助</text></view>
				<view class="lg"><view class="sw offline"></view><text>离线</text></view>
				<view class="lg"><view class="sw sedentary"></view><text>久坐</text></view>
			</view>

			<view class="chart-body">
				<view class="grid" style="bottom:25%"></view>
				<view class="grid" style="bottom:50%"></view>
				<view class="grid" style="bottom:75%"></view>
				<view class="grid base" style="bottom:0"></view>

				<view v-for="(c, i) in chart.cols" :key="i" class="col">
					<view
						v-for="t in barOrder"
						:key="t"
						class="seg"
						:class="[t, { hide: filterType !== 'all' }]"
						:style="segStyle(c[t], chart.max)"
					></view>
					<view
						class="seg"
						:class="[filterType, { hide: filterType === 'all' }]"
						:style="segStyle(c[filterType] || 0, chart.max)"
					></view>
				</view>
			</view>

			<view class="axis">
				<text>0时</text>
				<text>6时</text>
				<text>12时</text>
				<text>18时</text>
				<text>24时</text>
			</view>
		</view>

		<!-- ============================================ -->
		<!-- 工具条 -->
		<!-- ============================================ -->
		<view class="tools">
			<button
				size="mini"
				:disabled="pendingRecords === 0"
				@click="ackAll"
			>一键确认({{ pendingRecords }})</button>
			<button
				size="mini"
				:disabled="ackedRecords === 0"
				@click="clearAcked"
			>清除已确认</button>
			<button size="mini" @click="refreshAll">刷新</button>
		</view>

		<!-- ============================================ -->
		<!-- 统一时间线 -->
		<!-- ============================================ -->
		<view v-if="loadingNotifications" class="empty">正在加载…</view>
		<view v-else-if="filteredTimeline.length === 0" class="empty">
			{{ emptyText }}
		</view>

		<view
			v-for="item in filteredTimeline"
			:key="item.key"
			class="row"
			@click="openItem(item)"
		>
			<view class="row-bar" :class="[item.type, { strong: !item.acked }]"></view>

			<view class="row-icon" :class="item.type">{{ item.icon }}</view>

			<view class="row-body">
				<view class="row-title">
					{{ item.title }}
					<view v-if="item.key === latestOfflineKey && offlineUnread" class="unread-dot row-unread"></view>
				</view>
				<view class="row-sub">{{ item.sub }}</view>
			</view>

			<view v-if="item.statusText" class="row-status" :class="item.acked ? 'done' : 'pending'">
				{{ item.statusText }}
			</view>
			<view v-else class="row-arrow">›</view>
		</view>

		<tab-bar active="alarm"></tab-bar>
	</view>
</template>

<script>
	import {
		CLOUD_BASE_URL,
		fetchNotifications,
		groupByHour,
		normalizeNotifications,
		parseTime,
		startOfWeekMs
	} from '../../utils/alarm.js'

	import TabBar from '../../components/tab-bar/tab-bar.vue'

	const TYPE_META = {
		fall: { name: '跌倒', icon: '📣' },
		help: { name: '求助', icon: '🆘' },
		offline: { name: '离线', icon: '📡' },
		sedentary: { name: '久坐', icon: '🪑' }
	}

	// 堆叠时的自下而上顺序
	const BAR_ORDER = ['fall', 'help', 'offline', 'sedentary']
	const HOUR_MS = 3600000

	export default {
		components: { TabBar },
		data() {
			return {
				cloudBaseUrl: CLOUD_BASE_URL,
				deviceId: '',
				elderName: '',

				loadingNotifications: false,
				notificationError: '',

				originAlarmList: [],
				hourBuckets: [],
				helpRecords: [],
				helpAll: [],
				offlinePeriods: [],
				sedentaryRecords: [],

				offlineCount: 0,
				sedentaryCount: 0,
				offlineUnread: 0,
				sedentaryUnread: 0,

				filterType: 'all',
				// 'all' 全部时间 / 'week' 本周
				range: 'all',
				barOrder: BAR_ORDER,

				showCount: 0
			}
		},
		computed: {
			pendingRecords() {
				return this.originAlarmList.filter(i => i.status !== 'acked').length
			},
			ackedRecords() {
				return this.originAlarmList.filter(i => i.status === 'acked').length
			},

			// 跌倒告警总量 / 本周量
			statTotal() {
				return this.originAlarmList.length
			},
			statWeek() {
				const start = startOfWeekMs()
				return this.originAlarmList.filter((n) => {
					const t = this.toMs(n.created_text)
					return isFinite(t) && t >= start
				}).length
			},

			counts() {
				const start = this.rangeStart
				const inRange = (t) => !start || (isFinite(t) && t >= start)

				return {
					fall: this.originAlarmList.filter((n) => inRange(this.toMs(n.created_text))).length,
					help: this.helpAll.filter((h) => inRange(this.toMs(h.time))).length,
					offline: this.offlinePeriods.filter((p) => inRange(this.toMs(p.offline_start))).length,
					sedentary: this.sedentaryRecords
						.filter((s) => inRange(this.toMs(s.start_time || s.time || s.created_text))).length
				}
			},

			// 本周起点（0 表示不限时间）
			rangeStart() {
				return this.range === 'week' ? startOfWeekMs() : 0
			},
			totalCount() {
				const c = this.counts
				return c.fall + c.help + c.offline + c.sedentary
			},
			categories() {
				const c = this.counts
				return ['fall', 'help', 'offline', 'sedentary'].map((k) => ({
					key: k,
					name: TYPE_META[k].name,
					count: c[k]
				}))
			},

			// 四类记录合进一条时间线，按时间倒序
			allTimeline() {
				const list = []

				// 跌倒：按整点合并后的卡片
				this.hourBuckets.forEach((b) => {
					list.push({
						key: 'fall-' + b.key,
						type: 'fall',
						icon: TYPE_META.fall.icon,
						title: '跌倒告警',
						sub: '设备 ' + b.deviceId + ' · ' + b.rangeText + ' · 共 ' + b.count + ' 条',
						statusText: b.allAcked ? '已确认' : '待确认',
						acked: b.allAcked,
						sortMs: b.startMs,
						bucket: b
					})
				})

				// 主动求助
				this.helpAll.forEach((h, i) => {
					const t = parseTime(h.time || '')
					list.push({
						key: 'help-' + (h.id || i),
						type: 'help',
						icon: TYPE_META.help.icon,
						title: '老人主动求助',
						sub: (h.time || '—') + ' · 语音求助',
						statusText: h.acked ? '已确认' : '待确认',
						acked: Boolean(h.acked),
						sortMs: isFinite(t) ? t : 0,
						help: h
					})
				})

				// 离线记录
				this.offlinePeriods.forEach((p, i) => {
					const t = parseTime(p.offline_start || '')
					const sec = Number(p.duration_seconds) || 0
					list.push({
						key: 'offline-' + i,
						type: 'offline',
						icon: TYPE_META.offline.icon,
						title: '设备离线',
						sub: (p.offline_start || '—') + ' 起 · 断开 ' + this.fmtDuration(sec),
						statusText: '',
						acked: true,
						sortMs: isFinite(t) ? t : 0,
						route: 'offline'
					})
				})

				// 久坐记录（字段名做了兼容）
				this.sedentaryRecords.forEach((s, i) => {
					const startText = s.start_time || s.time || s.created_text || ''
					const t = parseTime(startText)
					let sec = Number(s.duration_seconds || 0)
					if (!sec && s.duration) sec = Number(s.duration) * 60
					list.push({
						key: 'sed-' + i,
						type: 'sedentary',
						icon: TYPE_META.sedentary.icon,
						title: '久坐提醒',
						sub: (startText || '—') + ' 起 · 持续约 ' + this.fmtDuration(sec),
						statusText: '',
						acked: true,
						sortMs: isFinite(t) ? t : 0,
						route: 'sedentary'
					})
				})

				return list.sort((a, b) => b.sortMs - a.sortMs)
			},

			// 按时间范围过滤：全部 / 本周
			scopedTimeline() {
				const start = this.rangeStart
				if (!start) return this.allTimeline
				return this.allTimeline.filter((x) => x.sortMs >= start)
			},

			filteredTimeline() {
				if (this.filterType === 'all') return this.scopedTimeline
				return this.scopedTimeline.filter((x) => x.type === this.filterType)
			},

			latestOfflineKey() {
				const first = this.filteredTimeline.find((x) => x.type === 'offline')
				return first ? first.key : ''
			},

			typeName() {
				return this.filterType === 'all' ? '全部' : TYPE_META[this.filterType].name
			},

			// 最近 24 小时，按小时分桶，四类各算各的
			chart() {
				const curHour = Math.floor(Date.now() / HOUR_MS) * HOUR_MS
				const cols = []
				for (let i = 0; i < 24; i++) {
					cols.push({ fall: 0, help: 0, offline: 0, sedentary: 0 })
				}

				const add = (rawTime, key) => {
					const ms = this.toMs(rawTime)
					if (!isFinite(ms) || ms <= 0) return
					const hourStart = Math.floor(ms / HOUR_MS) * HOUR_MS
					const idx = 23 - Math.round((curHour - hourStart) / HOUR_MS)
					if (idx >= 0 && idx < 24) cols[idx][key] += 1
				}

				this.originAlarmList.forEach((n) => add(n.created_text, 'fall'))
				this.helpAll.forEach((h) => add(h.time, 'help'))
				this.offlinePeriods.forEach((p) => add(p.offline_start, 'offline'))
				this.sedentaryRecords.forEach((s) => add(s.start_time || s.time || s.created_text, 'sedentary'))

				const shown = cols.map((c) => {
					if (this.filterType === 'all') {
						return c.fall + c.help + c.offline + c.sedentary
					}
					return c[this.filterType] || 0
				})
				const max = shown.reduce((m, v) => (v > m ? v : m), 0)

				return { cols, max }
			},

			emptyText() {
				if (this.notificationError) return this.notificationError
				if (this.filterType === 'all') return '暂无告警记录'
				return '暂无' + TYPE_META[this.filterType].name + '记录'
			}
		},
		onLoad(options) {
			this.deviceId = options.deviceId || uni.getStorageSync('deviceId') || ''
			this.elderName = options.elderName || uni.getStorageSync('elderName') || ''
			this.range = options.range === 'week' ? 'week' : 'all'
			this.loadCloudNotifications()
			this.loadHelpRecords()
			this.loadAuxCounts()
		},
		// 从详情页返回时刷新，避免确认状态不同步
		onShow() {
			this.showCount += 1
			if (this.showCount > 1) {
				this.refreshAll()
			}
		},
		methods: {
			getToken() {
				return uni.getStorageSync('token') || ''
			},

			setFilter(key) {
				this.filterType = key
			},

			setRange(r) {
				this.range = r
			},

			// 各种时间字段统一转成毫秒
			toMs(v) {
				if (v === null || v === undefined || v === '') return NaN
				if (typeof v === 'number') return v > 1e12 ? v : v * 1000
				return parseTime(v)
			},

			// 柱段高度：最小 6rpx，保证极少量的类型也看得见
			segStyle(count, max) {
				const n = Number(count) || 0
				if (!n || !max) return { height: '0rpx' }
				const pct = (n / max) * 100
				return { height: pct + '%', minHeight: '6rpx' }
			},

			refreshAll() {
				this.loadCloudNotifications()
				this.loadHelpRecords()
				this.loadAuxCounts()
			},

			// 秒 -> "12 分钟" / "1 小时 5 分"
			fmtDuration(sec) {
				const s = Number(sec) || 0
				if (s < 60) return s + ' 秒'
				const m = Math.floor(s / 60)
				if (m < 60) return m + ' 分钟'
				const h = Math.floor(m / 60)
				return h + ' 小时 ' + (m % 60) + ' 分'
			},

			openItem(item) {
				if (item.type === 'fall') return this.openHourDetail(item.bucket)
				if (item.type === 'help') return this.openHelp(item.help)
				if (item.route === 'offline') return this.goOfflinePeriods()
				if (item.route === 'sedentary') return this.goSedentaryList()
			},

			loadAuxCounts() {
				if (!this.deviceId) return
				const did = encodeURIComponent(this.deviceId)

				uni.request({
					url: this.cloudBaseUrl + '/api/device/offline_periods?device_id=' + did,
					method: 'GET',
					timeout: 5000,
					success: (res) => {
						const data = res && res.data && res.data.success ? res.data.data : []
						this.offlinePeriods = Array.isArray(data) ? data : []
						this.offlineCount = this.offlinePeriods.length
						const seen = Number(uni.getStorageSync('offlineSeenCount') || 0)
						this.offlineUnread = this.offlineCount > seen ? 1 : 0
						this.updateAlarmBadge()
					}
				})

				uni.request({
					url: this.cloudBaseUrl + '/api/device/sedentary_records?device_id=' + did,
					method: 'GET',
					timeout: 5000,
					success: (res) => {
						const data = res && res.data && res.data.success ? res.data.data : []
						this.sedentaryRecords = Array.isArray(data) ? data : []
						this.sedentaryCount = this.sedentaryRecords.length
						const seen = Number(uni.getStorageSync('sedentarySeenCount') || 0)
						this.sedentaryUnread = this.sedentaryCount > seen ? 1 : 0
						this.updateAlarmBadge()
					}
				})
			},

			loadHelpRecords() {
				if (!this.deviceId) return
				uni.request({
					url: this.cloudBaseUrl + '/api/alarm/history?device_id=' + encodeURIComponent(this.deviceId),
					method: 'GET',
					timeout: 5000,
					success: (res) => {
						const list = res && res.data && res.data.success && Array.isArray(res.data.data) ? res.data.data : []
						this.helpAll = list.filter(a => a && (a.location_source === 'help' || a.event === 'help'))
						this.helpRecords = this.helpAll.slice(0, 3)
						this.updateAlarmBadge()
					}
				})
			},

			openHelp(item) {
				uni.navigateTo({
					url: '/pages/family/helpDetail?deviceId=' + encodeURIComponent(this.deviceId) +
						'&id=' + encodeURIComponent(item.id || '') +
						'&time=' + encodeURIComponent(item.time || '') +
						'&source=' + encodeURIComponent(item.location_source || '') +
						'&acked=' + (item.acked ? '1' : '0')
				})
			},

			goHelpList() {
				uni.navigateTo({
					url: '/pages/family/helpList?deviceId=' + encodeURIComponent(this.deviceId)
				})
			},

			goOfflinePeriods() {
				uni.setStorageSync('offlineSeenCount', this.offlineCount)
				this.offlineUnread = 0
				this.updateAlarmBadge()
				uni.navigateTo({
					url: '/pages/family/offlinePeriods?deviceId=' + encodeURIComponent(this.deviceId) +
						'&elderName=' + encodeURIComponent(this.elderName || '')
				})
			},

			goSedentaryList() {
				uni.setStorageSync('sedentarySeenCount', this.sedentaryCount)
				this.sedentaryUnread = 0
				this.updateAlarmBadge()
				uni.navigateTo({
					url: '/pages/family/sedentaryList?deviceId=' + encodeURIComponent(this.deviceId)
				})
			},

			async loadCloudNotifications() {
				const token = this.getToken()
				if (!token) {
					this.notificationError = '登录信息不存在，请重新登录'
					this.originAlarmList = []
					this.doCalcMergeStat()
					return
				}
				if (this.loadingNotifications) return
				this.loadingNotifications = true
				this.notificationError = ''

				try {
					const res = await fetchNotifications(this.cloudBaseUrl, token)
					const result = normalizeNotifications(res)
					if (result.ok) {
						const all = result.list
						this.originAlarmList = this.deviceId ?
							all.filter(x => x.device_id === this.deviceId) : all
						this.notificationError = ''
						this.doCalcMergeStat()
					} else {
						this.notificationError = result.message
						this.doCalcMergeStat()
					}
				} catch (err) {
					console.error('告警请求异常', err)
					this.notificationError = '云端连接失败，请检查网络'
					this.doCalcMergeStat()
				} finally {
					this.loadingNotifications = false
				}
			},

			doCalcMergeStat() {
				this.hourBuckets = groupByHour(this.originAlarmList, this.deviceId)
				this.updateAlarmBadge()
			},

			updateAlarmBadge() {
				const pending = this.originAlarmList.filter(i => i.status !== 'acked').length
				const helpPending = this.helpAll.filter(h => !h.acked).length
				uni.setStorageSync('alarmUnread',
					Math.max(pending, helpPending, this.offlineUnread, this.sedentaryUnread))
			},

			openHourDetail(bucket) {
				uni.navigateTo({
					url: '/pages/family/alarmHourDetail?deviceId=' + encodeURIComponent(bucket.deviceId) +
						'&start=' + bucket.startMs
				})
			},

			ackAll() {
				if (this.pendingRecords === 0) return
				uni.showModal({
					title: '一键确认',
					content: `确定已知晓「${this.elderName || this.deviceId}」全部未确认跌倒告警？`,
					success: (m) => {
						if (m.confirm) this.postAction('/api/notifications/ack_all', '已全部确认')
					}
				})
			},

			clearAcked() {
				if (this.ackedRecords === 0) return
				uni.showModal({
					title: '清除已确认记录',
					content: '将清除全部已确认跌倒告警，未确认告警不受影响。',
					success: (m) => {
						if (m.confirm) this.postAction('/api/notifications/clear', '已清除')
					}
				})
			},

			ackFallHistory() {
				return new Promise(resolve => {
					uni.request({
						url: this.cloudBaseUrl + '/api/alarm/ack',
						method: 'POST',
						header: { 'Content-Type': 'application/json' },
						data: { device_id: this.deviceId, all_fallen: true },
						timeout: 5000,
						complete: resolve
					})
				})
			},

			async postAction(path, okTitle) {
				const token = this.getToken()
				if (!token) {
					uni.showToast({ title: '请重新登录', icon: 'none' })
					return
				}
				uni.showLoading({ title: '请稍候' })
				try {
					const res = await new Promise((resolve, reject) => {
						uni.request({
							url: this.cloudBaseUrl + path,
							method: 'POST',
							header: {
								'Authorization': 'Bearer ' + token,
								'Content-Type': 'application/json'
							},
							data: { device_id: this.deviceId },
							timeout: 10000,
							success: resolve,
							fail: reject
						})
					})
					if (res.statusCode === 200 && res.data && res.data.success) {
						// 同步确认云端告警历史，避免首页弹窗再次出现。
						if (path.indexOf('/notifications/ack_all') >= 0) {
							await this.ackFallHistory()
						}
						uni.showToast({
							title: `${okTitle}（${res.data.count || 0}条）`,
							icon: 'success'
						})
						await this.loadCloudNotifications()
					} else if (res.statusCode === 404) {
						uni.showToast({ title: '服务器需要更新后才支持该功能', icon: 'none' })
					} else {
						uni.showToast({
							title: (res.data && res.data.message) || '操作失败，请重试',
							icon: 'none'
						})
					}
				} catch (err) {
					uni.showToast({ title: '网络异常，请重试', icon: 'none' })
				} finally {
					uni.hideLoading()
				}
			}
		}
	}
</script>

<style scoped>
	.container {
		padding: 26rpx 30rpx 190rpx;
		background: #F4F6FA;
		min-height: 100vh;
		box-sizing: border-box;
	}

	/* ================= 概览 ================= */
	.overview {
		background: #FFFFFF;
		border-radius: 32rpx;
		padding: 24rpx 22rpx 10rpx;
		margin-bottom: 24rpx;
		box-shadow: 0 10rpx 30rpx rgba(31, 45, 74, 0.07);
	}

	.ov-caption {
		font-size: 21rpx;
		color: #98A0AE;
		letter-spacing: 1rpx;
		margin-bottom: 6rpx;
	}

	/* 上排：跌倒告警的四个状态 */
	.stat-row {
		display: flex;
		align-items: flex-end;
		padding: 4rpx 0 18rpx;
	}

	.stat {
		flex: 1;
		display: flex;
		flex-direction: column;
		align-items: center;
	}

	.stat-num {
		font-size: 40rpx;
		font-weight: 700;
		line-height: 1.15;
		color: #1B2233;
	}

	.stat-num.week { color: #2F6FED; }
	.stat-num.pending { color: #E08A00; }
	.stat-num.done { color: #07A35A; }
	.stat-num.zero { color: #C6CCD8; }

	.stat-label {
		font-size: 21rpx;
		color: #8A93A3;
		margin-top: 6rpx;
	}

	.ov-divider {
		height: 1rpx;
		background: #F2F5FA;
	}

	/* 下排：四类构成，可点筛选 */
	.type-row {
		display: flex;
		padding: 14rpx 0 4rpx;
	}

	.type-chip {
		flex: 1;
		display: flex;
		align-items: center;
		justify-content: center;
		padding: 10rpx 0;
		border-radius: 18rpx;
	}

	.type-chip.on {
		background: #F4F7FD;
	}

	.ov-dot {
		width: 10rpx;
		height: 10rpx;
		border-radius: 50%;
		margin-right: 8rpx;
		flex-shrink: 0;
	}

	.ov-dot.fall { background: #F04343; }
	.ov-dot.help { background: #F79009; }
	.ov-dot.offline { background: #98A0AE; }
	.ov-dot.sedentary { background: #2F6FED; }

	.type-name {
		font-size: 22rpx;
		color: #8A93A3;
		margin-right: 8rpx;
	}

	.type-num {
		font-size: 26rpx;
		font-weight: 700;
		color: #1B2233;
	}

	.type-num.fall { color: #F04343; }
	.type-num.help { color: #F79009; }
	.type-num.offline { color: #98A0AE; }
	.type-num.sedentary { color: #2F6FED; }

	/* ================= 筛选 ================= */
	.filters {
		white-space: nowrap;
		margin-bottom: 22rpx;
	}

	.filter {
		display: inline-block;
		padding: 12rpx 24rpx;
		margin-right: 14rpx;
		border-radius: 999rpx;
		background: #FFFFFF;
		border: 2rpx solid #E7ECF5;
		font-size: 25rpx;
		color: #5C6675;
	}

	.filter.on {
		background: #2F6FED;
		border-color: #2F6FED;
		color: #FFFFFF;
		font-weight: 600;
	}

	.filter-num {
		font-size: 22rpx;
		opacity: 0.75;
	}

	/* ================= 24 小时图表 ================= */
	.chart-card {
		background: #FFFFFF;
		border-radius: 32rpx;
		padding: 28rpx 26rpx 20rpx;
		margin-bottom: 24rpx;
		box-shadow: 0 10rpx 30rpx rgba(31, 45, 74, 0.07);
	}

	.chart-head {
		display: flex;
		align-items: baseline;
		justify-content: space-between;
		margin-bottom: 14rpx;
	}

	.chart-title {
		font-size: 30rpx;
		font-weight: 700;
		color: #1B2233;
	}

	.chart-max {
		font-size: 22rpx;
		color: #98A0AE;
	}

	.legend {
		display: flex;
		flex-wrap: wrap;
		gap: 18rpx;
		margin-bottom: 16rpx;
	}

	.lg {
		display: flex;
		align-items: center;
		font-size: 21rpx;
		color: #8A93A3;
	}

	.sw {
		width: 14rpx;
		height: 14rpx;
		border-radius: 4rpx;
		margin-right: 6rpx;
	}

	.sw.fall { background: #F04343; }
	.sw.help { background: #F79009; }
	.sw.offline { background: #98A0AE; }
	.sw.sedentary { background: #2F6FED; }

	.chart-body {
		position: relative;
		height: 220rpx;
		display: flex;
		align-items: flex-end;
		padding: 0 2rpx;
	}

	.grid {
		position: absolute;
		left: 0;
		right: 0;
		height: 1rpx;
		background: #EEF0F3;
	}

	.grid.base {
		background: #D9DDE3;
	}

	.col {
		position: relative;
		z-index: 2;
		flex: 1;
		height: 100%;
		display: flex;
		/* 从下往上堆叠 */
		flex-direction: column-reverse;
		align-items: center;
	}

	.seg {
		width: 14rpx;
		border-radius: 4rpx;
	}

	.seg.fall { background: #F04343; }
	.seg.help { background: #F79009; }
	.seg.offline { background: #98A0AE; }
	.seg.sedentary { background: #2F6FED; }

	.seg.hide {
		display: none;
	}

	.axis {
		display: flex;
		justify-content: space-between;
		margin-top: 12rpx;
		padding: 0 2rpx;
		font-size: 22rpx;
		color: #8B90A0;
	}

	/* ================= 工具条 ================= */
	.tools {
		display: flex;
		gap: 16rpx;
		margin-bottom: 24rpx;
	}

	.tools button {
		flex: 1;
		margin: 0;
		height: 66rpx;
		line-height: 62rpx;
		font-size: 24rpx;
		font-weight: 500;
		border-radius: 999rpx;
		background: #FFFFFF;
		color: #2F6FED;
		border: 2rpx solid #DCE6FA;
	}

	.tools button[disabled] {
		opacity: 0.45;
		color: #98A0AE;
		border-color: #E9EDF4;
	}

	/* ================= 时间线 ================= */
	.row {
		display: flex;
		align-items: center;
		background: #FFFFFF;
		border-radius: 28rpx;
		padding: 26rpx 24rpx 26rpx 0;
		margin-bottom: 18rpx;
		box-shadow: 0 8rpx 24rpx rgba(31, 45, 74, 0.05);
		overflow: hidden;
	}

	.row-bar {
		width: 6rpx;
		align-self: stretch;
		margin-right: 20rpx;
		border-radius: 0 6rpx 6rpx 0;
		flex-shrink: 0;
	}

	.row-bar.strong {
		width: 10rpx;
	}

	.row-bar.fall { background: #F04343; }
	.row-bar.help { background: #F79009; }
	.row-bar.offline { background: #98A0AE; }
	.row-bar.sedentary { background: #2F6FED; }

	.row-icon {
		width: 72rpx;
		height: 72rpx;
		line-height: 72rpx;
		text-align: center;
		font-size: 32rpx;
		border-radius: 50%;
		margin-right: 20rpx;
		flex-shrink: 0;
	}

	.row-icon.fall { background: #FFECEC; }
	.row-icon.help { background: #FFF7E6; }
	.row-icon.offline { background: #F2F5FA; }
	.row-icon.sedentary { background: #EAF1FF; }

	.row-body {
		flex: 1;
		min-width: 0;
	}

	.row-title {
		display: flex;
		align-items: center;
		font-size: 29rpx;
		font-weight: 700;
		color: #1B2233;
	}

	.unread-dot {
		width: 12rpx;
		height: 12rpx;
		border-radius: 50%;
		background: #F04343;
		margin-left: 8rpx;
		flex-shrink: 0;
	}

	.row-unread {
		margin-left: 10rpx;
	}

	.row-sub {
		font-size: 23rpx;
		color: #8A93A3;
		margin-top: 6rpx;
		line-height: 1.4;
	}

	.row-status {
		flex-shrink: 0;
		margin-left: 14rpx;
		font-size: 22rpx;
		padding: 6rpx 16rpx;
		border-radius: 999rpx;
	}

	.row-status.pending {
		color: #E08A00;
		background: #FFF7E6;
	}

	.row-status.done {
		color: #07A35A;
		background: #E9FBF1;
	}

	.row-arrow {
		font-size: 34rpx;
		color: #C6CCD8;
		margin-left: 10rpx;
	}

	.empty {
		color: #98A0AE;
		font-size: 26rpx;
		text-align: center;
		padding: 80rpx 10rpx;
	}
</style>
