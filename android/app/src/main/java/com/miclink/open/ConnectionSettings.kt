package com.miclink.open

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.provider.Settings
import android.widget.Toast

/** Settings navigation only; never changes or stops an active transport. */
object ConnectionSettings {
    fun open(context: Context, mode: ConnectionMode) {
        // AOSP defines this action, but does not expose it as a public SDK
        // constant. OEMs may omit it. Resolve by launching, then fall back to
        // public Wi-Fi settings; never hardcode a vendor component/fragment.
        val actions = when (mode) {
            ConnectionMode.DIRECT -> listOf("android.settings.WIFI_P2P_SETTINGS", Settings.ACTION_WIFI_SETTINGS)
            ConnectionMode.WIFI -> listOf(Settings.ACTION_WIFI_SETTINGS)
            ConnectionMode.BLUETOOTH -> listOf(Settings.ACTION_BLUETOOTH_SETTINGS)
            ConnectionMode.USB -> listOf(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS)
        }
        for ((index, action) in actions.withIndex()) {
            try {
                context.startActivity(Intent(action))
                if (mode == ConnectionMode.DIRECT && index > 0)
                    Toast.makeText(context, "此系统未提供直连设置入口，请在 WLAN 设置中查找 WLAN 直连", Toast.LENGTH_LONG).show()
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
