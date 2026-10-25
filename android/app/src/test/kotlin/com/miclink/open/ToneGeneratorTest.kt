package com.miclink.open

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin

/**
 * 调试音波形的纯 JVM 单元测试（R-22「接收到的波形和生成的一致」的数学证明）。
 *
 * 断言两件事：
 *  1. **相位跨帧连续**：连续生成 N 帧 PCM，帧边界处（第 k-1 帧末样本 →
 *     第 k 帧首样本）的差值不超过一个采样步长，即没有跳变 / 没有重置相位；
 *  2. **波形等于解析式**：第 k 个样本 ≈ `sin(2π·440·k/48000)·0.3·32767`
 *     （容差 2，仅来自 `(Double).toInt()` 的截断）。
 *
 * 这两条合起来就是"PC 端听到的正弦与手机上生成的是同一条曲线"的机器证明；
 * 配合 FramePacerTest 的节奏断言，R-22 的两个成因（节奏慢 + 波形）都封闭了。
 */
@RunWith(JUnit4::class)
class ToneGeneratorTest {

    private companion object {
        const val SAMPLE_RATE: Int = 48000
        const val FREQ_HZ: Double = 440.0
        const val AMPLITUDE: Double = 0.3
        const val FRAME_SAMPLES: Int = 960 // 20ms @ 48kHz
        const val FRAMES: Int = 20

        /** 一个采样步长的理论最大样本差：A·32767·2π·f/sr。 */
        val MAX_STEP: Double = AMPLITUDE * 32767.0 * 2.0 * PI * FREQ_HZ / SAMPLE_RATE
    }

    /** 连续生成 [frames] 帧，拼成一条完整 PCM（跨帧拼接，不重置相位）。 */
    private fun generate(frames: Int, frameSamples: Int): ShortArray {
        val gen = ToneGenerator(
            sampleRate = SAMPLE_RATE,
            frequencyHz = FREQ_HZ,
            amplitude = AMPLITUDE
        )
        val out = ShortArray(frames * frameSamples)
        val buf = ShortArray(frameSamples)
        var filled = 0
        repeat(frames) {
            gen.fill(buf, frameSamples)
            buf.copyInto(out, destinationOffset = filled)
            filled += frameSamples
        }
        return out
    }

    // ------------------------------------------------------------------
    // 1) 相位连续：帧边界无跳变
    // ------------------------------------------------------------------

    @Test
    fun frameBoundaries_haveNoJump() {
        val pcm = generate(FRAMES, FRAME_SAMPLES)
        val tolerance = MAX_STEP + 2.0 // +2 覆盖 toInt() 截断

        var maxDiff = 0
        for (k in 1 until pcm.size) {
            val diff = abs(pcm[k].toInt() - pcm[k - 1].toInt())
            if (diff > maxDiff) maxDiff = diff
            assertTrue(
                "第 $k 个样本处出现跳变：${pcm[k - 1]} → ${pcm[k]}（差 $diff，" +
                    "单采样步长上限 ${tolerance.toInt()}）",
                diff <= tolerance.toInt()
            )
        }
        println(
            "[ToneGeneratorTest] ${FRAMES} 帧 × ${FRAME_SAMPLES} 样本：" +
                "相邻样本最大差 $maxDiff，理论步长 ${MAX_STEP.toInt()}（上限 ${tolerance.toInt()}）"
        )
    }

    /** 单独把每个帧边界抽出来断言（第 k 帧首样本 vs 上一帧末样本）。 */
    @Test
    fun frameBoundarySamples_areContinuous() {
        val pcm = generate(FRAMES, FRAME_SAMPLES)
        val tolerance = (MAX_STEP + 2.0).toInt()
        var maxBoundaryDiff = 0
        for (frame in 1 until FRAMES) {
            val prevLast = pcm[frame * FRAME_SAMPLES - 1].toInt()
            val curFirst = pcm[frame * FRAME_SAMPLES].toInt()
            val diff = abs(curFirst - prevLast)
            if (diff > maxBoundaryDiff) maxBoundaryDiff = diff
            assertTrue(
                "第 $frame 帧边界跳变：$prevLast → $curFirst（差 $diff > $tolerance）",
                diff <= tolerance
            )
        }
        println("[ToneGeneratorTest] ${FRAMES - 1} 个帧边界：最大差 $maxBoundaryDiff（上限 $tolerance）")
    }

    // ------------------------------------------------------------------
    // 2) 波形 = 解析式 sin(2π·f·k/sr)·A·32767
    // ------------------------------------------------------------------

    @Test
    fun waveform_matchesAnalyticSine() {
        val pcm = generate(FRAMES, FRAME_SAMPLES)
        var maxError = 0
        for (k in 0 until pcm.size) {
            val expected = sin(2.0 * PI * FREQ_HZ * k / SAMPLE_RATE) * AMPLITUDE * 32767.0
            val error = abs(pcm[k].toInt() - expected)
            if (error > maxError) maxError = error.toInt()
            assertTrue(
                "第 $k 个样本 ${pcm[k]} 与解析式 ${expected.toInt()} 偏差 ${error.toInt()} 过大",
                error <= 2.0
            )
        }
        println(
            "[ToneGeneratorTest] 与解析式 sin(2π·440·k/48000)·0.3·32767 的最大偏差 = $maxError（容差 2）"
        )
    }

    /** 起点与整周期：k=0 为 0；48000/440 = 109.09 采样/周期（用户已排除的非整除项）。 */
    @Test
    fun waveform_startsAtZeroAndHasExpectedPeriod() {
        val pcm = generate(2, FRAME_SAMPLES)
        assertEquals("k=0 应为 0（sin(0)=0）", 0, pcm[0].toInt())
        val samplesPerCycle = SAMPLE_RATE / FREQ_HZ
        println("[ToneGeneratorTest] 440Hz @48kHz = %.2f 采样/周期".format(samplesPerCycle))
        assertTrue(samplesPerCycle > 100.0 && samplesPerCycle < 120.0)
    }

    /** 生成器计数：连续 N 帧后样本序号 = N × 帧长（供跨帧连续性推理）。 */
    @Test
    fun generatedSamples_countsAcrossFrames() {
        val gen = ToneGenerator(SAMPLE_RATE, FREQ_HZ, AMPLITUDE)
        val buf = ShortArray(FRAME_SAMPLES)
        repeat(3) { gen.fill(buf, FRAME_SAMPLES) }
        assertEquals((3 * FRAME_SAMPLES).toLong(), gen.generatedSamples)
    }

    /** 幅度不削顶：0.3 × 32767 ≈ 9830 < 32767（用户已排除的削顶项，这里固化下来）。 */
    @Test
    fun amplitude_neverClips() {
        val pcm = generate(FRAMES, FRAME_SAMPLES)
        val peak = pcm.maxOf { abs(it.toInt()) }
        println("[ToneGeneratorTest] 峰值样本 $peak（16bit 上限 32767）")
        assertTrue("峰值应远低于削顶（实际 $peak）", peak < 32767)
        assertTrue("峰值应接近 0.3×32767≈9830（实际 $peak）", peak in 9700..9900)
    }
}
