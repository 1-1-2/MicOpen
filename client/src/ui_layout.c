/* ui_layout.c — 两列布局与命中测试
 *
 * 布局（2026-10-25 重设计，依据《产品设计手册》§2）：
 *   左列（固定，不随右列滚动）：连接卡 + 驱动卡 —— 高频操作一屏常驻
 *   右列（可滚动）：电平卡（常驻）+ 音频参数 / 会话统计 / 事件（折叠）
 * 左列矩形用绝对坐标；右列经 cs() 减 g_ui.scroll。绘制与命中测试共用同一套矩形。
 */
#include "ui_layout.h"
#include "ui_draw.h"

UiLayout g_lo;

int ui_title_h(void)  { return sd(52); }
int ui_status_h(void) { return sd(28); }

/* 内容坐标 → 客户区坐标（减滚动偏移）。左列不经过此函数（固定）。 */
static RECT cs(int left, int top, int right, int bottom)
{
    RECT r;
    r.left = left; r.right = right;
    r.top = top - g_ui.scroll;
    r.bottom = bottom - g_ui.scroll;
    return r;
}

static RECT raw(int left, int top, int right, int bottom)
{
    RECT r;
    r.left = left; r.right = right; r.top = top; r.bottom = bottom;
    return r;
}

static void layout_once(void)
{
    int pad = sd(20), gap = sd(14), inner = sd(16);
    int leftW = sd(340);
    int x1 = g_ui.w - pad;
    int lx0 = pad, lx1 = pad + leftW;
    int rx0 = lx1 + gap, rx1 = x1;
    int y0 = ui_title_h() + sd(10);
    int yl = y0, yr = y0;
    int i;

    g_lo.title     = raw(0, 0, g_ui.w, ui_title_h());
    g_lo.btn_close = raw(g_ui.w - sd(8) - sd(46), sd(6), g_ui.w - sd(8), sd(48));
    g_lo.btn_min   = raw(g_ui.w - sd(8) - sd(92) - sd(2), sd(6), g_ui.w - sd(8) - sd(46) - sd(2), sd(48));
    g_lo.btn_theme = raw(g_ui.w - sd(8) - sd(92) - sd(2) - sd(6) - sd(34), sd(12),
                         g_ui.w - sd(8) - sd(92) - sd(2) - sd(6), sd(42));

    /* ---------------- 左列：连接卡（固定） ---------------- */
    g_lo.card_conn = raw(lx0, yl, lx1, yl + sd(208));
    {
        int ey  = yl + sd(38),  eyh = sd(32);
        int ey2 = yl + sd(88);
        int portw = sd(84);
        g_lo.ed_host = raw(lx0 + inner, ey, lx1 - inner - portw - sd(8), ey + eyh);
        g_lo.ed_port = raw(lx1 - inner - portw, ey, lx1 - inner, ey + eyh);
        g_lo.ed_media = raw(lx0 + inner, ey2, lx1 - inner, ey2 + eyh);
        g_lo.btn_conn = raw(lx0 + inner, yl + sd(144), lx1 - inner, yl + sd(188));
    }
    yl += sd(208) + gap;

    /* ---------------- 左列：驱动卡（固定） ---------------- */
    g_lo.card_drv = raw(lx0, yl, lx1, yl + sd(148));
    {
        int by = yl + sd(96);
        g_lo.btn_install   = raw(lx0 + inner, by, lx0 + inner + sd(104), by + sd(32));
        g_lo.btn_uninstall = raw(lx0 + inner + sd(112), by, lx0 + inner + sd(190), by + sd(32));
        g_lo.btn_help      = raw(lx0 + inner + sd(198), by, lx1 - inner, by + sd(32));
    }
    yl += sd(148);

    /* ---------------- 右列：电平卡（常驻，可滚动区起点） ---------------- */
    g_lo.card_meter = cs(rx0, yr, rx1, yr + sd(176));
    yr += sd(176) + gap;

    /* ---------------- 右列：音频参数（默认收起） ---------------- */
    g_lo.hdr_audio = cs(rx0, yr, rx1, yr + sd(38));
    g_lo.card_audio = cs(rx0, yr, rx1, yr + sd(38));
    yr += sd(38);
    if (g_ui.exp_audio) {
        int sw = (rx1 - rx0 - inner * 2) / 3;
        int ry = yr + sd(24);
        for (i = 0; i < 3; i++)
            g_lo.seg_rate[i] = cs(rx0 + inner + i * sw, ry, rx0 + inner + i * sw + sw - sd(6), ry + sd(30));
        ry = yr + sd(76);
        for (i = 0; i < 3; i++)
            g_lo.seg_buf[i] = cs(rx0 + inner + i * sw, ry, rx0 + inner + i * sw + sw - sd(6), ry + sd(28));
        g_lo.slider = cs(rx0 + inner, yr + sd(130), rx1 - inner, yr + sd(138));
        g_lo.card_audio = cs(rx0, g_lo.hdr_audio.top + g_ui.scroll, rx1, yr + sd(154));
        yr += sd(154);
    }
    yr += gap;

    /* ---------------- 右列：统计（默认收起） ---------------- */
    g_lo.hdr_stats = cs(rx0, yr, rx1, yr + sd(38));
    g_lo.card_stats = cs(rx0, yr, rx1, yr + sd(38));
    yr += sd(38);
    if (g_ui.exp_stats) {
        g_lo.card_stats = cs(rx0, g_lo.hdr_stats.top + g_ui.scroll, rx1, yr + sd(96));
        yr += sd(96);
    }
    yr += gap;

    /* ---------------- 右列：日志（默认收起） ---------------- */
    g_lo.hdr_log = cs(rx0, yr, rx1, yr + sd(38));
    g_lo.card_log = cs(rx0, yr, rx1, yr + sd(38));
    yr += sd(38);
    if (g_ui.exp_log) {
        g_lo.card_log = cs(rx0, g_lo.hdr_log.top + g_ui.scroll, rx1, yr + sd(220));
        yr += sd(220);
    }

    /* content_h 只由右列决定（左列固定不滚动） */
    g_lo.content_h = yr + sd(12);
    g_lo.view_h = g_ui.h - ui_title_h() - ui_status_h();

    {
        int maxv = g_lo.content_h - (ui_title_h() + g_lo.view_h);
        if (maxv < 0) maxv = 0;
        if (g_ui.scroll > maxv) g_ui.scroll = maxv;
        if (g_ui.scroll < 0) g_ui.scroll = 0;
    }
    /* 折叠面板收起时把子控件矩形清空，避免命中到看不见的区域 */
    if (!g_ui.exp_audio) {
        for (i = 0; i < 3; i++) { SetRectEmpty(&g_lo.seg_rate[i]); SetRectEmpty(&g_lo.seg_buf[i]); }
        SetRectEmpty(&g_lo.slider);
    }
}

int ui_hit_test(int x, int y)
{
    POINT p = { x, y };
    int i;
    if (PtInRect(&g_lo.btn_close, p)) return ID_CLOSE;
    if (PtInRect(&g_lo.btn_min, p)) return ID_MIN;
    if (PtInRect(&g_lo.btn_theme, p)) return ID_THEME;
    if (PtInRect(&g_lo.btn_conn, p)) return ID_CONN;
    if (PtInRect(&g_lo.btn_install, p)) return ID_INSTALL;
    if (PtInRect(&g_lo.btn_uninstall, p)) return ID_UNINSTALL;
    if (PtInRect(&g_lo.btn_help, p)) return ID_HELP;
    if (PtInRect(&g_lo.hdr_audio, p)) return ID_EXP_AUDIO;
    if (PtInRect(&g_lo.hdr_stats, p)) return ID_EXP_STATS;
    if (PtInRect(&g_lo.hdr_log, p)) return ID_EXP_LOG;
    for (i = 0; i < 3; i++) if (PtInRect(&g_lo.seg_rate[i], p)) return ID_RATE0 + i;
    for (i = 0; i < 3; i++) if (PtInRect(&g_lo.seg_buf[i], p)) return ID_BUF0 + i;
    {
        RECT s = g_lo.slider;
        if (!IsRectEmpty(&s)) {
            InflateRect(&s, 0, sd(10));
            if (PtInRect(&s, p)) return ID_SLIDER;
        }
    }
    return ID_NONE;
}

/* 两遍：第一遍排出 content_h 并把 scroll 夹紧，第二遍用夹紧后的 scroll 重算矩形。 */
void ui_layout_compute(void)
{
    layout_once();
    layout_once();
}
