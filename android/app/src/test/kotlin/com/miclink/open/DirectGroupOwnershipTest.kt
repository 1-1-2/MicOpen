package com.miclink.open

import org.junit.Assert.*
import org.junit.Test

class DirectGroupOwnershipTest {
    private val chosen = "02:ab:cd:44:ee:55"
    @Test fun unrelatedGroupCannotBeRemovedEvenIfInterfaceWasReused() {
        assertFalse(DirectGroupOwnership.mayLeave(chosen, "p2p0", "p2p0", "02:99:88:77:66:55"))
        assertFalse(DirectGroupOwnership.mayLeave(null, "p2p0", "p2p0", chosen))
        assertFalse(DirectGroupOwnership.mayLeave(chosen, "p2p0", "p2p1", chosen))
    }
    @Test fun lateCompletionRequiresPositivePeerIdentity() {
        assertTrue(DirectGroupOwnership.mayLeave(chosen, null, "p2p0", chosen.uppercase()))
        assertFalse(DirectGroupOwnership.mayLeave(chosen, null, "p2p0", "02:00:00:00:00:00"))
        assertFalse(DirectGroupOwnership.mayLeave(chosen, null, "p2p0", null))
    }
    @Test fun knownSessionCanBeReleasedWhenPlatformMasksOwner() {
        assertTrue(DirectGroupOwnership.mayLeave(chosen, "p2p0", "p2p0", "02:00:00:00:00:00"))
        assertFalse(DirectGroupOwnership.foreign(chosen, "02:00:00:00:00:00"))
        assertTrue(DirectGroupOwnership.foreign(chosen, "02:99:88:77:66:55"))
    }
}
