package com.miclink.open

/** Platform-free admission and callback lifetime rules, shared with replay tests. */
class DirectAttempt(private val now: () -> Long) {
    enum class Phase { IDLE, CONNECTING, WAITING_IP, CONNECTED, CLOSED }
    var phase = Phase.IDLE
        private set
    private var generation = 0L
    private var inspecting = false
    private var ipStarted: Long? = null
    fun begin(): Boolean {
        if (phase != Phase.IDLE) return false
        phase = Phase.CONNECTING
        return true
    }
    fun inspect(): Long? {
        if (phase == Phase.IDLE || phase == Phase.CLOSED || inspecting) return null
        inspecting = true
        return generation
    }
    fun current(token: Long) = token == generation && phase != Phase.CLOSED
    fun inspected(token: Long) { if (current(token)) inspecting = false }
    fun group(token: Long) {
        if (current(token) && phase == Phase.CONNECTING) {
            phase = Phase.WAITING_IP
            ipStarted = now()
        }
    }
    fun ipTimedOut() = phase == Phase.WAITING_IP && now() - (ipStarted ?: now()) >= 20_000
    fun deliver(token: Long): Boolean {
        if (!current(token) || phase != Phase.WAITING_IP) return false
        phase = Phase.CONNECTED
        return true
    }
    fun close() { phase = Phase.CLOSED; generation++; inspecting = false }
}
