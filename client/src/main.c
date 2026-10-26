/* main.c — 薄壳：窗口创建 + 事件分发 + 模块装配
 *
 * 绘制细节在 ui_draw / ui_layout / ui_voicemeter / ui_help；
 * 协议与灌音在 engine / devio / opusdec；
 * 驱动检测安装在 drvinst / drvpkg；命令行在 cli；单实例在 singleinst。
 * 本文件只负责把它们串起来，不掺业务逻辑。
 */
#define WIN32_LEAN_AND_MEAN
#include "app.h"
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

/* ---------------------------------------------------------------- 日志 */
static void eng_log_cb(const char *line)
{
    char *copy = (char *)malloc(LOG_LEN);
    if (!copy) return;
    strncpy(copy, line, LOG_LEN - 1);
    copy[LOG_LEN - 1] = 0;
    if (!PostMessageW(g_ui.hwnd, WM_APP + 1, (WPARAM)copy, 0)) free(copy);
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
    g_dirty = 1;
    if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, NULL, FALSE);
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

static void toggle_connection(void)
{
    if (g_ui.running) {
        eng_stop();
        g_ui.running = 0;
        g_ui.disc_at = GetTickCount();      /* D-08：断开时刻（10s 后清空历史） */
        ui_log_add("已断开连接", 0);
        g_dirty = 1;
        return;
    }
    {
        wchar_t host[64] = { 0 }, port[32] = { 0 }, mp[32] = { 0 };
        char hostA[96];
        int cp = 8125, pr, media = 0, port_defaulted = 0;
        GetWindowTextW(g_ui.edit_host, host, 64);
        WideCharToMultiByte(CP_UTF8, 0, host, -1, hostA, sizeof(hostA), NULL, NULL);
        if (!hostA[0]) {
            ui_log_add("请先输入手机地址", 1);
            g_dirty = 1;
            return;
        }
        /* 控制端口（D-01）：空 = 默认 8125 但必须明示；非空且非法 = 明确拒绝。
         * 这里不再有 `_wtoi` + `cp<=0 → 8125` 的静默回退，也不让 engine 里的
         * htons((u_short)port) 截断兜底 —— 99999 变 34463、65536 变 0 那种
         * 「填的和连的不是一个端口」正是本缺陷要消灭的。 */
        if (!read_edit_text(g_ui.edit_port, port, 32)) {
            ui_log_add("控制端口须为 1-65535 的整数（默认 8125）", 1);
            g_dirty = 1;
            return;
        }
        pr = cli_parse_port(port, &cp);
        if (pr == 0) {
            cp = 8125;
            port_defaulted = 1;       /* 空=默认可以，但下面的日志必须明示 */
        } else if (pr < 0) {
            ui_log_add("控制端口须为 1-65535 的整数（默认 8125）", 1);
            g_dirty = 1;
            return;
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
                return;
            }
            mpr = cli_parse_port(mp, &mv);
            if (mpr < 0) {
                ui_log_add("媒体端口须留空或填 1..65535（留空=随机分配）", 1);
                g_dirty = 1;
                return;
            }
            media = (mpr == 1) ? mv : 0;
        }
        /* 所有校验都过了才说「用了默认端口」：否则被媒体端口拦下时会留下
         * 一行让人误以为连接已发起的日志。 */
        if (port_defaulted) ui_log_add("未填控制端口，使用默认 8125", 0);
        /* 驱动未就绪时 GUI 主按钮已置灰（D-04）；键盘/CLI 路径走到这里时
         * 保留既有的「只做协议与统计、不出声」诊断模式，并把话说清楚。 */
        if (g_ui.drv.state != DRV_READY)
            ui_log_add("提示：驱动未就绪，将只做协议与统计，不出声", 0);
        eng_set_media_port(media);
        g_ui.running = eng_start(hostA, cp, rate_idx_of(g_ui.seg_rate),
                                 buf_ms_of(g_ui.buf_idx), g_ui.gain);
        if (g_ui.running) {
            /* 新会话：计数 +1，历史与峰值归零（D-08/D-12） */
            g_ui.conn_count++;
            g_ui.hist_count = 0;
            g_ui.kbps_peak = g_ui.lat_peak = 0;
            g_ui.lost_peak = 0.0f;
            ui_log_add("正在建立会话…", 0);
        } else {
            ui_log_add("启动引擎失败", 1);
        }
        g_dirty = 1;
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
    case ID_INSTALL:   request_driver_op(1); return;
    case ID_UNINSTALL: request_driver_op(0); return;
    case ID_OPENSET:   open_sound_settings(); return;
    case ID_HELP:  ui_show_help(GetModuleHandleW(NULL), g_ui.hwnd); return;
    case ID_MUTE:
        /* D-06 静音行：真实生效（engine 灌音前零填充），电平表照常显示输入 */
        g_ui.mute = !g_ui.mute;
        eng_set_mute(g_ui.mute);
        ui_log_add(g_ui.mute ? "已静音（输出截断，输入电平照常显示）"
                             : "已取消静音", 0);
        break;
    case ID_COPYLOG:
        if (ui_log_copy_all()) ui_log_add("事件已全部复制到剪贴板", 2);
        else ui_log_add("复制失败：剪贴板被其他程序占用", 1);
        break;
    case ID_EXP_AUDIO: g_ui.exp_audio = !g_ui.exp_audio; break;
    case ID_EXP_STATS: g_ui.exp_stats = !g_ui.exp_stats; break;
    case ID_EXP_LOG:   g_ui.exp_log = !g_ui.exp_log; break;
    default: break;
    }
    /* D-03：传输方式仅 Wi-Fi 已实现，其余段可见但禁用（点了不响应） */
    if (id == ID_TRANS0 && g_ui.transport != 0) {
        g_ui.transport = 0;
        ui_log_add("传输方式已切回 Wi-Fi", 0);
    }
    if (id >= ID_RATE0 && id <= ID_RATE2) g_ui.seg_rate = id - ID_RATE0;
    if (id >= ID_BUF0 && id <= ID_BUF2) g_ui.buf_idx = id - ID_BUF0;
    if (id == ID_SLIDER) {
        g_ui.drag_slider = 1;
        {
            int w = g_lo.slider.right - g_lo.slider.left;
            int pos = (x - g_lo.slider.left) * 1000 / (w ? w : 1);
            if (pos < 0) { pos = 0; }
            if (pos > 1000) { pos = 1000; }
            g_ui.gain = ui_pos_to_gain(pos);
        }
    }
    ui_layout_compute();
    g_dirty = 1;
}

void ui_on_release(void)
{
    g_ui.pressed = 0;
    g_ui.drag_slider = 0;
    g_dirty = 1;
}

/* ---------------------------------------------------------------- 窗口 */
/* 编辑框内缩 1px：让 draw_input 画的 1px border 不被编辑框客户区盖住 */
static void place_edits(void)
{
    if (g_ui.edit_host)
        MoveWindow(g_ui.edit_host, g_lo.ed_host.left + 1, g_lo.ed_host.top + 1,
                   g_lo.ed_host.right - g_lo.ed_host.left - 2,
                   g_lo.ed_host.bottom - g_lo.ed_host.top - 2, TRUE);
    if (g_ui.edit_port)
        MoveWindow(g_ui.edit_port, g_lo.ed_port.left + 1, g_lo.ed_port.top + 1,
                   g_lo.ed_port.right - g_lo.ed_port.left - 2,
                   g_lo.ed_port.bottom - g_lo.ed_port.top - 2, TRUE);
    if (g_ui.edit_media)
        MoveWindow(g_ui.edit_media, g_lo.ed_media.left + 1, g_lo.ed_media.top + 1,
                   g_lo.ed_media.right - g_lo.ed_media.left - 2,
                   g_lo.ed_media.bottom - g_lo.ed_media.top - 2, TRUE);
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
        g_ui.gain = 100;
        /* 开发/截图用：把折叠面板全展开，方便一眼看完整布局（正式运行默认收起） */
        if (GetEnvironmentVariableW(L"MICLINK_EXPAND_ALL", NULL, 0) > 0) {
            g_ui.exp_audio = g_ui.exp_stats = g_ui.exp_log = 1;
        }
        g_ui.edit_host = CreateWindowExW(0, L"EDIT", L"192.168.1.100",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
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
            HFONT f = ui_font(13, FW_NORMAL, 1);   /* 输入文字 13px Consolas */
            SendMessageW(g_ui.edit_host, WM_SETFONT, (WPARAM)f, TRUE);
            SendMessageW(g_ui.edit_port, WM_SETFONT, (WPARAM)f, TRUE);
            SendMessageW(g_ui.edit_media, WM_SETFONT, (WPARAM)f, TRUE);
        }
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
        int was = g_ui.running;
        DWORD now = GetTickCount();
        eng_snapshot(&st);
        g_ui.st = st;
        if (g_ui.running && (st.state == ST_IDLE || st.state == ST_ERROR)) {
            g_ui.running = 0;
            g_ui.disc_at = now;                 /* D-08：断开时刻 */
            if (st.state == ST_ERROR) ui_log_add(st.last_error, 1);
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

        /* ---- D-14：500ms 节拍（状态栏/面板摘要快照 + 峰值保持）---- */
        if (++g_ui.stat_half >= 31) {           /* 16ms × 31 ≈ 500ms */
            double lp;
            g_ui.stat_half = 0;
            g_ui.st_disp = st;
            if (st.kbps > g_ui.kbps_peak) g_ui.kbps_peak = st.kbps;
            if (st.latency_ms > g_ui.lat_peak) g_ui.lat_peak = st.latency_ms;
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
        ui_log_add(s, 0);
        free(s);
        g_dirty = 1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_APP + 2:                          /* 延迟执行的自动连接 */
        if (!g_ui.running) toggle_connection();
        return 0;

    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        g_ui.scroll += (d < 0 ? sd(40) : -sd(40));
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
            g_ui.gain = ui_pos_to_gain(pos);
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
        KillTimer(hwnd, 1);
        eng_stop();
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
        hwnd = CreateWindowExW(0, L"WomicLinkWnd", L"MicLink",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
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
        }
    }

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
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
