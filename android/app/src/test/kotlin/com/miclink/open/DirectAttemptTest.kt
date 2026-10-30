package com.miclink.open

import org.junit.Assert.*
import org.junit.Test

class DirectAttemptTest {
    @Test fun ackDoesNotMeanGroupOrReady() {
        val a = DirectAttempt { 0 }
        assertTrue(a.begin()); assertFalse(a.begin())
        assertEquals(DirectAttempt.Phase.CONNECTING, a.phase)
        assertFalse(a.deliver(a.inspect()!!))
    }
    @Test fun duplicateBroadcastsCannotStartParallelInspectionsOrDeliverTwice() {
        val a = DirectAttempt { 0 }; a.begin()
        val token = a.inspect()!!
        assertNull(a.inspect())
        a.group(token); assertTrue(a.deliver(token)); assertFalse(a.deliver(token))
        a.inspected(token); assertNotNull(a.inspect())
    }
    @Test fun lateCallbacksAfterStopCannotReviveSession() {
        val a = DirectAttempt { 0 }; a.begin()
        val old = a.inspect()!!; a.close()
        a.group(old); a.inspected(old)
        assertFalse(a.deliver(old)); assertNull(a.inspect()); assertFalse(a.begin())
        assertEquals(DirectAttempt.Phase.CLOSED, a.phase)
    }
    @Test fun delayedDhcpHasAnIndependentDeadline() {
        var clock = 100L
        val a = DirectAttempt { clock }; a.begin()
        val token = a.inspect()!!; a.group(token)
        clock += 19_999; assertFalse(a.ipTimedOut())
        clock++; a.group(token); assertTrue(a.ipTimedOut())
    }
    @Test fun connectedSessionDoesNotRetainDhcpTimer() {
        var clock = 0L
        val a = DirectAttempt { clock }; a.begin()
        val token = a.inspect()!!; a.group(token); assertTrue(a.deliver(token))
        clock += 90_000; assertFalse(a.ipTimedOut())
    }
    @Test fun localNetworkPermissionIsGatedByApiAndTransport() {
        for (sdk in listOf(26, 29, 33, 35, 36)) assertFalse(LocalNetworkPermission.required(sdk, true))
        assertTrue(LocalNetworkPermission.required(37, true))
        assertFalse(LocalNetworkPermission.required(37, false))
    }
}
