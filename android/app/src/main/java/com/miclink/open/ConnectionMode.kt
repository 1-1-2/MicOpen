package com.miclink.open

import android.content.Context

/** Persisted choice, shared by UI, permission planning and service startup. */
enum class ConnectionMode(val title: String, val preparation: String) {
    WIFI("Wi-Fi", "手机与电脑连接同一路由器，或让电脑连接手机热点。启动后，在 Windows 选择 Wi-Fi 并填写手机 IP。"),
    DIRECT("无线直连", "电脑先选择无线直连并点击“等待连接”。手机保持 Wi-Fi 开启，点击启动，在下方搜索列表点选电脑，再在电脑允许本手机连接。无需路由器、热点或填写 IP。旧版 Android 可能需要定位开关。"),
    USB("USB", "先用数据线连接电脑，开启 USB 调试并允许这台电脑调试。启动后，在 Windows 选择 USB；无需填写手机 IP。"),
    BLUETOOTH("蓝牙", "先开启蓝牙，在系统设置中与电脑配对。启动后，在 Windows 选择蓝牙和已配对的手机；无需填写 IP。");

    val localOnly get() = this == USB || this == BLUETOOTH
    val needsAddress get() = this == WIFI
    companion object {
        fun read(context: Context): ConnectionMode {
            val prefs = context.getSharedPreferences("transport", Context.MODE_PRIVATE)
            val saved = prefs.getString("mode", null)
            return fromSaved(saved, prefs.getBoolean("bluetooth", false))
        }
        /** Old HOTSPOT preferences become ordinary Wi-Fi, never silently Direct. */
        fun fromSaved(saved: String?, legacyBluetooth: Boolean = false): ConnectionMode {
            return entries.firstOrNull { it.name == saved }
                ?: if (saved == null && legacyBluetooth) BLUETOOTH else WIFI
        }
        fun save(context: Context, mode: ConnectionMode) {
            context.getSharedPreferences("transport", Context.MODE_PRIVATE).edit()
                .putString("mode", mode.name).remove("bluetooth").apply()
        }
    }
}
