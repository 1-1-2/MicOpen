package com.miclink.open

import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.ArrayDeque
import java.util.Locale

/** Process-local bounded log. No Context, Activity, disk or logcat dependency. */
class EventLog(
    private val maxEntries: Int = 300,
    private val maxCharacters: Int = 64 * 1024,
    private val clock: () -> Long = System::currentTimeMillis
) {
    data class Snapshot(val version: Long, val text: String, val count: Int, val dropped: Long)
    private val entries = ArrayDeque<String>()
    private var characters = 0
    private var version = 0L
    private var dropped = 0L
    private var cached = Snapshot(0, "", 0, 0)
    private val format = DateTimeFormatter.ofPattern("HH:mm:ss.SSS", Locale.ROOT).withZone(ZoneId.systemDefault())
    private val privateValue = Regex("""(?i)\b(pin|password|secret|token|authorization)\s*[:=]\s*(?:"[^"]*"|'[^']*'|\S+)""")
    private val authorization = Regex("""(?i)\bauthorization\s*[:=]\s*[^\r\n]+""")
    private val credential = Regex("""(?:github_pat_|gh[pousr]_|tskey-)[A-Za-z0-9_-]{8,}""")

    init { require(maxEntries > 0 && maxCharacters >= 128) }

    fun sanitize(value: String): String = credential.replace(privateValue.replace(authorization.replace(value, "Authorization=[已隐藏]")) {
        "${it.groupValues[1]}=[已隐藏]"
    }, "[已隐藏]")

    @Synchronized
    fun append(level: String, tag: String, message: String) {
        val safe = sanitize(message).replace('\r', ' ').replace('\n', ' ').take(512)
        val line = "${format.format(Instant.ofEpochMilli(clock()))} ${level.take(5)} ${tag.take(40)} · $safe"
        // Keep even a single very long entry within the total character budget.
        val bounded = line.take(maxCharacters - 1)
        entries.addLast(bounded)
        characters += bounded.length + 1
        while (entries.size > maxEntries || characters > maxCharacters) {
            characters -= entries.removeFirst().length + 1
            dropped++
        }
        version++
    }

    @Synchronized
    fun snapshot(): Snapshot {
        if (cached.version != version) cached = Snapshot(version, entries.joinToString("\n"), entries.size, dropped)
        return cached
    }
}
