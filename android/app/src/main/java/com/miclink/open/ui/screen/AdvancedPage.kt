package com.miclink.open.ui.screen

import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.miclink.open.AudioDeviceManager
import com.miclink.open.MainViewModel
import com.miclink.open.R
import com.miclink.open.theme.ThemeMode
import com.miclink.open.ui.component.CardDivider
import com.miclink.open.ui.component.HintText
import com.miclink.open.ui.component.LabeledSwitchRow
import com.miclink.open.ui.component.LocalMicLinkTokens
import com.miclink.open.ui.component.PortInputField
import com.miclink.open.ui.component.ReadOnlyValueRow
import com.miclink.open.ui.component.SectionCard
import com.miclink.open.ui.navigation.HomeFocus
import com.miclink.open.ui.theme.WomicTheme
import java.util.Locale

/**
 * AdvancedPage -- 三段导航的「高级」页（R-12）。
 *
 * 内容：
 *  ① 系统效果：NS / AEC / AGC 三个开关，`isAvailable()` 为 false 时禁用并说明；
 *  ② 高级参数：控制端口输入框（D-21，运行中锁定）+ 采样率**只读**行（R-18：
 *     由电脑端协商，非协议固定）+ 调试音开关；
 *  ③ 页底「关于与帮助」文字按钮（R-07：抽屉删除后，关于从抽屉挪到这里）。
 *
 * @param scrollState 由 AppNav 持有（切页不重置滚动）。
 * @param focus 锚点：从连接页摘要行跳来时滚到「系统效果」卡或「控制端口」行。
 */
@Composable
fun AdvancedPage(
    vm: MainViewModel,
    scrollState: ScrollState,
    focus: HomeFocus,
    onOpenAbout: () -> Unit,
    modifier: Modifier = Modifier
) {
    val state = vm.uiState
    val context = LocalContext.current
    val manager = remember(context) { AudioDeviceManager(context) }
    val noiseSuppressorAvailable = remember(manager) { manager.isNoiseSuppressorAvailable() }
    val echoCancelerAvailable = remember(manager) { manager.isEchoCancelerAvailable() }
    val gainControlAvailable = remember(manager) { manager.isGainControlAvailable() }

    // 锚点滚动：让滚动容器自己算偏移（bringIntoView），不做 y 坐标测量——
    // 切页回来时滚动位置会被恢复，测量基准不可靠。
    val effectsBringIntoView = remember { BringIntoViewRequester() }
    var effectsLaidOut by remember { mutableStateOf(false) }
    val portBringIntoView = remember { BringIntoViewRequester() }
    var portLaidOut by remember { mutableStateOf(false) }

    LaunchedEffect(focus, effectsLaidOut) {
        if (focus == HomeFocus.EFFECTS && effectsLaidOut) {
            effectsBringIntoView.bringIntoView()
            vm.consumeHomeFocus()
        }
    }
    LaunchedEffect(focus, portLaidOut) {
        if (focus == HomeFocus.PORT && portLaidOut) {
            portBringIntoView.bringIntoView()
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
            Text(
                text = stringResource(R.string.capture_debug_tone_note),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.tertiary
            )
        }

        // ---- ① 系统效果 ----
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

        // ---- ② 高级参数 ----
        SectionCard(title = stringResource(R.string.capture_section_advanced)) {
            // ① 控制端口：左标签（宽 72）+ 输入框（高 56、右对齐等宽、聚焦 2dp accent）
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .bringIntoViewRequester(portBringIntoView)
                    .onGloballyPositioned { portLaidOut = true }
            ) {
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
            // ② 采样率：只读行（R-18：由 PC 客户端握手协商，非协议固定）
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

        // ---- ③ 关于与帮助（R-07：抽屉移除后落在这里）----
        TextButton(
            onClick = onOpenAbout,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(text = stringResource(R.string.nav_about))
        }
    }
}

/**
 * 系统效果开关行：设备不支持（`isAvailable()` = false）时**禁用并说明原因**，
 * 不假装生效；勾选态仍反映已保存的设置（若设备实际拒绝附加，服务会发提示）。
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

/** 采样率 -> 「48 kHz」样式：整千显示整数，否则保留一位小数（如 44100 → 44.1 kHz）。 */
private fun formatRateKHz(rateHz: Int): String =
    if (rateHz % 1000 == 0) {
        "${rateHz / 1000} kHz"
    } else {
        String.format(Locale.US, "%.1f kHz", rateHz / 1000.0)
    }

@Preview(showBackground = true)
@Composable
private fun AdvancedPagePreview() {
    WomicTheme(mode = ThemeMode.LIGHT) {
        AdvancedPage(
            vm = MainViewModel(),
            scrollState = ScrollState(0),
            focus = HomeFocus.NONE,
            onOpenAbout = {}
        )
    }
}
