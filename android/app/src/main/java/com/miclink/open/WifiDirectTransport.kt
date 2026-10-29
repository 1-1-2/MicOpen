package com.miclink.open

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.wifi.WifiManager
import android.net.wifi.WpsInfo
import android.net.wifi.p2p.WifiP2pConfig
import android.net.wifi.p2p.WifiP2pDevice
import android.net.wifi.p2p.WifiP2pManager
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import java.io.Closeable
import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface

data class DirectPeer(val id: String, val name: String, val groupOwner: Boolean)

/** Phone initiates P2P; Windows advertises an autonomous group. No audio API. */
class WifiDirectTransport(
    private val context: Context,
    private val ready: (InetAddress) -> Unit,
    private val message: (String) -> Unit,
    private val failed: (String) -> Unit
) : Closeable {
    private val handler = Handler(Looper.getMainLooper())
    private var manager: WifiP2pManager? = null
    private var channel: WifiP2pManager.Channel? = null
    private var registered = false
    private var closed = false
    private var prepared = false
    private var discoveryRequested = false
    private var selected: String? = null
    private var joinedInterface: String? = null
    private var delivered = false
    var peers: List<DirectPeer> = emptyList()
        private set
    var isDiscovering = false
        private set
    var isConnecting = false
        private set
    private val retryDiscovery = Runnable { if (!closed && selected == null) discover() }
    private val deadline = Runnable { error("连接电脑超时，请检查电脑是否处于等待状态及两端系统配对提示") }
    private fun log(text: String) = AppLog.i("WifiDirectTransport", text)
    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, intent: Intent) {
            if (closed) return
            when (intent.action) {
                WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION -> {
                    if (intent.getIntExtra(WifiP2pManager.EXTRA_WIFI_STATE, -1) == WifiP2pManager.WIFI_P2P_STATE_DISABLED)
                        error("无线直连已关闭，请开启 Wi-Fi 后重试")
                }
                WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION -> refreshPeers()
                WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> if (selected != null) inspect()
                WifiP2pManager.WIFI_P2P_DISCOVERY_CHANGED_ACTION -> {
                    isDiscovering = intent.getIntExtra(WifiP2pManager.EXTRA_DISCOVERY_STATE, -1) == WifiP2pManager.WIFI_P2P_DISCOVERY_STARTED
                    log("discovery active=$isDiscovering")
                    if (!isDiscovering && selected == null && prepared) {
                        handler.removeCallbacks(retryDiscovery)
                        handler.postDelayed(retryDiscovery, 15_000)
                    }
                }
            }
        }
    }
    fun start() {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (!context.packageManager.hasSystemFeature(PackageManager.FEATURE_WIFI_DIRECT)) {
            error("本手机不支持 Wi-Fi Direct"); return
        }
        if (context.getSystemService(WifiManager::class.java)?.isWifiEnabled != true) {
            error("请先开启 Wi-Fi，无需开启热点或连接路由器"); return
        }
        val m = context.getSystemService(WifiP2pManager::class.java)
            ?: run { error("无法访问无线直连服务"); return }
        manager = m
        val ch = m.initialize(context, Looper.getMainLooper()) {
            if (!closed) error("无线直连服务已断开，请重新启动")
        }
        channel = ch
        try {
            ContextCompat.registerReceiver(context, receiver, IntentFilter().apply {
                addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_DISCOVERY_CHANGED_ACTION)
            }, ContextCompat.RECEIVER_EXPORTED)
            registered = true
            m.requestGroupInfo(ch) { existing ->
                if (!closed) {
                    if (existing != null) error("已有其他直连会话，请先在系统中断开后启动")
                    else { prepared = true; discover() }
                }
            }
        } catch (_: SecurityException) { error("请授予附近 Wi-Fi 设备权限；旧版 Android 还需定位权限及定位开关") }
        catch (e: RuntimeException) { error("无线直连启动失败：${e.message}") }
    }
    fun discover() {
        val m = manager ?: return
        val ch = channel ?: return
        if (closed || !prepared || selected != null) return
        if (isDiscovering) { refreshPeers(); return }
        handler.removeCallbacks(retryDiscovery)
        discoveryRequested = true
        try {
            m.discoverPeers(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    if (closed) return
                    isDiscovering = true
                    message("正在搜索电脑，请先在 Windows 点击“等待连接”，然后在下面选择电脑")
                    refreshPeers()
                }
                override fun onFailure(reason: Int) {
                    if (closed) return
                    isDiscovering = false
                    message("搜索未启动：${reasonText(reason)}；可点击重新搜索")
                }
            })
        } catch (_: SecurityException) { error("搜索权限不足，请检查附近 Wi-Fi 设备权限、定位权限及定位开关") }
        catch (e: RuntimeException) { error("搜索失败：${e.message}") }
    }
    private fun refreshPeers() {
        val m = manager ?: return
        val ch = channel ?: return
        try {
            m.requestPeers(ch) { found ->
                if (closed) return@requestPeers
                peers = found.deviceList.filter { it.status != WifiP2pDevice.UNAVAILABLE && it.status != WifiP2pDevice.FAILED }
                    .take(64).map { DirectPeer(it.deviceAddress, it.deviceName?.takeIf(String::isNotBlank) ?: "未命名设备", it.isGroupOwner) }
                    .sortedWith(compareByDescending<DirectPeer> { it.groupOwner }.thenBy { it.name })
                log("peers=${peers.size} groupOwners=${peers.count { it.groupOwner }}")
            }
        } catch (_: SecurityException) { error("获取附近设备的权限已被撤销") }
    }
    fun connect(id: String) {
        val m = manager ?: return
        val ch = channel ?: return
        if (closed || !prepared || selected != null || peers.none { it.id == id }) return
        selected = id
        isConnecting = true
        isDiscovering = false
        handler.removeCallbacks(retryDiscovery)
        handler.postDelayed(deadline, 90_000)
        message("正在连接所选电脑，请在电脑确认本手机的请求")
        // Mark before the async request; Stop cancels even before its ACK arrives.
        val config = WifiP2pConfig().apply {
            deviceAddress = id
            groupOwnerIntent = 0
            wps.setup = WpsInfo.PBC
        }
        try {
            m.stopPeerDiscovery(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() { beginConnect(m, ch, config) }
                override fun onFailure(reason: Int) { beginConnect(m, ch, config) }
            })
        } catch (_: SecurityException) { error("连接权限已被撤销") }
        catch (e: RuntimeException) { error("连接失败：${e.message}") }
    }
    private fun beginConnect(m: WifiP2pManager, ch: WifiP2pManager.Channel, config: WifiP2pConfig) {
        if (closed) return
        try {
            m.connect(ch, config, object : WifiP2pManager.ActionListener {
                override fun onSuccess() { log("connect request accepted by system; waiting for group"); if (!closed) inspect() }
                override fun onFailure(reason: Int) { if (!closed) error("无法连接电脑：${reasonText(reason)}") }
            })
        } catch (_: SecurityException) { error("连接权限已被撤销") }
        catch (e: RuntimeException) { error("连接失败：${e.message}") }
    }
    private fun inspect() {
        val m = manager ?: return
        val ch = channel ?: return
        if (closed || selected == null) return
        try {
            m.requestConnectionInfo(ch) { info ->
                if (closed) return@requestConnectionInfo
                if (!info.groupFormed) {
                    if (delivered) error("电脑直连网络已断开，请停止后重新选择电脑")
                    return@requestConnectionInfo
                }
                m.requestGroupInfo(ch) { group ->
                    if (closed || group == null) return@requestGroupInfo
                    if (DirectGroupOwnership.foreign(selected, group.owner?.deviceAddress)) {
                        error("当前直连组不属于所选电脑，请先检查系统中的其他直连会话")
                        return@requestGroupInfo
                    }
                    joinedInterface = group.`interface`
                    if (info.isGroupOwner) {
                        error("系统本次把手机分配为组主，与此实验方向不一致；请停止后检查这对设备的旧直连配对记录")
                        return@requestGroupInfo
                    }
                    // The GO address belongs to Windows. Bind only our own
                    // group interface's IPv4, after DHCP has assigned it.
                    val local = try {
                        NetworkInterface.getByName(group.`interface`)?.inetAddresses?.toList()
                            ?.filterIsInstance<Inet4Address>()?.firstOrNull {
                                !it.isLoopbackAddress && !it.isLinkLocalAddress && !it.isAnyLocalAddress
                            }
                    } catch (_: Exception) { null }
                    if (local == null) {
                        handler.postDelayed({ inspect() }, 300)
                    } else if (!delivered) {
                        delivered = true
                        isConnecting = false
                        handler.removeCallbacks(deadline)
                        log("group joined role=client interface=${group.`interface`}; local IPv4 ready")
                        ready(local)
                        if (!closed) message("已加入电脑直连组，等待音频会话")
                    }
                }
            }
        } catch (_: SecurityException) { error("无线直连权限已被撤销") }
    }
    private fun error(text: String) { if (!closed) { close(); failed(text) } }
    override fun close() {
        if (closed) return
        closed = true
        peers = emptyList(); isDiscovering = false; isConnecting = false
        handler.removeCallbacksAndMessages(null)
        if (registered) { context.unregisterReceiver(receiver); registered = false }
        val m = manager ?: return
        val ch = channel ?: return
        handler.postDelayed({ finishChannel() }, 5_000)
        try { if (discoveryRequested) m.stopPeerDiscovery(ch, null) } catch (_: RuntimeException) {}
        if (selected != null && !delivered) {
            try {
                m.cancelConnect(ch, object : WifiP2pManager.ActionListener {
                    override fun onSuccess() { removeJoinedGroup(m, ch) }
                    override fun onFailure(reason: Int) { removeJoinedGroup(m, ch) }
                })
            } catch (_: RuntimeException) { removeJoinedGroup(m, ch) }
        } else removeJoinedGroup(m, ch)
    }
    private fun removeJoinedGroup(m: WifiP2pManager, ch: WifiP2pManager.Channel) {
        if (channel !== ch) return
        try {
            m.requestGroupInfo(ch) { group ->
                if (channel !== ch) return@requestGroupInfo
                // Never remove a pre-existing group or an unrelated app's group.
                if (DirectGroupOwnership.mayLeave(selected, joinedInterface, group?.`interface`, group?.owner?.deviceAddress)) {
                    m.removeGroup(ch, object : WifiP2pManager.ActionListener {
                        override fun onSuccess() { finishChannel() }
                        override fun onFailure(reason: Int) { log("leave group failed reason=$reason"); finishChannel() }
                    })
                } else finishChannel()
            }
        } catch (_: RuntimeException) { finishChannel() }
    }
    private fun finishChannel() {
        handler.removeCallbacksAndMessages(null)
        channel?.close(); channel = null; manager = null
    }
    private fun reasonText(reason: Int) = when (reason) {
        WifiP2pManager.P2P_UNSUPPORTED -> "设备不支持 Wi-Fi Direct"
        WifiP2pManager.BUSY -> "系统繁忙，请检查其他直连或热点会话"
        else -> "系统错误 $reason，请检查 Wi-Fi、权限及定位开关"
    }
}
