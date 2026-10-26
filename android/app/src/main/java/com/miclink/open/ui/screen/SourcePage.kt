package com.miclink.open.ui.screen

import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
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
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
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
import com.miclink.open.ui.component.ChipGroup
import com.miclink.open.ui.component.HintText
import com.miclink.open.ui.component.IconChip
import com.miclink.open.ui.component.LocalMicLinkTokens
import com.miclink.open.ui.component.SectionCard
import com.miclink.open.ui.navigation.HomeFocus
import com.miclink.open.ui.theme.WomicTheme

/**
 * SourcePage -- 三段导航的「音源」页（R-12）。
 *
 * 只做**选择**：音源六档 chips（D-19）+ 输入设备 chips（D-20）。
 * 不支持的档位 / 设备**可见不可点**，禁用原因写在卡内（X-12，不藏起来）；
 * 底部一行显示**当前实际生效**的输入设备（`AudioRecord.getRoutedDevice`，
 * 而不是"用户选了啥"）。
 *
 * 选中即时生效并落盘（服务会重建 AudioRecord，可能有半秒杂音，提示走 Snackbar
 * R-05，不弹对话框打断）。
 *
 * @param scrollState 由 AppNav 持有（切页不重置滚动）。
 * @param focus 锚点：从连接页摘要行跳来时滚到音源 chips。
 */
@Composable
fun SourcePage(
    vm: MainViewModel,
    scrollState: ScrollState,
    focus: HomeFocus,
    modifier: Modifier = Modifier
) {
    val state = vm.uiState
    val context = LocalContext.current
    val manager = remember(context) { AudioDeviceManager(context) }

    // 设备枚举与能力查询：结果只进本地 state（设备列表不是"偏好"，无需持久化），
    // 热插拔经 AudioDeviceCallback 刷新。
    var devices by remember(manager) { mutableStateOf(manager.inputDevices()) }
    val unprocessedSupported = remember(manager) { manager.isUnprocessedSupported() }

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

    // 锚点滚动（音源 chips 就在页首）：回页首即可，随后把锚点消费掉
    val chipsBringIntoView = remember { BringIntoViewRequester() }
    var chipsLaidOut by remember { mutableStateOf(false) }
    LaunchedEffect(focus, chipsLaidOut) {
        if (focus == HomeFocus.SOURCE_CHIPS && chipsLaidOut) {
            chipsBringIntoView.bringIntoView()
            vm.consumeHomeFocus()
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        if (state.debugTone) {
            // 调试音模式不打开 AudioRecord：明说输入选择在此时不生效
            Text(
                text = stringResource(R.string.capture_debug_tone_note),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.tertiary
            )
        }

        SectionCard(
            title = stringResource(R.string.capture_group_input),
            modifier = Modifier
                .bringIntoViewRequester(chipsBringIntoView)
                .onGloballyPositioned { chipsLaidOut = true }
        ) {
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
    }
}

/** 分组子标签（D-20：14sp Medium text-2）。 */
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

@Preview(showBackground = true)
@Composable
private fun SourcePagePreview() {
    WomicTheme(mode = ThemeMode.LIGHT) {
        SourcePage(
            vm = MainViewModel(),
            scrollState = ScrollState(0),
            focus = HomeFocus.NONE
        )
    }
}
