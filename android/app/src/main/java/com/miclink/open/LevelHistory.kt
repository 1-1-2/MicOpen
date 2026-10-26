package com.miclink.open

import kotlin.math.pow

/**
 * 「最近 10 秒」双层波形的**纯数据层 + 数学**（R-14/R-15/R-17）。
 *
 * 本文件刻意不依赖 Compose / Android framework：双层增益计算、环形缓冲、
 * 峰值保持、缓动曲线全部可以在 JVM 单测里用数字断言（本地无模拟器）。
 * 渲染层见 `ui/component/LevelWaveform.kt`。
 */
/** 波形柱数：50 柱 × 200ms = 最近 10 秒（R-14）。 */
const val WAVEFORM_BARS: Int = 50

/** 数据采样周期（ms）：数据只按这个粒度进，渲染层每帧重画（R-17）。 */
const val WAVEFORM_SAMPLE_MS: Long = 200L

/** 高度归一化的可见 dB 下限（−60 dBFS 以下视为 0 高度）。 */
const val WAVEFORM_FLOOR_DB: Double = -60.0

/** 静音哨兵（上层"实际推流"在静音 / 0 档时的取值，渲染高度为 0）。 */
const val WAVEFORM_MUTE_DB: Double = -200.0

/** 缓动上行系数（R-17：每帧向上逼近目标的 25%，60fps 基准）。 */
const val WAVEFORM_ATTACK: Float = 0.25f

/** 缓动下行系数（R-17：每帧向下回落 8%，约 3 倍慢于上行 → "响应快又丝滑"）。 */
const val WAVEFORM_RELEASE: Float = 0.08f

/** 峰值保持时长：6 个采样 × 200ms = 1.2s，之后回落（与 PC 端一致）。 */
const val WAVEFORM_PEAK_HOLD_SAMPLES: Int = 6

/**
 * 柱高缓动（R-17）：**指数平滑，不做线性插值**。
 *
 * `current += (target − current) × k`，上行 k = 0.25、下行 k = 0.08；
 * [frameScale] 是本帧相对 60fps 帧长的倍数（帧率无关：低帧率设备一步走多点，
 * 高帧率设备步子小但更密，收敛速度一致）。
 */
fun easeLevel(current: Float, target: Float, frameScale: Float = 1f): Float {
    if (frameScale <= 0f) {
        return current
    }
    val base = if (target > current) WAVEFORM_ATTACK else WAVEFORM_RELEASE
    val factor = 1.0 - (1.0 - base).pow(frameScale.toDouble())
    return (current + (target - current) * factor).toFloat()
}

/**
 * dBFS -> 归一化柱高（0..1）：线性映射 [WAVEFORM_FLOOR_DB, 0] → [0, 1]；
 * 低于下限（含 −120 dBFS 的数字静音与静音哨兵）一律 0。
 */
fun dbToHeight(db: Double): Float =
    ((db - WAVEFORM_FLOOR_DB) / -WAVEFORM_FLOOR_DB).coerceIn(0.0, 1.0).toFloat()

/**
 * UI 消费的波形快照：[slots] 旧 → 新；[version] 每次灌数 +1
 * （渲染层用它检测"新柱从右缘进入"，重置平移相位）。
 */
data class WaveformData(
    val slots: List<LevelHistory.Slot>,
    val version: Long
)

/**
 * 10 秒环形缓冲（容量 [WAVEFORM_BARS]）+ 峰值保持。
 *
 * 每个槽位三层数据（R-15）：
 *  - `collectedDb`：**采集到的电平**（底层，暗柱）；
 *  - `streamDb`：**实际推流电平** = 采集电平 × 协议音量增益（上层，亮柱），
 *    静音 / 0 档时为 [WAVEFORM_MUTE_DB]；
 *  - `peakDb`：采集电平的峰值保持——被超过即抬升并重新计时，保持 1.2s
 *    （6 个采样）后回落到当前值（与 PC 端一致）。
 */
class LevelHistory(
    private val capacity: Int = WAVEFORM_BARS,
    private val peakHoldSamples: Int = WAVEFORM_PEAK_HOLD_SAMPLES
) {
    /** 单柱数据（dBFS）。 */
    data class Slot(
        val collectedDb: Double,
        val streamDb: Double,
        val peakDb: Double
    )

    private val slots = ArrayDeque<Slot>()
    private var peakDb: Double = WAVEFORM_MUTE_DB
    private var holdCount: Int = 0

    /** 当前槽位数（≤ [capacity]）。 */
    val size: Int get() = slots.size

    /**
     * 灌入一个采样（200ms 一次）。
     *
     * @param collectedDb 采集电平（dBFS）。
     * @param streamDb 推流电平（已按 [streamDbFor] 算好；静音为 [WAVEFORM_MUTE_DB]）。
     */
    fun push(collectedDb: Double, streamDb: Double) {
        if (collectedDb >= peakDb) {
            // 新峰值：抬升并重新计时
            peakDb = collectedDb
            holdCount = 0
        } else {
            holdCount++
            if (holdCount > peakHoldSamples) {
                // 保持超过 1.2s：回落到当前电平
                peakDb = collectedDb
                holdCount = 0
            }
        }
        slots.addLast(Slot(collectedDb, streamDb, peakDb))
        while (slots.size > capacity) {
            slots.removeFirst()
        }
    }

    /** 快照（旧 → 新）；渲染层每帧读取，灌数频率 5 次/秒，开销可忽略。 */
    fun snapshot(): List<Slot> = slots.toList()

    /** 清空（如服务停止）。 */
    fun clear() {
        slots.clear()
        peakDb = WAVEFORM_MUTE_DB
        holdCount = 0
    }

    companion object {
        /**
         * 协议音量档位 -> 增益 dB（手册 §7.2，与 PC 端 `apply_vol` 同语义）：
         * 0 = 静音；1-4 每档 −6 dB；5 = 原样（0 dB）；6-9 每档 +6 dB。
         */
        fun gainDb(volume: Int): Double = when {
            volume <= 0 -> Double.NEGATIVE_INFINITY
            else -> (volume.coerceIn(1, 9) - 5) * 6.0
        }

        /**
         * 推流电平 = 采集电平 + 增益，**饱和钳制在 0 dBFS**（+24 dB 的增益不会
         * 把波形推出数字满幅）；静音开关打开或 0 档 → [WAVEFORM_MUTE_DB]。
         */
        fun streamDbFor(collectedDb: Double, volume: Int, muted: Boolean): Double {
            if (muted || volume <= 0) {
                return WAVEFORM_MUTE_DB
            }
            return (collectedDb + gainDb(volume)).coerceAtMost(0.0)
        }
    }
}
