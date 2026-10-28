package dev.womic.wifistream.protocol

import org.junit.Assert.*
import org.junit.Test

class StreamTransportTest {
    private fun session() = WomicSession(object : SessionHost { override fun nowMs() = 0L })
    @Test fun `legacy hello stays on UDP and keeps reconcile capability`() {
        val s = session()
        s.onFrame(TcpFrame(101, byteArrayOf(4, 6, 3, 0, 0, 0)))
        assertFalse(s.streamMedia)
        assertEquals(1, FrameDecoder().feed(s.drainReplies().single()).single().payload[2].toInt())
    }
    @Test fun `stream hello negotiates explicitly without changing legacy header level`() {
        val s = session()
        s.onFrame(TcpFrame(101, byteArrayOf(4, 6, 3, 0, 0, 0x53)))
        assertTrue(s.streamMedia)
        assertArrayEquals(byteArrayOf(0,4,2), FrameDecoder().feed(s.drainReplies().single()).single().payload)
    }
    @Test fun `fragmented media and coalesced control retain packet boundaries`() {
        val data = ByteArray(2048) { it.toByte() }
        val wire = FrameDecoder.encode(200, data) + buildHeartbeat()
        val d = FrameDecoder()
        val frames = wire.asList().chunked(7).flatMap { d.feed(it.toByteArray()) }
        assertEquals(listOf(200,105),frames.map { it.type })
        assertArrayEquals(data,frames[0].payload)
    }
    @Test fun `oversized or negative length fails before buffering payload`() {
        for (length in listOf(2049, Int.MAX_VALUE, -1)) {
            val wire = FrameDecoder.encode(200, byteArrayOf()).copyOf()
            for (i in 0..3) wire[i+1] = (length ushr (24-8*i)).toByte()
            try { FrameDecoder().feed(wire); fail("accepted $length") }
            catch (_: IllegalArgumentException) {}
        }
    }
}
