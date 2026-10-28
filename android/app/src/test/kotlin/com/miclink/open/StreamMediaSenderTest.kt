package com.miclink.open

import dev.womic.wifistream.protocol.FrameDecoder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class StreamMediaSenderTest {
    @Test fun `packet reaches stream decoder intact`() {
        val received = CountDownLatch(1)
        var wire = byteArrayOf()
        val sender = StreamMediaSender({ wire = it; received.countDown() }, { fail("unexpected abort") })
        try {
            assertTrue(sender.open("",0))
            assertTrue(sender.send(byteArrayOf(4,0,1,2,3),5))
            assertTrue(received.await(2,TimeUnit.SECONDS))
            val f = FrameDecoder().feed(wire).single()
            assertEquals(200,f.type)
            assertArrayEquals(byteArrayOf(4,0,1,2,3),f.payload)
        } finally { sender.close() }
    }
    @Test fun `blocked output never blocks capture and deadline aborts owner`() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val aborted = CountDownLatch(1)
        val sender = StreamMediaSender({ entered.countDown(); release.await() }, { aborted.countDown(); release.countDown() })
        try {
            sender.open("",0); sender.send(byteArrayOf(1),1)
            assertTrue(entered.await(2,TimeUnit.SECONDS))
            for (i in 1..1000) assertTrue(sender.send(byteArrayOf(2),1))
            assertTrue(aborted.await(3,TimeUnit.SECONDS))
            assertTrue(sender.failureStreak > 0)
        } finally { release.countDown(); sender.close() }
    }
}
