package com.miclink.open

import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothServerSocket
import android.bluetooth.BluetoothSocket
import android.content.Context
import java.net.Socket
import java.util.UUID

/** Secure paired RFCOMM (Android BluetoothChat / Windows Winsock pattern).
 * Local relay reuses the existing session lifetime and streaming negotiation. */
class BluetoothTransport(private val context: Context, private val port: Int,
                         private val report: (String) -> Unit) : AutoCloseable {
    companion object { val SERVICE_UUID: UUID = UUID.fromString("ad5c9d21-8a64-487b-9c74-6f3aa069472e") }
    @Volatile private var server: BluetoothServerSocket? = null
    @Volatile private var remote: BluetoothSocket? = null
    @Volatile private var local: LoopbackRelay? = null
    @Volatile private var stopped = false
    fun owns(client: Socket): Boolean = local?.owns(client) == true
    fun start() {
        Thread({
            try {
                val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter
                if (adapter == null || !adapter.isEnabled) { report("蓝牙未开启或不可用"); return@Thread }
                val listener = adapter.listenUsingRfcommWithServiceRecord("MicLink", SERVICE_UUID)
                synchronized(this) { if (stopped) { listener.close(); return@Thread }; server = listener }
                while (!stopped) {
                    val bt = listener.accept()
                    synchronized(this) {
                        if (stopped) { bt.close(); return@Thread }
                        remote = bt
                    }
                    try {
                        val tcp = LoopbackRelay()
                        synchronized(this) {
                            if (stopped) { tcp.close(); bt.close(); return@Thread }
                            local = tcp
                        }
                        AppLog.i("BluetoothTransport", "RFCOMM已接受，内部来源端口=${tcp.sourcePort}，目标端口=$port")
                        tcp.connect(port)
                        val sent = java.util.concurrent.atomic.AtomicLong()
                        val received = java.util.concurrent.atomic.AtomicLong()
                        val copy = Thread({
                            try {
                                relayBytes(tcp.input, bt.outputStream) { received.addAndGet(it.toLong()) }
                            } catch (e: Exception) {
                                AppLog.w("BluetoothTransport", "内部会话→蓝牙失败", e)
                            } finally { runCatching { bt.close() }; runCatching { tcp.close() } }
                        }, "miclink-bt-return").also { it.start() }
                        try {
                            relayBytes(bt.inputStream, tcp.output) { sent.addAndGet(it.toLong()) }
                        } finally {
                            runCatching { bt.close() }; runCatching { tcp.close() }; copy.join(1000)
                            val detail = "蓝牙会话结束：电脑→手机 ${sent.get()} B，手机→电脑 ${received.get()} B"
                            AppLog.i("BluetoothTransport", detail)
                            if (!stopped && received.get() == 0L) report("$detail；内部会话未返回握手应答，请同时查看电脑握手错误")
                        }
                    } catch (e: Exception) {
                        if (!stopped) report("蓝牙会话断开：${e.message}")
                    } finally { runCatching { local?.close() }; remote = null; local = null; runCatching { bt.close() } }
                }
            } catch (e: Exception) { if (!stopped) report("蓝牙不可用：${e.message}") }
        }, "miclink-rfcomm").start()
    }
    override fun close() {
        synchronized(this) { stopped = true; runCatching { server?.close() }; server = null }
        runCatching { remote?.close() }; runCatching { local?.close() }
    }
}
