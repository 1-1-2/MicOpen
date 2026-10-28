package com.miclink.open

/** Audio packet sink; capture/codec know nothing about USB, Bluetooth or sockets. */
interface MediaSender {
    val lastError: String?
    val failureStreak: Int
    val packetsSent: Long
    val bytesSent: Long
    val isOpen: Boolean
    fun open(host: String, port: Int): Boolean
    fun send(data: ByteArray, length: Int): Boolean
    fun close()
    fun targetDescription(): String
}
