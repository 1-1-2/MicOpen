package com.miclink.open

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4

/**
 * R-38a 对账通道的纯 JVM 单测（本地无模拟器，协议与判定全靠这里锁死）。
 *
 * 覆盖三块：
 *  1. 探针包编解码：16B 布局（magic/type/seq/packets/lastSeq/rsv）与大端往返；
 *  2. 超时判定：假时钟，2 次探针无应答 → 未回执；第 1 次有应答 → 正常 + 丢包；
 *  3. 状态栏文案：正常 / 未回执 / 无数据三态，含「—」规则。
 */
@RunWith(JUnit4::class)
class ReconcileCodecTest {

    @Test
    fun probe_isSixteenBytesWithFixedLayout() {
        val p = ReconcileCodec.encodeProbe(seq = 0x01020304, sentPackets = 0x00000A0B, lastSeq = 0xBEEF)
        assertEquals(16, p.size)
        // magic "MLK1"
        assertEquals("MLK1", String(p, 0, 4, Charsets.US_ASCII))
        // type = 1（探针）
        assertEquals(RECONCILE_TYPE_PROBE, p[4].toInt())
        // seq / packets 大端
        assertEquals(0x01, p[5].toInt() and 0xFF)
        assertEquals(0x02, p[6].toInt() and 0xFF)
        assertEquals(0x03, p[7].toInt() and 0xFF)
        assertEquals(0x04, p[8].toInt() and 0xFF)
        // packets = 0x00000A0B → 偏移 9..12 = 00 00 0A 0B
        assertEquals(0x00, p[9].toInt() and 0xFF)
        assertEquals(0x0A, p[11].toInt() and 0xFF)
        assertEquals(0x0B, p[12].toInt() and 0xFF)
        // lastSeq 大端 u16
        assertEquals(0xBE, p[13].toInt() and 0xFF)
        assertEquals(0xEF, p[14].toInt() and 0xFF)
        // rsv
        assertEquals(0, p[15].toInt())
    }

    @Test
    fun probe_roundTrip() {
        val p = ReconcileCodec.encodeProbe(seq = 481, sentPackets = 123456, lastSeq = 65000)
        val back = ReconcileCodec.decodeProbe(p, p.size)
        requireNotNull(back)
        assertEquals(RECONCILE_TYPE_PROBE, back.type)
        assertEquals(481, back.seq)
        assertEquals(123456, back.packets)
        assertEquals(65000, back.lastSeq)
    }

    @Test
    fun report_roundTripAndTypeIsTwo() {
        val r = ReconcileCodec.encode(RECONCILE_TYPE_REPORT, seq = 7, packets = 120300, lastSeq = 64990)
        assertEquals(RECONCILE_TYPE_REPORT, r[4].toInt())
        val back = ReconcileCodec.decodeReport(r, r.size)
        requireNotNull(back)
        assertEquals(7, back.seq)
        assertEquals(120300, back.packets)
        assertEquals(64990, back.lastSeq)
        // 严格的类型区分：probe 不能当成 report 用
        assertNull(ReconcileCodec.decodeReport(ReconcileCodec.encodeProbe(1, 2, 3), 16))
        assertNull(ReconcileCodec.decodeProbe(r, r.size))
    }

    @Test
    fun decode_rejectsShortBadMagicAndUnknownType() {
        val good = ReconcileCodec.encodeProbe(1, 2, 3)
        assertNull(ReconcileCodec.decode(good, 15)) // 长度不足
        val badMagic = good.copyOf().also { it[0] = 'X'.code.toByte() }
        assertNull(ReconcileCodec.decode(badMagic, 16))
        val badType = good.copyOf().also { it[4] = 9 }
        assertNull(ReconcileCodec.decode(badType, 16))
    }
}

/** 超时判定（R-38a 的第 4 条）：假时钟逐拍推进，断言状态与丢包数字。 */
@RunWith(JUnit4::class)
class ReconcileTrackerTest {

    @Test
    fun twoProbesWithoutReply_marksNoReply() {
        val t = ReconcileTracker()
        t.start(0)
        // 第 1 个探针：立刻发
        assertTrue(t.probeDue(0))
        t.onProbeSent(0, sentPackets = 100)
        // 1.5s 后第 2 个
        assertTrue(!t.probeDue(500))
        assertTrue(t.probeDue(1500))
        t.onProbeSent(1500, sentPackets = 112)
        // 已发满 2 次 → 不再在启动阶段发探针，等应答判定
        assertTrue(!t.probeDue(2000))
        // 判定时刻 = 第 2 次探针 + 1.5s
        t.tick(2999)
        assertEquals(ReconcileStatus.WAITING, t.status)
        t.tick(3000)
        assertEquals(ReconcileStatus.NO_REPLY, t.status)
        println("[ReconcileTrackerTest] 2 次探针无应答 → ${t.status}（sent=${t.sentAtLastProbe}）")
        // 判定后**不影响推流**：状态只是标记，且仍按 3s 继续探（对端可能稍后起来）
        assertTrue(t.probeDue(6000))
    }

    @Test
    fun firstReport_marksOkAndComputesLoss() {
        val t = ReconcileTracker()
        t.start(0)
        assertTrue(t.probeDue(0))
        t.onProbeSent(0, sentPackets = 1000)
        // 第 1 次探针就有回执：PC 实收 980 → 丢 20 包 = 2.0%
        t.onReport(300, recvPackets = 980)
        assertEquals(ReconcileStatus.OK, t.status)
        assertEquals(20, t.lossPermille())
        println("[ReconcileTrackerTest] 首次即有回执 → ${t.status}，丢包 ${t.lossPermille()}‰")
        // 有回执后按 3s 稳态探针
        assertTrue(!t.probeDue(2000))
        assertTrue(t.probeDue(3300))
    }

    @Test
    fun recovery_afterNoReply_reportFlipsToOk() {
        val t = ReconcileTracker()
        t.start(0)
        t.onProbeSent(0, 100)
        t.onProbeSent(1500, 112)
        t.tick(3000)
        assertEquals(ReconcileStatus.NO_REPLY, t.status)
        // 判定后 3s 再探，这次对端回了
        assertTrue(t.probeDue(6000))
        t.onProbeSent(6000, sentPackets = 140)
        t.onReport(6100, recvPackets = 140)
        assertEquals(ReconcileStatus.OK, t.status)
        assertEquals(0, t.lossPermille())
    }

    @Test
    fun lossIsNullWhenNoData() {
        val t = ReconcileTracker()
        assertEquals(ReconcileStatus.IDLE, t.status)
        assertNull(t.lossPermille())
        t.start(0)
        assertNull(t.lossPermille()) // 还没回执
        t.onReport(10, recvPackets = 0)
        assertNull(t.lossPermille()) // 回执到了但自己还没发过包 → 不编造 0%
    }
}

/** 状态栏文案组装（R-34 右列 + R-38a 回执行）。 */
@RunWith(JUnit4::class)
class ReconcileTextTest {

    @Test
    fun sentBytes_formatting() {
        assertEquals("—", ReconcileText.formatBytes(0))
        assertEquals("—", ReconcileText.formatBytes(-1))
        assertEquals("999 B", ReconcileText.formatBytes(999))
        assertEquals("1.0 KB", ReconcileText.formatBytes(1024))
        assertEquals("12.3 MB", ReconcileText.formatBytes(12_900_000))
        println(
            "[ReconcileTextTest] 已发送格式：0→「—」，999→「${ReconcileText.formatBytes(999)}」，" +
                "12900000→「${ReconcileText.formatBytes(12_900_000)}」"
        )
    }

    @Test
    fun reconcileLine_threeStates() {
        assertEquals("回执：—", ReconcileText.line(ReconcileStatus.IDLE, null))
        assertEquals("回执：—", ReconcileText.line(ReconcileStatus.WAITING, null))
        assertEquals("回执正常", ReconcileText.line(ReconcileStatus.OK, null))
        assertEquals("回执正常 · 丢包 2.0%", ReconcileText.line(ReconcileStatus.OK, 20))
        assertEquals("回执正常 · 丢包 0.3%", ReconcileText.line(ReconcileStatus.OK, 3))
        assertEquals(
            "对方未回执 · 可能是原厂客户端或防火墙拦截",
            ReconcileText.line(ReconcileStatus.NO_REPLY, null)
        )
        println(
            "[ReconcileTextTest] 文案：${ReconcileText.line(ReconcileStatus.OK, 20)} / " +
                ReconcileText.line(ReconcileStatus.NO_REPLY, null)
        )
    }
}
