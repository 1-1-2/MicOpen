package com.miclink.open.ui.screen

import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import java.util.Locale
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.Input
import androidx.compose.material.icons.automirrored.outlined.ScreenShare
import androidx.compose.material.icons.outlined.Autorenew
import androidx.compose.material.icons.outlined.Bluetooth
import androidx.compose.material.icons.outlined.Code
import androidx.compose.material.icons.outlined.Dock
import androidx.compose.material.icons.outlined.GraphicEq
import androidx.compose.material.icons.outlined.Headset
import androidx.compose.material.icons.outlined.Hearing
import androidx.compose.material.icons.outlined.Mic
import androidx.compose.material.icons.outlined.PhoneInTalk
import androidx.compose.material.icons.outlined.Radio
import androidx.compose.material.icons.outlined.Smartphone
import androidx.compose.material.icons.outlined.Tv
import androidx.compose.material.icons.outlined.Tune
import androidx.compose.material.icons.outlined.Usb
import androidx.compose.material.icons.outlined.Videocam
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.miclink.open.AudioDeviceManager
import com.miclink.open.AudioSourceIds
import com.miclink.open.AudioSourceOption
import com.miclink.open.CaptureSettings
import com.miclink.open.MainViewModel
import com.miclink.open.R
import com.miclink.open.theme.ThemeMode
import com.miclink.open.ui.component.CardDivider
import com.miclink.open.ui.component.ChipGroup
import com.miclink.open.ui.component.HintText
import com.miclink.open.ui.component.IconChip
import com.miclink.open.ui.component.LabeledSwitchRow
import com.miclink.open.ui.component.LocalMicLinkTokens
import com.miclink.open.ui.component.PortInputField
import com.miclink.open.ui.component.ReadOnlyValueRow
import com.miclink.open.ui.component.SectionCard
import com.miclink.open.ui.theme.WomicTheme

/**
 * 采集设置页的锚点（D-05）：主页摘要行跳转时用 `capture?focus=<值>` 指定落点。
 *
 * 空串 = 无锚点（抽屉直接进采集页），页面行为与加入锚点前完全一致。
 */
object CaptureFocus {
    /** 输入卡（音源 / 输入设备）。 */
    const val SOURCE: String = "source"

    /** 系统效果卡（NS / AEC / AGC）。 */
    const val EFFECTS: String = "effects"
}

/**
 * CaptureScreen -- 采集设置页（抽屉导航目的地之一）。
 *
 * 定稿规范 D-19/D-20/D-21：选项全部 chips 化（图标 + 文字横排、flex-wrap），
 * 分三张卡：
 *  1. **输入**（D-19/D-20）：音源六档 chips + 输入设备 chips（跟随系统 +
 *     系统枚举设备）；不支持的档位/设备**可见不可点**并说明原因（X-12）；
 *  2. **系统效果**：NS / AEC / AGC 开关，`isAvailable()` 为 false 时禁用并说明；
 *  3. **高级**（D-21）：控制端口输入框 + 采样率**只读行**（工程适配：采样率
 *     由 PC 客户端连接时协商，app 侧不可选——禁止做成假开关）+ 调试音开关。
 *
 * 与主页端口/音量的区别：输入与系统效果**录音中也可改**，变更即时生效
 * （服务重建 AudioRecord，可能有半秒左右杂音），并即时落盘（重启后保持）。
 * 控制端口与调试音仍是「启动前锁定」的运行参数（运行中禁用）。
 *
 * @param focus 落点锚点（[CaptureFocus]；空串 = 页首，直接进页面时行为不变）。
 */
@Composable
fun CaptureScreen(vm: MainViewModel, focus: String = "", modifier: Modifier = Modifier) {
    val state = vm.uiState
    val context = LocalContext.current
    val manager = remember(context) { AudioDeviceManager(context) }

    // 设备枚举与能力查询：结果只进本地 state（设备列表不是"偏好"，无需持久化），
    // 热插拔经 AudioDeviceCallback 刷新。
    var devices by remember(manager) { mutableStateOf(manager.inputDevices()) }
    val unprocessedSupported = remember(manager) { manager.isUnprocessedSupported() }
    val noiseSuppressorAvailable = remember(manager) { manager.isNoiseSuppressorAvailable() }
    val echoCancelerAvailable = remember(manager) { manager.isEchoCancelerAvailable() }
    val gainControlAvailable = remember(manager) { manager.isGainControlAvailable() }

    // D-05：锚点滚动。系统效果卡用 BringIntoViewRequester（让滚动容器自己算偏移，
    // 不做 y 坐标测量，避免 restoreState 恢复滚动位置后测到错误基准）。
    val scrollState = rememberScrollState()
    val effectsBringIntoView = remember { BringIntoViewRequester() }
    // 卡片完成首次布局后才发 bringIntoView 请求（否则请求落在未 attach 的节点上会被丢弃）
    var effectsLaidOut by remember { mutableStateOf(false) }

    // 音源锚点 = 输入卡（第一张卡）：回页首。focus 为空（抽屉直接进入）时这两块
    // 都不做任何事，页面行为与加入锚点前完全一致。
    LaunchedEffect(focus) {
        if (focus == CaptureFocus.SOURCE) {
            scrollState.animateScrollTo(0)
        }
    }

    // 系统效果锚点：等首帧布局回填后再请求滚动，只发一次
    LaunchedEffect(focus, effectsLaidOut) {
        if (focus == CaptureFocus.EFFECTS && effectsLaidOut) {
            effectsBringIntoView.bringIntoView()
        }
    }

    DisposableEffect(manager) {
        val callback = object : AudioDeviceCallback() {
            override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>?) {
                devices = manager.inputDevices()
            }

            override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>?) {
                devices = manager.inputDevices()
            }
        }
        manager.registerDeviceCallback(callback)
        onDispose { manager.unregisterDeviceCallback(callback) }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        if (state.debugTone) {
            // 调试音模式不打开 AudioRecord：明说输入与效果暂不生效
            // （CI 联调闭环全程跑该模式，这里是唯一显式提示点）
            Text(
                text = stringResource(R.string.capture_debug_tone_note),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.tertiary
            )
        }

        // ---- 输入卡：音源 chips（D-19）+ 输入设备 chips（D-20）----
        SectionCard(title = stringResource(R.string.capture_group_input)) {
            GroupLabel(text = stringResource(R.string.capture_group_source))
            Spacer(Modifier.height(8.dp))
            ChipGroup {
                AudioSourceOption.entries.forEach { option ->
                    val enabled = option.id != AudioSourceIds.UNPROCESSED || unprocessedSupported
                    IconChip(
                        label = stringResource(option.labelRes),
                        icon = audioSourceIcon(option.id),
                        selected = state.capture.audioSource == option.id,
                        onClick = { vm.onAudioSourceChanged(option.id) },
                        enabled = enabled
                    )
                }
            }
            if (!unprocessedSupported) {
                Spacer(Modifier.height(8.dp))
                HintText(text = stringResource(R.string.capture_source_unprocessed_unsupported))
            }

            Spacer(Modifier.height(16.dp))
            GroupLabel(text = stringResource(R.string.capture_group_device))
            Spacer(Modifier.height(8.dp))
            ChipGroup {
                IconChip(
                    label = stringResource(R.string.capture_mic_auto),
                    icon = Icons.Outlined.Autorenew,
                    selected = state.capture.micDeviceId == CaptureSettings.MIC_DEVICE_AUTO,
                    onClick = { vm.onMicDeviceChanged(CaptureSettings.MIC_DEVICE_AUTO) }
                )
                devices.forEach { device ->
                    // 远程混音捕获的是系统播放声，不能当麦克风输入：可见不可点（X-12）
                    val usable = device.type != AudioDeviceInfo.TYPE_REMOTE_SUBMIX
                    IconChip(
                        label = device.label,
                        icon = deviceIcon(device.type),
                        selected = state.capture.micDeviceId == device.id,
                        onClick = { vm.onMicDeviceChanged(device.id) },
                        enabled = usable
                    )
                }
            }
            if (devices.any { it.type == AudioDeviceInfo.TYPE_REMOTE_SUBMIX }) {
                Spacer(Modifier.height(8.dp))
                HintText(text = stringResource(R.string.capture_device_submix_disabled))
            }
            Spacer(Modifier.height(8.dp))
            // 当前实际生效的设备（AudioRecord.getRoutedDevice，而非"用户选了啥"）
            Text(
                text = if (state.isStreaming) {
                    state.routedDeviceLabel?.let {
                        stringResource(R.string.capture_mic_routed, it)
                    } ?: stringResource(R.string.home_value_unknown)
                } else {
                    stringResource(R.string.capture_mic_not_recording)
                },
                style = MaterialTheme.typography.bodySmall,
                color = LocalMicLinkTokens.current.text2
            )
            Spacer(Modifier.height(4.dp))
            HintText(text = stringResource(R.string.capture_mic_hint))
        }

        // ---- 系统效果卡 ----
        // D-05：作为主页「系统降噪」摘要行的锚点目标（bringIntoView 让滚动容器自己
        // 算偏移；effectsLaidOut 保证首帧布局完成后才请求）。
        SectionCard(
            title = stringResource(R.string.capture_section_effects),
            modifier = Modifier
                .bringIntoViewRequester(effectsBringIntoView)
                .onGloballyPositioned { effectsLaidOut = true }
        ) {
            EffectSwitchRow(
                title = stringResource(R.string.capture_effect_ns),
                description = stringResource(R.string.capture_effect_ns_desc),
                available = noiseSuppressorAvailable,
                checked = state.capture.noiseSuppressor,
                onCheckedChange = { vm.onNoiseSuppressorChanged(it) }
            )
            CardDivider()
            EffectSwitchRow(
                title = stringResource(R.string.capture_effect_aec),
                description = stringResource(R.string.capture_effect_aec_desc),
                available = echoCancelerAvailable,
                checked = state.capture.echoCanceler,
                onCheckedChange = { vm.onEchoCancelerChanged(it) }
            )
            CardDivider()
            EffectSwitchRow(
                title = stringResource(R.string.capture_effect_agc),
                description = stringResource(R.string.capture_effect_agc_desc),
                available = gainControlAvailable,
                checked = state.capture.gainControl,
                onCheckedChange = { vm.onGainControlChanged(it) }
            )
            Spacer(Modifier.height(4.dp))
            HintText(text = stringResource(R.string.capture_effect_hint))
        }

        // ---- 高级卡（D-21）----
        SectionCard(title = stringResource(R.string.capture_section_advanced)) {
            // ① 控制端口：左标签（宽 72）+ 输入框（高 56、右对齐等宽、聚焦 2dp accent）
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = stringResource(R.string.label_control_port),
                    style = MaterialTheme.typography.bodyMedium,
                    color = LocalMicLinkTokens.current.text1,
                    modifier = Modifier.width(72.dp)
                )
                Spacer(Modifier.width(8.dp))
                PortInputField(
                    value = state.controlPortText,
                    onValueChange = { vm.onControlPortTextChanged(it) },
                    enabled = !state.isRunning,
                    isError = state.controlPortText.toIntOrNull()?.let { it !in 1..65535 } ?: true,
                    modifier = Modifier.weight(1f)
                )
            }
            Spacer(Modifier.height(4.dp))
            // D-04：超长输入不再静默截断——被忽略的位数在这里明示（err 色）。
            // 运行中优先显示"已锁定"（此时输入框禁用，截断提示无意义）。
            val dropped = state.controlPortDropped
            when {
                state.isRunning -> HintText(text = stringResource(R.string.capture_port_locked))
                dropped > 0 -> HintText(
                    text = stringResource(R.string.capture_port_overflow, dropped),
                    color = LocalMicLinkTokens.current.err
                )
                else -> HintText(text = stringResource(R.string.hint_control_port))
            }
            Spacer(Modifier.height(12.dp))
            // ② 采样率：只读行（app 侧不可选——禁止假开关）。
            // R-18：旧文案「48 kHz（协议固定）」是错的——真实口径是 PC 客户端在
            // CONFIG 握手时选档（8k/16k/48k 三档，规格 v1 第 2.2 节），手机端按
            // 协商值 openRecorder。未连接时显示默认口径，已连接显示本次协商值。
            ReadOnlyValueRow(
                label = stringResource(R.string.capture_sample_rate),
                value = if (state.negotiatedSampleRate > 0) {
                    stringResource(
                        R.string.capture_sample_rate_negotiated,
                        formatRateKHz(state.negotiatedSampleRate)
                    )
                } else {
                    stringResource(R.string.capture_sample_rate_value)
                }
            )
            Spacer(Modifier.height(4.dp))
            HintText(text = stringResource(R.string.capture_sample_rate_hint))
            Spacer(Modifier.height(12.dp))
            // ③ 调试音开关（运行参数：启动时锁定）
            LabeledSwitchRow(
                title = stringResource(R.string.label_debug_tone),
                description = stringResource(R.string.hint_debug_tone),
                checked = state.debugTone,
                onCheckedChange = { vm.onDebugToneChanged(it) },
                enabled = !state.isRunning
            )
        }
    }
}

/** 采样率 -> 「48 kHz」样式：整千显示整数，否则保留一位小数（如 44100 → 44.1 kHz）。 */
private fun formatRateKHz(rateHz: Int): String =
    if (rateHz % 1000 == 0) {
        "${rateHz / 1000} kHz"
    } else {
        String.format(Locale.US, "%.1f kHz", rateHz / 1000.0)
    }

/** 设备组子标签（D-20：14sp Medium text-2；音源组同规格）。 */
@Composable
private fun GroupLabel(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium),
        color = LocalMicLinkTokens.current.text2
    )
}

/** 音源 id -> M3 标准语义图标（outlined）。 */
private fun audioSourceIcon(id: Int): ImageVector = when (id) {
    AudioSourceIds.MIC -> Icons.Outlined.Mic
    AudioSourceIds.VOICE_RECOGNITION -> Icons.Outlined.GraphicEq
    AudioSourceIds.VOICE_COMMUNICATION -> Icons.Outlined.PhoneInTalk
    AudioSourceIds.CAMCORDER -> Icons.Outlined.Videocam
    AudioSourceIds.UNPROCESSED -> Icons.Outlined.Code
    // DEFAULT：跟随系统的调校档
    else -> Icons.Outlined.Tune
}

/** 输入设备类型 -> M3 标准语义图标（outlined）。 */
private fun deviceIcon(type: Int): ImageVector = when (type) {
    AudioDeviceInfo.TYPE_BUILTIN_MIC -> Icons.Outlined.Smartphone
    AudioDeviceInfo.TYPE_WIRED_HEADSET -> Icons.Outlined.Headset
    AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET,
    AudioDeviceInfo.TYPE_USB_ACCESSORY -> Icons.Outlined.Usb
    AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> Icons.Outlined.Bluetooth
    AudioDeviceInfo.TYPE_TELEPHONY -> Icons.Outlined.PhoneInTalk
    AudioDeviceInfo.TYPE_AUX_LINE -> Icons.AutoMirrored.Outlined.Input
    AudioDeviceInfo.TYPE_HDMI -> Icons.Outlined.Tv
    AudioDeviceInfo.TYPE_HEARING_AID -> Icons.Outlined.Hearing
    AudioDeviceInfo.TYPE_FM_TUNER -> Icons.Outlined.Radio
    AudioDeviceInfo.TYPE_REMOTE_SUBMIX -> Icons.AutoMirrored.Outlined.ScreenShare
    AudioDeviceInfo.TYPE_DOCK -> Icons.Outlined.Dock
    else -> Icons.AutoMirrored.Outlined.Input
}

/**
 * 系统效果开关行：设备不支持（`isAvailable()` = false）时**禁用并说明原因**，
 * 不假装生效；勾选态仍反映已保存的设置（若设备实际拒绝附加，服务会发明确提示）。
 */
@Composable
private fun EffectSwitchRow(
    title: String,
    description: String,
    available: Boolean,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyLarge,
                color = LocalMicLinkTokens.current.text1
            )
            Text(
                text = if (available) {
                    description
                } else {
                    stringResource(R.string.capture_effect_unavailable)
                },
                style = MaterialTheme.typography.bodySmall,
                color = LocalMicLinkTokens.current.text2
            )
        }
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            enabled = available
        )
    }
}

@Preview(showBackground = true)
@Composable
private fun CaptureScreenPreview() {
    WomicTheme(mode = ThemeMode.LIGHT) {
        CaptureScreen(vm = MainViewModel())
    }
}
