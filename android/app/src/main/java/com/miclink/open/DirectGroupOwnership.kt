package com.miclink.open

/** Only release a group attributable to this phone's selected connection. */
object DirectGroupOwnership {
    private fun usable(owner: String?) = !owner.isNullOrBlank() && owner != "02:00:00:00:00:00"
    fun foreign(selected: String?, owner: String?): Boolean =
        selected != null && usable(owner) && !selected.equals(owner, ignoreCase = true)

    fun mayLeave(selected: String?, knownInterface: String?, actualInterface: String?, owner: String?): Boolean {
        if (selected == null || actualInterface.isNullOrBlank() || foreign(selected, owner)) return false
        if (knownInterface != null) return knownInterface == actualInterface
        // A late join after cancellation needs positive identity evidence.
        return usable(owner) && selected.equals(owner, ignoreCase = true)
    }
}
