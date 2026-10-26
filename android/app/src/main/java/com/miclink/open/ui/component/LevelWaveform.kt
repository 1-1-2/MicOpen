package com.miclink.open.ui.component

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.miclink.open.LevelHistory
import com.miclink.open.WAVEFORM_BARS
import com.miclink.open.WAVEFORM_MUTE_DB
import com.miclink.open.WAVEFORM_SAMPLE_MS
import com.miclink.open.dbToHeight
import com.miclink.open.easeLevel

/**
 * 「最近 10 秒」双层电平波形（R-14/R-15）。
 *
 * 双层语义（用户原话："现在有这么大声音，但我只推出去这么多"）：
 *  - **底层暗柱 = 采集到的电平**（同色 30% 透明度）；
 *  - **上层亮柱 = 实际推流电平** = 采集电平 × 协议音量增益（实色）；
 *    静音 / 0 档时上层归零——「有声音但没推出去」一眼可见，同时也是音量字节
 *    链路的可视化自检（协议 0-9 → ±6dB/档 → 发到 PC 的 vol 字节就是这条曲线的差）。
 *  - 细横线 = 采集电平的峰值保持（1.2s 后回落，与 PC 端一致）。
 *
 * 配色（功能派，与 PC 端同语义）：按每根柱的**采集 dB** 上色——
 * < −48 灰 / −48…−18 绿 / −18…−6 橙 / −6…0 红（语义色取令牌 ok/warn/err）。
 *
 * 动效（R-17）：
 *  - 柱高**指数平滑**（上行 0.25 / 下行 0.08，不做线性插值，见 [easeLevel]）；
 *  - 时间轴**逐帧平移**：`phase ∈ [0,1)` 是当前 200ms 采样格内的进度，整条
 *    波形随 phase 连续左移，新采样落地时相位归零、右缘无缝接上新柱——
 *    不是"一秒跳一格"；
 *  - 自管 `withFrameNanos` 渲染循环：每帧重画，数据仍只按 200ms 粒度进。
 *
 * @param slots 波形数据（旧 → 新，来自 ViewModel 的 10 秒环形缓冲）。
 * @param version 每次灌数 +1：渲染层据此把显示数组左移一格并重置平移相位。
 */
@Composable
fun LevelWaveform(
    slots: List<LevelHistory.Slot>,
    version: Long,
    modifier: Modifier = Modifier
) {
    val tokens = LocalMicLinkTokens.current
    // 灰桶：onSurfaceVariant 38%（接近不可见，只留"这里是静的"的痕迹）
    val gray = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)

    // 平滑后的显示高度（归一化 0..1），跨帧持久；两个图层各一份
    val displayCollected = remember { FloatArray(WAVEFORM_BARS) }
    val displayStream = remember { FloatArray(WAVEFORM_BARS) }
    var seenVersion by remember { mutableLongStateOf(0L) }
    var lastFrameNanos by remember { mutableLongStateOf(0L) }
    var phaseStartNanos by remember { mutableLongStateOf(0L) }
    val frameNanos = remember { mutableLongStateOf(0L) }

    // 自管帧循环：每帧更新一次时间戳，触发 Canvas 重画
    LaunchedEffect(Unit) {
        while (true) {
            withFrameNanos { frameNanos.longValue = it }
        }
    }

    Canvas(modifier = modifier.fillMaxWidth().height(48.dp)) {
        val now = frameNanos.longValue
        val dtMs = if (lastFrameNanos == 0L) {
            16.7f
        } else {
            ((now - lastFrameNanos) / 1_000_000f).coerceIn(0.5f, 100f)
        }
        lastFrameNanos = now
        val frameScale = dtMs / 16.667f

        // 新数据落地：显示数组整体左移（同一根物理柱的缓动值跟着走，时间连续），
        // 右缘新柱从"上一根的位置"起步（不突跳），并重置平移相位
        if (version != seenVersion) {
            val shift = (version - seenVersion).coerceAtMost(WAVEFORM_BARS.toLong()).toInt()
            repeat(shift) {
                for (i in 0 until WAVEFORM_BARS - 1) {
                    displayCollected[i] = displayCollected[i + 1]
                    displayStream[i] = displayStream[i + 1]
                }
                displayCollected[WAVEFORM_BARS - 1] = displayCollected[WAVEFORM_BARS - 2]
                displayStream[WAVEFORM_BARS - 1] = displayStream[WAVEFORM_BARS - 2]
            }
            seenVersion = version
            phaseStartNanos = now
        }
        val phase = if (phaseStartNanos == 0L) {
            0f
        } else {
            (((now - phaseStartNanos) / 1_000_000f) / WAVEFORM_SAMPLE_MS).coerceIn(0f, 1f)
        }

        // 目标高度（不足 50 柱时左侧留空）+ 逐帧缓动
        val pad = WAVEFORM_BARS - slots.size
        for (i in 0 until WAVEFORM_BARS) {
            val slot = slots.getOrNull(i - pad)
            displayCollected[i] = easeLevel(
                current = displayCollected[i],
                target = if (slot != null) dbToHeight(slot.collectedDb) else 0f,
                frameScale = frameScale
            )
            displayStream[i] = easeLevel(
                current = displayStream[i],
                target = if (slot != null) dbToHeight(slot.streamDb) else 0f,
                frameScale = frameScale
            )
        }

        // 绘制：底层暗柱 -> 峰值保持线 -> 上层亮柱
        val slotW = size.width / WAVEFORM_BARS
        val barW = slotW * 0.7f
        val capH = 2.dp.toPx()
        for (i in 0 until WAVEFORM_BARS) {
            val slot = slots.getOrNull(i - pad) ?: continue
            val x = size.width -
                (1f + phase + (WAVEFORM_BARS - 1 - i)) * slotW + (slotW - barW) / 2f
            if (x + barW <= 0f) {
                continue
            }
            val color = levelColor(slot.collectedDb, gray, tokens)

            val hCollected = displayCollected[i] * size.height
            if (hCollected > 0.5f) {
                drawRect(
                    color = color.copy(alpha = 0.30f),
                    topLeft = Offset(x, size.height - hCollected),
                    size = Size(barW, hCollected)
                )
            }

            val hPeak = dbToHeight(slot.peakDb) * size.height
            if (hPeak > capH) {
                drawRect(
                    color = color.copy(alpha = 0.85f),
                    topLeft = Offset(x, size.height - hPeak),
                    size = Size(barW, capH)
                )
            }

            val hStream = displayStream[i] * size.height
            if (hStream > 0.5f) {
                drawRect(
                    color = color,
                    topLeft = Offset(x, size.height - hStream),
                    size = Size(barW, hStream)
                )
            }
        }
    }
}

/**
 * 柱色（按采集 dB 分桶）：< −48 灰 / −48…−18 绿 / −18…−6 橙 / −6…0 红。
 * （静音哨兵与数字静音都落灰桶，高度为 0，实际不可见。）
 */
private fun levelColor(collectedDb: Double, gray: Color, tokens: MicLinkTokens): Color = when {
    collectedDb < -48.0 -> gray
    collectedDb < -18.0 -> tokens.ok
    collectedDb < -6.0 -> tokens.warn
    else -> tokens.err
}
