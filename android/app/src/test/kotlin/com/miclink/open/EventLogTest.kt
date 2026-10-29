package com.miclink.open

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import kotlin.concurrent.thread

class EventLogTest {
    @Test fun retentionAndStableSnapshots() {
        val log = EventLog(maxEntries = 3, clock = { 0L })
        repeat(5) { log.append("INFO", "test", "event-$it") }
        val snapshot = log.snapshot()
        assertEquals(3, snapshot.count)
        assertEquals(2L, snapshot.dropped)
        assertFalse(snapshot.text.contains("event-1"))
        assertTrue(snapshot.text.indexOf("event-2") < snapshot.text.indexOf("event-4"))
        assertSame(snapshot, log.snapshot())
        log.append("WARN", "test", "later")
        assertFalse(snapshot.text.contains("later")) // paused UI keeps its immutable copy
        assertNotEquals(snapshot.version, log.snapshot().version)
    }
    @Test fun characterBoundAndRedaction() {
        val log = EventLog(maxCharacters = 512, clock = { 0L })
        log.append("ERROR", "test", "PIN=12345678 secret='do not copy' token=ghp_abcdefghijklmnop\nnext")
        val safe = log.snapshot().text
        for (privateText in listOf("12345678", "do not copy", "ghp_abcdefghijklmnop")) assertFalse(safe.contains(privateText))
        assertTrue(safe.contains("已隐藏"))
        assertFalse(log.sanitize("Authorization: Bearer arbitrary-private-value").contains("arbitrary-private-value"))
        repeat(8) { log.append("INFO", "test", "x".repeat(2000)) }
        assertTrue(log.snapshot().text.length < 512)
        assertTrue(log.snapshot().dropped > 0)
    }
    @Test fun concurrentWritersStayBoundedAndComplete() {
        val log = EventLog(maxEntries = 300, clock = { 0L })
        val gate = CountDownLatch(1)
        val workers = (0..3).map { worker -> thread {
            gate.await()
            repeat(100) { log.append("INFO", "worker-$worker", "entry-$it") }
        } }
        gate.countDown()
        workers.forEach { it.join() }
        val snapshot = log.snapshot()
        assertEquals(400L, snapshot.version)
        assertEquals(300, snapshot.count)
        assertEquals(100L, snapshot.dropped)
        assertEquals(300, snapshot.text.lines().size)
    }
}
