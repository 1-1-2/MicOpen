/* app.h — MicLink 客户端公共定义
 *
 * 设计约束（来自需求）：
 *  1. 体积几十 KB 级：纯 Win32 + GDI/GDI+（系统自带 dll，动态加载），静态链接 CRT，
 *     不引入 Qt/Electron/.NET，任何第三方解码器都做成可选动态加载。
 *  2. 零额外运行库：只用 kernel32/user32/gdi32/ws2_32/shell32/dwmapi/gdiplus
 *     这类每台 Windows 都有的系统组件；不依赖 VC++ Redistributable。
 *  3. 驱动相关的硬约束见 docs/protocol/driver-whitepaper.md §9，违反任一条都会「静悄悄失败」。
 */
#ifndef WOMIC_APP_H
#define WOMIC_APP_H

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>

/* ------------------------------------------------------------------ 版本 */
/* 产品名统一 MicLink（D-01：截图不得出现任何参照产品名）。
 * 注意：驱动相关字符串（Root\WOMic、\\.\WOMic、WO Mic 字样）不随产品名改动，
 * 那是第三方驱动的既有身份，见 docs/protocol/driver-whitepaper.md §9。 */
#define APP_NAME        "MicLink"
#define APP_VERSION     "1.0.0"
#define APP_VERSION_Q   1, 0, 0, 0

/* 内置驱动归属声明（--help / --status 输出与 NOTICE.md 保持一致） */
#define DRV_OWNER       "Wolicheng (WO Mic)"
#define DRV_VERSION     "2.1.0.0"
#define DRV_SOURCE_URL  "https://www.wolicheng.com/womic/"

/* ------------------------------------------------------------------ 主题 */
/* 令牌全部出自《产品设计手册》§4.1–§4.3 与《定稿规范》§0（定稿图即验收）：
 *   text-1/2/3、bg-app/card/sunken、border、accent/ok/warn/err
 *   on_accent  = accent 上的文字（深色近黑 / 浅色白）
 *   ok_12/acc_12/err_12 = 徽标底 = 语义色 12% 混合 bg-card（C-11 派生，禁止手调）
 *   pill       = pill 底 = accent 8% 混合 bg-card（D-08/D-09 派生）
 * 旧字段名保留兼容映射：text=text-1、text_dim=text-2、text_mute=text-3、
 *   stroke=border、bg=bg-app、card=bg-card、sunken=bg-sunken。
 */
typedef struct {
    COLORREF bg;          /* bg-app 窗口底色 */
    COLORREF bg2;         /* 渐变次色（定稿为纯色，= bg） */
    COLORREF card;        /* bg-card 卡片 */
    COLORREF card_hi;     /* 卡片悬停/选中提亮 */
    COLORREF sunken;      /* bg-sunken 轨道·日志底·状态栏 */
    COLORREF stroke;      /* border 1px 描边 */
    COLORREF text;        /* text-1 主文字 */
    COLORREF text_dim;    /* text-2 次文字 */
    COLORREF text_mute;   /* text-3 弱文字/禁用 */
    COLORREF accent;      /* accent primary 底、选中分段、滑杆填充 */
    COLORREF accent_dim;  /* 强调色弱化（= acc_12） */
    COLORREF on_accent;   /* accent 上的文字 */
    COLORREF ok;          /* ok 已连接、驱动就绪、健康区间 */
    COLORREF warn;        /* warn 临界（抖动 80–200ms / 丢包 1%–5% / >-3 dBFS） */
    COLORREF err;         /* err 断开、驱动未就绪、破坏性操作 */
    COLORREF ok_12;       /* 徽标底 = ok 12% 混合 bg-card */
    COLORREF warn_12;     /* 徽标底 = warn 12% 混合 bg-card（同公式派生） */
    COLORREF acc_12;      /* 徽标底 = accent 12% 混合 bg-card */
    COLORREF err_12;      /* 徽标底 = err 12% 混合 bg-card */
    COLORREF pill;        /* pill 底 = accent 8% 混合 bg-card */
    COLORREF meter_off;   /* 电平表未点亮段（= sunken） */
    int      dark;
} Theme;

extern Theme g_th;
void theme_apply(int dark);

/* ------------------------------------------------------- 架构与驱动包 */
typedef enum {
    DRVARCH_UNKNOWN = 0,
    DRVARCH_INTEL   = 1,   /* PROCESSOR_ARCHITECTURE_INTEL  → womic.* */
    DRVARCH_AMD64   = 2,   /* PROCESSOR_ARCHITECTURE_AMD64 → womic_1.* */
    DRVARCH_ARM64   = 3    /* PROCESSOR_ARCHITECTURE_ARM64 → womic_2.* */
} DrvArch;

int  drvpkg_detect_arch(void);
const wchar_t *drvpkg_arch_dir(int arch);    /* "x86" / "amd64" / "arm64" */
const wchar_t *drvpkg_arch_label(int arch);  /* 人类可读，用于 --status */
int  drvpkg_available(int arch);             /* 内置包里有没有这一档 */
int  drvpkg_extract(int arch, const wchar_t *out_dir, int force);
int  drvpkg_extract_dir(const wchar_t *src_dir, const wchar_t *out_dir, int force);
const char *drvpkg_last_error(void);

/* ------------------------------------------------------------- 驱动状态 */
typedef enum {
    DRV_UNKNOWN = 0,
    DRV_NOT_INSTALLED,
    DRV_READY,          /* L1+L2 通过 */
    DRV_NEED_REBOOT     /* INF 已装但设备起不来 */
} DrvState;

typedef struct {
    DrvState state;
    int      arch;
    int      handle_ok;             /* L1：CreateFile \\.\WOMic 成功 */
    int      inf_ok;                /* L2：InfPath 非空 */
    int      sys_on_disk;           /* L3：System32\drivers\womic.sys 落盘 */
    wchar_t  inf_path[MAX_PATH];    /* oemNN.inf */
    wchar_t  inf_full[MAX_PATH];    /* 系统 INF 目录中的 oemNN.inf */
    wchar_t  provider[64];
    wchar_t  version[32];
    wchar_t  friendly[64];          /* 设备描述名（DriverDesc，D-09 pill 用真实值） */
    wchar_t  sys_path[MAX_PATH];
    char     detail[256];           /* 最近一次操作的说明/错误 */
} DrvStatus;

void   drv_set_external_pack(const wchar_t *dir);   /* --driver-pack */
const wchar_t *drv_external_pack(void);
int    drv_detect(DrvStatus *out, int probe_handle);
int    drv_install(void);
int    drv_uninstall(void);
int    drv_is_elevated(void);
const char *drv_last_error(void);
const wchar_t *drv_last_error_w(void);

/* ------------------------------------------------------------- 设备 IO */
/* 全部 IOCTL 编号来自 docs/protocol/driver-whitepaper.md §4（2022 驱动 2.1.0.0）。
 * 旧的 0x222000 是 2017 驱动的编号，在 2022 驱动上 51/51 失败，已删除。 */
#define DEV_CAP_MIN     1           /* 0x222014 无下界保护，0 会永久静音 */
#define DEV_CAP_MAX     0x2D00      /* 11520 B = 120 ms @48k/16/mono */
#define DEV_CAP_26MS    0x0A00
#define DEV_CAP_60MS    0x1680
#define DEV_CAP_120MS   0x2D00

typedef enum { DEV_FEED_IOCTL = 0, DEV_FEED_WRITE = 1 } DevFeedMode;

int  devio_open(void);              /* 全进程只调用一次；句柄长期持有 */
void devio_close(void);
int  devio_is_open(void);
int  devio_probe(int *val);         /* 0x22200C，恒 3；无副作用，可做存在性探测 */
int  devio_start(int capacity, DevFeedMode mode);   /* 完整握手 + 容量 + 启动 */
int  devio_stop(void);
int  devio_write(const void *pcm, int bytes);       /* 成功返回写入字节数 */
int  devio_rehandshake(void);       /* 门控被别的进程清零时的看门狗恢复 */
int  devio_handshake_ok(void);
int  devio_capacity(void);
int  devio_written_bytes(void);
const char *devio_last_error(void);

/* ------------------------------------------------------------- Opus */
int  opusdec_open(int rate);
void opusdec_close(void);
int  opusdec_ready(void);
int  opusdec_decode(const unsigned char *pkt, int len, int16_t *out, int cap, int fec);
const char *opusdec_source(void);   /* "static" / "opus.dll" / "none" */

/* ------------------------------------------------------------- 引擎 */
typedef enum {
    ST_IDLE = 0, ST_CONNECTING, ST_HANDSHAKE, ST_STREAMING, ST_ERROR
} EngState;

typedef struct {
    EngState state;
    char     peer[64];        /* 对端地址:端口 */
    char     session[32];     /* 会话标识（对端返回/本地生成） */
    char     codec[16];
    int      rate;            /* 协商采样率（8k/16k/48k） */
    int      rate_out;        /* 灌给驱动的采样率（恒 48000） */
    int      buffer_ms;       /* 环形缓冲档位 26/60/120 */
    int      gain_db10;       /* 本地增益，单位 0.1 dB（-600..+60）；% 只在 UI 反算显示 */
    uint64_t frames;          /* 收到帧数 */
    uint64_t bytes;           /* 收到字节 */
    int      media_port;      /* 102 里上报的 UDP 媒体端口（0 = 尚未绑定） */
    int      media_fixed;     /* 1 = --media-port 指定；0 = 系统随机分配 */
    uint32_t lost;            /* 序号缺口估算 */
    uint32_t last_seq;
    uint32_t expected;        /* 期望收到的帧总数（已收 + 跳号），丢包率的分母 */
    int      vol;             /* 协议音量字节 0..9（5=原样，0=静音）；-1 = 还没收到包 */
    float    level;           /* 0..1 平滑电平 */
    float    peak;            /* 0..1 峰值保持 */
    float    db;              /* dBFS */
    int      kbps;            /* 瞬时码率 */
    /* 注意口径：这是 UDP 帧**到达间隔**的 EMA，是网络抖动（jitter），
     * 不是端到端延迟（编码+网络+解码+驱动缓冲都算上才是延迟）。
     * 以前标成「延迟」是错名，UI 一律显示「抖动」。 */
    int      jitter_ms;
    uint64_t injected;        /* 已注入驱动的 PCM 字节 */
    int      device_ok;       /* 虚拟麦克风可写（复用同一句柄，不额外 CreateFile） */
    int      dev_handshake_ok;/* 驱动握手成功 */
    int      silent_frames;   /* 连续静音帧计数（VoiceMeter 转“无信号”用） */
    char     opus_src[16];    /* static / dll / none */
    char     last_error[160];
    DWORD    started_at;
    /* R-38w 对账通道（旁路 UDP media_port+1）：
     *   0 未启用（side socket bind 失败 / 未协商）
     *   1 正常（收到过探针并已回 report）
     *   2 对端未回执（我们开着，但对方一直不发探针） */
    int      echo_state;
    int      echo_probes;     /* 收到的探针数 */
} EngineStats;

/* 引擎线程生命周期 */
int  eng_start(const char *host, int ctrl_port, int rate_idx, int buffer_ms, int gain);
void eng_stop(void);
/* 回收「worker 已自行结束」的会话资源（线程句柄 / 设备 / 解码器），
 * 但**保留** g_st.state —— 状态栏要靠它继续显示「连接错误」。
 * 为什么需要它：worker 因连接失败自己退出时不会清 g_run，若没人回收，
 * 下一次 eng_start() 会在 `if (g_run) return 0;` 处立刻失败（表现为
 * 「启动引擎失败」且无任何原因），用户必须重启程序才能重连。
 * 幂等：线程仍在跑、或已回收过，都直接返回。 */
void eng_reap(void);
void eng_snapshot(EngineStats *out);   /* 线程安全快照 */
int  eng_device_available(void);       /* 不再独立开句柄：返回 devio_is_open() */

/* 静音（D-06 静音行）：真实生效 —— 灌入驱动前把 PCM 零填充，
 * 电平表照常显示输入电平（静音只截输出，不变红）。 */
void eng_set_mute(int on);
/* 媒体（UDP）端口：0 = 由系统随机分配（默认，向后兼容）；1..65535 = 固定端口。
 * 跨网络/过防火墙时必须固定，否则手机端无从放行。必须在 eng_start 之前调用。 */
int  eng_set_media_port(int port);
int  eng_media_port(void);
/* ---- 增益常量（R-31/R-47）：engine 与 UI 共用，只此一份 ----
 * 本地增益：单位 0.1 dB，低限恒 -600（-60 dB = 0.001 倍，不是静音）。
 * 上限随「扩展增益」开关在 +6.0 dB（200%）与 +14.0 dB（501%）之间切换，
 * 轨道 pos(0..1000) ↔ dB 线性，低端恒为 -60 dB，只把高端拉开：
 *   常态  dB = -60 + 66*pos/1000（跨度 660）
 *   扩展  dB = -60 + 74*pos/1000（跨度 740）
 * ⚠ 跨度 = 上限 − 下限：+140 对应 **740**，写成 800 会让拉满变成 1000%
 * （20 dB），与 GAIN_DB10_MAX_EXT 对不上，钳制会悄悄吃掉最后 60。
 *
 * 扩展档是刻意留的"炸麦"档：>100% 对满量程信号必然削波，样本侧在
 * engine.c 的 apply_gain() 里做 int16 **饱和钳制**（不是回绕），
 * 所以听到的是"削平的响"，不是整数环绕的爆音。
 * 静音只由 eng_set_mute() 表达，不再用"增益 0"表示。 */
#define GAIN_DB10_MIN      (-600)
#define GAIN_DB10_MAX      (60)     /* 常态上限：+6.0 dB = 200% */
#define GAIN_DB10_MAX_EXT  (140)    /* 扩展上限：+14.0 dB = 501% */
#define GAIN_SPAN_NORMAL   (660)    /* -600 → +60  */
#define GAIN_SPAN_EXT      (740)    /* -600 → +140 */

/* 设置本地增益（0.1 dB）。按当前范围上限钳制，返回实际值。 */
int  eng_set_gain_db(int db10);
/* R-47 扩展增益范围：1 = 上限抬到 +14 dB；0 = 回到 +6 dB。
 * 缩回时会把当前增益钳到新上限，返回钳后的实际值。 */
int  eng_set_gain_ext(int on);
int  eng_gain_ext(void);
/* 异步请求监听启停；实际设备状态见 monitor_snapshot。 */
int eng_set_monitor(int on);
void eng_set_feed_mode(int mode);      /* DEV_FEED_IOCTL（默认）/ DEV_FEED_WRITE */

/* 日志回调：引擎线程调用，UI 通过 PostMessage 转投 */
/* kind：0 INFO / 1 ERROR / 2 OK（与 ui_log_add 的分级一致，「复制全部」同收） */
typedef void (*LogFn)(const char *line, int kind);
void eng_set_logger(LogFn fn);

/* 单实例回调：第二个实例的命令行被转发过来 */
typedef void (*SingleInstCb)(const wchar_t *cmdline);
typedef void (*SingleInstActivate)(void);

/* ------------------------------------------------------------- 单实例 */
/* 返回：1 = 本进程是唯一实例；0 = 已有实例在跑（命令行已转发，调用方应退出） */
int  singleinst_init(SingleInstCb cb, SingleInstActivate act);
void singleinst_shutdown(void);

/* ------------------------------------------------------------- CLI */
typedef struct {
    const wchar_t *host_pos;  /* --host 的原始（宽字符）值 */
    int         port;         /* --port，默认 8125 */
    int         media_port;   /* --media-port，0 = 随机分配（默认） */
    int         rate;         /* --rate 48000/16000/8000 → rate_idx 2/1/0 */
    int         rate_idx;
    int         buffer_ms;    /* --buffer 26|60|120 */
    int         gain;         /* --gain 0..200 */
    int         connect;      /* --connect */
    int         status;       /* --status */
    int         install;      /* --install-driver */
    int         uninstall;    /* --uninstall-driver */
    int         no_gui;       /* --no-gui */
    int         help;         /* --help */
    int         version;      /* --version */
    int         dark;         /* --dark */
    int         light;        /* --light */
    int         theme_set;
    int         feed_write;   /* --feed write：灌音走 WriteFile 而不是 IOCTL */
    int         console;      /* 输出是否需要走 stdout（已挂上控制台） */
    int         gain_table;   /* --gain-table：打印增益映射表后退出（CI/回归用） */
    int         gain_ext;     /* --gain-ext：配合 --gain-table 打印扩展量程表（R-47） */
    const wchar_t *driver_pack;  /* --driver-pack <dir> */
    int         argc;
    wchar_t   **argv;
} CliArgs;

#define CLI_EXIT_OK        0
#define CLI_EXIT_PARAM     1
#define CLI_EXIT_CONNECT   2
#define CLI_EXIT_DEVUNAVA  3
#define CLI_EXIT_DEVINST   4

int  cli_parse(CliArgs *a, int argc, wchar_t **argv);
const char *cli_parse_error(void);    /* cli_parse 返回 0 时的具体原因（不是 NULL） */
void cli_attach_console(void);        /* -mwindows 下的输出方案 */
void cli_detach_console(void);
int  cli_run(CliArgs *a);             /* 返回 1 表示需要退出进程（code 见 cli_exit_code） */
int  cli_exit_code(void);
void cli_print_help(void);
const char *cli_help_text(void);      /* GUI 说明书窗口复用同一份文本 */
void cli_set_defaults(CliArgs *a);

/* ------------------------------------------------------------- 界面 */
#define LOG_LINES 200
#define LOG_LEN   220
#define HIST_BARS 48          /* 最近 10 秒电平历史柱数（约 208ms/柱，D-08） */

typedef struct { char text[LOG_LEN]; wchar_t ts[16]; int kind; } LogLine;

typedef struct {
    HWND  hwnd;
    HWND  edit_host, edit_port, edit_media;
    HBRUSH br_card;
    HBRUSH br_sunken;           /* 输入框底色刷（= bg-sunken，随主题重建） */
    UINT  dpi;
    int   w, h;                /* 客户区逻辑尺寸 */
    int   scroll;              /* 纵向滚动偏移 */
    int   content_h;           /* 内容总高（布局后得出） */
    int   hover, pressed;
    int   drag_slider;
    int   monitor;             /* R-39 监听开关（与静音同形态） */
    int   autorec;             /* R-27 自动重连开关（默认关） */
    int   rc_count;            /* 本次断线后已重试次数（成功后归零） */
    int   rc_armed;            /* 1 = 上次会话非用户主动断开，可以自动重连 */
    DWORD rc_next;             /* 下一次重试的时刻（GetTickCount） */
    int   hover_fade;          /* D-10：hover 描边过渡 0..1000（T1 120ms） */
    int   hover_fade_id;       /* 正在过渡的命中 id */
    int   seg_rate;            /* 0 48k 1 16k 2 8k */
    int   buf_idx;             /* 0 26ms 1 60ms 2 120ms */
    int   gain_db10;           /* 本地增益，0.1 dB（低限 -600；上限随 gain_ext 切换） */
    int   gain_ext;            /* R-47 扩展增益范围：1 = 上限 +14 dB（501%，炸麦档） */
    int   dark;
    int   running;
    int   transport;           /* 0 Wi-Fi（默认且唯一已实现）/1 Wi-Fi 直连 /2 蓝牙 /3 USB */
    int   mute;                /* 静音开关（D-06，eng_set_mute 真实生效） */
    int   exp_audio, exp_stats, exp_log;      /* 折叠面板展开标志（D-11 默认收起） */
    float hist[HIST_BARS];     /* 最近 10 秒电平历史（0=旧 … hist_count-1=新） */
    int   hist_count;
    DWORD hist_last;           /* 上次历史采样时刻 */
    DWORD disc_at;             /* 断开时刻（>10s 清空历史，D-08） */
    int   stat_half;           /* 500ms 节拍计数（D-14） */
    int   conn_count;          /* 会话建立次数（重连次数 = conn_count-1，D-12） */
    int   kbps_peak, jit_peak; /* 码率/抖动峰值（D-12 当前/峰值小字） */
    float lost_peak;           /* 丢包峰值（%） */
    LogLine logs[LOG_LINES];
    int   log_head, log_count;
    EngineStats st;            /* 每帧快照（电平/环形/横条用） */
    EngineStats st_disp;       /* 500ms 快照（状态栏与面板摘要用，D-14） */
    DrvStatus drv;
    int   tick;
} Ui;

extern Ui g_ui;

/* 命中 id */
enum {
    ID_NONE = 0,
    ID_MIN, ID_MAX, ID_CLOSE,
    ID_THEME_LIGHT, ID_THEME_DARK,
    ID_CONN,
    ID_TRANS0, ID_TRANS1, ID_TRANS2, ID_TRANS3,
    ID_MUTE,
    ID_MONITOR, ID_AUTOREC, ID_GAINEXT,
    ID_SLIDER,
    ID_INSTALL, ID_OPENSET, ID_UNINSTALL, ID_HELP,
    ID_EXP_AUDIO, ID_EXP_STATS, ID_EXP_LOG,
    ID_RATE0, ID_RATE1, ID_RATE2,
    ID_BUF0, ID_BUF1, ID_BUF2,
    ID_COPYLOG,
    ID_MAX_
};

/* 界面模块（详见各 .h） */
void ui_log_add(const char *s, int kind);
int  ui_log_count(void);
const LogLine *ui_log_get(int i);   /* i=0 为最新 */

void ui_layout_compute(void);
void ui_draw_all(HDC dc);
int  ui_hit_test(int x, int y);
void ui_on_click(int id, int x, int y);
void ui_on_release(void);
void ui_toggle_theme(void);
void ui_show_help(HINSTANCE hInst, HWND parent);
int  ui_help_handle_key(WPARAM vk);

#endif /* WOMIC_APP_H */
