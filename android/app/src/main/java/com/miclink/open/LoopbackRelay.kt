package com.miclink.open

import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket

/** Publish a bound source port before connect can wake the session accept thread. */
class LoopbackRelay : AutoCloseable {
    private val socket = Socket().apply { bind(InetSocketAddress("127.0.0.1", 0)) }
    val sourcePort: Int = socket.localPort
    fun owns(peer: Socket): Boolean = peer.inetAddress.isLoopbackAddress && peer.port == sourcePort
    fun connect(port: Int) {
        socket.connect(InetSocketAddress("127.0.0.1", port), 3000)
        socket.tcpNoDelay = true
    }
    val input: InputStream get() = socket.getInputStream()
    val output: OutputStream get() = socket.getOutputStream()
    override fun close() = socket.close()
}

/** Explicit byte forwarding with progress; usable by radio-free regression tests. */
internal fun relayBytes(input: InputStream, output: OutputStream, progress: (Int) -> Unit) {
    val buffer = ByteArray(8192)
    while (true) {
        val count = input.read(buffer)
        if (count < 0) return
        if (count == 0) continue
        output.write(buffer, 0, count)
        output.flush()
        progress(count)
    }
}
