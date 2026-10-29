package com.miclink.open

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.wifi.WifiManager
import android.net.wifi.p2p.WifiP2pManager
import android.net.wifi.p2p.WifiP2pDevice
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import java.io.Closeable
import java.net.InetAddress
import java.net.Inet4Address
import java.net.NetworkInterface

/** Owns only a newly created P2P group. Audio and protocol stay in the service.
 * Uses public Android Wi-Fi Direct APIs; never enables SoftAP/tethering or uses
 * hidden APIs to rename devices. Windows selects the phone's system P2P name.
 */
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
    private var creating = false
    private var owned = false
    private var delivered = false
    var isDiscoverable = false
        private set
    private var listeningActive = false
    private var deviceName = "本手机"
    private var visibleRequested = false
    private var discoveryOwned = false
    private var connectedPeer = false
    private var invitedPeer = false
    private val rediscover = Runnable { if (!closed && !connectedPeer && !invitedPeer) makeVisible() }
    private fun log(text: String) = android.util.Log.i("WifiDirectTransport", text)
    private val deadline = Runnable { error("无线直连组网超时，请检查 Wi-Fi、权限及定位开关后重试") }
    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, intent: Intent) {
            if (closed) return
            when (intent.action) {
                WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION -> {
                    if (intent.getIntExtra(WifiP2pManager.EXTRA_WIFI_STATE, -1) == WifiP2pManager.WIFI_P2P_STATE_DISABLED)
                        error("无线直连已关闭，请开启 Wi-Fi 后重新启动")
                }
                WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> if (owned) inspect()
                WifiP2pManager.WIFI_P2P_DISCOVERY_CHANGED_ACTION -> {
                    val active = intent.getIntExtra(WifiP2pManager.EXTRA_DISCOVERY_STATE, -1) == WifiP2pManager.WIFI_P2P_DISCOVERY_STARTED
                    log("discovery active=$active")
                    if (!active && !listeningActive) {
                        isDiscoverable = false
                        if (delivered && !connectedPeer && !invitedPeer)
                            message("设备发现已暂停，正在等待重新搜索；也可打开对应设置检查 WLAN 直连。")
                    }
                    if (!active && owned && delivered && !connectedPeer && !invitedPeer) {
                        handler.removeCallbacks(rediscover)
                        handler.postDelayed(rediscover, 15_000)
                    }
                }
                WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION -> updatePeers()
            }
        }
    }

    fun start() {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (!context.packageManager.hasSystemFeature(PackageManager.FEATURE_WIFI_DIRECT)) {
            error("本手机不支持 Wi-Fi Direct，请选择 Wi-Fi、蓝牙或 USB"); return
        }
        if (context.applicationContext.getSystemService(WifiManager::class.java)?.isWifiEnabled != true) {
            error("请先开启 Wi-Fi；无线直连不需要连接路由器或开启热点"); return
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
                addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_DISCOVERY_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION)
            }, ContextCompat.RECEIVER_EXPORTED) // Protected system Wi-Fi broadcasts.
            registered = true
            handler.postDelayed(deadline, 20_000)
            m.requestGroupInfo(ch) { existing ->
                if (!closed) {
                    if (existing != null) error("已有其他无线直连会话，请先在系统中断开，再启动 MicLink")
                    else {
                        creating = true
                        try { m.createGroup(ch, object : WifiP2pManager.ActionListener {
                            override fun onSuccess() {
                                creating = false
                                owned = true
                                if (closed) removeOwnedGroup() else inspect()
                            }
                            override fun onFailure(reason: Int) {
                                creating = false
                                if (closed) finishChannel()
                                else error("无法创建无线直连：${reasonText(reason)}")
                            }
                        }) } catch (_: SecurityException) {
                            creating = false
                            error("无线直连权限已被撤销，请重新授权")
                        } catch (e: RuntimeException) {
                            creating = false
                            error("无线直连启动失败：${e.message}")
                        }
                    }
                }
            }
        } catch (_: SecurityException) {
            error("无线直连权限不足，请授予附近 Wi-Fi 设备权限（旧版 Android 需定位权限）")
        } catch (e: RuntimeException) {
            error("无线直连启动失败：${e.message}")
        }
    }

    private fun inspect() {
        val m = manager ?: return
        val ch = channel ?: return
        try {
            m.requestConnectionInfo(ch) { info ->
                if (closed) return@requestConnectionInfo
                if (info.groupFormed && info.isGroupOwner) {
                    m.requestGroupInfo(ch) { group ->
                        if (closed || group == null) return@requestGroupInfo
                        // Recent Android versions may anonymize the GO address.
                        // Bind to an actual IPv4 on this group's interface only.
                        val local = try {
                            NetworkInterface.getByName(group.`interface`)?.inetAddresses?.toList()
                                ?.filterIsInstance<Inet4Address>()
                                ?.firstOrNull { !it.isLoopbackAddress && !it.isLinkLocalAddress && !it.isAnyLocalAddress }
                        } catch (_: Exception) { null }
                        val address = local ?: info.groupOwnerAddress?.takeIf { candidate ->
                            candidate is Inet4Address && try {
                                NetworkInterface.getByInetAddress(candidate) != null
                            } catch (_: Exception) { false }
                        }
                        if (address == null) {
                            if (!delivered) handler.postDelayed({ if (!closed) inspect() }, 300)
                            return@requestGroupInfo
                        }
                        deviceName = group.owner?.deviceName ?: "本手机"
                        val wasConnected = connectedPeer
                        connectedPeer = group.clientList.isNotEmpty()
                        if (!delivered) {
                            delivered = true
                            handler.removeCallbacks(deadline)
                            ready(address)
                            if (closed) return@requestGroupInfo
                            log("group ready interface=${group.`interface`} clients=${group.clientList.size}")
                            message("无线直连已就绪：在电脑搜索并选择 ${group.owner?.deviceName ?: "本手机"}，确认配对后连接")
                            makeVisible()
                        } else if (wasConnected && !connectedPeer) {
                            visibleRequested = false
                            handler.postDelayed(rediscover, 1500)
                        }
                        if (connectedPeer) {
                            if (isDiscoverable) message("无线直连已建立，等待电脑开始音频会话")
                            isDiscoverable = false
                            handler.removeCallbacks(rediscover)
                        }
                    }
                } else if (delivered) error("无线直连网络已断开，请重新启动")
                else handler.postDelayed({ if (!closed) inspect() }, 300)
            }
        } catch (_: SecurityException) { error("无线直连权限已被撤销，请重新授权") }
    }

    /** Visibility is separate from group ownership. Entering Settings used to
     * supply this missing listen/discovery operation on some OEM phones. */
    private fun makeVisible() {
        val m = manager ?: return
        val ch = channel ?: return
        if (closed || !owned || connectedPeer || invitedPeer) return
        handler.removeCallbacks(rediscover)
        if (Build.VERSION.SDK_INT >= 33 && !visibleRequested) {
            visibleRequested = true
            try {
                m.startListening(ch, object : WifiP2pManager.ActionListener {
                    override fun onSuccess() {
                        log("listen started")
                        if (closed) return
                        discoveryOwned = true
                        listeningActive = true
                        isDiscoverable = !connectedPeer && !invitedPeer
                        message("可被发现：$deviceName · 等待电脑连接")
                    }
                    override fun onFailure(reason: Int) {
                        if (!closed) {
                            log("listen failed reason=$reason; fallback to discovery")
                            discover()
                        }
                    }
                })
            } catch (_: SecurityException) { error("无线直连发现权限不足，请检查附近 Wi-Fi 设备权限") }
            catch (_: RuntimeException) { discover() }
        } else discover()
    }

    private fun discover() {
        val m = manager ?: return
        val ch = channel ?: return
        if (closed || connectedPeer || invitedPeer) return
        try {
            m.discoverPeers(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    if (closed) return
                    discoveryOwned = true
                    log("peer discovery started")
                    isDiscoverable = !connectedPeer && !invitedPeer
                    message("可被发现：$deviceName · 等待电脑连接")
                }
                override fun onFailure(reason: Int) {
                    if (closed) return
                    isDiscoverable = false
                    log("peer discovery failed reason=$reason")
                    message("直连组已就绪，但设备发现未启动：${reasonText(reason)}。可打开对应设置检查 WLAN 直连。")
                    // BUSY can mean pairing is in progress. Avoid a rapid loop
                    // that would interrupt the incoming pairing exchange.
                    handler.removeCallbacks(rediscover)
                    handler.postDelayed(rediscover, 15_000)
                }
            })
        } catch (_: SecurityException) { error("无线直连发现权限不足，请检查附近 Wi-Fi 设备权限及定位开关") }
        catch (e: RuntimeException) { log("peer discovery unavailable: ${e.javaClass.simpleName}") }
    }

    private fun updatePeers() {
        val m = manager ?: return
        val ch = channel ?: return
        if (closed || !owned) return
        try {
            m.requestPeers(ch) { peers ->
                if (closed) return@requestPeers
                val wasInvited = invitedPeer
                invitedPeer = peers.deviceList.any { it.status == WifiP2pDevice.INVITED }
                log("peers=${peers.deviceList.size} invited=$invitedPeer")
                if (invitedPeer) {
                    if (isDiscoverable) message("正在配对，请查看系统确认提示")
                    isDiscoverable = false
                    handler.removeCallbacks(rediscover)
                }
                else if (wasInvited && !connectedPeer) handler.postDelayed(rediscover, 15_000)
            }
        } catch (_: SecurityException) { error("无线直连发现权限已被撤销") }
    }

    private fun error(text: String) { if (!closed) { close(); failed(text) } }

    override fun close() {
        if (closed) return
        closed = true
        isDiscoverable = false
        handler.removeCallbacksAndMessages(null)
        if (discoveryOwned || visibleRequested) {
            try { manager?.stopPeerDiscovery(channel, null) } catch (_: RuntimeException) {}
            discoveryOwned = false
        }
        if (registered) { context.unregisterReceiver(receiver); registered = false }
        if (owned) removeOwnedGroup()
        else if (!creating) finishChannel()
        // A createGroup completion after stop still removes only the group we created.
    }

    private fun removeOwnedGroup() {
        val ch = channel ?: return
        try {
            manager?.removeGroup(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() { owned = false; finishChannel() }
                override fun onFailure(reason: Int) { finishChannel() }
            })
        } catch (_: RuntimeException) { finishChannel() }
    }
    private fun finishChannel() { channel?.close(); channel = null; manager = null }

    private fun reasonText(reason: Int) = when (reason) {
        WifiP2pManager.P2P_UNSUPPORTED -> "设备不支持 Wi-Fi Direct"
        WifiP2pManager.BUSY -> "系统繁忙，请关闭热点或其他无线直连后重试"
        else -> "系统错误 $reason，请检查 Wi-Fi、权限及定位开关"
    }
}
