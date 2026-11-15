// ============================================================
// AI 助手：根据实时数据回答家人的问题
// 不调用外部大模型，只整理云端已有数据。
// ============================================================

export const SUGGESTIONS = [
	'老人现在怎么样',
	'老人在哪里',
	'最近有跌倒吗',
	'设备还连着吗',
	'设备电量'
]

function pick(name) {
	return name || '老人'
}

function yesNo(v) {
	if (v === true) return '是'
	if (v === false) return '否'
	return '未知'
}

function statusText(s) {
	if (!s || !s.time) return '暂无数据'
	if (s.fallen) return '检测到跌倒'
	if (s.monitoring) return '正在监测中'
	return '监测已停止'
}

function linkText(s) {
	if (!s) return '未知'
	const on = []
	if (s.wifi) on.push('WiFi')
	if (s.bluetooth) on.push('蓝牙')
	if (s.g4) on.push('4G')
	return on.length ? on.join(' + ') : '无链路'
}

function motionText(s) {
	const lines = []
	if (s.accMag != null) lines.push(`加速度：${s.accMag} g`)
	if (s.gyroMag != null) lines.push(`角速度：${s.gyroMag} dps`)
	return lines
}

function answerLocation(ctx) {
	const name = pick(ctx.elderName)
	const loc = ctx.location || {}
	if (!loc.ok) {
		return `暂时拿不到${name}的定位。\n可能是手机没开定位权限，或者设备还没上报过位置。`
	}
	const where = loc.address || (loc.lat.toFixed(5) + ', ' + loc.lng.toFixed(5))
	const lines = [
		`${name}现在在：${where}`,
		`定位来源：${loc.sourceText || '未知'}`,
		`更新时间：${loc.time || '未知'}`
	]
	if (loc.accuracy) lines.push(`定位精度：约 ${loc.accuracy} 米`)
	return lines.join('\n')
}

function answerStatus(ctx) {
	const name = pick(ctx.elderName)
	const s = ctx.status || {}
	if (!s.time) {
		return `暂时没有收到${name}的设备数据。\n可能是设备没开机，或者网络断了。`
	}
	const lines = [`${name}当前状态：${statusText(s)}`]
	if (s.wearingOk === false) lines.push('佩戴方向：异常，请检查设备是否戴反')
	else if (s.wearingOk === true) lines.push('佩戴方向：正常')
	lines.push(...motionText(s))
	lines.push(`通信链路：${linkText(s)}`)
	lines.push(`最近上报：${s.time}`)
	if (s.fallen) {
		lines.push('')
		lines.push('⚠️ 设备判断可能有跌倒，请尽快确认老人情况。')
	}
	return lines.join('\n')
}

function answerAlarms(ctx) {
	const name = pick(ctx.elderName)
	const a = ctx.alarms || {}
	if (!a.total) {
		return `最近没有${name}的跌倒告警记录，一切正常。`
	}
	const lines = [`最近共有 ${a.total} 条跌倒告警。`]
	if (a.lastTime) lines.push(`最近一次：${a.lastTime}`)
	if (a.pending > 0) lines.push(`其中 ${a.pending} 条还没确认，建议去「告警」页处理。`)
	else lines.push('这些告警都已经确认过了。')
	return lines.join('\n')
}

function answerLink(ctx) {
	const s = ctx.status || {}
	if (!s.time) return '设备暂时没有上报，判断不了连接情况。'
	const lines = [`当前通信方式：${linkText(s)}`]
	lines.push(`WiFi：${yesNo(s.wifi)}　蓝牙：${yesNo(s.bluetooth)}　4G：${yesNo(s.g4)}`)
	lines.push(`最近一次上报：${s.time}`)
	return lines.join('\n')
}

function answerFall(ctx) {
	const s = ctx.status || {}
	const name = pick(ctx.elderName)
	if (!s.time) return `暂时没有${name}的最新数据，无法判断。`
	if (s.fallen) {
		return `⚠️ 设备现在判断${name}可能处于跌倒状态。\n请立即联系老人或去「位置」页看看他在哪。`
	}
	return `${name}目前没有检测到跌倒，状态是「${statusText(s)}」。`
}

function answerBattery(ctx) {
	const s = ctx.status || {}
	if (!s.batteryAvailable) {
		return '这台设备没有上报电量信息（固件可能没接电量检测）。'
	}
	const lines = [`设备电量：${s.batteryPercent}%`]
	if (s.batteryLow) lines.push('⚠️ 电量偏低，建议尽快充电。')
	return lines.join('\n')
}

function answerOverview(ctx) {
	const name = pick(ctx.elderName)
	const s = ctx.status || {}
	const loc = ctx.location || {}
	const a = ctx.alarms || {}

	const lines = [`${name}的情况：`]
	lines.push(`· 状态：${s.time ? statusText(s) : '暂无数据'}`)
	if (s.wearingOk === false) lines.push('· 佩戴：异常，请检查是否戴反')
	lines.push(...motionText(s).map(x => '· ' + x))
	lines.push(`· 位置：${loc.ok ? (loc.address || '已定位') : '暂无'}`)
	lines.push(`· 告警：${a.total ? '共 ' + a.total + ' 条' : '无'}`)
	return lines.join('\n')
}

const HELP = [
	'我可以回答这些：',
	'· 老人现在怎么样',
	'· 老人在哪里',
	'· 最近有跌倒吗',
	'· 设备还连着吗',
	'· 设备电量',
	'',
	'回答的内容都来自设备实时上报，不是编的。'
].join('\n')

export function buildAnswer(ctx, question) {
	const q = String(question || '').toLowerCase().trim()
	if (!q) return HELP

	const has = (words) => words.some((w) => q.indexOf(w) !== -1)

	if (has(['帮助', '能问', '能做', '做什么', '会什么', '怎么用', '功能', '说明'])) return HELP
	if (has(['你好', '您好', '在吗', 'hello', 'hi'])) {
		return '你好，我是跌倒监测助手。\n可以问我老人的状态、位置、有没有跌倒。'
	}

	if (has(['跌倒', '摔', '摔倒'])) return answerFall(ctx)
	if (has(['位置', '在哪', '哪里', '地址', '定位'])) return answerLocation(ctx)
	if (has(['电量', '电池', '充电'])) return answerBattery(ctx)
	if (has(['连接', '连着', '连上', '在线', '链路', 'wifi', 'wi-fi', '蓝牙', '信号', '4g'])) return answerLink(ctx)
	if (has(['告警', '报警', '警报', '记录'])) return answerAlarms(ctx)
	if (has(['状态', '怎么样', '现状', '情况', '还好', '安全'])) return answerStatus(ctx)

	return answerOverview(ctx)
}
