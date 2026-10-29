package com.miclink.open

import org.junit.Assert.*
import org.junit.Test

class ConnectionModeTest {
    @Test fun upgradesPreserveTheOldNetworkPreparation() {
        assertEquals(ConnectionMode.WIFI, ConnectionMode.fromSaved("HOTSPOT"))
        assertEquals(ConnectionMode.WIFI, ConnectionMode.fromSaved("WIFI"))
        assertEquals(ConnectionMode.BLUETOOTH, ConnectionMode.fromSaved(null, true))
        assertEquals(ConnectionMode.USB, ConnectionMode.fromSaved("USB", true))
        assertEquals(ConnectionMode.DIRECT, ConnectionMode.fromSaved("DIRECT"))
        assertEquals(ConnectionMode.WIFI, ConnectionMode.fromSaved("unknown", true))
    }
    @Test fun directUsesAnIpLinkButNeverAsksTheUserForAnAddress() {
        assertFalse(ConnectionMode.DIRECT.localOnly)
        assertFalse(ConnectionMode.DIRECT.needsAddress)
        assertTrue(ConnectionMode.WIFI.needsAddress)
        assertTrue(ConnectionMode.BLUETOOTH.localOnly)
        assertTrue(ConnectionMode.USB.localOnly)
    }
}
