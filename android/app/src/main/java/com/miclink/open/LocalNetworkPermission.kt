package com.miclink.open

object LocalNetworkPermission {
    const val NAME = "android.permission.ACCESS_LOCAL_NETWORK"
    fun required(sdk: Int, localSockets: Boolean) = sdk >= 37 && localSockets
}
