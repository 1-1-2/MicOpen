package com.miclink.open.ui.screen

import android.os.SystemClock
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miclink.open.AudioSourceOption
import com.miclink.open.MainUiState
import com.miclink.open.MainViewModel
import com.miclink.open.MicCaptureService
import com.miclink.open.R
import com.miclink.open.theme.ThemeMode
import com.miclink.open.theme.ThemeState
import com.miclink.open.ui.component.CardDivider
import com.miclink.open.ui.component.DotTrack
import com.miclink.open.ui.component.HintText
import com.miclink.open.ui.component.LabeledSwitchRow
import com.miclink.open.ui.component.LocalMicLinkTokens
import com.miclink.open.ui.component.SectionCard
import com.miclink.open.ui.component.SettingsNavRow
import com.miclink.open.ui.component.StatusBadge
import com.miclink.open.ui.theme.WomicTheme

/**
 * HomeScreen -- 主页（定稿规范 D-15…D-18）。
 *
 * 三张卡，全部数据来自服务真实回传（数据诚实性：测不到的显示「—」）：
 *  1. **状态卡**（D-16）：大状态徽标 + 右端监听地址 + 四行字段
 *     （传输方式 / 延迟 / 丢包 / 时长）+ 1px 分隔线 + 主按钮；
 *  2. **音量卡**（D-18）：十档离散点轨（协议 0-9，吸附整数档）+ 静音开关；
 *  3. **设置摘要卡**（D-17）：音源 / 系统降噪 / 主题三行跳转。
 *
 * 主按钮（D-15）：空闲 = accent 填充「启动」；运行 = err 描边「停止」+
 * err 呼吸外发光（仅运行态）。「停止」不直接停服务，而是经
 * [MainViewModel.requestStopConfirmation] 由 AppNav 弹二次确认（C-05）；
 * 前台服务通知的「停止」走同一条确认路径（MainActivity EXTRA_CONFIRM_STOP）。
 *
 * @param vm 主界面状态容器；
 * @param onStartClicked 启动服务（含权限申请流程，由 Activity 实现）；
 * @param onOpenCaptureSettings 跳转采集设置，参数为落点锚点（[CaptureFocus]：
 *   「音源」→ 输入卡、「系统降噪」→ 系统效果卡，D-05）；
 * @param onOpenAppearance 跳转外观页。
 */
@Composable
fun HomeScreen(
    vm: MainViewModel,
    onStartClicked: () -> Unit,
    onOpenCaptureSettings: (String) -> Unit,
    onOpenAppearance: () -> Unit,
    modifier: Modifier = Modifier
) {
    val state = vm.uiState

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        StatusSection(
            state = state,
            onStartClicked = onStartClicked,
            onStopRequested = { vm.requestStopConfirmation() }
        )
        VolumeSection(
            state = state,
            onVolumeChanged = { vm.onVolumeChanged(it) },
            onMuteChanged = { vm.onMuteChanged(it) }
        )
        SettingsSummarySection(
            state = state,
            themeState = vm.themeState,
            onOpenCaptureSettings = onOpenCaptureSettings,
            onOpenAppearance = onOpenAppearance
        )
    }
}

// ------------------------------------------------------------------
// 状态卡（D-16）
// ------------------------------------------------------------------

@Composable
private fun StatusSection(
    state: MainUiState,
    onStartClicked: () -> Unit,
    onStopRequested: () -> Unit
) {
    SectionCard {
        // ① 大状态徽标（文字 = 服务状态词，与前台通知标题同一事实来源）+ 右端地址
        Row(verticalAlignment = Alignment.CenterVertically) {
            val (badgeColor, badgeContainer) = statusBadgeColors(state.state)
            StatusBadge(
                text = MicCaptureService.stateWord(state.state),
                color = badgeColor,
                container = badgeContainer
            )
            Spacer(Modifier.weight(1f))
            val address = state.listeningAddress.ifBlank {
                stringResource(R.string.home_value_unknown)
            }
            // 无障碍标签在组合层取（semantics lambda 不是 @Composable 上下文）
            val addressLabel = stringResource(R.string.phone_address)
            Text(
                text = address,
                style = MaterialTheme.typography.bodyMedium.copy(
                    fontSize = 13.sp,
                    fontFamily = FontFamily.Monospace
                ),
                color = LocalMicLinkTokens.current.text2,
                maxLines = 1,
                // 无障碍标签（可见文本只有地址；朗读时补上用途）
                modifier = Modifier.semantics {
                    contentDescription = "$addressLabel $address"
                }
            )
        }

        Spacer(Modifier.height(16.dp))

        // ② 分行字段（禁「·」分隔）：只保留**有真实来源**的字段。
        // R-20：延迟 / 丢包两行已删除——app 侧没有测量通道，摆两行恒为「—」的
        // 伪遥测字段只会误导用户（诚实性纪律：没有来源就不要有字段）。
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            StatusFieldRow(
                label = stringResource(R.string.home_field_transport),
                value = state.transport.ifBlank { stringResource(R.string.home_value_unknown) }
            )
            StatusFieldRow(
                label = stringResource(R.string.home_field_duration),
                value = formatSessionDuration(
                    startElapsedMs = state.sessionStartElapsedMs,
                    nowElapsedMs = SystemClock.elapsedRealtime()
                ).ifEmpty { stringResource(R.string.home_value_unknown) }
            )
        }

        // ③ 1px 分隔线（上下各 16）
        Spacer(Modifier.height(16.dp))
        CardDivider()
        Spacer(Modifier.height(16.dp))

        // ④ 主按钮
        MainActionButton(
            running = state.isRunning,
            onStart = onStartClicked,
            onStopRequest = onStopRequested
        )
    }
}

/** 分行字段行（D-16）：标签 13sp text-2 左、值 14sp text-1 右，行高 24。 */
@Composable
private fun StatusFieldRow(label: String, value: String) {
    val tokens = LocalMicLinkTokens.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(24.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium.copy(fontSize = 13.sp),
            color = tokens.text2
        )
        Spacer(Modifier.weight(1f))
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium.copy(fontSize = 14.sp),
            color = tokens.text1,
            maxLines = 1
        )
    }
}

/** 服务状态 -> 徽标配色（内容色, 容器色 = 语义色 12% 混合 bg-card）。 */
@Composable
private fun statusBadgeColors(state: String): Pair<Color, Color> {
    val tokens = LocalMicLinkTokens.current
    return when (state) {
        MicCaptureService.STATE_STREAMING -> tokens.ok to tokens.okContainer
        MicCaptureService.STATE_CONNECTED -> tokens.accent to tokens.accentContainer
        MicCaptureService.STATE_LISTENING -> tokens.warn to tokens.warnContainer
        MicCaptureService.STATE_STARTING -> tokens.accent to tokens.accentContainer
        MicCaptureService.STATE_ERROR -> tokens.err to tokens.errContainer
        // 空闲 / 已停止：中性色（无语义色可用，不用假彩色）
        else -> tokens.text2 to tokens.bgSunken
    }
}

/**
 * 会话时长文本（mm:ss，超过一小时 h:mm:ss）。
 *
 * @param startElapsedMs 服务记录的本回合话开始时刻（elapsedRealtime；0 = 无会话）。
 * @param nowElapsedMs 当前时刻（调用方传入，保证纯函数可测）。
 * @return 无会话返回空串（调用方显示「—」）。
 */
private fun formatSessionDuration(startElapsedMs: Long, nowElapsedMs: Long): String {
    if (startElapsedMs <= 0L) {
        return ""
    }
    val seconds = ((nowElapsedMs - startElapsedMs) / 1000L).coerceAtLeast(0L)
    val hours = seconds / 3600
    val minutes = (seconds % 3600) / 60
    val secs = seconds % 60
    return if (hours > 0) {
        String.format("%d:%02d:%02d", hours, minutes, secs)
    } else {
        String.format("%02d:%02d", minutes, secs)
    }
}

// ------------------------------------------------------------------
// 主按钮（D-15 / D-22 呼吸外发光）
// ------------------------------------------------------------------

/** 呼吸外发光半周期（ms）：600ms 正向 + 600ms 反向 = 1200ms 整周期。 */
private const val GLOW_HALF_CYCLE_MS = 600

/** 呼吸外发光峰值 alpha（0 → 0.30 → 0）。 */
private const val GLOW_MAX_ALPHA = 0.30f

/**
 * 主操作按钮：高 48、R2、整宽。
 *
 * - 空闲：accent 填充 + onAccent 文字「启动」；
 * - 运行：transparent 底 + err 1px 描边 + err 文字「停止」+ err 呼吸外发光。
 *
 * 呼吸参数与 Windows 接收端一致（D-22）：周期 1200ms（tween 600ms +
 * RepeatMode.Reverse）、alpha 0→0.30→0、err 语义色、**仅采集运行中**。
 * 修改任一端须同步另一端（跨端一致性验收项）。
 */
@Composable
private fun MainActionButton(
    running: Boolean,
    onStart: () -> Unit,
    onStopRequest: () -> Unit
) {
    val tokens = LocalMicLinkTokens.current
    val shape = RoundedCornerShape(8.dp)
    val glowAlpha = rememberBreathingGlow(enabled = running)

    // R-16b：容器高度**恒为 60dp**（发光层与按钮都常驻），呼吸动画只作用于
    // 绘制层的 alpha，绝不参与布局。
    //
    // 旧实现 `if (running && glowAlpha > 0.001f)` 会把 60dp 的发光层在 alpha
    // 归零时**移出组合** → 容器 60dp → 48dp；下一轮插回来又 48 → 60：
    // 每 1.2s 伸缩 12dp，用户看到"页面每隔几秒跳一下"。
    // 纪律：任何动画只许改绘制（alpha / 变换 / 颜色），不许改布局尺寸。
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(60.dp),
        contentAlignment = Alignment.Center
    ) {
        // 呼吸外发光层：常驻组合，只改 alpha（0 时不可见，但**不退出布局**）。
        // 空闲态/alpha 归零时连 blur 都不挂，省掉无意义的模糊开销。
        val glowVisible = running && glowAlpha > 0.001f
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(60.dp)
                .then(if (glowVisible) Modifier.blur(16.dp) else Modifier)
                .background(
                    color = tokens.err.copy(alpha = if (glowVisible) glowAlpha else 0f),
                    shape = RoundedCornerShape(12.dp)
                )
        )
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(48.dp)
                .clip(shape)
                .background(if (running) Color.Transparent else tokens.accent)
                .border(
                    width = if (running) 1.dp else 0.dp,
                    color = if (running) tokens.err else Color.Transparent,
                    shape = shape
                )
                .clickable { if (running) onStopRequest() else onStart() },
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = stringResource(
                    if (running) R.string.home_action_stop else R.string.home_action_start
                ),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Medium,
                color = if (running) tokens.err else tokens.onAccent
            )
        }
    }
}

/**
 * 呼吸外发光 alpha（0 → [GLOW_MAX_ALPHA] → 0，周期 2 * [GLOW_HALF_CYCLE_MS]）。
 *
 * 仅 [enabled]（采集运行中）时启动无限动画；停止时组件直接不创建
 * infinite transition（省电，也不留常驻重组）。
 */
@Composable
private fun rememberBreathingGlow(enabled: Boolean): Float {
    if (!enabled) {
        return 0f
    }
    val transition = rememberInfiniteTransition(label = "stop-glow")
    val alpha by transition.animateFloat(
        initialValue = 0f,
        targetValue = GLOW_MAX_ALPHA,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = GLOW_HALF_CYCLE_MS, easing = LinearEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "stop-glow-alpha"
    )
    return alpha
}

// ------------------------------------------------------------------
// 音量卡（D-18）
// ------------------------------------------------------------------

@Composable
private fun VolumeSection(
    state: MainUiState,
    onVolumeChanged: (Int) -> Unit,
    onMuteChanged: (Boolean) -> Unit
) {
    val tokens = LocalMicLinkTokens.current
    // 静音联动：静音开 -> 读数固定「档位 0 · 静音」、点轨置灰（协议 0 = 真静音）
    val displayVolume = if (state.muted) 0 else state.volume
    SectionCard {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = stringResource(R.string.home_volume_title),
                style = MaterialTheme.typography.titleSmall,
                color = tokens.text1
            )
            Spacer(Modifier.weight(1f))
            Text(
                text = stringResource(
                    R.string.home_volume_reading,
                    displayVolume,
                    volumeDb(displayVolume, stringResource(R.string.home_volume_db_mute))
                ),
                style = MaterialTheme.typography.bodyMedium.copy(
                    fontSize = 14.sp,
                    fontFamily = FontFamily.Monospace
                ),
                fontWeight = FontWeight.Medium,
                color = tokens.text1,
                maxLines = 1
            )
        }
        Spacer(Modifier.height(4.dp))
        HintText(text = stringResource(R.string.home_volume_hint))
        Spacer(Modifier.height(8.dp))
        DotTrack(
            value = displayVolume,
            onValueChange = onVolumeChanged,
            enabled = !state.muted
        )
        Spacer(Modifier.height(4.dp))
        LabeledSwitchRow(
            title = stringResource(R.string.home_mute),
            description = stringResource(R.string.home_mute_desc),
            checked = state.muted,
            onCheckedChange = onMuteChanged
        )
    }
}

/**
 * 协议音量档位 -> dB 语义（手册 §7.2）：0 静音；1-4 每档 -6 dB；5 原样；
 * 6-9 每档 +6 dB。
 *
 * @param muteLabel 0 档的展示词（字符串资源传入，避免 UI 文案硬编码）。
 */
private fun volumeDb(volume: Int, muteLabel: String): String = when {
    volume <= 0 -> muteLabel
    volume == 5 -> "0 dB"
    volume < 5 -> "-${(5 - volume) * 6} dB"
    else -> "+${(volume - 5) * 6} dB"
}

// ------------------------------------------------------------------
// 设置摘要卡（D-17）
// ------------------------------------------------------------------

@Composable
private fun SettingsSummarySection(
    state: MainUiState,
    themeState: ThemeState,
    onOpenCaptureSettings: (String) -> Unit,
    onOpenAppearance: () -> Unit
) {
    SectionCard {
        SettingsNavRow(
            label = stringResource(R.string.home_row_source),
            value = audioSourceLabel(state.capture.audioSource),
            // D-05：落到输入卡（音源 / 输入设备）
            onClick = { onOpenCaptureSettings(CaptureFocus.SOURCE) }
        )
        CardDivider()
        SettingsNavRow(
            label = stringResource(R.string.home_row_noise),
            value = stringResource(
                if (state.capture.noiseSuppressor) {
                    R.string.home_value_on
                } else {
                    R.string.home_value_off
                }
            ),
            // D-05：落到系统效果卡（此前与「音源」同落点，停在页首）
            onClick = { onOpenCaptureSettings(CaptureFocus.EFFECTS) }
        )
        CardDivider()
        SettingsNavRow(
            label = stringResource(R.string.home_row_theme),
            value = stringResource(themeModeLabel(themeState.mode)),
            onClick = onOpenAppearance
        )
    }
}

/** 音源 id -> 当前选中档位名（未知 id 回落「默认」，与 sanitize 行为一致）。 */
@Composable
private fun audioSourceLabel(id: Int): String {
    val option = AudioSourceOption.byId(id)
    val res = option?.labelRes ?: R.string.capture_source_default
    return stringResource(res)
}

@Preview(showBackground = true)
@Composable
private fun HomeScreenPreview() {
    WomicTheme(mode = ThemeMode.LIGHT) {
        HomeScreen(
            vm = MainViewModel(),
            onStartClicked = {},
            onOpenCaptureSettings = {},
            onOpenAppearance = {}
        )
    }
}
