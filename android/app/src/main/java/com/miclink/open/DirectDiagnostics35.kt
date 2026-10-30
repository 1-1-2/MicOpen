package com.miclink.open

import android.net.wifi.p2p.WifiP2pGroup
import android.net.wifi.p2p.WifiP2pInfo
import android.net.wifi.p2p.WifiP2pManager
import androidx.annotation.RequiresApi
import java.io.Closeable
import java.util.concurrent.Executor

/** Public API 35 callbacks, isolated so older Android never loads this type. */
@RequiresApi(35)
internal class DirectDiagnostics35(
    private val manager: WifiP2pManager,
    executor: Executor,
    private val event: (String) -> Unit,
    private val changed: () -> Unit
) : Closeable {
    private var active = true
    private val listener = object : WifiP2pManager.WifiP2pListener {
        override fun onGroupCreating() { if (active) event("group creating") }
        override fun onGroupCreationFailed(reason: Int) {
            if (!active) return
            val name = when (reason) {
                0 -> "CONNECTION_CANCELLED"
                1 -> "TIMED_OUT"
                2 -> "USER_REJECTED"
                3 -> "PROVISION_DISCOVERY_FAILED"
                4 -> "GROUP_REMOVED"
                5 -> "INVITATION_FAILED"
                else -> "UNKNOWN"
            }
            event("group creation failed source=WifiP2pListener reason=$reason($name)")
            changed()
        }
        override fun onGroupCreated(info: WifiP2pInfo, group: WifiP2pGroup) {
            if (!active) return
            event("group created role=${if (info.isGroupOwner) "GO" else "GC"} interface=${group.`interface`} clients=${group.clientList.size}")
            changed()
        }
        override fun onGroupRemoved() { if (active) { event("group removed"); changed() } }
    }
    init { manager.registerWifiP2pListener(executor, listener) }
    override fun close() { active = false; manager.unregisterWifiP2pListener(listener) }
}
