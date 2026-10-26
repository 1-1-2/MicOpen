package com.miclink.open.ui.screen

import android.os.SystemClock
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ScrollState
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
import com.miclink.open.Prefs
import com.miclink.open.R
import com.miclink.open.theme.ThemeMode
import com.miclink.open.ui.component.CardDivider
import com.miclink.open.ui.component.DotTrack
import com.miclink.open.ui.component.HintText
import com.miclink.open.ui.component.LabeledSwitchRow
import com.miclink.open.ui.component.LevelWaveform
import com.miclink.open.ui.component.LocalMicLinkTokens
import com.miclink.open.ui.component.SectionCard
import com.miclink.open.ui.component.SettingsNavRow
import com.miclink.open.ui.component.StatusBadge
import com.miclink.open.ui.navigation.SummaryRow
import com.miclink.open.ui.theme.WomicTheme

/**
 * ConnectPage -- 三段导航的「连接」页（R-12）。
 *
 * 结构（自上而下）：
 *  ① 连接状态卡：大状态徽标 + 右端地址 + **两列并排**的字段（R-06）+ 1px 分隔线
 *     + 主按钮；
 *  ② 主按钮（D-15）：空闲「启动」accent 填充；运行「停止」err 描边 + err 呼吸
 *     外发光；「停止」走二次确认（C-05）；
 *  ③ 推流音量（D-18）：十档点轨 + 静音开关；
 *  ④ 参数摘要：音源 / 系统降噪 / 控制端口三行，点跳对应页（不退出本页栈）。
 *
 * 数据诚实性：字段只保留**有真实来源**的（传输方式 / 时长），延迟与丢包已删除
 * （R-20：app 侧无测量通道）。
 *
 * @param scrollState 由 AppNav 持有（切页不重置滚动，R-12）。
 * @param onJump 摘要行跳转（目标页 + 锚点由 [com.miclink.open.ui.navigation.summaryTarget] 决定）。
 */
@Composable
fun ConnectPage(
    vm: MainViewModel,
    scrollState: ScrollState,
    onStartClicked: () -> Unit,
    onJump: (SummaryRow) -> Unit,
    modifier: Modifier = Modifier
) {
    val state = vm.uiState
    val tokens = LocalMicLinkTokens.current
    // 波形无障碍标签（semantics lambda 不是 @Composable 上下文，组合层先取好）
    val waveformA11y = stringResource(R.string.waveform_caption)

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // ---- ① 连接状态卡 ----
        SectionCard {
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
                    color = tokens.text2,
                    maxLines = 1,
                    // 可见文本只有地址；朗读时补上用途（CI 断言同源）
                    modifier = Modifier.semantics {
                        contentDescription = "$addressLabel $address"
                    }
                )
            }

            Spacer(Modifier.height(16.dp))

            // ② 字段**两列并排**（R-06）：真实字段只有传输方式与时长，一行放得下
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                FieldCell(
                    label = stringResource(R.string.home_field_transport),
                    value = state.transport.ifBlank { stringResource(R.string.home_value_unknown) },
                    modifier = Modifier.weight(1f)
                )
                FieldCell(
                    label = stringResource(R.string.home_field_duration),
                    value = formatSessionDuration(
                        startElapsedMs = state.sessionStartElapsedMs,
                        nowElapsedMs = SystemClock.elapsedRealtime()
                    ).ifEmpty { stringResource(R.string.home_value_unknown) },
                    modifier = Modifier.weight(1f)
                )
            }

            // ③ 「最近 10 秒」双层波形（R-14/R-15）：底层=采集电平（暗）、
            //    上层=实际推流电平（亮）；静音 / 0 档时上层归零
            Spacer(Modifier.height(12.dp))
            Text(
                text = stringResource(R.string.waveform_caption),
                style = MaterialTheme.typography.bodySmall.copy(fontSize = 11.sp),
                color = tokens.text2
            )
            Spacer(Modifier.height(4.dp))
            val wf = vm.waveform
            // R-35：波形区必须有自己的容器；未连接时也保留可识别的视觉区域。
            // 容器常驻，内部基线在空态下给出 0 电平参照。
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f))
                    .border(1.dp, tokens.border, RoundedCornerShape(8.dp))
                    .padding(horizontal = 8.dp, vertical = 4.dp)
            ) {
                LevelWaveform(
                    slots = wf.slots,
                    version = wf.version,
                    modifier = Modifier
                        .fillMaxWidth()
                        .semantics {
                            contentDescription = waveformA11y
                        }
                )
            }

            Spacer(Modifier.height(16.dp))
            CardDivider()
            Spacer(Modifier.height(16.dp))

            // ③ 主按钮
            MainActionButton(
                running = state.isRunning,
                onStart = onStartClicked,
                onStopRequest = { vm.requestStopConfirmation() }
            )
        }

        // ---- ② 推流音量 ----
        VolumeSection(
            state = state,
            onVolumeChanged = { vm.onVolumeChanged(it) },
            onMuteChanged = { vm.onMuteChanged(it) }
        )

        // ---- ③ 参数摘要 ----
        SectionCard {
            SettingsNavRow(
                label = stringResource(R.string.home_row_source),
                value = audioSourceLabel(state.capture.audioSource),
                onClick = { onJump(SummaryRow.SOURCE) }
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
                onClick = { onJump(SummaryRow.NOISE) }
            )
            CardDivider()
            SettingsNavRow(
                label = stringResource(R.string.label_control_port),
                value = state.controlPortText.ifBlank {
                    Prefs.DEFAULT_CONTROL_PORT.toString()
                },
                onClick = { onJump(SummaryRow.PORT) }
            )
        }
    }
}

// ------------------------------------------------------------------
// 状态卡零件
// ------------------------------------------------------------------

/**
 * 状态字段格（R-06 两列并排用）：标签 13sp text-2 左、值 14sp text-1 右，
 * 行高 24（与 D-16 单行字段同规格，只是两个并排）。
 */
@Composable
private fun FieldCell(label: String, value: String, modifier: Modifier = Modifier) {
    val tokens = LocalMicLinkTokens.current
    Row(
        modifier = modifier.height(24.dp),
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
        else -> tokens.text2 to tokens.bgSunken
    }
}

/** 会话时长 mm:ss / h:mm:ss；无会话返回空串（UI 显示「—」）。 */
private fun formatSessionDuration(startElapsedMs: Long, nowElapsedMs: Long): String {
    if (startElapsedMs <= 0L) {
        return ""
    }
    val totalSeconds = ((nowElapsedMs - startElapsedMs) / 1000L).coerceAtLeast(0L)
    val hours = totalSeconds / 3600L
    val minutes = (totalSeconds % 3600L) / 60L
    val seconds = totalSeconds % 60L
    return if (hours > 0L) {
        "%d:%02d:%02d".format(hours, minutes, seconds)
    } else {
        "%02d:%02d".format(minutes, seconds)
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

/** 呼吸外发光 alpha（0 → [GLOW_MAX_ALPHA] → 0，周期 2 * [GLOW_HALF_CYCLE_MS]）。 */
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
 */
private fun volumeDb(volume: Int, muteLabel: String): String = when {
    volume <= 0 -> muteLabel
    volume == 5 -> "0 dB"
    volume < 5 -> "-${(5 - volume) * 6} dB"
    else -> "+${(volume - 5) * 6} dB"
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
private fun ConnectPagePreview() {
    WomicTheme(mode = ThemeMode.LIGHT) {
        ConnectPage(
            vm = MainViewModel(),
            scrollState = ScrollState(0),
            onStartClicked = {},
            onJump = {}
        )
    }
}
