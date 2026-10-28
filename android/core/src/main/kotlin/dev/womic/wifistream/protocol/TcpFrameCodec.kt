// TCP frame codec and message builders for the WO Mic WiFi control protocol.
//
// Every frame is [type:u8][length:u32 BE][payload]. Replies mirror the official
// app (evidence: b2/AbstractC0175c.java e0(), d2/*.java d0(), V1/l.java).

package dev.womic.wifistream.protocol

import java.io.ByteArrayOutputStream

/** One decoded control frame. */
class TcpFrame(val type: Int, val payload: ByteArray) {
    override fun equals(other: Any?): Boolean =
        other is TcpFrame && type == other.type && payload.contentEquals(other.payload)

    override fun hashCode(): Int = type * 31 + payload.contentHashCode()

    override fun toString(): String =
        "TcpFrame(type=$type, payload=${payload.toHexString()})"
}

/** Incremental decoder tolerant of TCP fragmentation (recv may split frames). */
class FrameDecoder {
    private val buf = ByteArrayOutputStream()

    /** Feed raw bytes; returns zero or more complete frames. */
    fun feed(chunk: ByteArray, len: Int = chunk.size): List<TcpFrame> {
        buf.write(chunk, 0, len)
        val out = ArrayList<TcpFrame>()
        val b = buf.toByteArray()
        var off = 0
        while (b.size - off >= 5) {
            val type = b[off].toInt() and 0xFF
            val length = readU32BE(b, off + 1)
            require(length in 0..2048) { "Invalid control frame length: $length" }
            if (b.size - off - 5 < length) break // wait for more bytes
            val payload = b.copyOfRange(off + 5, off + 5 + length)
            out.add(TcpFrame(type, payload))
            off += 5 + length
        }
        buf.reset()
        if (off < b.size) buf.write(b, off, b.size - off)
        return out
    }

    companion object {
        fun readU32BE(b: ByteArray, off: Int): Int =
            ((b[off].toInt() and 0xFF) shl 24) or
                ((b[off + 1].toInt() and 0xFF) shl 16) or
                ((b[off + 2].toInt() and 0xFF) shl 8) or
                (b[off + 3].toInt() and 0xFF)

        fun encode(type: Int, payload: ByteArray): ByteArray {
            val out = ByteArrayOutputStream(5 + payload.size)
            out.write(type)
            out.write((payload.size ushr 24) and 0xFF)
            out.write((payload.size ushr 16) and 0xFF)
            out.write((payload.size ushr 8) and 0xFF)
            out.write(payload.size and 0xFF)
            out.write(payload)
            return out.toByteArray()
        }
    }
}

// ---------------------------------------------------------------- builders

/** PC -> phone: hello. Official 6.3 sends HELLO_PAYLOAD_OFFICIAL. */
fun buildHello(payload: ByteArray = HELLO_PAYLOAD_OFFICIAL): ByteArray =
    FrameDecoder.encode(Msg.HELLO, payload)

/** Phone -> PC: hello reply. status=0, level byte must be 4 (client checks it). */
/**
 * Phone -> PC: hello reply.
 *
 * R-38a：`status == OK` 时在**尾部追加 1 字节能力位** `cap = 1`（支持对账探针
 * 回执）。只追加、不改既有字段顺序与语义：官方端按 len 读前 2 字节，多余字节
 * 不参与解析（施工书 §0：对已知 type 尾部追加风险低；新增未知 type 才危险）。
 * 错误应答（如 10）保持 2 字节，与官方 trace 逐字节一致。
 */
fun buildHelloReply(
    status: Int = Status.OK,
    level: Int = 4,
    cap: Int = HELLO_CAP_RECONCILE
): ByteArray {
    val payload = if (status == Status.OK) {
        byteArrayOf(status.toByte(), level.toByte(), cap.toByte())
    } else {
        byteArrayOf(status.toByte(), level.toByte())
    }
    return FrameDecoder.encode(Msg.HELLO, payload)
}

/** R-38a 能力位：`1` = 本端支持对账探针（media_port + 1）。 */
const val HELLO_CAP_RECONCILE: Int = 1

/** PC -> phone: codec/rate/media-port. */
fun buildConfig(codec: Int, rateIndex: Int, mediaPort: Int): ByteArray {
    val p = ByteArray(6)
    p[0] = codec.toByte()
    p[1] = rateIndex.toByte()
    p[2] = (mediaPort ushr 24).toByte()
    p[3] = (mediaPort ushr 16).toByte()
    p[4] = (mediaPort ushr 8).toByte()
    p[5] = mediaPort.toByte()
    return FrameDecoder.encode(Msg.CONFIG, p)
}

/** Phone -> PC: config/start status reply (type mirrors request; see d2/e.java). */
fun buildStatusReply(requestType: Int, status: Int): ByteArray =
    FrameDecoder.encode(requestType, byteArrayOf(status.toByte()))

/**
 * Phone -> PC: reply to STOP.
 *
 * QUIRK (verified in dex + disassembly): the official app answers STOP with a
 * type-102 frame because V1/l.java case 104 uses the no-arg d2.e constructor
 * whose f12092q defaults to 0 -> e0(out, 102, 1). We replicate byte-exactly.
 */
fun buildStopReply(status: Int = Status.OK): ByteArray =
    FrameDecoder.encode(Msg.CONFIG, byteArrayOf(status.toByte()))

/** PC -> phone: start (no payload). */
fun buildStart(): ByteArray = FrameDecoder.encode(Msg.START, ByteArray(0))

/** PC -> phone: stop (no payload). */
fun buildStop(): ByteArray = FrameDecoder.encode(Msg.STOP, ByteArray(0))

/** PC -> phone: heartbeat (no payload, no reply). */
fun buildHeartbeat(): ByteArray = FrameDecoder.encode(Msg.HEARTBEAT, ByteArray(0))

/** Phone -> PC: error string frame: [0][len:u32 BE][1][ascii]. */
fun buildError(message: String): ByteArray {
    val ascii = message.toByteArray(Charsets.US_ASCII)
    val out = ByteArrayOutputStream(5 + ascii.size + 1)
    out.write(Msg.ERROR)
    val len = ascii.size + 1
    out.write((len ushr 24) and 0xFF)
    out.write((len ushr 16) and 0xFF)
    out.write((len ushr 8) and 0xFF)
    out.write(len and 0xFF)
    out.write(1)
    out.write(ascii)
    return out.toByteArray()
}

/** Parsed CONFIG payload. */
class ConfigParams(val codec: Int, val rateIndex: Int, val mediaPort: Int) {
    val sampleRate: Int
        get() = if (rateIndex in SampleRates.TABLE.indices) SampleRates.TABLE[rateIndex] else -1

    companion object {
        fun parse(payload: ByteArray): ConfigParams {
            require(payload.size >= 6) { "config payload too short: ${payload.size}" }
            val port = FrameDecoder.readU32BE(payload, 2)
            return ConfigParams(payload[0].toInt() and 0xFF, payload[1].toInt() and 0xFF, port)
        }
    }
}

/** Parsed HELLO payload: only byte 0 (UDP header format variant) is meaningful. */
fun parseHello(payload: ByteArray): Int {
    require(payload.size >= 6) { "hello payload too short: ${payload.size}" }
    return payload[0].toInt() and 0xFF
}

internal fun ByteArray.toHexString(): String =
    joinToString(" ") { "%02X".format(it) }
