package com.miclink.open

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import kotlin.math.abs
import kotlin.math.pow

/**
 * 「最近 10 秒」双层波形（R-14/R-15/R-17）的纯 JVM 单元测试。
 *
 * 覆盖任务书要求的三个数字断言：
 *  1. **双层计算**：rms=−20dB、vol=7（+12dB）→ 推流层 −8dB；+增益饱和钳到
 *     0 dBFS；vol=0 / 静音 → 推流层归零（静音哨兵）；
 *  2. **环形缓冲**：灌 60 个采样，缓冲里只剩最后 50 个（10 秒窗口）；
 *  3. **缓动**：上行 3 个采样帧（200ms/帧）内到位 ≥90%，下行 8 个采样帧内
 *     回落 ≥90%——并给出 0.25/0.08 系数下的精确帧数（9 / 28 帧 @60fps），
 *     证明上行明显快于下行（"响应快又丝滑"）。
 *
 * 另覆盖峰值保持（1.2s = 6 采样）与高度归一化边界。
 */
@RunWith(JUnit4::class)
class LevelHistoryTest {

    // ------------------------------------------------------------------
    // 1) 双层计算（R-15）
    // ------------------------------------------------------------------

    /** rms=−20dB、vol=7（+12dB）→ 推流层 = −8dB。 */
    @Test
    fun dualLayer_volume7_addsTwelveDb() {
        val stream = LevelHistory.streamDbFor(collectedDb = -20.0, volume = 7, muted = false)
        println("[LevelHistoryTest] rms=-20dB vol=7 → 推流层 $stream dB（期望 -8）")
        assertEquals(-8.0, stream, 1e-9)
    }

    /** +增益饱和：rms=−2dB、vol=9（+24dB）→ 理论 +22，钳制到 0 dBFS（数字满幅）。 */
    @Test
    fun dualLayer_saturatesAtZeroDbfs() {
        val stream = LevelHistory.streamDbFor(collectedDb = -2.0, volume = 9, muted = false)
        println("[LevelHistoryTest] rms=-2dB vol=9(+24dB) → 推流层 $stream dB（饱和钳制到 0）")
        assertEquals(0.0, stream, 1e-9)
    }

    /** 增益表与 PC 端 apply_vol 同语义：0=静音，1-4 每档 −6dB，5=原样，6-9 每档 +6dB。 */
    @Test
    fun gainTable_matchesProtocolSemantics() {
        assertEquals(Double.NEGATIVE_INFINITY, LevelHistory.gainDb(0), 1e-9)
        assertEquals(-24.0, LevelHistory.gainDb(1), 1e-9)
        assertEquals(-6.0, LevelHistory.gainDb(4), 1e-9)
        assertEquals(0.0, LevelHistory.gainDb(5), 1e-9)
        assertEquals(6.0, LevelHistory.gainDb(6), 1e-9)
        assertEquals(24.0, LevelHistory.gainDb(9), 1e-9)
    }

    /** vol=0 或静音开关 → 推流层 = 静音哨兵（渲染高度 0），采集层不受影响。 */
    @Test
    fun dualLayer_muteZeroesStreamLayerOnly() {
        assertEquals(
            WAVEFORM_MUTE_DB,
            LevelHistory.streamDbFor(-20.0, volume = 0, muted = false),
            1e-9
        )
        assertEquals(
            WAVEFORM_MUTE_DB,
            LevelHistory.streamDbFor(-20.0, volume = 7, muted = true),
            1e-9
        )
        // 静音只作用上层：历史里采集层原样保留
        val history = LevelHistory()
        history.push(-20.0, LevelHistory.streamDbFor(-20.0, volume = 0, muted = false))
        val slot = history.snapshot().single()
        assertEquals(-20.0, slot.collectedDb, 1e-9)
        assertEquals(WAVEFORM_MUTE_DB, slot.streamDb, 1e-9)
    }

    // ------------------------------------------------------------------
    // 2) 环形缓冲（R-14：50 柱 = 10 秒）
    // ------------------------------------------------------------------

    /** 灌 60 个采样，缓冲里只剩最后 50 个，且顺序为旧 → 新。 */
    @Test
    fun ringBuffer_keepsLastFifty() {
        val history = LevelHistory()
        repeat(60) { i ->
            val db = i.toDouble() - 1000.0 // 逐个可分辨的值
            history.push(db, db)
        }
        val slots = history.snapshot()
        println("[LevelHistoryTest] 灌 60 个采样 → 缓冲 ${slots.size} 柱")
        assertEquals(50, slots.size)
        // 第 60 个采样（i=59）在最右（新），第 11 个（i=10）在最左（旧）
        assertEquals(-990.0, slots.first().collectedDb, 1e-9)
        assertEquals(-941.0, slots.last().collectedDb, 1e-9)
    }

    /** 窗口语义：50 × 200ms = 10 秒。 */
    @Test
    fun window_isTenSeconds() {
        assertEquals(50, WAVEFORM_BARS)
        assertEquals(10_000L, WAVEFORM_BARS * WAVEFORM_SAMPLE_MS)
    }

    // ------------------------------------------------------------------
    // 3) 缓动（R-17：上行 0.25 / 下行 0.08，指数平滑）
    // ------------------------------------------------------------------

    /** 从 0 逐帧逼近 1，返回到达 ≥90% 所需帧数。 */
    private fun framesToReach90Up(): Int {
        var v = 0f
        var n = 0
        while (v < 0.9f && n < 1000) {
            v = easeLevel(v, 1f)
            n++
        }
        return n
    }

    /** 从 1 逐帧回落到 0，返回回落 ≥90% 所需帧数。 */
    private fun framesToFall90Down(): Int {
        var v = 1f
        var n = 0
        while (v > 0.1f && n < 1000) {
            v = easeLevel(v, 0f)
            n++
        }
        return n
    }

    /**
     * 上行响应快：3 个**采样帧**（200ms/帧 = 60fps 下 36 个渲染帧）内到位 ≥90%。
     */
    @Test
    fun easing_upReaches90pctWithinThreeSampleFrames() {
        var v = 0f
        repeat(36) { v = easeLevel(v, 1f) } // 3 × 200ms @ 60fps
        println("[LevelHistoryTest] 上行 3 采样帧（36 渲染帧）后高度 $v（要求 ≥0.9）")
        assertTrue("上行 3 采样帧应到位 ≥90%（实际 $v）", v >= 0.9f)
    }

    /** 下行丝滑：8 个采样帧（96 渲染帧）内回落 ≥90%。 */
    @Test
    fun easing_downFalls90pctWithinEightSampleFrames() {
        var v = 1f
        repeat(96) { v = easeLevel(v, 0f) } // 8 × 200ms @ 60fps
        println("[LevelHistoryTest] 下行 8 采样帧（96 渲染帧）后高度 $v（要求 ≤0.1）")
        assertTrue("下行 8 采样帧应回落 ≥90%（实际 $v）", v <= 0.1f)
    }

    /**
     * 精确机制：0.25/帧的上行 90% 需 9 渲染帧（150ms），0.08/帧的下行需 28 帧
     * （467ms）——上行明显快于下行（≥3 倍），"响应快又丝滑"。
     */
    @Test
    fun easing_attackIsAtLeastThreeTimesFasterThanRelease() {
        val up = framesToReach90Up()
        val down = framesToFall90Down()
        println("[LevelHistoryTest] 到位/回落 90%：上行 $up 帧（${up * 16.67}ms），下行 $down 帧（${down * 16.67}ms）")
        assertEquals(9, up)
        assertEquals(28, down)
        assertTrue("上行应比下行快 ≥3 倍（$up vs $down）", down >= up * 3)
    }

    /** 上行单帧步进 = 25% 的剩余距离；下行 = 8%。 */
    @Test
    fun easing_stepFactorsMatchSpec() {
        assertEquals(0.25f, easeLevel(0f, 1f), 1e-6f)
        assertEquals(0.08f, easeLevel(1f, 0f).let { 1f - it }, 1e-6f)
    }

    /** 帧率无关：一次 2 帧长的步进 == 两次 1 帧步进（低帧率设备收敛速度一致）。 */
    @Test
    fun easing_isFrameRateIndependent() {
        val oneStep = easeLevel(easeLevel(0.2f, 0.8f, 1f), 0.8f, 1f)
        val twoStep = easeLevel(0.2f, 0.8f, 2f)
        println("[LevelHistoryTest] 帧率无关性：两步 $oneStep vs 一步×2 $twoStep")
        assertTrue(abs(oneStep - twoStep) < 1e-4f)
    }

    // ------------------------------------------------------------------
    // 峰值保持（1.2s = 6 采样）与高度归一化
    // ------------------------------------------------------------------

    /** 峰值保持 6 个采样后回落：第 7 个更低采样起，峰值降到当前电平。 */
    @Test
    fun peakHold_lastSixSamplesThenFalls() {
        val history = LevelHistory()
        history.push(-10.0, -10.0)
        repeat(6) { history.push(-40.0, -40.0) } // 保持期（1.2s）
        val during = history.snapshot().last().peakDb
        history.push(-40.0, -40.0) // 第 7 个采样：超出保持期 → 回落
        val after = history.snapshot().last().peakDb
        println("[LevelHistoryTest] 峰值保持：期内 $during dB，1.2s 后 $after dB")
        assertEquals(-10.0, during, 1e-9)
        assertEquals(-40.0, after, 1e-9)
    }

    /** 更高峰把保持计时清零（峰值抬升后重新保持 1.2s）。 */
    @Test
    fun peakHold_resetsOnNewPeak() {
        val history = LevelHistory()
        history.push(-10.0, -10.0)
        repeat(5) { history.push(-40.0, -40.0) }
        history.push(-8.0, -8.0) // 新峰值：计时清零
        repeat(5) { history.push(-40.0, -40.0) } // 仍未超时
        assertEquals(-8.0, history.snapshot().last().peakDb, 1e-9)
    }

    /** 高度归一化：−60→0、0→1、数字静音/静音哨兵→0、−30→0.5。 */
    @Test
    fun dbToHeight_boundaries() {
        assertEquals(0f, dbToHeight(-60.0))
        assertEquals(1f, dbToHeight(0.0))
        assertEquals(0f, dbToHeight(LevelMeter.SILENCE_DB.toDouble()))
        assertEquals(0f, dbToHeight(WAVEFORM_MUTE_DB))
        assertEquals(0.5f, dbToHeight(-30.0), 1e-6f)
        assertTrue(dbToHeight(-6.0) in 0.89f..0.91f)
    }
}
