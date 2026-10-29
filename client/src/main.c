/* main.c — 薄壳：窗口创建 + 事件分发 + 模块装配
 *
 * 绘制细节在 ui_draw / ui_layout / ui_voicemeter / ui_help；
 * 协议与灌音在 engine / devio / opusdec；
 * 驱动检测安装在 drvinst / drvpkg；命令行在 cli；单实例在 singleinst。
 * 本文件只负责把它们串起来，不掺业务逻辑。
 */
#define WIN32_LEAN_AND_MEAN
#include "app.h"
#include "transport.h"
#include "monitor.h"
#include "ui_draw.h"
#include "ui_layout.h"
#include "ui_voicemeter.h"
#include "ui_help.h"
#include "cli.h"
#include "drvinst.h"
#include "drvbridge.h"
#include "devio.h"
#include "opusdec.h"
#include "singleinst.h"
#include <windowsx.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Ui g_ui;
static int g_dirty;              /* 有交互产生、需要重绘 */
static CliArgs g_args;
/* The selector owns no audio session. Its cancel token is separate from the
 * engine token so switching modes cannot cancel a later connection. */
typedef struct {
    HANDLE thread;
    volatile LONG cancel;
    HWND owner;
    POINT point;
    MicDirectTheme theme;
    MicDirectDevice device;
    char error[512];
    int result, discard;
} DirectSelection;
static DirectSelection *g_direct_picker;
static MicDirectDevice g_direct_choice;
static wchar_t g_hosts[4][64] = { L"192.168.1.100", L"", L"", L"" };
static MicDirectTheme direct_ui_theme(void)
{
    MicDirectTheme t = {g_th.card,g_th.sunken,g_th.text,g_th.text_dim,g_th.accent,g_ui.dark};
    return t;
}
static DWORD WINAPI pick_direct_worker(void *arg)
{
    DirectSelection *work=(DirectSelection *)arg;
    work->result=transport_pick_direct(&work->cancel,work->owner,work->point.x,work->point.y,
                                      &work->theme,&work->device,work->error,sizeof(work->error));
    /* Results are consumed only after thread completion by the main timer. */
    return 0;
}
static void cancel_direct_picker(void)
{
    if (g_direct_picker) {
        g_direct_picker->discard=1;
        InterlockedExchange(&g_direct_picker->cancel,1);
    }
}
static void start_direct_picker(void)
{
    DirectSelection *work;
    if (g_direct_picker || g_ui.running) return;
    work=(DirectSelection *)calloc(1,sizeof(*work));
    if (!work) return;
    work->owner=g_ui.hwnd;GetCursorPos(&work->point);work->theme=direct_ui_theme();
    work->thread=CreateThread(NULL,0,pick_direct_worker,work,0,NULL);
    if (!work->thread) {free(work);ui_log_add("无法启动无线直连搜索",1);return;}
    g_direct_picker=work;
    ui_log_add("正在搜索附近的直连设备，点击手机名称完成选择",0);
}
static void reap_direct_picker(void)
{
    DirectSelection *work=g_direct_picker;
    if (!work || WaitForSingleObject(work->thread,0)!=WAIT_OBJECT_0) return;
    if (!work->discard && g_ui.transport==1 && !g_ui.running) {
        if (work->result) {
            g_direct_choice=work->device;
            SetWindowTextW(g_ui.edit_host,g_direct_choice.name);
            ui_log_add("已选择直连手机，点击连接；再次点击无线直连可重新选择",0);
        } else if (work->error[0]) ui_log_add(work->error,1);
        g_dirty=1;
    }
    CloseHandle(work->thread);free(work);g_direct_picker=NULL;
}


/* ---------------------------------------------------------------- 日志 */
static void eng_log_cb(const char *line, int kind)
{
    /* kind 随行携带（0 INFO / 1 ERROR / 2 OK），PostMessage 只有一个
     * 自定义消息槽，把 kind 塞进 lParam。 */
    char *copy = (char *)malloc(LOG_LEN);
    if (!copy) return;
    strncpy(copy, line, LOG_LEN - 1);
    copy[LOG_LEN - 1] = 0;
    if (!PostMessageW(g_ui.hwnd, WM_APP + 1, (WPARAM)copy, (LPARAM)kind)) free(copy);
}

/* ---------------------------------------------------------------- 主题 */
void ui_toggle_theme(void)
{
    g_ui.dark = !g_ui.dark;
    theme_apply(g_ui.dark);
    if (g_ui.br_card) DeleteObject(g_ui.br_card);
    g_ui.br_card = CreateSolidBrush(g_th.card);
    if (g_ui.br_sunken) DeleteObject(g_ui.br_sunken);
    g_ui.br_sunken = CreateSolidBrush(g_th.sunken);
    /* 字体与主题无关（缓存键里没有颜色），不重建：编辑框还持有旧 HFONT，
     * DeleteObject 会让它们悬挂。只有 DPI 变化时才 ui_fonts_reset()。 */
    monitor_theme_changed();
    g_dirty = 1;
    if (g_ui.hwnd) RedrawWindow(g_ui.hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

/* ---------------------------------------------------------------- 连接 */
static int rate_idx_of(int seg) { return seg == 0 ? 2 : seg == 1 ? 1 : 0; }
static int buf_ms_of(int idx) { return idx == 0 ? 26 : idx == 1 ? 60 : 120; }

/* 读输入框文本，且**不允许读到被截断的内容**（D-01）。
 * GetWindowTextW 到不了的长度会静默截断，截断后再解析就会出现
 * 「框里显示 0000000000008125，实际连 812」这种填/连不一致 —— 这正是
 * 不静默纪律要挡的。所以先用 GetWindowTextLengthW 判长，太长就交给
 * 调用方按「非法」处理，而不是悄悄用前半段。
 * 返回值：1 = 正常读到（可能为空串）；0 = 文本过长（buf 已被置空）。 */
static int read_edit_text(HWND h, wchar_t *buf, int cap)
{
    int n;
    if (!buf || cap <= 0) return 0;
    buf[0] = 0;
    if (!h) return 1;                          /* 控件还没建好：当空处理 */
    n = GetWindowTextLengthW(h);
    if (n >= cap - 1) return 0;                /* 过长：不读，避免半截数字 */
    GetWindowTextW(h, buf, cap);
    return 1;
}

/* 发起一次会话。manual=1 是用户点「连接」（成功/失败都会"武装"自动重连）；
 * manual=0 是 R-27 的自动重试（不重复刷日志、不重置重试计数）。
 * 返回 1 = 引擎线程已启动。 */
static int connect_session(int manual)
{
    if (g_ui.running) return 0;
    if (g_ui.transport==1 && !g_args.connect && !g_direct_choice.id[0]) {
        start_direct_picker();
        ui_log_add("请先在搜索列表中选择手机，再点击连接",0);
        return 0;
    }
    cancel_direct_picker();
    {
        wchar_t host[64] = { 0 }, port[32] = { 0 }, mp[32] = { 0 };
        char hostA[96];
        int cp = 8125, pr, media = 0, port_defaulted = 0;
        GetWindowTextW(g_ui.edit_host, host, 64);
        WideCharToMultiByte(CP_UTF8, 0, host, -1, hostA, sizeof(hostA), NULL, NULL);
        if (g_ui.transport == 1 || (!hostA[0] && g_ui.transport == 3)) strcpy(hostA, "auto");
        if (!hostA[0]) {
            ui_log_add("请先输入手机地址", 1);
            g_dirty = 1;
            return 0;
        }
        /* 控制端口（D-01）：空 = 默认 8125 但必须明示；非空且非法 = 明确拒绝。
         * 这里不再有 `_wtoi` + `cp<=0 → 8125` 的静默回退，也不让 engine 里的
         * htons((u_short)port) 截断兜底 —— 99999 变 34463、65536 变 0 那种
         * 「填的和连的不是一个端口」正是本缺陷要消灭的。 */
        if (!read_edit_text(g_ui.edit_port, port, 32)) {
            ui_log_add("控制端口须为 1-65535 的整数（默认 8125）", 1);
            g_dirty = 1;
            return 0;
        }
        pr = cli_parse_port(port, &cp);
        if (pr == 0) {
            cp = 8125;
            port_defaulted = 1;       /* 空=默认可以，但下面的日志必须明示 */
        } else if (pr < 0) {
            ui_log_add("控制端口须为 1-65535 的整数（默认 8125）", 1);
            g_dirty = 1;
            return 0;
        }
        /* 媒体端口：留空 = 随机（默认）；填了就固定，非法值当场拒绝。
         * 与控制端口共用同一个解析器（cli_parse_port），两种口径从此不会漂移：
         *   · 空 → 0（随机分配，与原行为一致）
         *   · 1..65535 → 固定
         *   · 其他（0/负数/非数字/超范围/溢出/尾随垃圾/过长）→ 拒绝 */
        if (g_ui.edit_media) {
            int mv = 0, mpr;
            if (!read_edit_text(g_ui.edit_media, mp, 32)) {
                ui_log_add("媒体端口须留空或填 1..65535（留空=随机分配）", 1);
                g_dirty = 1;
                return 0;
            }
            mpr = cli_parse_port(mp, &mv);
            if (mpr < 0) {
                ui_log_add("媒体端口须留空或填 1..65535（留空=随机分配）", 1);
                g_dirty = 1;
                return 0;
            }
            media = (mpr == 1) ? mv : 0;
        }
        /* 所有校验都过了才说「用了默认端口」：否则被媒体端口拦下时会留下
         * 一行让人误以为连接已发起的日志。 */
        if (port_defaulted && manual) ui_log_add("未填控制端口，使用默认 8125", 0);
        /* 驱动未就绪时 GUI 主按钮已置灰（D-04）；键盘/CLI 路径走到这里时
         * 保留既有的「只做协议与统计、不出声」诊断模式，并把话说清楚。 */
        if (g_ui.drv.state != DRV_READY && manual)
            ui_log_add("提示：驱动未就绪，将只做协议与统计，不出声", 0);
        if (g_ui.transport==1) {
            MicDirectTheme theme=direct_ui_theme();
            transport_select_direct(g_direct_choice.id[0]?&g_direct_choice:NULL,g_ui.hwnd,&theme);
        }
        eng_set_transport(g_ui.transport);
        eng_set_media_port(media);
        g_ui.running = eng_start(hostA, cp, rate_idx_of(g_ui.seg_rate),
                                 buf_ms_of(g_ui.buf_idx), g_ui.gain_db10);
        if (g_ui.running) {
            /* 新会话：计数 +1，历史与峰值归零（D-08/D-12） */
            g_ui.conn_count++;
            g_ui.hist_count = 0;
            g_ui.kbps_peak = g_ui.jit_peak = 0;
            g_ui.lost_peak = 0.0f;
            g_ui.rc_next = 0;                  /* 计数在真正进入推流时才清零，见 WM_TIMER */
            if (manual) ui_log_add("正在建立会话…", 0);
        } else if (manual) {
            /* 自动重试不进这条：0.5 s 一次会把事件面板刷爆（状态栏有计数） */
            ui_log_add("启动引擎失败", 1);
        }
        g_dirty = 1;
        return g_ui.running;
    }
}

/* 用户点「连接 / 断开连接」：这里负责"用户意图"，自动重连的武装/解除都在这里。 */
static void toggle_connection(void)
{
    /* R-40：自动重连是"进行态"，此时按钮是「停止重连」——点它就是停下来，
     * 绝不能重新发起连接（否则只会把下一次重试提前 0.5 s，用户永远停不下来）。
     * 注意必须带 rc_count>0：手动连过一次后断开、还没开始重试时 rc_count==0，
     * 那种时候按钮仍是「连接」，点击要照常发起会话，不能被这里吞掉。 */
    if (!g_ui.running && g_ui.autorec && g_ui.rc_armed && g_ui.rc_count > 0) {
        g_ui.rc_armed = 0;
        g_ui.rc_count = 0;
        g_ui.rc_next = 0;
        ui_log_add("已停止自动重连", 1);
        g_dirty = 1;
        return;
    }
    if (g_ui.running) {
        /* 用户主动断开 → 解除自动重连（R-27 明确要求：主动断开不重连） */
        g_ui.rc_armed = 0;
        g_ui.rc_count = 0;
        g_ui.rc_next = 0;
        eng_stop();
        g_ui.running = 0;
        g_ui.disc_at = GetTickCount();      /* D-08：断开时刻（10s 后清空历史） */
        ui_log_add("已断开连接", 0);
        g_dirty = 1;
        return;
    }
    if (connect_session(1)) {
        g_ui.rc_armed = 1;                 /* 从这一刻起，断线可自动重连 */
        g_ui.rc_count = 0;
        g_ui.rc_next = 0;
    }
}

/* ------------------------------------------------- 驱动安装/卸载：转发给安装器 */
/* 安装器 exe 自带 requireAdministrator manifest，双击/被拉起时会自己申请提权，
 * 应用本体继续保持 asInvoker（日常使用不弹 UAC）。因此这里不再
 * ShellExecute("runas") 重启自身，而是把同目录的 womic-drvinst.exe 拉起来。
 * 安装逻辑有且只有一份（drvinst.c 的 drv_install/uninstall，两个 exe 共用）。 */
static void request_driver_op(int install)
{
    DrvOp op = install ? DRVOP_INSTALL : DRVOP_UNINSTALL;
    if (!drvbridge_launch(op, g_args.driver_pack)) {
        ui_log_add(drvbridge_last_error(), 1);
        ui_log_add("提示：把 womic-drvinst.exe 放在本程序同一目录即可安装驱动", 0);
        g_dirty = 1;
        return;
    }
    ui_log_add(install ? "已启动驱动安装器（如需权限请同意 UAC 提示）…"
                       : "已启动驱动安装器卸载驱动…", 0);
    g_dirty = 1;
}

/* ------------------------------------------------- 打开 Windows 声音设置 */
/* 驱动就绪后用户最常做的一件事就是把默认麦克风设成虚拟设备。
 * 优先现代设置页（ms-settings:sound），失败（老系统/策略禁用）退回经典
 * 声音面板（mmsys.cpl）——两条路都是系统组件，不引入新依赖。 */
static void open_sound_settings(void)
{
    HINSTANCE r = ShellExecuteW(g_ui.hwnd, L"open", L"ms-settings:sound",
                                NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        ShellExecuteW(g_ui.hwnd, L"open", L"control.exe",
                      L"mmsys.cpl", NULL, SW_SHOWNORMAL);
    }
}

/* ---------------------------------------------------------------- 交互 */
void ui_on_click(int id, int x, int y)
{
    switch (id) {
    case ID_MIN:   ShowWindow(g_ui.hwnd, SW_MINIMIZE); return;
    case ID_MAX:
        ShowWindow(g_ui.hwnd, IsZoomed(g_ui.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        return;
    case ID_CLOSE: DestroyWindow(g_ui.hwnd); return;
    case ID_THEME_LIGHT: if (g_ui.dark) ui_toggle_theme(); return;
    case ID_THEME_DARK:  if (!g_ui.dark) ui_toggle_theme(); return;
    case ID_CONN:  toggle_connection(); return;
    case ID_INSTALL:
        request_driver_op(1);
        return;
    case ID_UNINSTALL: request_driver_op(0); return;
    case ID_OPENSET:   open_sound_settings(); return;
    case ID_HELP:  ui_show_help(GetModuleHandleW(NULL), g_ui.hwnd); return;
    case ID_MUTE:
        /* D-06 静音行：真实生效（engine 灌音前零填充），电平表照常显示输入 */
        g_ui.mute = !g_ui.mute;
        eng_set_mute(g_ui.mute);
        ui_switch_anim_set(SWI_MUTE, g_ui.mute);
        ui_log_add(g_ui.mute ? "已静音（输出截断，输入电平照常显示）"
                             : "已取消静音", 0);
        break;
    case ID_MONITOR:
        monitor_show(g_ui.hwnd);
        return;
    case ID_GAINEXT: {
        int old_gain = g_ui.gain_db10;
        /* R-47 扩展增益范围：量程上限 +6 dB(200%) ↔ +14 dB(501%)。
         * 打开 = 把轨道上半段让给过载区（炸麦效果）；关闭时引擎会把当前增益
         * 钳回 +6 dB，这里必须用返回值把 UI 的 dB 也一起钳，否则滑杆位置
         * （由 dB 反算）与实际增益各说一套。 */
        g_ui.gain_ext = !g_ui.gain_ext;
        g_ui.gain_db10 = eng_set_gain_ext(g_ui.gain_ext);
        ui_switch_anim_set(SWI_GAINEXT, g_ui.gain_ext);
        ui_log_add(g_ui.gain_ext
                       ? "扩展增益已开启：上限 +14.0 dB（501%）。高输入电平可能削波"
                       : "扩展增益已关闭：上限回到 +6.0 dB（200%）", 0);
        if (!g_ui.gain_ext && g_ui.gain_db10 != old_gain)
            ui_log_add("增益已按新上限钳制", 0);
        break;
    }
    case ID_AUTOREC:
        /* R-27：默认关；开启后断线 0.5 s 固定间隔重试，无上限、不退避。
         * R-40：关开关时若正在重连，必须把三个状态一起清掉 —— 否则
         * rc_armed/rc_count 还留着，主按钮会停在「停止重连」而实际已无重试。 */
        g_ui.autorec = !g_ui.autorec;
        if (!g_ui.autorec) {
            g_ui.rc_armed = 0;
            g_ui.rc_count = 0;
            g_ui.rc_next = 0;
        } else {
            g_ui.rc_count = 0;
            g_ui.rc_next = 0;
        }
        ui_log_add(g_ui.autorec ? "已开启自动重连（断线后 0.5 s 重试，无上限）"
                                : "已关闭自动重连", 0);
        ui_switch_anim_set(SWI_REC, g_ui.autorec);
        break;
    case ID_COPYLOG:
        if (ui_log_copy_all()) ui_log_add("事件已全部复制到剪贴板", 2);
        else ui_log_add("复制失败：剪贴板被其他程序占用", 1);
        break;
    case ID_CLEARLOG: ui_log_clear(); break;
    case ID_PAUSELOG: ui_log_toggle_pause(); break;
    case ID_LOG_SCROLL:
        g_ui.drag_log = 1; SetCapture(g_ui.hwnd); ui_log_scroll_to(y); break;
    case ID_EXP_AUDIO: g_ui.exp_audio = !g_ui.exp_audio; break;
    case ID_EXP_STATS: g_ui.exp_stats = !g_ui.exp_stats; break;
    case ID_EXP_LOG:   g_ui.exp_log = !g_ui.exp_log; break;
    default: break;
    }
    /* Connection adapters may be changed only while idle. */
    if (id >= ID_TRANS0 && id <= ID_TRANS3 && !g_ui.running) {
        if (g_ui.transport != id - ID_TRANS0) cancel_direct_picker();
        if (g_ui.transport != 1) GetWindowTextW(g_ui.edit_host,g_hosts[g_ui.transport],64);
        g_ui.transport = id - ID_TRANS0;
        SetWindowTextW(g_ui.edit_host,g_ui.transport==1?g_direct_choice.name:g_hosts[g_ui.transport]);
        SendMessageW(g_ui.edit_host,EM_SETREADONLY,g_ui.transport==1,0);
        if (g_ui.transport == 1) {
            reap_direct_picker();
            start_direct_picker();
        }
        if (g_ui.transport == 2) {
            TransportDevice devices[32];
            int count = transport_paired_devices(devices,32), i;
            HMENU menu = CreatePopupMenu(); POINT point;
            for(i=0;i<count;i++) AppendMenuW(menu,MF_STRING,i+1,devices[i].name);
            if(count) {
                int choice;
                GetCursorPos(&point);
                choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,g_ui.hwnd,NULL);
                if(choice>0 && choice<=count) SetWindowTextA(g_ui.edit_host,devices[choice-1].address);
            } else ui_log_add("未找到已配对蓝牙设备，请先在系统设置中配对手机",1);
            DestroyMenu(menu);
        }
        static const char *hints[] = {"同一路由器或电脑加入手机热点：填写手机IP", "手机选择无线直连并启动；选择手机后点击连接，再次点击无线直连可重新搜索，无需IP", "先配对并在手机启用蓝牙传输；填写12位蓝牙地址（不含冒号）", "需要PATH中的adb.exe及USB调试授权；地址留空自动选择，或填序列号"};
        ui_log_add(hints[g_ui.transport], 0);
    }
    if (id >= ID_RATE0 && id <= ID_RATE2 && !g_ui.running) g_ui.seg_rate = id - ID_RATE0;
    if (id >= ID_BUF0 && id <= ID_BUF2 && !g_ui.running) g_ui.buf_idx = id - ID_BUF0;
    if (id == ID_SLIDER) {
        g_ui.drag_slider = 1;
        {
            int w = g_lo.slider.right - g_lo.slider.left;
            int pos = (x - g_lo.slider.left) * 1000 / (w ? w : 1);
            if (pos < 0) { pos = 0; }
            if (pos > 1000) { pos = 1000; }
            g_ui.gain_db10 = ui_pos_to_db10(pos);
            eng_set_gain_db(g_ui.gain_db10);   /* 实时生效 */
        }
    }
    ui_layout_compute();
    g_dirty = 1;
}

void ui_on_release(void)
{
    g_ui.pressed = 0;
    g_ui.drag_slider = 0;
    g_ui.drag_log = 0;
    if (GetCapture() == g_ui.hwnd) ReleaseCapture();
    g_dirty = 1;
}

/* ---------------------------------------------------------------- 窗口 */
/* 输入框定位（R-10 水平居中 / R-30 垂直居中）
 *
 * 单行 EDIT **不会**在自己客户区里垂直居中（它把文字贴在顶部），而视觉上的"框"
 * 是自绘的、比一行文字高 —— 所以不能让 EDIT 撑满整框，否则文字看着贴顶。
 * 做法：EDIT 高度 = 一行文字高，整体在自绘框内垂直居中；左右各内缩 1px，
 * 免得盖住自绘的 1px 描边。框线仍然由 draw_input 画。 */
static void place_one_edit(HWND h, RECT box)
{
    int w = box.right - box.left - 2;
    int th = ui_text_height(14) + sd(2);      /* 一行文字高 + 上下各 1px 余量 */
    int y;
    if (w < sd(8)) w = sd(8);
    y = box.top + (box.bottom - box.top - th) / 2;
    if (y < box.top + 1) y = box.top + 1;
    MoveWindow(h, box.left + 1, y, w, th, TRUE);
}

static void place_edits(void)
{
    if (g_ui.edit_host)  place_one_edit(g_ui.edit_host, g_lo.ed_host);
    if (g_ui.edit_port)  place_one_edit(g_ui.edit_port, g_lo.ed_port);
    if (g_ui.edit_media) place_one_edit(g_ui.edit_media, g_lo.ed_media);
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, g_ui.w, g_ui.h);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    ui_draw_all(mem);
    BitBlt(dc, 0, 0, g_ui.w, g_ui.h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_ui.hwnd = hwnd;
        g_ui.br_card = CreateSolidBrush(g_th.card);
        g_ui.br_sunken = CreateSolidBrush(g_th.sunken);
        g_ui.seg_rate = 0;
        g_ui.buf_idx = 1;
        g_ui.gain_db10 = 0;      /* R-31：0.0 dB = 100%（内部一律 dB） */
        /* 开发/截图用：把折叠面板全展开，方便一眼看完整布局（正式运行默认收起） */
        if (GetEnvironmentVariableW(L"MICLINK_EXPAND_ALL", NULL, 0) > 0) {
            g_ui.exp_audio = g_ui.exp_stats = g_ui.exp_log = 1;
        }
        /* 开发/截图用：启动即打开说明书窗口（无 WM 环境下无法用鼠标点开它，
         * 只能这样取证「无系统标题栏 + 关闭按钮比例正常」）。 */
        if (GetEnvironmentVariableW(L"MICLINK_SHOW_HELP", NULL, 0) > 0) {
            PostMessageW(hwnd, WM_APP + 3, 0, 0);
        }
        /* R-46：开关动画初值必须**与真实状态一致**。
         * 动画位置默认全 0（= 关），若某个开关初始就是「开」（比如恢复上次
         * 设置、或外部改了状态），滑块会画在关闭位，直到用户手动拨一次才归位。
         * 这里统一对齐：位置直接置到目标，不播入场动画。 */
        ui_switch_anim_set(SWI_MUTE, g_ui.mute);
        ui_switch_anim_set(SWI_MON, g_ui.monitor);
        ui_switch_anim_set(SWI_REC, g_ui.autorec);
        ui_switch_anim_set(SWI_GAINEXT, g_ui.gain_ext);
        ui_switch_anim_sync();
        g_ui.edit_host = CreateWindowExW(0, L"EDIT", L"192.168.1.100",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER,   /* R-10：三处统一水平居中 */
            0, 0, 10, 10, hwnd, (HMENU)100, NULL, NULL);
        g_ui.edit_port = CreateWindowExW(0, L"EDIT", L"8125",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER,
            0, 0, 10, 10, hwnd, (HMENU)101, NULL, NULL);
        g_ui.edit_media = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER,
            0, 0, 10, 10, hwnd, (HMENU)102, NULL, NULL);
        SetWindowTheme(g_ui.edit_host, L" ", L" ");
        SetWindowTheme(g_ui.edit_port, L" ", L" ");
        SetWindowTheme(g_ui.edit_media, L" ", L" ");
        {
            HFONT f = ui_font(14, FW_NORMAL, 1);   /* R-24：正文 13→14，读数 Consolas */
            SendMessageW(g_ui.edit_host, WM_SETFONT, (WPARAM)f, TRUE);
            SendMessageW(g_ui.edit_port, WM_SETFONT, (WPARAM)f, TRUE);
            SendMessageW(g_ui.edit_media, WM_SETFONT, (WPARAM)f, TRUE);
        }
        monitor_init();
        if (GetEnvironmentVariableW(L"MICLINK_SHOW_MONITOR", NULL, 0) > 0) PostMessageW(hwnd, WM_APP + 4, 0, 0);
        eng_set_logger(eng_log_cb);
        ui_log_add("客户端已启动，等待连接", 0);
        SetTimer(hwnd, 1, 16, NULL);
        return 0;
    case WM_SIZE:
        g_ui.w = LOWORD(lp); g_ui.h = HIWORD(lp);
        ui_layout_compute();
        place_edits();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(hwnd); return 0;

    case WM_TIMER: {
        EngineStats st;
        reap_direct_picker();
        int was = g_ui.running;
        DWORD now = GetTickCount();
        eng_snapshot(&st);
        { MonitorStatus m; monitor_snapshot(&m);
          if(g_ui.monitor!=m.wanted){g_ui.monitor=m.wanted;ui_switch_anim_set(SWI_MON,m.wanted);g_dirty=1;} }
        g_ui.st = st;
        if (g_ui.running && (st.state == ST_IDLE || st.state == ST_ERROR)) {
            g_ui.running = 0;
            g_ui.disc_at = now;                 /* D-08：断开时刻 */
            if (st.state == ST_ERROR) ui_log_add(st.last_error, 1);
            /* Direct opens a user selector. Never reopen it repeatedly through
             * the automatic reconnect loop after cancellation or link loss. */
            if (g_ui.transport == 1) {
                g_ui.rc_armed = 0; g_ui.rc_count = 0; g_ui.rc_next = 0;
            }
            g_dirty = 1;
        }
        /* worker 自行结束（连接失败 / 对端断开）后回收线程与设备资源。
         * 必须**每个节拍都试**，不能只在上面那一拍试一次：worker 是先把
         * state 置成 ST_ERROR 再去收尾（devio_stop/关套接字），那一拍它
         * 往往还没真正退出，eng_reap() 的 WaitForSingleObject(0) 会超时返回；
         * 而此后 g_ui.running 已经是 0，那一拍就再也不会重来 —— g_run 永久
         * 留在 1，下一次 eng_start() 在首行 `if (g_run) return 0;` 处立刻失败，
         * 用户看到的是「启动引擎失败」且没有原因。
         * eng_reap() 幂等，且线程还在跑时直接返回，所以这样反复调用是安全的。 */
        if (!g_ui.running) eng_reap();

        /* R-27 自动重连：只在「非用户主动断开」后生效（rc_armed 由 toggle_connection
         * 武装/解除）；固定 0.5 s 间隔，不封顶、不退避；每次重试都走完整
         * connect_session()（= 完整握手 + 重开驱动流 + eng_reap 已放掉旧线程）。 */
        if (!g_ui.running && g_ui.autorec && g_ui.rc_armed) {
            DWORD now2 = GetTickCount();
            if (!g_ui.rc_next) {
                g_ui.rc_next = now2 + 500;
            } else if ((int)(now2 - g_ui.rc_next) >= 0) {
                g_ui.rc_next = now2 + 500;
                g_ui.rc_count++;
                if (g_ui.rc_count <= 3 || (g_ui.rc_count % 20) == 0)
                    ui_log_add("自动重连中…", 0);   /* 限流：避免 0.5s 刷屏 */
                connect_session(0);
                g_dirty = 1;
            }
        }
        ui_meter_update(&st, g_ui.running);

        /* ---- D-08：历史采样（约 208ms/柱，10 秒 48 柱）---- */
        if (g_ui.running && now - g_ui.hist_last >= 208) {
            float db = ui_meter_cur_db();
            float t = (db + 60.0f) / 60.0f;     /* dB → 柱高比例 */
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            if (g_ui.hist_count < HIST_BARS) {
                g_ui.hist[g_ui.hist_count++] = t;
            } else {
                memmove(g_ui.hist, g_ui.hist + 1,
                        (HIST_BARS - 1) * sizeof(float));
                g_ui.hist[HIST_BARS - 1] = t;
            }
            g_ui.hist_last = now;
            g_dirty = 1;
        }
        /* 断开超过 10s 清空历史（D-08：不再显示上一次会话的残留） */
        if (!g_ui.running && g_ui.hist_count && g_ui.disc_at &&
            now - g_ui.disc_at > 10000) {
            g_ui.hist_count = 0;
            g_dirty = 1;
        }

        /* R-27：真正进入推流才算"重连成功"，这时才把计数清零（eng_start 只是
         * 起了线程，握手还没走完，用它清零会让"重连中 (第 N 次)"永远显示不出来） */
        if (g_ui.running && st.state == ST_STREAMING && g_ui.rc_count > 0) {
            ui_log_add("自动重连成功，已恢复推流", 2);
            g_ui.rc_count = 0;
            g_ui.rc_next = 0;
        }

        /* ---- R-46：开关滑块动画推进（120ms 走完 0↔1000）---- */
        if (ui_switch_anim_step()) g_dirty = 1;

        /* ---- D-14：500ms 节拍（状态栏/面板摘要快照 + 峰值保持）---- */
        if (++g_ui.stat_half >= 31) {           /* 16ms × 31 ≈ 500ms */
            double lp;
            g_ui.stat_half = 0;
            g_ui.st_disp = st;
            if (st.kbps > g_ui.kbps_peak) g_ui.kbps_peak = st.kbps;
            if (st.jitter_ms > g_ui.jit_peak) g_ui.jit_peak = st.jitter_ms;
            lp = (st.frames + st.lost)
                 ? (double)st.lost * 100.0 / (double)(st.frames + st.lost) : 0.0;
            if (lp > g_ui.lost_peak) g_ui.lost_peak = (float)lp;
            g_dirty = 1;
        }

        /* ---- D-10：hover 描边过渡 120ms（fade 0..1000，T1）---- */
        if (g_ui.hover_fade_id) {
            int step = 1000 * 16 / 120;
            if (g_ui.hover == g_ui.hover_fade_id) {
                g_ui.hover_fade += step;
                if (g_ui.hover_fade >= 1000) g_ui.hover_fade = 1000;
            } else {
                g_ui.hover_fade -= step;
                if (g_ui.hover_fade <= 0) {
                    g_ui.hover_fade = 0;
                    g_ui.hover_fade_id = 0;
                }
            }
            g_dirty = 1;
        }

        g_ui.tick++;
        if (g_ui.tick % 125 == 0) {          /* ~2 s 复检一次驱动状态 */
            DrvStatus d;
            drv_detect(&d, 0);
            if (d.state != g_ui.drv.state) {
                g_ui.drv = d;
                ui_log_add(d.state == DRV_READY ? "虚拟麦克风已就绪" : "驱动状态变化", 0);
                g_dirty = 1;
            } else {
                g_ui.drv = d;
            }
        }
        /* 只在运行或电平未归零时高频重绘，空闲不占 CPU。
         * 呼吸外发光（D-04）只画在运行/连接中，随运行态重绘一起动。 */
        if (g_ui.running || was || ui_meter_state() != 0 || g_ui.st.level > 0.0f) {
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (g_dirty) {
            g_dirty = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_APP + 1: {                        /* 引擎线程日志 */
        char *s = (char *)wp;
        ui_log_add(s, (int)lp);
        free(s);
        g_dirty = 1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_APP + 2:                          /* 延迟执行的自动连接 */
        if (!g_ui.running) toggle_connection();
        return 0;
    case WM_APP + 4:
        monitor_show(hwnd);
        return 0;
    case WM_APP + 3:                          /* 开发用：打开说明书窗口 */
        ui_show_help(GetModuleHandleW(NULL), g_ui.hwnd);
        return 0;

    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        POINT point = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &point);
        if (g_ui.exp_log && point.y >= ui_title_h() && point.y < g_ui.h - ui_status_h()
            && PtInRect(&g_lo.card_log, point)) {
            static int remainder;
            remainder += d;
            ui_log_scroll(-(remainder / WHEEL_DELTA) * 3);
            remainder %= WHEEL_DELTA;
        } else g_ui.scroll += (d < 0 ? sd(40) : -sd(40));
        ui_layout_compute();
        place_edits();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int h = ui_hit_test(x, y);
        if (h != g_ui.hover) {
            g_ui.hover = h;
            /* D-10：新目标从 0 开始淡入（旧目标的淡出由 WM_TIMER 收尾） */
            if (h != ID_NONE) {
                g_ui.hover_fade_id = h;
                g_ui.hover_fade = 0;
            }
            g_dirty = 1;
            {
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = 0;
                TrackMouseEvent(&tme);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (g_ui.drag_slider) {
            int w = g_lo.slider.right - g_lo.slider.left;
            int pos = (x - g_lo.slider.left) * 1000 / (w ? w : 1);
            if (pos < 0) { pos = 0; }
            if (pos > 1000) { pos = 1000; }
            g_ui.gain_db10 = ui_pos_to_db10(pos);
            eng_set_gain_db(g_ui.gain_db10);   /* 实时生效 */
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (g_ui.drag_log) {
            ui_log_scroll_to(y);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        SetCursor(LoadCursorW(NULL, (LPCWSTR)((h == ID_SLIDER) ? IDC_SIZEWE :
                              (h != ID_NONE) ? IDC_HAND : IDC_ARROW)));
        return 0;
    }
    case WM_MOUSELEAVE:
        g_ui.hover = ID_NONE;
        g_dirty = 1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int h = ui_hit_test(x, y);
        g_ui.pressed = h;
        ui_on_click(h, x, y);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        ui_on_release();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        g_ui.drag_log = g_ui.drag_slider = 0;
        return 0;

    case WM_CTLCOLORSTATIC: /* read-only / disabled EDIT uses STATIC coloring */
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_th.text);
        SetBkColor(dc, g_th.sunken);      /* 输入框底 = bg-sunken（C-06） */
        return (LRESULT)g_ui.br_sunken;
    }
    case WM_KEYDOWN:
        if (wp == VK_RETURN) toggle_connection();
        if (wp == VK_ESCAPE && ui_help_is_open()) ui_help_close();
        /* D-13：Ctrl+C 复制全部事件（与「复制全部」按钮同效） */
        if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (ui_log_copy_all()) ui_log_add("事件已全部复制到剪贴板", 2);
            else ui_log_add("复制失败：剪贴板被其他程序占用", 1);
            g_dirty = 1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_NCCALCSIZE:
        if (wp) return 0;                     /* 去掉系统标题栏，内容铺满 */
        break;
    case WM_NCHITTEST: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        int bw = sd(6);
        if (p.y < bw && p.x < bw) return HTTOPLEFT;
        if (p.y < bw && p.x > g_ui.w - bw) return HTTOPRIGHT;
        if (p.y > g_ui.h - bw && p.x < bw) return HTBOTTOMLEFT;
        if (p.y > g_ui.h - bw && p.x > g_ui.w - bw) return HTBOTTOMRIGHT;
        if (p.y < bw) return HTTOP;
        if (p.y > g_ui.h - bw) return HTBOTTOM;
        if (p.x < bw) return HTLEFT;
        if (p.x > g_ui.w - bw) return HTRIGHT;
        if (p.y < ui_title_h() && p.x < g_lo.seg_theme[0].left) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        /* 最小窗口 900×560（逻辑像素）：两列布局在该尺寸下不破版。
         * 本窗口自绘标题栏（WM_NCCALCSIZE 返回 0，客户区 = 整窗），所以
         * 「窗口尺寸」与「客户区尺寸」是同一个数 —— 这里**不能**再套
         * AdjustWindowRectEx 加边框补偿，否则客户区会比规格大一圈（D-03：
         * 曾得到 916×599），与「最小客户区 900×560」的验收口径不符。 */
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = sd(900);
        mm->ptMinTrackSize.y = sd(560);
        return 0;
    }
    case WM_DESTROY:
        g_ui.rc_armed = 0;                    /* R-27：关窗口不重连 */
        g_ui.autorec = 0;
        KillTimer(hwnd, 1);
        cancel_direct_picker();
        if (g_direct_picker && WaitForSingleObject(g_direct_picker->thread,5000)==WAIT_OBJECT_0)
            reap_direct_picker();
        eng_stop();
        monitor_shutdown();
        devio_close();
        singleinst_shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------ 第二个实例的命令行转发 */
static void on_forwarded(const wchar_t *cmdline)
{
    CliArgs a;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(cmdline, &argc);
    if (!argv) return;
    cli_set_defaults(&a);
    if (cli_parse(&a, argc, argv)) {
        if (a.driver_pack) drv_set_external_pack(a.driver_pack);
        if (a.connect && !g_ui.running) {
            wchar_t port[16];
            g_ui.transport = a.transport == 4 ? 0 : a.transport;
            _snwprintf(port, 16, L"%d", a.port); port[15] = 0;
            SetWindowTextW(g_ui.edit_port, port);
            if ((a.transport == 3 && !a.host_pos) || a.transport == 1) SetWindowTextW(g_ui.edit_host, L"");
            SendMessageW(g_ui.edit_host,EM_SETREADONLY,a.transport==1,0);
        }
        if (a.host_pos) SetWindowTextW(g_ui.edit_host, a.host_pos);
        if (a.media_port > 0 && g_ui.edit_media) {
            wchar_t buf[16];
            _snwprintf(buf, 16, L"%d", a.media_port);
            buf[15] = 0;
            SetWindowTextW(g_ui.edit_media, buf);
        }
        if (a.connect) PostMessageW(g_ui.hwnd, WM_APP + 2, 0, 0);
    }
    LocalFree(argv);
}

static void on_activate(void)
{
    if (g_ui.hwnd) {
        if (IsIconic(g_ui.hwnd)) ShowWindow(g_ui.hwnd, SW_RESTORE);
        SetForegroundWindow(g_ui.hwnd);
        FlashWindow(g_ui.hwnd, TRUE);
    }
}

/* ---------------------------------------------------------------- 入口 */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE prev, LPSTR cmd, int show)
{
    int argc = 0;
    LPWSTR *argv;
    HWND hwnd;
    MSG m;
    (void)prev; (void)cmd;

    /* 高 DPI：Per-Monitor V2（Win10 1703+；失败退回系统 DPI 感知） */
    {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        typedef BOOL (WINAPI *fn_ctx)(void *);
        fn_ctx f = (fn_ctx)(void *)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (f) f((void *)-4); else SetProcessDPIAware();
    }

    ui_gdiplus_init();
    theme_apply(1);

    /* ---- 命令行（--help / --version / --status / --install-driver 等都在这里退出） ---- */
    cli_set_defaults(&g_args);
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (!cli_parse(&g_args, argc, argv)) {
            char line[256];
            cli_ensure_console(1);
            /* 具体原因优先（例如 --media-port 取值非法），不要只丢一份帮助 */
            _snprintf(line, sizeof(line), "参数错误：%s\n\n", cli_parse_error());
            fputs(line, stdout);
            fflush(stdout);
            cli_print_help();
            LocalFree(argv);
            return CLI_EXIT_PARAM;
        }
        LocalFree(argv);
    }
    if (g_args.driver_pack) drv_set_external_pack(g_args.driver_pack);
    if (cli_run(&g_args)) {
        int code = cli_exit_code();
        cli_release_console();
        return code;
    }

    /* ---- 强制单实例：驱动的灌音门控是全局的，两个实例会互相踩 ---- */
    if (singleinst_init(on_forwarded, on_activate) == 0) {
        /* 命令行已转发给已有实例，本进程退出 */
        return 0;
    }

    if (g_args.theme_set) theme_apply(g_args.dark ? 1 : 0);
    g_ui.dark = g_th.dark;

    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wndproc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"WomicLinkWnd";
        wc.hIcon = LoadIconW(hInst, (LPCWSTR)MAKEINTRESOURCE(1));
        wc.hIconSm = LoadIconW(hInst, (LPCWSTR)MAKEINTRESOURCE(1));
        RegisterClassExW(&wc);
    }

    /* DPI 必须在算尺寸之前就位，否则 sd() 返回 0，窗口退化成 1x1 */
    {
        HDC screen = GetDC(NULL);
        g_ui.dpi = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(NULL, screen);
        if (!g_ui.dpi) g_ui.dpi = 96;
    }
    g_ui.w = sd(1060); g_ui.h = sd(680);
    {
        RECT wr = { 0, 0, g_ui.w, g_ui.h };
        AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
        /* R-29：必须带 WS_CLIPCHILDREN —— 否则 30fps 整窗重绘时父窗口会刷掉
         * EDIT 子控件区域，鼠标一动输入框里的文字就闪（子控件反复重画）。 */
        hwnd = CreateWindowExW(0, L"WomicLinkWnd", L"MicLink",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               wr.right - wr.left, wr.bottom - wr.top,
                               NULL, NULL, hInst, NULL);
    }
    if (!hwnd) return 1;
    g_ui.hwnd = hwnd;

    /* Win11 圆角 + 暗色边框（失败无所谓，Win10 会忽略） */
    {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            typedef HRESULT (WINAPI *fn_attr)(HWND, DWORD, LPCVOID, DWORD);
            fn_attr set = (fn_attr)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (set) {
                int pref = 2;                 /* DWMWCP_ROUND */
                set(hwnd, 33, &pref, sizeof(pref));
                BOOL dark = TRUE;
                set(hwnd, 20, &dark, sizeof(dark));
            }
            /* R-23：把 DWM 画的非客户区收掉（零边框扩展）。
             * 症状：首次显示时 DWM 会按 WS_CAPTION 合成一层原生标题栏叠在
             * 自绘标题栏上，最小化/还原重算非客户区后才“自愈”。根因是
             * DWM 的合成在首帧早于应用对非客户区的改写 —— 零边框扩展
             * （DwmExtendFrameIntoClientArea 0,0,0,0）+ 下面的 FRAMECHANGED
             * 强制在显示前重算一次，第一帧就是自绘标题栏。 */
            typedef struct { int cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight; } Margins;
            typedef HRESULT (WINAPI *fn_ext)(HWND, const Margins *);
            fn_ext ext = (fn_ext)(void *)GetProcAddress(dwm, "DwmExtendFrameIntoClientArea");
            if (ext) {
                Margins m = { 0, 0, 0, 0 };
                ext(hwnd, &m);
            }
        }
    }
    /* R-23：显示前强制重算非客户区（WM_NCCALCSIZE 无条件返回 0 的结果
     * 必须在首帧就生效；SHOW 之后再改就晚了一帧）。 */
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                 SWP_FRAMECHANGED);

    cli_apply_to_ui(&g_args);
    if (g_args.feed_write) eng_set_feed_mode(DEV_FEED_WRITE);

    /* 驱动检测放在窗口创建之后：L1 会打开设备句柄（全进程只开一次），
     * 打开失败不阻塞界面，用户可点“安装驱动”。 */
    drv_detect(&g_ui.drv, 1);
    if (g_ui.drv.state == DRV_READY)
        ui_log_add("虚拟麦克风驱动已就绪", 2);
    else {
        wchar_t inst[MAX_PATH];
        ui_log_add("未检测到虚拟麦克风驱动：可在下方点“安装驱动”", 0);
        /* 安装器不在时对他说清楚：此时再点按钮也只能得到同一句提示，
         * 提前说一次避免用户在 GUI 里空转。 */
        if (!drvbridge_find(inst, MAX_PATH))
            ui_log_add("也未找到 womic-drvinst.exe：把它与本程序放同一目录才能安装驱动", 0);
    }

    ui_layout_compute();
    place_edits();
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    if (g_args.connect && g_args.host_pos) PostMessageW(hwnd, WM_APP + 2, 0, 0);

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (monitor_handle_message(&m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
