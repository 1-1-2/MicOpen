package com.miclink.open

/** Shared sink for the UI and this application's existing logcat messages. */
object AppLog {
    private val buffer = EventLog()
    fun snapshot(): EventLog.Snapshot = buffer.snapshot()
    fun clear(): EventLog.Snapshot = buffer.clear()
    private fun write(priority: Int, level: String, tag: String, message: String, error: Throwable?): Int {
        val text = if (error == null) message else "$message (${error.javaClass.simpleName}: ${error.message.orEmpty()})"
        val safe = buffer.sanitize(text)
        buffer.append(level, tag, safe)
        // Keep the same safe message in logcat; no separate unredacted copy.
        return android.util.Log.println(priority, tag, safe)
    }
    fun i(tag: String, message: String, error: Throwable? = null): Int = write(android.util.Log.INFO, "INFO", tag, message, error)
    fun w(tag: String, message: String, error: Throwable? = null): Int = write(android.util.Log.WARN, "WARN", tag, message, error)
    fun e(tag: String, message: String, error: Throwable? = null): Int = write(android.util.Log.ERROR, "ERROR", tag, message, error)
}
