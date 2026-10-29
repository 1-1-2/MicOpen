package com.miclink.open.ui.screen

import android.os.SystemClock
import com.miclink.open.ConnectionMode
import androidx.compose.material3.RadioButton
import androidx.compose.foundation.selection.selectable
import androidx.compose.ui.semantics.Role
import android.content.Intent
import android.provider.Settings
import androidx.compose.ui.platform.LocalContext
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.material3.TextButton
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miclink.open.AudioSourceOption
import com.miclink.open.CaptureSettings
import com.miclink.open.ReconcileStatus
import com.miclink.open.ReconcileText
import com.miclink.open.MainUiState
import com.miclink.open.MainViewModel
import com.miclink.open.MicCaptureService
import com.miclink.open.Prefs
import com.miclink.open.R
import com.miclink.open.theme.ThemeMode
import com.miclink.open.ui.component.DotTrack
import com.miclink.open.ui.component.HintText
import com.miclink.open.ui.component.LabeledSwitchRow
import com.miclink.open.ui.component.LevelWaveform
import com.miclink.open.ui.component.LocalMicLinkTokens
import com.miclink.open.ui.component.SectionCard
import com.miclink.open.ui.component.StatusBadge
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
 */
@Composable
fun ConnectPage(
    vm: MainViewModel,
    scrollState: ScrollState,
    onStartClicked: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var mode by remember { mutableStateOf(ConnectionMode.read(context)) }
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
        SectionCard {
            Text("1. 选择连接方式", style = MaterialTheme.typography.titleSmall)
            ConnectionMode.entries.chunked(2).forEach { pair ->
                Row(Modifier.fillMaxWidth()) {
                    pair.forEach { option ->
                        Row(
                            modifier = Modifier.weight(1f).selectable(
                                selected = mode == option,
                                enabled = !state.isRunning,
                                role = Role.RadioButton,
                                onClick = { mode = option; ConnectionMode.save(context, option) }
                            ).padding(vertical = 4.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            RadioButton(selected = mode == option, onClick = null, enabled = !state.isRunning)
                            Text(option.title, color = if (state.isRunning) tokens.text2 else tokens.text1)
                        }
                    }
                }
            }
            Text("准备：${mode.title}", style = MaterialTheme.typography.labelLarge)
            HintText(mode.preparation)
            val settingsAction = when (mode) {
                ConnectionMode.WIFI -> Settings.ACTION_WIFI_SETTINGS
                ConnectionMode.DIRECT -> Settings.ACTION_WIFI_SETTINGS
                ConnectionMode.BLUETOOTH -> Settings.ACTION_BLUETOOTH_SETTINGS
                ConnectionMode.USB -> Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS
            }
            TextButton(onClick = {
                try { context.startActivity(Intent(settingsAction)) }
                catch (_: android.content.ActivityNotFoundException) { context.startActivity(Intent(Settings.ACTION_SETTINGS)) }
            }, enabled = !state.isRunning) { Text("打开系统设置") }
            HintText(if (state.isRunning) "当前使用${mode.title}。更换方式请先停止。"
                else "2. 准备完成后点击下方“启动”，等待 Windows 连接。")
        }

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
                val address = if (!mode.needsAddress) "无需填写 IP" else state.listeningAddress.ifBlank {
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

            // R-47：12dp（原 16）—— 徽标行与字段行同属"当前状态"，不必留大空档
            Spacer(Modifier.height(12.dp))

            // ② 字段**两列并排**（R-06）：真实字段只有传输方式与时长，一行放得下
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                FieldCell(
                    label = "所选方式",
                    value = mode.title,
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
            // R-47：8dp（原 12）—— 标题「最近 10 秒」与波形容器是一组
            Spacer(Modifier.height(8.dp))
            Text(
                text = stringResource(R.string.waveform_caption),
                style = MaterialTheme.typography.bodySmall.copy(fontSize = 11.sp),
                color = tokens.text2
            )
            Spacer(Modifier.height(4.dp))
            val wf = vm.waveform
            // R-35：波形区**必须有自己的容器**——未连接时也是一块"区域"，
            // 不再是空白断层（1dp border + surfaceVariant 低透明度底 + R2 圆角，
            // 与卡片语言一致）。容器常驻，内部画一条极淡基线（见 LevelWaveform），
            // 空态也有"这里是一条 0 电平线"的视觉交代。
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

            // R-47：波形与按钮之间的**分隔线已删除**（用户反馈：状态页被这根线
            // 割成两块，显长）。原来这里是 16dp + 分隔线 + 16dp = 33dp 的断层，
            // 现在压到 12dp 的连续留白 —— 波形容器与按钮同属"当前状态"这一件事，
            // 中间不需要再画一道墙。
            Spacer(Modifier.height(12.dp))
            HintText(when (state.state) {
                MicCaptureService.STATE_CONNECTED, MicCaptureService.STATE_STREAMING -> "电脑已连接。更换连接方式请先停止。"
                else -> if (state.isRunning) "3. 在 Windows 选择${mode.title}，点击连接。"
                    else "启动后只等待电脑接入，不会自动连接电脑。"
            })
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

        // ---- ③ R-34 只读状态栏（无卡片 / 无分隔线 / 不可点）----
        //
        // 用户视角：这些行是"现在是什么配置"的**事实陈述**，不是入口——
        // 所以全部不可点（没有 clickable、没有 ripple）。
        //
        // ⚠ R-47 布局改造（用户反馈：降噪 + 回声消除 + 自动增益都打开时，
        //   那一行会超出文字长度范围）：
        //   旧布局把三个效果**挤在同一格**里（"降噪 开 · 回声消除 开 · 自动增益
        //   开"），右列给了 1.15 权重仍然放不下，超出的部分被硬裁掉。
        //   现在改成 **2 列 × 4 行、每格只放一个「标签 + 值」**：
        //     音源 / 输入设备   采样率 / 控制端口   降噪 / 回声消除   自动增益 / 已发送
        //   · 每格只有一个数值 → 不再有三态挤一行的溢出；
        //   · 行距压到 2dp（原 6dp）、字号 11sp 标签 / 12sp 值 —— 像文本清单一样密排；
        //   · 标签列固定 52dp → 上下两行的值**左对齐成一条竖线**，扫读更快。
        //
        // 全部值都有真实来源：音源与效果来自 CaptureSettings（已落盘），采样率与
        // 已发送来自服务（协商值 / 真实发包字节计数），没有就显示「—」。
        val deviceText = when {
            state.capture.micDeviceId == CaptureSettings.MIC_DEVICE_AUTO ->
                stringResource(R.string.capture_mic_auto)
            state.routedDeviceLabel != null -> state.routedDeviceLabel!!
            else -> stringResource(R.string.status_device_selected)
        }
        val rateText = if (state.negotiatedSampleRate > 0) {
            formatRateKHz(state.negotiatedSampleRate)
        } else {
            stringResource(R.string.home_value_unknown)
        }
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(12.dp))
                .background(tokens.bgSunken)
                .border(1.dp, tokens.border, RoundedCornerShape(12.dp))
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Text(
                text = stringResource(R.string.capture_status_title),
                style = MaterialTheme.typography.titleSmall,
                color = tokens.text1
            )
            StatusRow(
                leftLabel = stringResource(R.string.home_row_source),
                leftValue = audioSourceLabel(state.capture.audioSource),
                rightLabel = stringResource(R.string.capture_group_device),
                rightValue = deviceText
            )
            StatusRow(
                leftLabel = stringResource(R.string.capture_sample_rate),
                leftValue = rateText,
                rightLabel = stringResource(R.string.label_control_port),
                rightValue = state.controlPortText.ifBlank {
                    Prefs.DEFAULT_CONTROL_PORT.toString()
                }
            )
            StatusRow(
                leftLabel = stringResource(R.string.status_ns),
                leftValue = onOff(state.capture.noiseSuppressor),
                rightLabel = stringResource(R.string.status_aec),
                rightValue = onOff(state.capture.echoCanceler)
            )
            StatusRow(
                leftLabel = stringResource(R.string.status_agc),
                leftValue = onOff(state.capture.gainControl),
                rightLabel = stringResource(R.string.status_sent),
                rightValue = ReconcileText.formatBytes(state.bytes)
            )
            Text(
                text = stringResource(R.string.capture_status_effect_hint),
                style = MaterialTheme.typography.bodySmall.copy(fontSize = 11.sp),
                color = tokens.text2
            )
            // R-38a 回执行：常驻一行（不随状态增减、不抖布局），
            // 未回执时用 WARN 色警示，但**不阻断推流**。
            Text(
                text = ReconcileText.line(state.reconcileStatus, state.reconcileLossPermille),
                style = MaterialTheme.typography.bodySmall.copy(fontSize = 12.sp),
                color = if (state.reconcileStatus == ReconcileStatus.NO_REPLY) {
                    tokens.warn
                } else {
                    tokens.text2
                }
            )
        }
    }
}

// ------------------------------------------------------------------
// 状态卡零件
// ------------------------------------------------------------------

/** 开 / 关（状态栏三态用，与 settings 里的语义一致）。 */
@Composable
private fun onOff(value: Boolean): String =
    stringResource(if (value) R.string.home_value_on else R.string.home_value_off)

/**
 * 只读状态行（R-34 / R-47）：一行两格，**每格只放一个「标签 + 值」**。
 *
 * 与旧 [StatusLine] 的差别：
 *  - 两格**等宽**（各 50%），不再给右格 1.15 的超额权重 —— 每格只有一个短值，
 *    不需要靠权重抢空间，也不会再有一格被撑破；
 *  - 标签列固定 52dp，于是上下两行的值**左对齐成一条竖线**；
 *  - 标签 11sp / 值 12sp，行距由调用方压到 2dp。
 *
 * 整行**没有任何 clickable / ripple**——它是事实陈述，不是入口。
 */
@Composable
private fun StatusRow(
    leftLabel: String,
    leftValue: String,
    rightLabel: String,
    rightValue: String
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        StatusItem(
            label = leftLabel,
            value = leftValue,
            modifier = Modifier.weight(1f)
        )
        Spacer(Modifier.width(10.dp))
        StatusItem(
            label = rightLabel,
            value = rightValue,
            modifier = Modifier.weight(1f)
        )
    }
}

/**
 * 状态栏的一格：标签（11sp text-2，固定 52dp 宽）+ 值（12sp text-1）。
 *
 * 值超长时**省略号**收尾（旧实现用 `softWrap = false` 硬裁，会把「已选设备」
 * 切成「已选设」这种半截词）。
 */
@Composable
private fun StatusItem(label: String, value: String, modifier: Modifier = Modifier) {
    val tokens = LocalMicLinkTokens.current
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodySmall.copy(fontSize = 11.sp),
            color = tokens.text2,
            maxLines = 1,
            modifier = Modifier.width(52.dp)
        )
        Spacer(Modifier.width(4.dp))
        Text(
            text = value,
            style = MaterialTheme.typography.bodySmall.copy(fontSize = 12.sp),
            color = tokens.text1,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

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
// 主按钮（D-15；R-47 起不再有呼吸外发光）
// ------------------------------------------------------------------

/**
 * 主操作按钮：高 48、R2、整宽。
 *
 * - 空闲：accent 填充 + onAccent 文字「启动」；
 * - 运行：transparent 底 + err 1px 描边 + err 文字「停止」。
 *
 * ⚠ R-47：**呼吸外发光已关闭**。用户反馈"连接之后按钮一直闪"。
 *   原本的外发光（D-22，周期 1200ms、alpha 0→0.30→0、err 语义色）在采集运行期间
 *   持续呼吸，既是视觉噪音，也让「停止」按钮看起来像在待处理状态。
 *   「正在推流」这件事已经由**状态徽标 + 时长 + 波形 + 已发送字节**四处在陈述，
 *   不需要按钮再自己闪一遍。
 *
 *   容器高度保持 60dp 不变（R-16b 纪律）：外发光层退出组合时，容器会随它一起
 *   缩到 48dp，整页每隔 1.2s 跳一下——那是比"闪"更糟的抖动。
 *   所以这里保留常驻的 60dp 盒子，只是不再往里面放会动的东西。
 *   60 → 48 的 12dp 留白用于视觉呼吸，也让按钮在两态间不再跳动。
 */
@Composable
private fun MainActionButton(
    running: Boolean,
    onStart: () -> Unit,
    onStopRequest: () -> Unit
) {
    val tokens = LocalMicLinkTokens.current
    val shape = RoundedCornerShape(8.dp)

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(60.dp),
        contentAlignment = Alignment.Center
    ) {
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
            onStartClicked = {}
        )
    }
}
