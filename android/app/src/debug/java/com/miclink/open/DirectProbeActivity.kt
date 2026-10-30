package com.miclink.open

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.net.wifi.p2p.WifiP2pManager
import android.widget.TextView
import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.net.DatagramSocket
import java.net.DatagramPacket
import java.util.concurrent.CopyOnWriteArrayList
import java.io.Closeable
import org.json.JSONObject

/** Debug-only peer/harness. No microphone, ADB data forwarding or audio engine. */
class DirectProbeActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private val sockets = CopyOnWriteArrayList<Closeable>()
    @Volatile private var closed = false
    private var owned = false
    private var serverStarted = false
    private var transport: WifiDirectTransport? = null
    private lateinit var manager: WifiP2pManager
    private lateinit var channel: WifiP2pManager.Channel
    private val port = 48215
    private fun emit(event: String, vararg fields: Pair<String, Any?>) {
        val out = JSONObject().put("event", event)
        for ((key, value) in fields) out.put(key, value)
        android.util.Log.i("DirectProbe", out.toString())
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(TextView(this).apply { text = "MicLink · 自动 P2P 测试\n仅用于模拟器验证" })
        ConnectionMode.save(this, ConnectionMode.DIRECT)
        manager = getSystemService(WifiP2pManager::class.java)
        channel = manager.initialize(this, mainLooper) { emit("error", "message" to "channel lost") }
        emit("started", "role" to intent.getStringExtra("role"), "sdk" to android.os.Build.VERSION.SDK_INT)
        if (intent.getStringExtra("role") == "owner") owner() else client()
    }
    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        if (intent.getStringExtra("role") == "stop") finish()
    }
    private fun owner() {
        manager.requestGroupInfo(channel) { previous ->
            if (closed) return@requestGroupInfo
            if (previous != null) { emit("error", "message" to "pre-existing group; not deleting"); return@requestGroupInfo }
            manager.createGroup(channel, object : WifiP2pManager.ActionListener {
                override fun onSuccess() { owned = true; pollOwner() }
                override fun onFailure(reason: Int) { emit("error", "message" to "createGroup failed", "reason" to reason) }
            })
        }
    }
    private fun pollOwner() {
        if (closed) return
        manager.requestGroupInfo(channel) { group ->
            if (closed) return@requestGroupInfo
            if (group != null) {
                val local = NetworkInterface.getByName(group.`interface`)?.inetAddresses?.toList()
                    ?.filterIsInstance<Inet4Address>()?.firstOrNull { !it.isLoopbackAddress && !it.isLinkLocalAddress }
                if (local != null && !serverStarted) {
                    serverStarted = true
                    emit("owner_ready", "interface" to group.`interface`, "ip" to local.hostAddress)
                    serve(local)
                    manager.discoverPeers(channel, null)
                }
                if (group.clientList.isNotEmpty()) emit("owner_has_client", "clients" to group.clientList.size, "interface" to group.`interface`)
            }
            handler.postDelayed({ pollOwner() }, 1_000)
        }
    }
    private fun serve(local: InetAddress) {
        Thread {
            try {
                val server = ServerSocket(port, 1, local).also { sockets.add(it) }
                server.soTimeout = 90_000
                val socket = server.accept().also { sockets.add(it) }
                socket.soTimeout = 10_000
                val bytes = ByteArray("MicLink-P2P-echo".toByteArray().size)
                java.io.DataInputStream(socket.getInputStream()).readFully(bytes)
                socket.getOutputStream().write(bytes)
                emit("tcp_echo_served", "bytes" to bytes.size)
            } catch (e: Exception) { if (!closed) emit("error", "message" to "TCP ${e.javaClass.simpleName}") }
        }.start()
        Thread {
            try {
                val socket = DatagramSocket(port, local).also { sockets.add(it) }
                socket.soTimeout = 90_000
                val packet = DatagramPacket(ByteArray(128), 128)
                repeat(10) { socket.receive(packet); socket.send(packet); packet.length = 128 }
                emit("udp_echo_served", "packets" to 10)
            } catch (e: Exception) { if (!closed) emit("error", "message" to "UDP ${e.javaClass.simpleName}") }
        }.start()
    }
    private fun client() {
        transport = WifiDirectTransport(this,
            ready = { local ->
                manager.requestConnectionInfo(channel) { info ->
                    if (closed) return@requestConnectionInfo
                    manager.requestGroupInfo(channel) { group ->
                        if (closed) return@requestGroupInfo
                        emit("client_ready", "formed" to info.groupFormed, "isGO" to info.isGroupOwner,
                            "interface" to group?.`interface`, "ip" to local.hostAddress, "remote" to info.groupOwnerAddress?.hostAddress)
                        echo(local, info.groupOwnerAddress)
                    }
                }
            }, message = { emit("message", "text" to it) }, failed = { emit("error", "message" to it) })
        transport!!.start()
        select()
    }
    private fun select() {
        if (closed) return
        val adapter = transport ?: return
        // Only two disposable AVDs exist in this test; final endpoint checks
        // still require the selected owner to match the other AVD's group IP.
        val peer = adapter.peers.firstOrNull { it.groupOwner }
        if (peer != null && !adapter.isConnecting) adapter.connect(peer.id)
        else if (!adapter.isConnecting) handler.postDelayed({ select() }, 300)
    }
    private fun echo(local: InetAddress, remote: InetAddress?) {
        Thread {
            try {
                require(remote != null)
                val payload = "MicLink-P2P-echo".toByteArray()
                Socket().use { socket ->
                    sockets.add(socket); socket.bind(java.net.InetSocketAddress(local, 0))
                    socket.connect(java.net.InetSocketAddress(remote, port), 10_000); socket.soTimeout = 10_000
                    socket.getOutputStream().write(payload)
                    val reply = ByteArray(payload.size); java.io.DataInputStream(socket.getInputStream()).readFully(reply)
                    require(reply.contentEquals(payload))
                }
                DatagramSocket(0, local).use { socket ->
                    sockets.add(socket); socket.soTimeout = 3_000
                    for (sequence in 0 until 10) {
                        val data = "MicLink-$sequence".toByteArray()
                        socket.send(DatagramPacket(data, data.size, remote, port))
                        val reply = DatagramPacket(ByteArray(128), 128); socket.receive(reply)
                        require(reply.address == remote && reply.port == port && reply.data.copyOf(reply.length).contentEquals(data))
                    }
                }
                emit("passed", "tcp" to true, "udpPackets" to 10, "scope" to "Android emulator P2P; not Windows WFD")
            } catch (e: Exception) { if (!closed) emit("error", "message" to "echo ${e.javaClass.simpleName}") }
        }.start()
    }
    override fun onDestroy() {
        closed = true; handler.removeCallbacksAndMessages(null)
        sockets.forEach { try { it.close() } catch (_: Exception) {} }
        transport?.close()
        if (::channel.isInitialized) {
            if (owned) manager.removeGroup(channel, object : WifiP2pManager.ActionListener {
                override fun onSuccess() { channel.close() }
                override fun onFailure(reason: Int) { channel.close() }
            }) else channel.close()
        }
        emit("stopped")
        super.onDestroy()
    }
}
