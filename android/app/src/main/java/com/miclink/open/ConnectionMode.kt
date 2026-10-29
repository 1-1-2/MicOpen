package com.miclink.open

import android.content.Context

/** Persisted choice, shared by UI, permission planning and service startup. */
enum class ConnectionMode(val title: String, val preparation: String) {
    WIFI("Wi-Fi", "手机与电脑连接同一路由器，或让电脑连接手机热点。启动后，在 Windows 选择 Wi-Fi 并填写手机 IP。"),
    DIRECT("无线直连", "无需路由器或开启手机热点。保持 Wi-Fi 开启，点击启动；在 Windows 选择无线直连并点击连接，搜索并选择本手机，按系统提示确认配对。首次连接可能需要定位开关。"),
    USB("USB", "先用数据线连接电脑，开启 USB 调试并允许这台电脑调试。启动后，在 Windows 选择 USB；无需填写手机 IP。"),
    BLUETOOTH("蓝牙", "先开启蓝牙，在系统设置中与电脑配对。启动后，在 Windows 选择蓝牙和已配对的手机；无需填写 IP。");

    val localOnly get() = this == USB || this == BLUETOOTH
    val needsAddress get() = this == WIFI
    companion object {
        fun read(context: Context): ConnectionMode {
            val prefs = context.getSharedPreferences("transport", Context.MODE_PRIVATE)
            val saved = prefs.getString("mode", null)
            return entries.firstOrNull { it.name == saved }
                ?: if (saved == null && prefs.getBoolean("bluetooth", false)) BLUETOOTH else WIFI
        }
        fun save(context: Context, mode: ConnectionMode) {
            context.getSharedPreferences("transport", Context.MODE_PRIVATE).edit()
                .putString("mode", mode.name).remove("bluetooth").apply()
        }
    }
}
