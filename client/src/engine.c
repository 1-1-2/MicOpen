/* engine.c — 协议引擎
 *
 * 实现 WO Mic Wi-Fi 协议 v1 的 PC 侧（连接发起方）：
 *   TCP 8125 控制面：101 hello → 102 音频参数 → 103 开始 → 105 心跳(1s) → 104 停止
 *   UDP 媒体面：手机单向下发，头格式 C（variant=4）
 *       [04 00][len:u16BE][seq:u16BE][ts:u32BE][vol:u8][payload]
 *   收到 Opus 包 → 解码 → 重采样到 48 kHz/16bit/mono → 由 devio 灌入驱动。
 *
 * 与上一版的三处关键差异（都是 P0）：
 *   1. 灌音改成 devio 的完整握手序列（0x222000 在 2022 驱动上 51/51 失败）；
 *   2. **全进程只开一次 \\.\WOMic**：eng_device_available() 不再自己 CreateFile
 *      （每次 CreateFile 都会把驱动的门控清零，实测会让另一个句柄立刻静音）；
 *   3. 输出固定 48 kHz / 16 bit / 单声道：驱动只搬运字节不做重采样，
 *      喂 44.1k 会变调且不报错（E3 实测：1000Hz → 1088Hz），所以 8k/16k 协商时
 *      由本文件做整数倍上采样（48000/16000=3、48000/8000=6）。
 *
 * 依赖：ws2_32（系统自带）。Opus 静态优先（见 opusdec.c）。
 */
#include "app.h"
#include "transport.h"
#include "monitor.h"
#include "devio.h"
#include "opusdec.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mmsystem.h>          /* R-39 监听：waveOut（winmm） */
#include <process.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define MAX_PKT          2048
#define DEV_RATE         48000          /* 驱动硬要求：不设成别的值 */
#define DEV_SAMPLES_20MS (DEV_RATE / 50)   /* 960 */
#define DEV_FRAME_BYTES  (DEV_SAMPLES_20MS * 2)  /* 1920 */

static volatile LONG g_run;
static int g_transport;
void eng_set_transport(int mode) { if (!g_run) g_transport = mode; }
static HANDLE        g_thread;
static SOCKET        g_tcp = INVALID_SOCKET, g_udp = INVALID_SOCKET;
/* R-38w 对账通道：旁路 UDP（media_port+1）。只在收到探针时回 report，
 * 绝不主动发 —— 对官方客户端零影响。 */
static SOCKET        g_side = INVALID_SOCKET;
static int           g_echo_state;      /* 0 未启用 / 1 正常 / 2 对端未回执 */
static int           g_echo_probes;
static DWORD         g_echo_warned;
static CRITICAL_SECTION g_cs;
static int           g_cs_inited;
static EngineStats   g_st;
static LogFn         g_log;

static int           g_rate_idx = 2;      /* 2=48k 1=16k 0=8k */
static int           g_buf_ms   = 60;
static volatile LONG g_gain_db10 = 0;      /* 本地增益，0.1 dB（eng_set_gain_db） */
/* R-47 扩展增益范围：0 = 上限 +6 dB（常态）；1 = +14 dB（501%，过载档）。
 * 与 UI 共用 ui_draw.h 的 GAIN_DB10_MAX / GAIN_DB10_MAX_EXT，不另留一份常量。 */
static int g_gain_ext = 0;
static DevFeedMode   g_mode     = DEV_FEED_IOCTL;
static volatile LONG g_mute;               /* D-06：静音（灌音前零填充） */

/* 协议音量字节 vol（格式 C 头第 10 字节，0..9）。只在变化时打一次日志，
 * 免得每 20 ms 刷一行把事件面板冲掉。 */
static int           g_vol      = 5;       /* 当前生效的 vol（5 = 原样） */
static int           g_vol_seen;           /* 本会话是否已经见过 vol 字节 */
static int           g_vol_warn;           /* vol 越界只提示一次 */

/* 丢包统计：seq 是 16 位循环计数，只认「向前跳号」。
 * 直接拿 (seq - last - 1) 会在乱序/重连时下溢成 65535，丢包率瞬间爆表。 */
static int           g_seq_valid;
static uint16_t      g_next_seq;

/* R-01：无音频超时提示（防火墙引导）。
 * 场景：TCP 三步全绿但 UDP 入站被防火墙拦 → 一个媒体包都收不到，统计全 0，
 * 界面毫无提示，用户只能靠猜。条件卡得很死：START 发出后 5 秒零包才提示
 * （协议里 START 后手机立即发包），一次性不刷屏；收到首个媒体包即清理。 */
static DWORD  g_start_at;         /* 103 START 发出时刻 */
static int    g_got_media;        /* 本会话是否收到过媒体包 */
static int    g_fw_hint_shown;    /* 防火墙提示是否已显示 */

/* 抖动估计：UDP 帧间隔的 EMA（D-14 状态栏/会话统计的「抖动」读数）。
 * 手机端固定 20 ms 一帧，到达间隔的抖动就是网络抖动的直接体现；
 * 旧的硬编码 20 会让读数永远是 20 ms，是个假读数。
 * 口径：只反映到达间隔的抖动，不是端到端延迟。 */
static DWORD         g_last_frame_at;
static double        g_lat_ema;

/* 媒体（UDP）端口：0 = 随机分配（默认）；1..65535 = 固定。
 * 由 eng_set_media_port() 设置，必须在 eng_start() 之前调用。 */
static int           g_media_port_req;

static int16_t       g_decbuf[DEV_SAMPLES_20MS];
static int16_t       g_feedbuf[DEV_SAMPLES_20MS];
static int           g_short_writes;

/* ------------------------------------------------------------ 日志 */
static void elog_lvl(int kind, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    if (g_log) g_log(buf, kind);
}

static void elog(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    elog_lvl(0, "%s", buf);
}

/* WARN 级（事件面板橙色）：可疑但没坏，例如"对端未回执""监听打不开" */
static void elog_warn(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    elog_lvl(3, "%s", buf);
}

/* ERROR 级（事件面板红色；「复制全部」同样收入） */
static void elog_err(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    elog_lvl(1, "%s", buf);
}

/* ------------------------------------------------------- 采样率转换 */
/* 48000 / {48000,16000,80000} 都是整数倍：{1,3,6}。线性插值足够（人耳不敏感，
 * 且驱动侧本来就是「只搬字节」的简单环形缓冲，没必要上 soxr）。 */
static void upsample_int(const int16_t *in, int n, int16_t *out, int factor)
{
    int i, k;
    if (factor <= 1 || n <= 0) { memcpy(out, in, (size_t)n * sizeof(int16_t)); return; }
    for (i = 0; i < n; i++) {
        int a = in[i];
        int b = (i + 1 < n) ? in[i + 1] : in[i];
        for (k = 0; k < factor; k++) {
            int v = a + ((b - a) * k) / factor;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[i * factor + k] = (int16_t)v;
        }
    }
}

/* 本地增益（R-31）：按 dB 算倍率 pow(10, dB/20)，-60 dB = 0.001 倍（不是 0）。
 * 旧实现存百分比并在 pos→gain 里做 `v<=1 → 0`，-38 dB 就掉成静音，
 * 而且 -60…-36 dB 整段被压进 0.1%…1.6% 的窄区间，半条轨道不可用。 */
static void apply_gain(int16_t *p, int n)
{
    double m = pow(10.0, (double)g_gain_db10 / 200.0);
    int i;
    if (n <= 0) return;
    if (g_gain_db10 == 0) return;              /* 0.0 dB = 原样，省一次乘 */
    for (i = 0; i < n; i++) {
        int v = (int)((double)p[i] * m + (m >= 0 ? 0.5 : -0.5));
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        p[i] = (int16_t)v;
    }
}

/* 协议音量字节 vol（0..9，5 = 原样，每档 6 dB）—— 必须在灌入驱动前对 PCM 生效。
 * 老实现把 vol 从包头里读出来就丢掉了，于是手机上调推流音量，PC 这边的听感
 * 毫无变化（CI 的 C08 只验"包里字节对不对"，没验"PC 有没有按它处理"）。
 *
 *   0     → 静音：零填充，**推流继续**（不中断会话、不停止写驱动）
 *   1..4  → 算术右移 (5 - vol) 位：−24/−18/−12/−6 dB
 *   5     → 原样
 *   6..9  → 左移 (vol - 5) 位：+6/+12/+18/+24 dB，**int16 饱和钳制**
 *           （左移溢出若回绕就是一耳朵爆音，必须钳在 ±32767）
 *
 * 顺序固定：先 vol 位移，再 apply_gain() 叠加本地增益。 */
static void apply_vol(int16_t *p, int n, int vol)
{
    int i, s;
    if (n <= 0 || vol == 5) return;
    if (vol == 0) {
        memset(p, 0, (size_t)n * sizeof(int16_t));
        return;
    }
    if (vol < 5) {
        s = 5 - vol;                                  /* 1..4 */
        for (i = 0; i < n; i++)
            p[i] = (int16_t)((int)p[i] >> s);         /* 算术右移：负数高位补 1 */
        return;
    }
    s = vol - 5;                                      /* 1..4 */
    for (i = 0; i < n; i++) {
        int v = (int)p[i] << s;
        if (v > 32767) v = 32767;                     /* 饱和，不回绕 */
        if (v < -32768) v = -32768;
        p[i] = (int16_t)v;
    }
}

/* vol 的档位说明（给事件日志用的可读文案） */
static const char *vol_desc(int vol)
{
    static const char *d[10] = {
        "静音", "−24 dB", "−18 dB", "−12 dB", "−6 dB",
        "原样 0 dB", "+6 dB", "+12 dB", "+18 dB", "+24 dB"
    };
    return (vol >= 0 && vol <= 9) ? d[vol] : "越界";
}

/* ------------------------------------------------------- TCP 小工具 */
static int tcp_send_all(const unsigned char *b, int n)
{
    int off = 0;
    while (off < n) {
        int r = send(g_tcp, (const char *)b + off, n - off, 0);
        if (r <= 0) return 0;
        off += r;
    }
    return 1;
}

/* 发一帧：[type][len BE u32][payload] */
static int msg_send(unsigned char type, const unsigned char *pl, uint32_t len)
{
    unsigned char head[5 + 64];
    head[0] = type;
    head[1] = (unsigned char)(len >> 24);
    head[2] = (unsigned char)(len >> 16);
    head[3] = (unsigned char)(len >> 8);
    head[4] = (unsigned char)(len);
    if (len) memcpy(head + 5, pl, len);          /* 头 5B + 负载，一次 send 降低小包开销 */
    return tcp_send_all(head, 5 + (int)len);
}

/* 收满 n 字节（带超时） */
static char g_receive_detail[128];
static int recv_exact(unsigned char *b, int n, int timeout_ms)
{
    int got = 0;
    DWORD t0 = GetTickCount();
    while (got < n) {
        fd_set rf;
        struct timeval tv;
        int remain = timeout_ms - (int)(GetTickCount() - t0), ready, r;
        if (remain <= 0) {
            snprintf(g_receive_detail, sizeof(g_receive_detail), "接收超时（%d/%d B）", got, n);
            return 0;
        }
        FD_ZERO(&rf); FD_SET(g_tcp, &rf);
        tv.tv_sec = remain / 1000; tv.tv_usec = (remain % 1000) * 1000;
        ready = select(0, &rf, NULL, NULL, &tv);
        if (ready <= 0) {
            if (!ready) snprintf(g_receive_detail, sizeof(g_receive_detail), "接收超时（%d/%d B）", got, n);
            else snprintf(g_receive_detail, sizeof(g_receive_detail), "等待接收失败 WSA=%d", WSAGetLastError());
            return 0;
        }
        r = recv(g_tcp, (char *)b + got, n - got, 0);
        if (r <= 0) {
            if (!r) snprintf(g_receive_detail, sizeof(g_receive_detail), "对端已关闭连接（%d/%d B）", got, n);
            else snprintf(g_receive_detail, sizeof(g_receive_detail), "接收失败 WSA=%d（%d/%d B）", WSAGetLastError(), got, n);
            return 0;
        }
        got += r;
    }
    return 1;
}

/* 读一帧：type + len + payload。返回 0 表示超时/断链。 */
static int msg_recv(unsigned char *type, unsigned char *pl, int cap, int *len, int timeout_ms)
{
    unsigned char h[5];
    if (!recv_exact(h, 5, timeout_ms)) return 0;
    *type = h[0];
    uint32_t l = ((uint32_t)h[1] << 24) | ((uint32_t)h[2] << 16) |
                 ((uint32_t)h[3] << 8) | h[4];
    if (l > (uint32_t)cap) { snprintf(g_receive_detail, sizeof(g_receive_detail), "应答长度异常：%u B", (unsigned)l); return 0; }
    *len = (int)l;
    if (l && !recv_exact(pl, (int)l, timeout_ms)) return 0;
    return 1;
}

/* ------------------------------------------------------------ 握手
 * 顺序很关键：UDP 必须先 bind，102 才能上报真实媒体端口。
 * 否则手机端会把音频发到端口 0，表现为"握手全成功但永远收不到包"。
 */
static int do_handshake(int rate_idx)
{
    unsigned char pl[64], type, rsp[256];
    int len, mport;
    SOCKADDR_IN ua;
    int ual = sizeof(ua);

    if (getsockname(g_udp, (SOCKADDR *)&ua, &ual) != 0) {
        strcpy(g_st.last_error, "读取本地媒体端口失败");
        return 0;
    }
    mport = ntohs(ua.sin_port);

    EnterCriticalSection(&g_cs);
    g_st.media_port = mport;
    g_st.media_fixed = (g_media_port_req != 0);
    LeaveCriticalSection(&g_cs);

    /* 101：payload[0]=4 表示 UDP 头格式 C；其余 5 字节原样复刻官方 6.3 */
    unsigned char hello[6] = { 0x04, 0x06, 0x03, 0x00, 0x00, 0x00 };
    if (g_transport >= 2) hello[5] = 0x53;
    if (!msg_send(101, hello, 6)) { strcpy(g_st.last_error, "101 发送失败"); return 0; }
    if (!msg_recv(&type, rsp, sizeof(rsp), &len, 6000)) { snprintf(g_st.last_error, sizeof(g_st.last_error), "101 握手失败：%s", g_receive_detail); return 0; }
    if (type != 101 || len < 2) { sprintf(g_st.last_error, "101 应答异常 type=%u len=%d", type, len); return 0; }
    if (rsp[0] != 0) { sprintf(g_st.last_error, "对端拒绝 hello，status=%u", rsp[0]); return 0; }
    /* R-38w：bit0 现在兼作"支持回执"标志（官方端恒为 0，不影响兼容）。
     * 头格式本体仍然必须是 4，只是允许最低位带上能力位。 */
    if ((rsp[1] & (unsigned char)~1u) != 4) {
        sprintf(g_st.last_error, "对端头格式=%u（预期 4）", rsp[1]);
        return 0;
    }
    if (g_transport >= 2 && (len < 3 || !(rsp[2] & 2))) {
        strcpy(g_st.last_error, "手机端不支持流式音频，请更新 Android 应用"); return 0;
    }
    if (len >= 2 && (rsp[1] & 1)) elog("对端支持回执（cap=1），旁路对账通道可用");
    elog("101 hello 成功：头格式 C（variant=4）");

    /* 102：codec=2(Opus) / rateIdx / 媒体端口（大端） */
    pl[0] = 2;
    pl[1] = (unsigned char)rate_idx;
    pl[2] = (unsigned char)(mport >> 24);
    pl[3] = (unsigned char)(mport >> 16);
    pl[4] = (unsigned char)(mport >> 8);
    pl[5] = (unsigned char)(mport);
    if (!msg_send(102, pl, 6)) { strcpy(g_st.last_error, "102 发送失败"); return 0; }
    if (!msg_recv(&type, rsp, sizeof(rsp), &len, 6000)) { strcpy(g_st.last_error, "102 无应答"); return 0; }
    if (type != 102 || len < 1) { strcpy(g_st.last_error, "102 应答异常"); return 0; }
    if (rsp[0] != 0) { sprintf(g_st.last_error, "音频参数被拒 status=%u（12=codec 非法 13=采样率非法）", rsp[0]); return 0; }
    elog("102 参数接受：Opus / %d Hz / 媒体端口 %d",
         rate_idx == 0 ? 8000 : rate_idx == 1 ? 16000 : 48000, mport);

    /* 103：开始推流 */
    if (!msg_send(103, NULL, 0)) { strcpy(g_st.last_error, "103 发送失败"); return 0; }
    if (!msg_recv(&type, rsp, sizeof(rsp), &len, 6000)) { strcpy(g_st.last_error, "103 无应答"); return 0; }
    if (len < 1 || rsp[0] != 0) { strcpy(g_st.last_error, "103 被拒"); return 0; }
    elog("103 推流已启动，等待 UDP 音频…");
    return 1;
}

/* Monitoring owns its output and DSP on a separate worker (monitor.cpp). */
/* ------------------------------------------------ UDP 帧解析（格式 C） */
static void on_udp_frame(const unsigned char *buf, int n)
{
    float rms, db;
    int i, samples = 0, factor;

    if (n < 11 || buf[0] != 0x04 || buf[1] != 0x00) return;
    int plen = (buf[2] << 8) | buf[3];
    uint16_t seq = (uint16_t)((buf[4] << 8) | buf[5]);
    const unsigned char *payload = buf + 11;
    int paylen = plen - 7;                 /* 去掉 seq2 + ts4 + vol1 */
    if (paylen <= 0 || 11 + paylen > n) return;

    /* vol：格式 C 头部第 10 字节（payload 前一字节）。老代码只是把它从 paylen
     * 里减掉，从未使用 —— 手机上调音量 PC 端毫无变化就是这么来的。 */
    int vol = buf[10];
    if (vol > 9) {
        if (!g_vol_warn) {
            g_vol_warn = 1;
            elog("协议音量字节异常 vol=%d（合法 0..9），本会话按 9 处理", vol);
        }
        vol = 9;
    }
    if (!g_vol_seen || vol != g_vol) {
        elog("手机端音量 vol=%d（%s）", vol, vol_desc(vol));
        g_vol = vol;
        g_vol_seen = 1;
    }

    /* R-01：第一个媒体包到了。若防火墙提示已显示，补一条收尾，把状态清干净 */
    if (!g_got_media) {
        g_got_media = 1;
        if (g_fw_hint_shown) elog("已开始接收音频");
    }

    EnterCriticalSection(&g_cs);
    /* 丢包 = seq 跳号数 / 期望总数。seq 是 16 位循环计数，只统计「向前跳号」：
     * 重复包与乱序包不记丢失 —— 否则 seq 回退（手机端重启推流、网络乱序）会
     * 让 (seq - last - 1) 下溢成 65535，丢包率一夜之间变成天文数字。 */
    if (g_seq_valid) {
        uint16_t ahead = (uint16_t)(seq - g_next_seq);
        if (ahead && ahead < 0x8000) {
            g_st.lost += ahead;           /* 跳过的号 = 丢包 */
            g_st.expected += ahead;
        }
    } else {
        g_seq_valid = 1;
    }
    g_next_seq = (uint16_t)(seq + 1);
    g_st.last_seq = seq;
    g_st.frames++;
    g_st.expected++;
    g_st.bytes += (uint64_t)n;
    g_st.vol = vol;
    LeaveCriticalSection(&g_cs);

    /* 抖动 = 帧间隔 EMA（α=0.1）。间隔 >1s 视为进程刚被唤醒/卡顿，不采样，
     * 避免一次调度抖动把读数拉到几百 ms。 */
    {
        DWORD now = GetTickCount();
        if (g_last_frame_at) {
            int d = (int)(now - g_last_frame_at);
            if (d > 0 && d < 1000) {
                double ema = g_lat_ema ? g_lat_ema : (double)d;
                ema = ema * 0.9 + (double)d * 0.1;
                g_lat_ema = ema;
                EnterCriticalSection(&g_cs);
                g_st.jitter_ms = (int)(ema + 0.5);
                LeaveCriticalSection(&g_cs);
            }
        }
        g_last_frame_at = now;
    }

    if (!opusdec_ready()) {
        /* 无解码器：用包大小作为活动度代理，界面仍有反馈 */
        float act = paylen / 400.0f; if (act > 1) act = 1;
        EnterCriticalSection(&g_cs);
        g_st.level += (act - g_st.level) * 0.2f;
        if (act > g_st.peak) g_st.peak = act;
        if (act < 0.01f) g_st.silent_frames++; else g_st.silent_frames = 0;
        LeaveCriticalSection(&g_cs);
        return;
    }

    samples = opusdec_decode(payload, paylen, g_decbuf, DEV_SAMPLES_20MS, 0);
    if (samples <= 0) return;
    if (samples > DEV_SAMPLES_20MS) samples = DEV_SAMPLES_20MS;

    /* 解码出来的采样率 = 协商速率；驱动只认 48k，这里做整数倍上采样 */
    factor = DEV_RATE / (g_st.rate ? g_st.rate : DEV_RATE);
    if (factor < 1) factor = 1;
    upsample_int(g_decbuf, samples, g_feedbuf, factor);
    samples *= factor;                       /* 正好 960（20 ms @48k） */
    apply_vol(g_feedbuf, samples, vol);      /* 先协议 vol（含饱和钳制） */
    apply_gain(g_feedbuf, samples);          /* 再本地增益（dB→倍率），顺序固定 */

    /* D-06 静音：灌入驱动前把 PCM 零填充。电平表照常显示输入电平
     * （上面的 rms/level 统计在静音之前就算完了），静音只截输出。 */
    if (g_mute) memset(g_feedbuf, 0, (size_t)samples * sizeof(int16_t));

    /* R-39：监听与灌驱动用**同一份**样本（静音时这里已经被零填充，
     * 所以监听也跟着静音，符合"静音时监听也静音"） */
    monitor_feed(g_feedbuf, samples);

    {
        double acc = 0;
        for (i = 0; i < samples; i++) { double v = g_feedbuf[i] / 32768.0; acc += v * v; }
        rms = (float)sqrt(acc / (samples ? samples : 1));
        db = (rms > 1e-6f) ? 20.0f * (float)log10((double)rms) : -96.0f;
    }

    EnterCriticalSection(&g_cs);
    g_st.level += (rms - g_st.level) * 0.35f;     /* attack 快 */
    if (rms > g_st.peak) g_st.peak = rms;
    g_st.db = db;
    if (rms < 0.004f) g_st.silent_frames++; else g_st.silent_frames = 0;
    LeaveCriticalSection(&g_cs);

    if (devio_is_open()) {
        int w = devio_write(g_feedbuf, samples * 2);
        if (w > 0) {
            EnterCriticalSection(&g_cs);
            g_st.injected += (uint64_t)w;
            LeaveCriticalSection(&g_cs);
            /* 背压看门狗：只有 WriteFile 路径能看到「被截断」，IOCTL 路径看不到，
             * 因此这个恢复手段只在 WriteFile 模式下启用。连续被截断说明环形缓冲
             * 将满，且门控可能已被别的进程清零 —— 重新走一遍握手（不重试失败）。 */
            if (g_mode == DEV_FEED_WRITE && w < samples * 2) {
                if (++g_short_writes > 20) {
                    g_short_writes = 0;
                    if (devio_rehandshake()) elog("检测到写入被持续截断，已重新握手");
                }
            } else {
                g_short_writes = 0;
            }
        }
    }
}

/* --------------------------------------------------------- 工作线程 */
static unsigned __stdcall worker(void *arg)
{
    WSADATA wsa;
    SOCKADDR_IN local;
    DWORD last_hb = 0, last_stat = 0;
    uint64_t prev_bytes = 0;
    unsigned char type, pl[MAX_PKT];
    int len;
    char host[64], *colon;
    int port = 8125;
    (void)arg;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        strcpy(g_st.last_error, "WSAStartup 失败");
        g_st.state = ST_ERROR;
        return 0;
    }

    strncpy(host, g_st.peer, sizeof(host) - 1);
    host[sizeof(host) - 1] = 0;
    colon = strrchr(host, ':');
    if (colon) { *colon = 0; port = atoi(colon + 1); }
    /* peer 串是唯一的端口来源，取出来后再校验一次：万一有调用点绕过
     * eng_start() 的参数闸门往 peer 里塞了越界端口，这里也要拒绝而不是
     * 让下面的 htons((u_short)port) 悄悄改成别的端口（D-01）。 */
    if (port < 1 || port > 65535) {
        snprintf(g_st.last_error, sizeof(g_st.last_error),
                 "控制端口 %d 非法：须为 1-65535 的整数（默认 8125）", port);
        g_st.state = ST_ERROR;
        WSACleanup();
        return 0;
    }

    /* 1) 先建 UDP 并 bind，拿到真实端口
     * g_media_port_req == 0 → 端口 0，由系统随机分配（默认行为，向后兼容）；
     * 否则绑到 --media-port 指定的固定端口（跨网络/过防火墙时必须固定）。 */
    g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp == INVALID_SOCKET) { strcpy(g_st.last_error, "UDP socket 失败"); g_st.state = ST_ERROR; goto out; }
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons((u_short)(g_media_port_req > 0 ? g_media_port_req : 0));
    if (bind(g_udp, (SOCKADDR *)&local, sizeof(local)) != 0) {
        int e = WSAGetLastError();
        if (g_media_port_req > 0) {
            /* 固定端口绑不上：绝大多数是端口被占用（WSAEADDRINUSE=10048）。
             * 明确带出端口与 WSA 错误码，且不静默回退到随机端口 —— 回退会让
             * "已放行防火墙"的假设悄悄失效，排查成本极高。 */
            sprintf(g_st.last_error,
                    "UDP 无法绑定固定媒体端口 %d，WSA=%d（10048=端口被占用，"
                    "10013=权限不足）；换一个 --media-port 或先释放该端口",
                    g_media_port_req, e);
        } else {
            sprintf(g_st.last_error, "UDP bind 失败 WSA=%d", e);
        }
        g_st.state = ST_ERROR;
        goto out;
    }
    {
        SOCKADDR_IN ba;
        int bal = sizeof(ba);
        int bound = 0;
        if (getsockname(g_udp, (SOCKADDR *)&ba, &bal) == 0) bound = ntohs(ba.sin_port);
        g_st.media_port = bound;
        g_st.media_fixed = (g_media_port_req > 0);
        elog("UDP 已绑定媒体端口 %d（%s）", bound,
             g_media_port_req > 0 ? "--media-port 指定" : "系统随机分配");
    }

    /* R-38w：旁路对账端口 = media_port+1。bind 失败只降级（不启用回执），
     * 绝不影响主链路 —— 这个通道纯属附加观测。 */
    g_echo_state = 0;
    g_echo_probes = 0;
    g_echo_warned = 0;
    if (g_transport < 2 && g_st.media_port > 0 && g_st.media_port < 65535) {
        SOCKADDR_IN sa2;
        g_side = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_side != INVALID_SOCKET) {
            memset(&sa2, 0, sizeof(sa2));
            sa2.sin_family = AF_INET;
            sa2.sin_addr.s_addr = INADDR_ANY;
            sa2.sin_port = htons((u_short)(g_st.media_port + 1));
            if (bind(g_side, (SOCKADDR *)&sa2, sizeof(sa2)) == 0) {
                elog("对账通道已就绪（UDP %d，仅回执不主动发包）", g_st.media_port + 1);
            } else {
                elog_warn("对账通道不可用（UDP %d 绑定失败，WSA=%d）：不影响录音",
                          g_st.media_port + 1, WSAGetLastError());
                closesocket(g_side);
                g_side = INVALID_SOCKET;
            }
        }
    }

    /* 2) 再连 TCP（port 已在上面校验过 1..65535，这里的 u_short 转换是恒等映射，
     *    不再是截断兜底） */
    g_tcp = transport_connect(g_transport, host, port, g_st.last_error, sizeof(g_st.last_error));
    if (g_tcp == INVALID_SOCKET) { g_st.state = ST_ERROR; goto out; }
    if (g_transport == 2) elog("蓝牙 RFCOMM 已连接，开始 101 握手");

    g_st.state = ST_HANDSHAKE;
    if (!do_handshake(g_rate_idx)) { g_st.state = ST_ERROR; goto out; }

    g_st.state = ST_STREAMING;
    g_st.started_at = GetTickCount();
    g_start_at = g_st.started_at;        /* R-01：无音频 5 秒计时从此刻起 */
    elog("已连接，进入推流状态");

    while (g_run) {
        fd_set rf;
        struct timeval tv = { 0, 200 * 1000 };
        FD_ZERO(&rf);
        FD_SET(g_tcp, &rf);
        FD_SET(g_udp, &rf);
        if (g_side != INVALID_SOCKET) FD_SET(g_side, &rf);
        int sel = select(0, &rf, NULL, NULL, &tv);
        if (sel > 0) {
            if (g_side != INVALID_SOCKET && FD_ISSET(g_side, &rf)) {
                /* 探针：magic4 "MLK1" + type1(=1) + seq4 + packets4 + last_seq2 + rsv1
                 * 回 report：同布局，type=2，packets/last_seq 填我们这边的真实计数。 */
                unsigned char pb[64], rb[16];
                SOCKADDR_IN from;
                int fl = sizeof(from);
                int rn = recvfrom(g_side, (char *)pb, sizeof(pb), 0,
                                  (SOCKADDR *)&from, &fl);
                if (rn >= 16 && memcmp(pb, "MLK1", 4) == 0 && pb[4] == 1) {
                    uint32_t seq = ((uint32_t)pb[5] << 24) | ((uint32_t)pb[6] << 16) |
                                   ((uint32_t)pb[7] << 8) | (uint32_t)pb[8];
                    uint16_t last;
                    EnterCriticalSection(&g_cs);
                    last = (uint16_t)g_st.last_seq;
                    LeaveCriticalSection(&g_cs);
                    memcpy(rb, "MLK1", 4);
                    rb[4] = 2;
                    rb[5]  = (unsigned char)(seq >> 24); rb[6] = (unsigned char)(seq >> 16);
                    rb[7]  = (unsigned char)(seq >> 8);  rb[8] = (unsigned char)seq;
                    EnterCriticalSection(&g_cs);
                    rb[9]  = (unsigned char)(g_st.frames >> 24);
                    rb[10] = (unsigned char)(g_st.frames >> 16);
                    rb[11] = (unsigned char)(g_st.frames >> 8);
                    rb[12] = (unsigned char)(g_st.frames);
                    LeaveCriticalSection(&g_cs);
                    rb[13] = (unsigned char)(last >> 8);
                    rb[14] = (unsigned char)(last & 0xFF);
                    rb[15] = 0;
                    sendto(g_side, (const char *)rb, 16, 0, (SOCKADDR *)&from, fl);
                    g_echo_probes++;
                    if (g_echo_state != 1) {
                        g_echo_state = 1;
                        elog("对账通道收到探针（第 %d 次），已回执：对端支持回执",
                             g_echo_probes);
                    }
                    EnterCriticalSection(&g_cs);
                    g_st.echo_state = 1;
                    g_st.echo_probes = g_echo_probes;
                    LeaveCriticalSection(&g_cs);
                }
            }
            if (FD_ISSET(g_udp, &rf)) {
                unsigned char buf[MAX_PKT];
                int n = recv(g_udp, (char *)buf, sizeof(buf), 0);
                if (n > 0) on_udp_frame(buf, n);
            }
            if (FD_ISSET(g_tcp, &rf)) {
                if (!msg_recv(&type, pl, sizeof(pl), &len, 100)) {
                    elog("控制连接已断开");
                    strcpy(g_st.last_error, "对端关闭控制连接");
                    g_st.state = ST_ERROR;
                    break;
                }
                if (type == 200 && g_transport >= 2) on_udp_frame(pl, len);
                if (type == 0 && len >= 5) {      /* 手机端错误串：00 len 01 ascii */
                    char err[128] = { 0 };
                    int cp = len - 5 < 127 ? len - 5 : 127;
                    memcpy(err, pl + 5, cp);
                    elog("对端错误：%s", err);
                }
            }
        }
        DWORD now = GetTickCount();
        /* R-01：START 后 5 秒零媒体包 → 防火墙引导（ERROR，一次性）。
         * 不做自动 netsh：改防火墙要管理员，弹 UAC 的体验比一句可复制的
         * 命令差得多。命令里带本 exe 完整路径，复制即可用。 */
        /* R-38w：旁路通道开着但对方从不发探针 → 一次 WARN（不刷屏）。
         * 官方客户端不会发探针，这是预期现象，只是让用户知道"回执没开"。 */
        if (g_side != INVALID_SOCKET && g_echo_state != 1 && !g_echo_warned &&
            (int)(now - g_start_at) > 10000) {
            g_echo_warned = 1;
            g_echo_state = 2;
            EnterCriticalSection(&g_cs);
            g_st.echo_state = 2;
            LeaveCriticalSection(&g_cs);
            elog_warn("对端未回执（可能是原厂客户端或防火墙拦截）；录音不受影响，"
                      "对账通道 UDP %d 已就绪", g_st.media_port + 1);
        }
        if (!g_got_media && !g_fw_hint_shown &&
            (int)(now - g_start_at) > 5000) {
            if (g_transport >= 2) {
                g_fw_hint_shown = 1;
                elog_err("已连接但没有音频：检查手机采集权限、后台运行及USB/蓝牙链路");
            } else {
            wchar_t exe[MAX_PATH];
            char exeA[MAX_PATH * 3];
            g_fw_hint_shown = 1;
            GetModuleFileNameW(NULL, exe, MAX_PATH);
            exe[MAX_PATH - 1] = 0;
            WideCharToMultiByte(CP_UTF8, 0, exe, -1, exeA, sizeof(exeA), NULL, NULL);
            elog_err("已连接但未收到音频数据。最常见原因：Windows 防火墙拦截了"
                     "本程序的 UDP 入站。可执行下方命令放行（需管理员）。");
            elog("netsh advfirewall firewall add rule name=\"MicLink\" dir=in "
                 "action=allow program=\"%s\" enable=yes", exeA);
            }
        }
        if (now - last_hb >= 1000) {              /* 105 心跳 1s/次；对端无应答 */
            msg_send(105, NULL, 0);
            last_hb = now;
        }
        if (now - last_stat >= 1000) {
            uint64_t b;
            EnterCriticalSection(&g_cs);
            b = g_st.bytes;
            g_st.kbps = (int)((b - prev_bytes) * 8 / 1000);
            g_st.peak *= 0.72f;                   /* release 慢 */
            if (g_st.peak < 0.002f) g_st.peak = 0;
            LeaveCriticalSection(&g_cs);
            prev_bytes = b;
            last_stat = now;
        }
    }

out:
    monitor_flush();
    if (g_tcp != INVALID_SOCKET) {
        msg_send(104, NULL, 0);                   /* 主动停止推流 */
        closesocket(g_tcp); g_tcp = INVALID_SOCKET;
    }
    transport_cleanup();
    if (g_udp != INVALID_SOCKET) { closesocket(g_udp); g_udp = INVALID_SOCKET; }
    if (g_side != INVALID_SOCKET) { closesocket(g_side); g_side = INVALID_SOCKET; }
    WSACleanup();
    devio_stop();
    if (g_st.state != ST_ERROR) g_st.state = ST_IDLE;
    return 0;
}

/* ------------------------------------------------- 媒体（UDP）端口 */
/* 0 = 随机（默认，与加这个开关之前完全一致）；1..65535 = 固定端口。
 * 非法值（<=0 或 >65535）一律按 0 处理 —— 真正的取值范围校验在
 * cli.c 的 --media-port 解析里做（那里要给出明确报错 + 退出码 1）。
 * 必须在 eng_start() 之前调用；返回实际生效值。 */
int eng_set_media_port(int port)
{
    g_media_port_req = (port > 0 && port <= 65535) ? port : 0;
    return g_media_port_req;
}

int eng_media_port(void) { return g_media_port_req; }

/* 本地增益：0..200（%），运行中可随时改（滑杆拖动即时生效）。
 * 旧实现只在 eng_start() 里赋一次值，滑杆要等下次连接才起作用。 */
int eng_set_gain_db(int db10)
{
    int hi = g_gain_ext ? GAIN_DB10_MAX_EXT : GAIN_DB10_MAX;
    int g = (db10 < -600) ? -600 : (db10 > hi) ? hi : db10;
    InterlockedExchange(&g_gain_db10, g);
    return g;
}

/* R-47 扩展增益范围：1 = 上限抬到 +14.0 dB（501%，过载/炸麦档）；
 * 0 = 回到 +6.0 dB（200%）。
 *
 * 缩回时会把当前增益**钳到新上限**（否则"关掉开关却仍在 500%"是隐性不一致，
 * 而滑杆位置又是从 dB 反算的，UI 与引擎会各说一套）。
 * 上限常量与 UI 共用 ui_draw.h 的 GAIN_DB10_MAX*，不在 engine 里另留一份。 */
int eng_set_gain_ext(int on)
{
    g_gain_ext = on ? 1 : 0;
    return eng_set_gain_db(g_gain_db10);   /* 顺手按新上限钳一次，返回实际值 */
}

int eng_gain_ext(void) { return g_gain_ext; }

/* --------------------------------------------------------- 对外 API */
void eng_set_logger(LogFn fn) { g_log = fn; }

/* 不再独立 CreateFile：门控是驱动全局的，任何新句柄都会把它清零。
 * “设备可用”只看本进程持有的那个长句柄。 */
int eng_device_available(void) { return devio_is_open() && devio_handshake_ok(); }

static int cap_from_ms(int ms)
{
    if (ms <= 26) return DEV_CAP_26MS;
    if (ms <= 60) return DEV_CAP_60MS;
    return DEV_CAP_120MS;
}

int eng_start(const char *hostport, int ctrl_port, int rate_idx, int buffer_ms, int gain_db10)
{
    if (g_run) return 0;
    if (!g_cs_inited) { InitializeCriticalSection(&g_cs); g_cs_inited = 1; }

    g_rate_idx = (rate_idx >= 0 && rate_idx <= 2) ? rate_idx : 2;
    g_buf_ms = (buffer_ms == 26 || buffer_ms == 60 || buffer_ms == 120) ? buffer_ms : 60;
    eng_set_gain_db(gain_db10);

    EnterCriticalSection(&g_cs);
    memset(&g_st, 0, sizeof(g_st));
    LeaveCriticalSection(&g_cs);
    g_last_frame_at = 0;                 /* 抖动 EMA 状态随会话复位 */
    g_lat_ema = 0.0;
    /* 丢包/vol/防火墙提示的会话内状态一并复位：不清的话重连后第一帧会被
     * 当成跳号、沿用上一次会话的 vol，或者因为残留标志而永远不再提示。 */
    g_seq_valid = 0;
    g_next_seq = 0;
    g_got_media = 0;
    g_fw_hint_shown = 0;
    g_vol = 5;
    g_vol_seen = 0;
    g_vol_warn = 0;
    g_st.vol = -1;                       /* -1 = 还没收到包，UI 显示「—」 */

    g_st.state = ST_CONNECTING;
    g_st.rate = g_rate_idx == 0 ? 8000 : g_rate_idx == 1 ? 16000 : 48000;
    g_st.rate_out = DEV_RATE;
    g_st.buffer_ms = g_buf_ms;
    g_st.gain_db10 = g_gain_db10;
    /* 控制端口最后一道闸门（D-01）：上层（main.c 的输入框、cli.c 的 --port）
     * 已经先行校验并给出可读错误，走到这里还把非法值带进来，只能是有人新增
     * 调用点漏了校验 —— 明确拒绝，绝不用 htons((u_short)port) 截断后照连
     * （99999→34463、65536→0 这种「填的和连的不是一个端口」比连不上更难查）。 */
    if (ctrl_port < 1 || ctrl_port > 65535) {
        snprintf(g_st.last_error, sizeof(g_st.last_error),
                 "控制端口 %d 非法：须为 1-65535 的整数（默认 8125）", ctrl_port);
        g_st.state = ST_ERROR;
        elog("%s", g_st.last_error);
        return 0;
    }
    snprintf(g_st.peer, sizeof(g_st.peer), "%s:%d", hostport, ctrl_port);
    snprintf(g_st.session, sizeof(g_st.session), "local-%08X", (unsigned)GetTickCount());
    strcpy(g_st.codec, "Opus");

    if (!opusdec_open(g_st.rate)) {
        elog("Opus 解码器不可用（%s）：仅统计与电平，不注入音频", opusdec_source());
    } else {
        elog("Opus 解码器已就绪（%s，%d Hz → 输出 48000 Hz）", opusdec_source(), g_st.rate);
    }
    snprintf(g_st.opus_src, sizeof(g_st.opus_src), "%s", opusdec_source());

    /* 设备：复用全局唯一句柄。打开失败不阻塞连接（协议与统计照跑）。 */
    if (devio_open()) {
        int cap = cap_from_ms(g_buf_ms);
        if (devio_start(cap, g_mode)) {
            g_st.device_ok = 1;
            g_st.dev_handshake_ok = 1;
            elog("虚拟麦克风已就绪：容量 %d B（%d ms），灌音路径 %s",
                 cap, g_buf_ms, g_mode == DEV_FEED_WRITE ? "WriteFile" : "IOCTL 0x222001");
        } else {
            elog("驱动握手失败：%s", devio_last_error());
            elog("提示：2022 驱动握手失败计数没有复位点，需重启电脑或重载驱动后重试");
        }
    } else {
        elog("未检测到 \\\\.\\WOMic：%s（可点“安装驱动”）", devio_last_error());
    }

    transport_prepare();
    g_run = 1;
    g_thread = (HANDLE)_beginthreadex(NULL, 0, worker, (void *)(intptr_t)g_rate_idx, 0, NULL);
    if (!g_thread) { g_run = 0; return 0; }
    return 1;
}

void eng_stop(void)
{
    if (!g_run) return;
    InterlockedExchange(&g_run, 0);
    transport_cancel();
    if (g_tcp != INVALID_SOCKET) shutdown(g_tcp, SD_BOTH);
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    devio_stop();
    opusdec_close();
    EnterCriticalSection(&g_cs);
    g_st.state = ST_IDLE;
    g_st.level = 0;
    g_st.peak = 0;
    g_st.db = -96.0f;
    g_st.silent_frames = 0;
    LeaveCriticalSection(&g_cs);
}

/* 回收自行结束的会话（见 app.h 的说明）。
 *
 * 与 eng_stop() 的区别只有后半段：不动 g_st.state。
 * 连接失败时 worker 已经把 state 置成 ST_ERROR 就退出了，若这里也跟着
 * 复位成 ST_IDLE，状态栏的「连接错误」徽标会瞬间变回「未连接」——
 * 那就把刚修好的「不静默」又改回去了。
 *
 * 只做资源回收，不动任何状态字段，因此幂等：
 *   · g_run 已清（回收过 / 用户主动断过）→ 直接返回；
 *   · 线程还活着（重连中/推流中）→ 直接返回，绝不打断它。 */
void eng_reap(void)
{
    if (!g_run) return;
    if (g_thread && WaitForSingleObject(g_thread, 0) == WAIT_TIMEOUT) return;
    InterlockedExchange(&g_run, 0);
    if (g_thread) {
        CloseHandle(g_thread);      /* 线程自己已经退出，无需再 Wait */
        g_thread = NULL;
    }
    devio_stop();
    opusdec_close();
}

void eng_snapshot(EngineStats *out)
{
    if (!g_cs_inited) { memset(out, 0, sizeof(*out)); return; }
    EnterCriticalSection(&g_cs);
    *out = g_st;
    LeaveCriticalSection(&g_cs);
}

/* R-39 监听开关：on=1 打开（失败返回 0，UI 回弹并记 WARN）；on=0 立刻停。 */
int eng_set_monitor(int on) { return monitor_enable(on); }

/* D-06 静音：只截输出（灌音前零填充），不碰电平统计，
 * 所以电平表照常显示输入电平、静音不会「变红」。 */
void eng_set_mute(int on) { InterlockedExchange(&g_mute, on ? 1 : 0); }

/* 供 CLI/GUI 选择灌音路径（默认 IOCTL，见 devio.c 顶部注释） */
void eng_set_feed_mode(int mode) { g_mode = (DevFeedMode)mode; }
