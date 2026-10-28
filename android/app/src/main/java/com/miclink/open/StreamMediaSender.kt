package com.miclink.open

import dev.womic.wifistream.protocol.FrameDecoder
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Bounded queue: no socket writes on the AudioRecord thread. Closing the owning
 * connection interrupts a blocked writer. USB and RFCOMM use identical framing. */
class StreamMediaSender(
    private val write: (ByteArray) -> Unit,
    private val abort: () -> Unit
) : MediaSender {
    private data class Packet(val bytes: ByteArray, val at: Long = System.nanoTime())
    private val queue = ArrayBlockingQueue<Packet>(3)
    private val running = AtomicBoolean(false)
    private var worker: Thread? = null
    private var guard: Thread? = null
    @Volatile private var writeAt = 0L
    @Volatile override var lastError: String? = null; private set
    @Volatile override var failureStreak = 0; private set
    @Volatile override var packetsSent = 0L; private set
    @Volatile override var bytesSent = 0L; private set
    override val isOpen get() = running.get()
    override fun open(host: String, port: Int): Boolean {
        if (!running.compareAndSet(false, true)) return false
        worker = Thread({
            try {
                while (running.get()) {
                    val p = queue.poll(100, TimeUnit.MILLISECONDS) ?: continue
                    if (System.nanoTime() - p.at > 200_000_000) continue
                    writeAt = System.nanoTime()
                    try { write(FrameDecoder.encode(200, p.bytes)) } finally { writeAt = 0 }

                    packetsSent++; bytesSent += p.bytes.size
                }
            } catch (_: InterruptedException) {
                // Normal stop.
            } catch (e: Exception) {
                lastError = "音频流断开：${e.message}"
                failureStreak++
                abort()
            } finally { running.set(false) }
        }, "miclink-media-writer").also { it.start() }
        guard = Thread({
            try {
                while (running.get()) {
                    Thread.sleep(50)
                    val at = writeAt
                    if (at != 0L && System.nanoTime() - at > 500_000_000) {
                        lastError = "音频发送阻塞超过500ms，断开以避免延迟积累"
                        failureStreak++
                        abort()
                        break
                    }
                }
            } catch (_: InterruptedException) {}
        }, "miclink-media-deadline").also { it.start() }
        return true
    }
    override fun send(data: ByteArray, length: Int): Boolean {
        if (!running.get() || length !in 1..minOf(data.size, 2048)) return false
        val p = Packet(data.copyOf(length))
        if (!queue.offer(p)) { queue.poll(); queue.offer(p) }
        return true
    }
    override fun close() { running.set(false); queue.clear(); worker?.interrupt(); if (writeAt != 0L) abort(); guard?.interrupt() }
    override fun targetDescription() = "流式音频（USB / 蓝牙）"
}
