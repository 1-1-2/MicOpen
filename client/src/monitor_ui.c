/* MIT. Functional native controls for the monitor test build. Visual polish is
 * intentionally separate from the audio architecture. */
#include "app.h"
#include "monitor.h"
#include "ui_draw.h"
#include <commctrl.h>
#include <stdio.h>

static HWND panel, device, module, enabled, suppress, volume, delay, status, vol_label;
static MonitorDevice devices[128];
static int device_count;
static HBRUSH panel_brush;
static DWORD reset_notice_until;
static wchar_t selected_id[MON_DEVICE_ID];
enum { C_ENABLE = 701, C_DEVICE, C_MODULE, C_FX, C_VOLUME, C_DELAY, C_RESET, C_REFRESH };
static HWND control(HWND w, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y,
                    int width, int height, int id) {
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, sd(x), sd(y), sd(width), sd(height), w,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)ui_font(14, FW_NORMAL, 0), TRUE);
    return h;
}
static void refresh(void) {
    int i, selected = 0;
    device_count = monitor_devices(devices, 128);
    SendMessageW(device, CB_RESETCONTENT, 0, 0);
    SendMessageW(device, CB_ADDSTRING, 0, (LPARAM)L"跟随系统默认声音输出");
    for (i = 0; i < device_count; i++) {
        SendMessageW(device, CB_ADDSTRING, 0, (LPARAM)devices[i].name);
        if (!wcscmp(selected_id, devices[i].id))
            selected = i + 1;
    }
    if (selected_id[0] && !selected) {
        SendMessageW(device, CB_ADDSTRING, 0, (LPARAM)L"已选设备当前不可用（保留选择）");
        selected = device_count + 1;
    }
    SendMessageW(device, CB_SETCURSEL, selected, 0);
}
static void sync(void) {
    MonitorStatus s;
    wchar_t text[768], v[64];
    monitor_snapshot(&s);
    SendMessageW(enabled, BM_SETCHECK, s.wanted ? BST_CHECKED : BST_UNCHECKED, 0);
    swprintf(v, 64, L"监听音量：%d%%", s.volume);
    SetWindowTextW(vol_label, v);
    swprintf(text, 768,
             L"%ls\r\n当前输出：%ls\r\nRing Out：%ls（陷波 %d）\r\nSpeexDSP AEC：%ls\r\n%ls；丢弃过期音频：%u 块",
             s.message[0] ? s.message : L"监听默认关闭；选择输出后勾选监听。",
             s.active ? s.device : L"未播放",
             !s.ring_available ? L"不可用" : s.suppression && s.module == 0 && s.active ? L"已启用" : L"可用（未启用）", s.filters,
             !s.aec_available ? L"不可用" : s.suppression && s.module == 1 && s.active ? L"已启用" : L"可用（未启用）",
             !s.active ? L"等待播放" : s.switching ? L"效果切换中" : s.suppression && (s.module == 0 ? s.ring_available : s.aec_available) ? L"处理音频" : L"原声旁路", s.dropped_frames);
    if ((int)(reset_notice_until - GetTickCount()) > 0)
        wcscat(text, L"\r\n已请求重置当前算法；延迟保持不变。");
    SetWindowTextW(status, text);
}
static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        MonitorStatus s;
        monitor_init();
        panel_brush = CreateSolidBrush(g_th.card);
        monitor_snapshot(&s);
        enabled = control(w, L"BUTTON", L"启用监听", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 16, 140, 26,
                          C_ENABLE);
        control(w, L"STATIC", L"输出设备（选择后立即切换）", 0, 20, 53, 350, 22, 0);
        device = control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 20, 80,
                         440, 240, C_DEVICE);
        control(w, L"BUTTON", L"刷新设备", BS_PUSHBUTTON | WS_TABSTOP, 474, 78, 100, 28, C_REFRESH);
        vol_label = control(w, L"STATIC", L"监听音量", 0, 20, 121, 180, 22, 0);
        volume = control(w, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 196, 115,
                         378, 36, C_VOLUME);
        SendMessageW(volume, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(volume, TBM_SETPOS, TRUE, s.volume);
        suppress = control(w, L"BUTTON", L"启用啸叫抑制 / AEC（取消勾选即可对比原声）",
                           BS_AUTOCHECKBOX | WS_TABSTOP, 20, 163, 550, 26, C_FX);
        SendMessageW(suppress, BM_SETCHECK, s.suppression ? BST_CHECKED : BST_UNCHECKED, 0);
        module = control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 20, 202, 554, 160,
                         C_MODULE);
        SendMessageW(module, CB_ADDSTRING, 0, (LPARAM)L"Ring Out — 自动检测并抑制啸叫");
        SendMessageW(module, CB_ADDSTRING, 0, (LPARAM)L"SpeexDSP AEC — 播放参考回声消除（实验）");
        SendMessageW(module, CB_SETCURSEL, s.module, 0);
        control(w, L"STATIC", L"AEC 播放到返回延迟（ms）", 0, 20, 246, 340, 22, 0);
        delay = control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 354, 241, 100, 200,
                        C_DELAY);
        for (int ms = 10; ms <= 400; ms += 10) {
            wchar_t b[32];
            swprintf(b, 32, L"%d", ms);
            SendMessageW(delay, CB_ADDSTRING, 0, (LPARAM)b);
        }
        SendMessageW(delay, CB_SETCURSEL, s.delay_ms / 10 - 1, 0);
        control(w, L"BUTTON", L"重置当前算法", BS_PUSHBUTTON | WS_TABSTOP, 20, 302, 160, 32, C_RESET);
        control(w, L"STATIC", L"重置当前算法的学习状态：清空陷波或重新收敛 AEC；延迟数值不会改变。",
                0, 20, 344, 554, 40, 0);
        control(w, L"STATIC", L"AEC 只使用本监听的播放参考，不含其他应用的声音。",
                0, 20, 388, 554, 24, 0);
        status = control(w, L"STATIC", L"", 0, 20, 426, 554, 145, 0);
        refresh();
        sync();
        SetTimer(w, 1, 250, NULL);
        return 0;
    }
    case WM_APP + 20:
        if (panel_brush) DeleteObject(panel_brush);
        panel_brush = CreateSolidBrush(g_th.card);
        RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor((HDC)wp, g_th.text);
        SetBkColor((HDC)wp, g_th.card);
        return (LRESULT)panel_brush;
    case WM_ERASEBKGND: { RECT r; GetClientRect(w, &r); FillRect((HDC)wp, &r, panel_brush); return 1; }
    case WM_TIMER:
        sync();
        return 0;
    case WM_DEVICECHANGE:
        refresh();
        return 0;
    case WM_HSCROLL:
        if ((HWND)lp == volume)
            monitor_volume((int)SendMessageW(volume, TBM_GETPOS, 0, 0));
        sync();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), event = HIWORD(wp);
        if (id == C_ENABLE && event == BN_CLICKED)
            monitor_enable(SendMessageW(enabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == C_DEVICE && event == CBN_SELCHANGE) {
            int i = (int)SendMessageW(device, CB_GETCURSEL, 0, 0);
            if (i >= 0 && i <= device_count) {
                lstrcpynW(selected_id, i ? devices[i - 1].id : L"", MON_DEVICE_ID);
                monitor_device(selected_id);
            }
        }
        if ((id == C_MODULE && event == CBN_SELCHANGE) || (id == C_FX && event == BN_CLICKED))
            monitor_effect(SendMessageW(suppress, BM_GETCHECK, 0, 0) == BST_CHECKED,
                           (int)SendMessageW(module, CB_GETCURSEL, 0, 0));
        if (id == C_DELAY && event == CBN_SELCHANGE)
            monitor_delay(((int)SendMessageW(delay, CB_GETCURSEL, 0, 0) + 1) * 10);
        if (id == C_RESET && event == BN_CLICKED) {
            monitor_reset();
            reset_notice_until = GetTickCount() + 4000;
        }
        if (id == C_REFRESH && event == BN_CLICKED)
            refresh();
        sync();
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(w, SW_HIDE);
        return 0;
    case WM_DESTROY:
        KillTimer(w, 1);
        if (panel_brush) { DeleteObject(panel_brush); panel_brush = NULL; }
        panel = NULL;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
void monitor_show(HWND owner) {
    if (!panel) {
        WNDCLASSW wc = {0};
        INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_BAR_CLASSES};
        InitCommonControlsEx(&ic);
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"MicLinkMonitorTest";
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        RECT r = {0, 0, sd(596), sd(585)};
        AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 0, WS_EX_TOOLWINDOW);
        panel =
            CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"MicLink · 监听实验室",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, owner, NULL, wc.hInstance, NULL);
    }
    ShowWindow(panel, SW_SHOW);
    SetForegroundWindow(panel);
}

void monitor_theme_changed(void) { if (panel) SendMessageW(panel, WM_APP + 20, 0, 0); }
