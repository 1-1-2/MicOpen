package com.miclink.open

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.provider.Settings
import android.widget.Toast

/** Settings navigation only; never changes or stops an active transport. */
object ConnectionSettings {
    fun open(context: Context, mode: ConnectionMode) {
        // Public Android SDK has no portable Wi-Fi Direct settings/picker
        // intent. Keep this entry honest and use the public WLAN settings.
        // Discovery/listen is owned by our transport, not by this settings UI.
        val actions = when (mode) {
            ConnectionMode.DIRECT -> listOf(Settings.ACTION_WIFI_SETTINGS)
            ConnectionMode.WIFI -> listOf(Settings.ACTION_WIFI_SETTINGS)
            ConnectionMode.BLUETOOTH -> listOf(Settings.ACTION_BLUETOOTH_SETTINGS)
            ConnectionMode.USB -> listOf(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS)
        }
        for (action in actions) {
            try {
                context.startActivity(Intent(action))
                if (mode == ConnectionMode.DIRECT)
                    Toast.makeText(context, "如需系统直连面板，请在 WLAN 设置中查找 WLAN 直连", Toast.LENGTH_LONG).show()
                return
            } catch (_: ActivityNotFoundException) {
                // Try the next portable entry point.
            } catch (_: SecurityException) {
                // Some OEMs expose the action only to system apps.
            }
        }
        try {
            context.startActivity(Intent(Settings.ACTION_SETTINGS))
            Toast.makeText(context, if (mode == ConnectionMode.USB)
                "请在系统设置中启用开发者选项和 USB 调试" else "请在系统设置中查找对应连接选项", Toast.LENGTH_LONG).show()
        } catch (_: ActivityNotFoundException) {
            Toast.makeText(context, "此系统无法打开设置，请手动进入对应设置", Toast.LENGTH_LONG).show()
        } catch (_: SecurityException) {
            Toast.makeText(context, "系统限制了设置入口，请手动进入对应设置", Toast.LENGTH_LONG).show()
        }
    }
}
