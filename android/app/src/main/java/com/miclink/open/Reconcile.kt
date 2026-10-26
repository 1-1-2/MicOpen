package com.miclink.open

import android.os.SystemClock
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException
import java.util.Locale

/**
 * R-38a：对账通道（App 侧）。
 *
 * 官方协议的硬事实：手机 UDP **只发不收**、PC 只收不回，"有没有声音到"在协议内
 * 不可知。本文件实现一条**零侵入旁路**：
 *
 *  - 手机在推流期间每 3 s 向 `media_port + 1` 发一个 16B 探针（含自己已发包数、
 *    最后 seq）；支持回执的 PC 在**同一端口**回一个 16B report（已收包数）。
 *  - 手机据此算出**真实丢包**（sent − recv），并在连接页状态栏显示「回执正常 ·
 *    丢包 x%」；对端是原厂客户端 / 防火墙拦截时 2 次探针无应答 → 显示
 *    「对方未回执 · 可能是原厂客户端或防火墙拦截」，**不阻断推流**。
 *  - **绝不新增 TCP type**：能力位只是 101 应答尾部追加 1 字节（`cap=1`），
 *    既有字段语义与顺序不变；对账 socket 独立于媒体 socket，任何异常只记日志。
 *
 * 本文件刻意不依赖 Compose/Android framework（`ReconcileLink` 只需
 * `SystemClock`，且时钟已注入），因此编解码、超时判定、文案组装全部可在
 * JVM 单测里用假时钟精确验证。
 */

/** 探针魔数（4 字节 ASCII）。 */
const val RECONCILE_MAGIC: String = "MLK1"

/** 对账端口相对媒体端口的偏移（`media_port + 1`）。 */
const val RECONCILE_PORT_OFFSET: Int = 1

/** 探针 / 回执固定长度（字节）。 */
const val RECONCILE_PACKET_SIZE: Int = 16

/** 探针类型（手机 → PC）。 */
const val RECONCILE_TYPE_PROBE: Int = 1

/** 回执类型（PC → 手机）。 */
const val RECONCILE_TYPE_REPORT: Int = 2

/** 稳态探针间隔：3 s（16 B / 3 s ≈ 5 B/s，可忽略）。 */
const val RECONCILE_PROBE_INTERVAL_MS: Long = 3_000L

/** 启动阶段探针间隔：1.5 s。 */
const val RECONCILE_STARTUP_INTERVAL_MS: Long = 1_500L

/** 启动阶段探针次数：发 2 次仍无应答 → 判定「未回执」。 */
const val RECONCILE_STARTUP_ATTEMPTS: Int = 2

/**
 * 探针 / 回执的十六进制布局（**大端**，16 B）：
 *
 * ```
 * 0..3   magic  "MLK1"
 * 4      type   1=探针 / 2=回执
 * 5..8   seq    u32
 * 9..12  packets u32（探针=已发包数，回执=已收包数）
 * 13..14 lastSeq u16
 * 15     rsv    保留（0）
 * ```
 */
data class ReconcilePacket(
    val type: Int,
    val seq: Int,
    val packets: Int,
    val lastSeq: Int
)

/** 探针 / 回执编解码（纯函数，往返一致性由单测锁死）。 */
object ReconcileCodec {

    fun encode(type: Int, seq: Int, packets: Int, lastSeq: Int): ByteArray {
        val out = ByteArray(RECONCILE_PACKET_SIZE)
        val magic = RECONCILE_MAGIC.toByteArray(Charsets.US_ASCII)
        System.arraycopy(magic, 0, out, 0, 4)
        out[4] = type.toByte()
        putInt(out, 5, seq)
        putInt(out, 9, packets)
        out[13] = (lastSeq ushr 8).toByte()
        out[14] = lastSeq.toByte()
        out[15] = 0
        return out
    }

    /** 解码；长度不足、magic 不符、type 未知均返回 null（绝不抛异常）。 */
    fun decode(buf: ByteArray, length: Int): ReconcilePacket? {
        if (length < RECONCILE_PACKET_SIZE || buf.size < RECONCILE_PACKET_SIZE) {
            return null
        }
        val magic = String(buf, 0, 4, Charsets.US_ASCII)
        if (magic != RECONCILE_MAGIC) {
            return null
        }
        val type = buf[4].toInt() and 0xFF
        if (type != RECONCILE_TYPE_PROBE && type != RECONCILE_TYPE_REPORT) {
            return null
        }
        return ReconcilePacket(
            type = type,
            seq = readInt(buf, 5),
            packets = readInt(buf, 9),
            lastSeq = ((buf[13].toInt() and 0xFF) shl 8) or (buf[14].toInt() and 0xFF)
        )
    }

    fun encodeProbe(seq: Int, sentPackets: Int, lastSeq: Int): ByteArray =
        encode(RECONCILE_TYPE_PROBE, seq, sentPackets, lastSeq)

    fun decodeProbe(buf: ByteArray, length: Int): ReconcilePacket? =
        decode(buf, length)?.takeIf { it.type == RECONCILE_TYPE_PROBE }

    fun decodeReport(buf: ByteArray, length: Int): ReconcilePacket? =
        decode(buf, length)?.takeIf { it.type == RECONCILE_TYPE_REPORT }

    private fun putInt(out: ByteArray, at: Int, value: Int) {
        out[at] = (value ushr 24).toByte()
        out[at + 1] = (value ushr 16).toByte()
        out[at + 2] = (value ushr 8).toByte()
        out[at + 3] = value.toByte()
    }

    private fun readInt(buf: ByteArray, at: Int): Int =
        ((buf[at].toInt() and 0xFF) shl 24) or
            ((buf[at + 1].toInt() and 0xFF) shl 16) or
            ((buf[at + 2].toInt() and 0xFF) shl 8) or
            (buf[at + 3].toInt() and 0xFF)
}

/** 回执状态（UI 三态 + 空闲）。 */
enum class ReconcileStatus {
    /** 还没开始推流。 */
    IDLE,

    /** 已发探针、等回执。 */
    WAITING,

    /** 收到回执：对端支持对账。 */
    OK,

    /** 启动阶段 2 次探针无应答：对端可能是原厂客户端或防火墙拦截。 */
    NO_REPLY
}

/**
 * 回执跟踪状态机（纯逻辑 + 注入时钟）。
 *
 * 时序（与施工书一致）：
 *  - `start()` 后立刻发第 1 个探针，1.5 s 后发第 2 个；第 2 个发出后再等 1.5 s
 *    仍无应答 → [ReconcileStatus.NO_REPLY]（**不阻断推流**，之后仍每 3 s 探一次，
 *    一旦收到回执就转 [ReconcileStatus.OK]）；
 *  - 收到 report → `OK`，丢包率 = `(sentAtLastProbe - recvAtLastReport) / sent`。
 */
class ReconcileTracker(
    private val probeIntervalMs: Long = RECONCILE_PROBE_INTERVAL_MS,
    private val startupIntervalMs: Long = RECONCILE_STARTUP_INTERVAL_MS,
    private val startupAttempts: Int = RECONCILE_STARTUP_ATTEMPTS
) {
    var status: ReconcileStatus = ReconcileStatus.IDLE
        private set

    /** 最近一次探针里的「已发包数」。 */
    var sentAtLastProbe: Int = 0
        private set

    /** 最近一次回执里的「已收包数」。 */
    var recvAtLastReport: Int = 0
        private set

    private var seqCounter: Int = 0
    private var lastProbeAtMs: Long = -1L
    private var attempts: Int = 0
    private var startupPhase: Boolean = false
    private var replyDeadlineMs: Long = -1L

    /** 开始一轮对账（每次推流开始时调用）。 */
    fun start(nowMs: Long) {
        status = ReconcileStatus.WAITING
        startupPhase = true
        attempts = 0
        seqCounter = 0
        lastProbeAtMs = -1L
        replyDeadlineMs = -1L
        sentAtLastProbe = 0
        recvAtLastReport = 0
    }

    /** 是否该发下一个探针。 */
    fun probeDue(nowMs: Long): Boolean {
        if (status == ReconcileStatus.IDLE) {
            return false
        }
        if (status == ReconcileStatus.WAITING && startupPhase) {
            if (attempts >= startupAttempts) {
                return false // 已发满 2 次，等应答判定（tick 负责超时）
            }
            return lastProbeAtMs < 0L || nowMs - lastProbeAtMs >= startupIntervalMs
        }
        return lastProbeAtMs < 0L || nowMs - lastProbeAtMs >= probeIntervalMs
    }

    /** 下一个探针序号。 */
    fun takeSeq(): Int = seqCounter++

    /** 探针已发出。 */
    fun onProbeSent(nowMs: Long, sentPackets: Int) {
        lastProbeAtMs = nowMs
        sentAtLastProbe = sentPackets
        if (status == ReconcileStatus.WAITING && startupPhase) {
            attempts++
            if (attempts >= startupAttempts) {
                replyDeadlineMs = nowMs + startupIntervalMs
            }
        }
    }

    /** 收到回执。 */
    fun onReport(nowMs: Long, recvPackets: Int) {
        status = ReconcileStatus.OK
        recvAtLastReport = recvPackets
        startupPhase = false
        replyDeadlineMs = -1L
        // 从收到回执起重新计 3 s，避免刚回执又立刻探
        lastProbeAtMs = nowMs
    }

    /** 超时判定（由轮询线程周期调用）。 */
    fun tick(nowMs: Long) {
        if (status == ReconcileStatus.WAITING && replyDeadlineMs >= 0L && nowMs >= replyDeadlineMs) {
            status = ReconcileStatus.NO_REPLY
            startupPhase = false
            replyDeadlineMs = -1L
            // 判定后继续每 3 s 探一次（对端可能稍后才起来）
            lastProbeAtMs = nowMs
        }
    }

    /**
     * 真实丢包（千分比）：`(上次探针已发包数 − 上次回执已收包数) × 1000 / sent`。
     * 未回执 / 还没发过包 → null（UI 显示「—」，不编造数字）。
     */
    fun lossPermille(): Int? {
        if (status != ReconcileStatus.OK) {
            return null
        }
        val sent = sentAtLastProbe
        if (sent <= 0) {
            return null
        }
        val lost = (sent - recvAtLastReport).coerceAtLeast(0)
        return lost * 1000 / sent
    }
}

/** 状态栏文案组装（纯函数，文案由单测锁死）。 */
object ReconcileText {

    /** 已发送字节数 → 「—」/「812 B」/「12.3 KB」/「12.3 MB」/「1.2 GB」。 */
    fun formatBytes(bytes: Long): String {
        if (bytes <= 0L) {
            return "—"
        }
        return when {
            bytes < 1024L -> "$bytes B"
            bytes < 1024L * 1024L -> String.format(Locale.US, "%.1f KB", bytes / 1024.0)
            bytes < 1024L * 1024L * 1024L -> String.format(Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0))
            else -> String.format(Locale.US, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0))
        }
    }

    /**
     * 回执行：三态 + 无数据。
     *  - IDLE / WAITING → 「回执：—」（还没结果，不编造）
     *  - OK + 有丢包数字 → 「回执正常 · 丢包 2.0%」
     *  - OK + 还没发过包 → 「回执正常」
     *  - NO_REPLY → 「对方未回执 · 可能是原厂客户端或防火墙拦截」
     */
    fun line(status: ReconcileStatus, lossPermille: Int?): String = when (status) {
        ReconcileStatus.IDLE, ReconcileStatus.WAITING -> "回执：—"
        ReconcileStatus.NO_REPLY -> "对方未回执 · 可能是原厂客户端或防火墙拦截"
        ReconcileStatus.OK ->
            if (lossPermille == null) {
                "回执正常"
            } else {
                String.format(Locale.US, "回执正常 · 丢包 %.1f%%", lossPermille / 10.0)
            }
    }
}

/**
 * 对账链路的 socket 部分：独立 UDP socket（**不绑媒体端口**），每 3 s 探一次，
 * 收到 report 交给 [ReconcileTracker]。
 *
 * 纪律：任何异常只记日志，绝不 throw 到推流路径；socket 关闭后线程自行退出。
 */
internal class ReconcileLink(
    private val peerHost: String,
    private val peerPort: Int,
    private val sentPackets: () -> Int,
    private val lastSeq: () -> Int,
    private val now: () -> Long = { SystemClock.elapsedRealtime() },
    private val log: (String) -> Unit = {}
) {
    private val tracker = ReconcileTracker()
    private var socket: DatagramSocket? = null
    private var thread: Thread? = null
    private var failureLogged = false

    val status: ReconcileStatus get() = tracker.status

    fun lossPermille(): Int? = tracker.lossPermille()

    /** 启动对账线程（幂等；重复调用无效）。 */
    fun start() {
        if (thread != null) {
            return
        }
        val s = try {
            DatagramSocket()
        } catch (t: Throwable) {
            log("对账 socket 打开失败：${t.message}（不影响推流）")
            return
        }
        s.soTimeout = 200 // 短超时轮询：兼顾探针节奏与线程退出响应
        socket = s
        tracker.start(now())
        val t = Thread({ loop(s) }, "reconcile")
        t.isDaemon = true
        thread = t
        t.start()
    }

    private fun loop(s: DatagramSocket) {
        val address = try {
            InetAddress.getByName(peerHost)
        } catch (t: Throwable) {
            log("对账目标地址解析失败：${t.message}（不影响推流）")
            return
        }
        val buf = ByteArray(RECONCILE_PACKET_SIZE * 2)
        while (!Thread.currentThread().isInterrupted && !s.isClosed) {
            val t = now()
            if (tracker.probeDue(t)) {
                val probe = ReconcileCodec.encodeProbe(tracker.takeSeq(), sentPackets(), lastSeq())
                try {
                    s.send(DatagramPacket(probe, probe.size, address, peerPort))
                    tracker.onProbeSent(t, sentPackets())
                    failureLogged = false
                } catch (io: IOException) {
                    if (!failureLogged) {
                        // 只记一次（对端不接受 ICMP unreachable 时会连续失败）
                        log("对账探针发送失败：${io.message}（不影响推流）")
                        failureLogged = true
                    }
                }
            }
            try {
                val packet = DatagramPacket(buf, buf.size)
                s.receive(packet)
                val report = ReconcileCodec.decodeReport(packet.data, packet.length)
                if (report != null) {
                    tracker.onReport(now(), report.packets)
                }
            } catch (_: SocketTimeoutException) {
                // 正常：短超时轮询，继续检查探针节奏 / 超时判定
            } catch (t: Throwable) {
                if (!s.isClosed) {
                    log("对账 socket 异常：${t.message}（不影响推流）")
                }
                break
            }
            tracker.tick(now())
        }
    }

    /** 关闭（停推流 / 服务停止时调用）；幂等。 */
    fun close() {
        try {
            socket?.close()
        } catch (_: Throwable) {
            // 关闭失败无影响
        }
        thread?.interrupt()
        thread = null
        socket = null
    }
}
