package com.miclink.open

import java.net.ServerSocket
import java.net.InetAddress
import java.net.Socket
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class LoopbackRelayTest {
    @Test fun `ownership exists before connect and fragmented hello reaches both directions`() {
        val executor = Executors.newSingleThreadExecutor()
        try {
            ServerSocket(0, 1, InetAddress.getByName("127.0.0.1")).use { server ->
                LoopbackRelay().use { relay ->
                    assertTrue(relay.sourcePort in 1..65535)
                    val hello = byteArrayOf(101, 0, 0, 0, 6, 4, 6, 3, 0, 0, 0x53)
                    val reply = byteArrayOf(101, 0, 0, 0, 3, 0, 4, 2)
                    val result = executor.submit<Boolean> {
                        server.accept().use { accepted ->
                            accepted.soTimeout = 2000
                            assertTrue(relay.owns(accepted)) // may run before connect() returns
                            assertArrayEquals(hello, accepted.getInputStream().readNBytes(hello.size))
                            for (b in reply) accepted.getOutputStream().write(byteArrayOf(b))
                        }
                        true
                    }
                    relay.connect(server.localPort)
                    // Two writes model a fragmented RFCOMM read, not a whole-frame assumption.
                    relayBytes(java.io.ByteArrayInputStream(hello.copyOfRange(0, 4)), relay.output) {}
                    relayBytes(java.io.ByteArrayInputStream(hello.copyOfRange(4, hello.size)), relay.output) {}
                    val received = java.io.ByteArrayOutputStream()
                    relayBytes(relay.input, received) {}
                    assertArrayEquals(reply, received.toByteArray())
                    assertTrue(result.get(3, TimeUnit.SECONDS))
                }
            }
        } finally { executor.shutdownNow() }
    }
    @Test fun `unrelated connection fails ownership`() {
        ServerSocket(0).use { server ->
            LoopbackRelay().use { relay ->
                Socket("127.0.0.1", server.localPort).use {
                    server.accept().use { other -> assertFalse(relay.owns(other)) }
                }
            }
        }
    }
}
