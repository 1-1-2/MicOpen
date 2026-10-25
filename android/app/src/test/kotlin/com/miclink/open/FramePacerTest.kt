package com.miclink.open

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4

/**
 * 调试音发送节奏（R-22）的纯 JVM 单元测试。
 *
 * 背景：调试音模式旧实现每帧 `sleep(FRAME_MS)`（20ms），**不减掉**编码 + 组包
 * + 发送的耗时，于是帧周期 = 20ms + 处理耗时 → 慢于实时 5~15% → PC 端环形
 * 缓冲被逐渐抽干 → 周期性欠载爆音（用户真机实测，调缓冲 26/60/120 都一样）。
 *
 * 这里用**假时钟**把"改前 / 改后"都跑一遍：
 *  - 改前（[simulateOldPacing]）：固定 sleep(20) 再干活；
 *  - 改后（[FramePacer]）：绝对时间轴 + 重同步。
 *
 * 时钟与 sleep 全部注入，测试不依赖真实时间，结果确定性可复现。
 */
@RunWith(JUnit4::class)
class FramePacerTest {

    /** 假时钟：`sleep` 与"处理耗时"共用同一条时间轴（毫秒）。 */
    private class FakeClock {
        var nowMs: Long = 0L
            private set

        fun now(): Long = nowMs

        fun sleep(ms: Long) {
            if (ms > 0L) nowMs += ms
        }

        /** 模拟非睡眠的耗时（编码 / 组包 / 发送）。 */
        fun work(ms: Long) {
            if (ms > 0L) nowMs += ms
        }

        /** 模拟系统挂起 / GC 卡顿：时间凭空前进。 */
        fun stall(ms: Long) {
            nowMs += ms
        }
    }

    private companion object {
        const val FRAME_MS: Long = 20L
        const val FRAMES: Int = 1000
    }

    /** 改前的节奏：固定 sleep(20ms) + 处理耗时，返回总耗时（ms）。 */
    private fun simulateOldPacing(clock: FakeClock, workMs: Long, frames: Int = FRAMES): Long {
        repeat(frames) {
            clock.sleep(FRAME_MS) // 旧代码：SystemClock.sleep(Defaults.FRAME_MS)
            clock.work(workMs) // 编码 + 组包 + 发送
        }
        return clock.now()
    }

    /** 改后的节奏：FramePacer 绝对时间轴，返回总耗时（ms）。 */
    private fun simulateNewPacing(clock: FakeClock, workMs: Long, frames: Int = FRAMES): Long {
        val pacer = FramePacer(
            frameMs = FRAME_MS,
            now = clock::now,
            sleep = clock::sleep
        )
        repeat(frames) {
            pacer.awaitNextFrame()
            clock.work(workMs) // 编码 + 组包 + 发送
        }
        return clock.now()
    }

    // ------------------------------------------------------------------
    // 1) 节奏：改前偏 ~10%，改后 ≈ 实时
    // ------------------------------------------------------------------

    /**
     * 改前基线（用户实测"慢 5~15%"的复现）：每帧 sleep(20) + 处理 2ms = 22ms，
     * 1000 帧 = 22000ms，比实时（20000ms）**慢 10%**。
     */
    @Test
    fun oldPacing_driftsAboutTenPercentSlower() {
        val clock = FakeClock()
        val elapsed = simulateOldPacing(clock, workMs = 2L)
        val expected = FRAMES * FRAME_MS
        val drift = (elapsed - expected).toDouble() / expected.toDouble() * 100.0

        println("[FramePacerTest] 改前：${FRAMES} 帧耗时 ${elapsed}ms（实时 ${expected}ms，偏差 +%.2f%%".format(drift) + ")")
        assertEquals(22000L, elapsed)
        assertTrue("改前偏差应 ≈ +10%（实际 +$drift%）", drift > 9.0 && drift < 11.0)
    }

    /**
     * 处理耗时 1/2/3ms 分别对应慢 5%/10%/15% —— 与用户实测的"5~15%"区间吻合，
     * 证明"处理耗时没被计入"就是根因。
     */
    @Test
    fun oldPacing_driftBandMatchesFieldReport() {
        val drifts = listOf(1L, 2L, 3L).map { workMs ->
            val clock = FakeClock()
            val elapsed = simulateOldPacing(clock, workMs, frames = 200)
            val expected = 200L * FRAME_MS
            println("[FramePacerTest] 改前 work=${workMs}ms → ${elapsed}ms / 实时 ${expected}ms")
            (elapsed - expected).toDouble() / expected.toDouble() * 100.0
        }
        assertEquals(5.0, drifts[0], 0.01)
        assertEquals(10.0, drifts[1], 0.01)
        assertEquals(15.0, drifts[2], 0.01)
    }

    /**
     * 改后：1000 帧耗时 ≈ 1000×20ms，**偏差不超过 1 帧**（20ms）。
     * 注入每帧 22ms 的墙钟成本（= 20ms 睡眠 + 2ms 处理，即改前实测口径）。
     */
    @Test
    fun newPacing_thousandFramesStayOnRealTimeAxis() {
        val clock = FakeClock()
        val elapsed = simulateNewPacing(clock, workMs = 2L)
        val expected = FRAMES * FRAME_MS
        val drift = (elapsed - expected).toDouble() / expected.toDouble() * 100.0

        println(
            "[FramePacerTest] 改后：${FRAMES} 帧耗时 ${elapsed}ms（实时 ${expected}ms，偏差 %+.4f%%".format(drift) + ")"
        )
        assertTrue(
            "改后 1000 帧总耗时应落在 ±1 帧内（实际 ${elapsed}ms，实时 ${expected}ms）",
            kotlin.math.abs(elapsed - expected) <= FRAME_MS
        )
        assertTrue("改后偏差应 < 0.2%（实际 $drift%）", kotlin.math.abs(drift) < 0.2)
    }

    /** 处理耗时 1~3ms 三档，改后都不慢于实时（偏差 < 1 帧）。 */
    @Test
    fun newPacing_handlesVaryingWorkWithoutDrift() {
        for (workMs in listOf(1L, 2L, 3L)) {
            val clock = FakeClock()
            val elapsed = simulateNewPacing(clock, workMs, frames = 500)
            val expected = 500L * FRAME_MS
            println("[FramePacerTest] 改后 work=${workMs}ms → ${elapsed}ms / 实时 ${expected}ms")
            assertTrue(
                "work=${workMs}ms 时偏差应 < 1 帧（实际 ${elapsed - expected}ms）",
                kotlin.math.abs(elapsed - expected) < FRAME_MS
            )
        }
    }

    /**
     * 处理耗时超过帧长（如 26ms > 20ms）时：节奏器**不会**比改前更慢
     * （此时物理上做不到实时，只能尽力跑），也不累积欠账。
     */
    @Test
    fun newPacing_neverWorseThanOld_whenWorkExceedsFrame() {
        val workMs = 26L
        val oldClock = FakeClock()
        val oldElapsed = simulateOldPacing(oldClock, workMs, frames = 100)
        val newClock = FakeClock()
        val newElapsed = simulateNewPacing(newClock, workMs, frames = 100)

        println("[FramePacerTest] work>frame：改前 ${oldElapsed}ms，改后 ${newElapsed}ms")
        assertTrue("改后不得比改前更慢", newElapsed <= oldElapsed)
    }

    // ------------------------------------------------------------------
    // 2) 重同步：卡顿后不雪崩连发
    // ------------------------------------------------------------------

    /**
     * 注入一次 200ms 停顿（切后台 / GC / 系统挂起）：
     *  - 触发**恰好 1 次**重同步；
     *  - 重同步后**最多 1 帧**立即补发（不把 200ms 里欠的 10 帧一次灌给 PC）。
     */
    @Test
    fun resync_afterLongStallNoBurst() {
        val clock = FakeClock()
        val pacer = FramePacer(frameMs = FRAME_MS, now = clock::now, sleep = clock::sleep)

        // 先稳定跑 50 帧（每帧 2ms 处理）
        repeat(50) {
            pacer.awaitNextFrame()
            clock.work(2L)
        }
        val stallAt = clock.now()
        assertEquals(0L, pacer.resyncs)

        // 系统挂起 200ms
        clock.stall(200L)

        var immediate = 0 // 没睡就发的帧数（= 连发）
        repeat(10) {
            val waited = pacer.awaitNextFrame()
            if (waited == 0L) immediate++
            clock.work(2L)
        }

        println(
            "[FramePacerTest] 卡顿 200ms（@${stallAt}ms）：resyncs=${pacer.resyncs}，" +
                "紧随其后 10 帧中立即补发 $immediate 帧"
        )
        assertEquals("长停顿应触发恰好 1 次重同步", 1L, pacer.resyncs)
        assertTrue("重同步后最多 1 帧立即补发（实际 $immediate 帧）", immediate <= 1)
    }

    /**
     * 对照：一次**小**抖动（25ms，约 1 帧多一点）不应触发重同步——
     * 只是少睡一点把节奏追回来，不动时间轴。
     */
    @Test
    fun noResync_onSmallJitter() {
        val clock = FakeClock()
        val pacer = FramePacer(frameMs = FRAME_MS, now = clock::now, sleep = clock::sleep)

        repeat(10) {
            pacer.awaitNextFrame()
            clock.work(2L)
        }
        clock.stall(25L)
        repeat(5) {
            pacer.awaitNextFrame()
            clock.work(2L)
        }

        println("[FramePacerTest] 小抖动 25ms：resyncs=${pacer.resyncs}")
        assertEquals("25ms 抖动（< 3 帧）不应重同步", 0L, pacer.resyncs)
    }

    /** 首帧不预睡（立刻产出），避免启动后先黑 20ms。 */
    @Test
    fun firstFrame_isImmediate() {
        val clock = FakeClock()
        val pacer = FramePacer(frameMs = FRAME_MS, now = clock::now, sleep = clock::sleep)
        val waited = pacer.awaitNextFrame()
        assertEquals(0L, waited)
        assertEquals(0L, clock.now())
        assertEquals(1L, pacer.frames)
    }
}
