/* ui_layout.c — 两列布局与命中测试
 *
 * 几何来源：design-demos/round2/win-final.html（定稿图即验收截图，逐元素对齐）。
 * 所有数值为该 HTML 在 1060×680 下的实测值，过 sd() 支持高 DPI。
 */
#include "ui_layout.h"
#include "ui_draw.h"

UiLayout g_lo;

int ui_title_h(void)  { return sd(48); }
int ui_status_h(void) { return sd(28); }

/* 内容坐标 → 客户区坐标（减滚动偏移）。左列不经过此函数（固定不滚）。 */
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

/* ---------------------------------------------------------------- 常量 */
const wchar_t *ui_step_label(int i)
{
    static const wchar_t *lb[3] = { L"装驱动", L"手机启动", L"电脑连接" };
    return (i >= 0 && i < 3) ? lb[i] : L"";
}

/* 三步进度的标签宽度（布局期测量，连线均分剩余空间） */
static int step_label_w(int i)
{
    return ui_measure_text(ui_step_label(i), ui_font(11, FW_NORMAL, 0));
}

/* 驱动卡标题行两个小文字按钮的文案（宽度决定权在布局手里，见 layout_once）。
 * 为什么不把字面量写在绘制里：按钮宽度要按文字实测算，文案和矩形必须是
 * 同一份决定，否则「算宽度用全称、画的时候用简称」这类漂移迟早发生。 */
static int g_small_hdr;                  /* 1 = 标题行空间不足，用短标签 */

const wchar_t *ui_lbl_uninstall(void) { return g_small_hdr ? L"卸载" : L"卸载驱动"; }
const wchar_t *ui_lbl_help(void)      { return g_small_hdr ? L"说明" : L"命令行说明书"; }

static void layout_once(void)
{
    int pad = sd(20), gap = sd(16), inner = ui_pad();   /* R-24：卡片内距 16→14 */
    int leftW, x1, lx0, lx1, rx0, rx1;
    int y0, yl, yr;
    int i;

    /* 左列 = clamp(窗口宽 ×1/3, 320, 400)（手册 §6.1；1060 时 353） */
    leftW = g_ui.w / 3;
    if (leftW > sd(400)) leftW = sd(400);
    if (leftW < sd(320)) leftW = sd(320);

    x1  = g_ui.w - pad;
    lx0 = pad;
    lx1 = pad + leftW;
    rx0 = lx1 + gap;
    rx1 = x1;
    y0  = ui_title_h() + sd(16);

    /* tight：左列在当前窗高下放不下时垂直压缩（900×560 不裁切） */
    g_lo.view_h = g_ui.h - ui_title_h() - ui_status_h();
    g_lo.tight  = (g_lo.view_h < sd(540));

    /* ---------------- 标题栏（D-01）---------------- */
    g_lo.title = raw(0, 0, g_ui.w, ui_title_h());
    {
        int bw = sd(46), bh = ui_title_h(), rm = sd(8);
        int segw = sd(64), segh = sd(28);
        g_lo.btn_close = raw(g_ui.w - rm - bw, 0, g_ui.w - rm, bh);
        g_lo.btn_max   = raw(g_lo.btn_close.left - bw, 0, g_lo.btn_close.left, bh);
        g_lo.btn_min   = raw(g_lo.btn_max.left - bw, 0, g_lo.btn_max.left, bh);
        /* 主题两段 segmented：贴在三键左侧，间隔 16 */
        g_lo.seg_theme[1] = raw(g_lo.btn_min.left - sd(16) - segw,
                                (ui_title_h() - segh) / 2,
                                g_lo.btn_min.left - sd(16),
                                (ui_title_h() - segh) / 2 + segh);
        g_lo.seg_theme[0] = raw(g_lo.seg_theme[1].left - segw, g_lo.seg_theme[1].top,
                                g_lo.seg_theme[1].left, g_lo.seg_theme[1].bottom);
    }

    yl = y0;
    yr = y0;

    /* ---------------- 左列：连接卡（D-02/D-03/D-04）---------------- */
    {
        int T = g_lo.tight;
        int t_title = T ? sd(10)  : sd(12);   /* 卡标题 */
        int t_step  = T ? sd(42)  : sd(49);   /* 三步进度 */
        int t_tlab  = T ? sd(76)  : sd(90);   /* 传输方式标签 */
        int t_seg   = T ? sd(92)  : sd(108);  /* 四段 segmented */
        int h_seg   = T ? sd(30)  : sd(36);   /* R-24：按钮高 32→36（tight 保持 30 不破版） */
        int t_hlab  = T ? sd(132) : sd(153);  /* 手机 IP 标签 */
        int t_host  = T ? sd(148) : sd(170);  /* 手机 IP 输入框 */
        int h_in    = T ? sd(30)  : sd(32);   /* 输入框高度不变（R-10 只改对齐） */
        int h_btn   = T ? sd(30)  : sd(36);   /* 主按钮：R-24 32→36 */
        int t_plab  = T ? sd(188) : sd(216);  /* 端口标签 */
        int t_port  = T ? sd(204) : sd(234);  /* 端口输入框 */
        int t_btn   = T ? sd(244) : sd(290);  /* 主按钮 */
        int bpad    = T ? sd(12)  : sd(16);   (void)bpad;

        g_lo.card_conn = raw(lx0, yl, lx1, yl + (T ? sd(286) : sd(338)));
        {
            int cx0 = lx0 + inner, cx1 = lx1 - inner;
            int dot = sd(16);
            /* 三步：圆 + 6 间隙 + 标签；两条连接线 flex 均分剩余空间
             * （定稿图 .step-line{flex:1;margin:0 8px}），左右各内缩 8。
             * 注意 line_w 是「每条线」的宽度：剩余空间扣掉内容与 4×8 后对半分。 */
            int lw[3], total = 0, line_w, sx = cx0, k;
            for (k = 0; k < 3; k++) { lw[k] = step_label_w(k); total += dot + sd(6) + lw[k]; }
            line_w = ((cx1 - cx0) - total - sd(16) * 2) / 2;
            if (line_w < sd(16)) line_w = sd(16);
            for (k = 0; k < 3; k++) {
                SetRect(&g_lo.step[k], sx, yl + t_step, sx + dot, yl + t_step + dot);
                sx += dot + sd(6) + lw[k];
                if (k < 2) sx += line_w + sd(16);
            }
            /* 传输方式四段（等分） */
            {
                int cw = (cx1 - cx0) / 4;
                for (k = 0; k < 4; k++)
                    SetRect(&g_lo.seg_trans[k], cx0 + k * cw, yl + t_seg,
                            cx0 + (k + 1) * cw, yl + t_seg + h_seg);
            }
            /* 手机 IP（整宽） */
            g_lo.ed_host = raw(cx0, yl + t_host, cx1, yl + t_host + h_in);
            /* 控制端口 / 媒体端口（半宽，间距 8） */
            {
                int hw = (cx1 - cx0 - sd(8)) / 2;
                g_lo.ed_port  = raw(cx0, yl + t_port, cx0 + hw, yl + t_port + h_in);
                g_lo.ed_media = raw(cx0 + hw + sd(8), yl + t_port, cx1, yl + t_port + h_in);
            }
            /* 主按钮：整卡宽，运行态 = danger + 呼吸（D-04） */
            g_lo.btn_conn = raw(cx0, yl + t_btn, cx1, yl + t_btn + h_btn);
            (void)t_title; (void)t_hlab; (void)t_plab; (void)t_tlab;
            (void)bpad;
        }
        yl += g_lo.card_conn.bottom - g_lo.card_conn.top + gap;
    }

    /* ---------------- 左列：驱动卡（D-09）---------------- */
    {
        int T = g_lo.tight;
        int t_pill = T ? sd(38)  : sd(44);
        int t_btn  = T ? sd(112) : sd(129);
        int h_btn  = T ? sd(30)  : sd(36);    /* R-24：按钮高 32→36 */
        g_lo.card_drv = raw(lx0, yl, lx1, yl + (T ? sd(154) : sd(181)));
        {
            int cx0 = lx0 + inner, cx1 = lx1 - inner;
            g_lo.pill_dev = raw(cx0, yl + t_pill, cx1, yl + t_pill + sd(24));
            g_lo.pill_fmt = raw(cx0, yl + t_pill + sd(32), cx1, yl + t_pill + sd(56));
            /* 就绪 = 打开声音设置（secondary 通栏）；未就绪 = 安装驱动（primary 通栏） */
            g_lo.btn_openset  = raw(cx0, yl + t_btn, cx1, yl + t_btn + h_btn);
            g_lo.btn_install  = g_lo.btn_openset;
            /* 标题行右侧两个小文字按钮：宽度按**实测字宽**算，不再写死 56px。
             * 原来固定 56px 装不下 11px 下的「命令行说明书」（实测约 66px），
             * DT_CENTER 会让文字居中溢出矩形，右侧被卡片边缘裁掉 ——
             * leftW 固定 320（900×560）时表现为「行说明书」。
             * 一行放不下（标题 + 状态徽标 + 两个按钮）时退化为短标签
             * （卸载 / 说明），窄到头也不会越界或压到徽标上。 */
            {
                int bh = sd(18);
                int g8 = sd(8);
                int hdrw = cx1 - cx0;
                int titlew, badgew, hw, uw, pad = sd(14);
                HFONT lf = ui_font(11, FW_NORMAL, 0);     /* 与 BTN_TEXT 同款 */
                HFONT sf = ui_font(11, FW_SEMIBOLD, 0);   /* 与状态徽标同款 */
                /* 标题「驱动」占位 + 徽标最宽文案（驱动已就绪/驱动需重启/驱动未安装） */
                titlew = ui_measure_text(L"驱动", ui_font(14, FW_SEMIBOLD, 0));
                badgew = ui_measure_text(L"驱动未安装", sf) + sd(26);
                g_small_hdr = 0;
                hw = ui_measure_text(ui_lbl_help(), lf) + pad;
                uw = ui_measure_text(ui_lbl_uninstall(), lf) + pad;
                if (titlew + sd(12) + badgew + g8 * 2 + hw + uw > hdrw) {
                    g_small_hdr = 1;                      /* 窄窗：短标签 */
                    pad = sd(10);
                    hw = ui_measure_text(ui_lbl_help(), lf) + pad;
                    uw = ui_measure_text(ui_lbl_uninstall(), lf) + pad;
                }
                g_lo.btn_help = raw(cx1 - hw, yl + sd(13), cx1, yl + sd(13) + bh);
                g_lo.btn_uninstall = raw(cx1 - hw - g8 - uw, yl + sd(13),
                                         cx1 - hw - g8, yl + sd(13) + bh);
            }
        }
        yl += g_lo.card_drv.bottom - g_lo.card_drv.top;
    }
    g_lo.left_h = yl - y0;

    /* ---------------- 右列：电平卡（常驻，D-05…D-08）---------------- */
    /* 右子列 x = 卡内 16 + 环柱 184 + 16（定稿图 .level-body{gap:16}）；
     * 横条 60 段×5px + 59×2px 间距 = 418，正好铺满子列（606…1024 @1060）。 */
    {
        RECT c = raw(rx0, yr, rx1, yr + sd(374));
        int ringx = c.left + inner + sd(2);
        int rx = c.left + inner + sd(184) + sd(16);
        g_lo.card_meter = cs(c.left, c.top, c.right, c.bottom);
        /* R-09：环形区的所有子件全部过 cs()，跟卡片一起滚动。
         * 旧实现 ring/图例/滑杆/静音用 raw()——卡片滚走了它们却钉在原地，
         * 既脱锚又会画到别的卡片上（R-08 的浮层就是这么来的之一）。 */
        g_lo.ring = cs(ringx, c.top + sd(103), ringx + sd(180), c.top + sd(283));
        /* 图例改流式三行（当前值/峰值/目标，各带读数）：环下 8px 起，行高 18 */
        g_lo.ring_legend = cs(c.left + inner, c.top + sd(291),
                              c.left + inner + sd(188), c.top + sd(345));
        {
            int rw = c.right - inner - rx;
            g_lo.hist   = cs(rx, c.top + sd(150), c.right - inner, c.top + sd(206));
            g_lo.slider = cs(rx, c.top + sd(280), rx + rw, c.top + sd(296));
            g_lo.sw_mute = cs(c.right - inner - sd(40), c.top + sd(341),
                              c.right - inner, c.top + sd(361));
            g_lo.bar_x0 = rx;
            g_lo.bar_x1 = c.right - inner;
            g_lo.bar_y  = c.top + sd(83) - g_ui.scroll;   /* 与 cs() 同一坐标系 */
        }
        yr = c.bottom + gap;
    }

    /* ---------------- 右列：三个折叠面板（D-11 默认收起）---------------- */
    /* 收起态卡片高 48（R3 + 1px border，标题行 40 + 垂直内边距 4 各） */
    g_lo.hdr_audio = cs(rx0, yr, rx1, yr + sd(48));
    g_lo.card_audio = g_lo.hdr_audio;
    yr += sd(48);
    if (g_ui.exp_audio) {
        int cw = (rx1 - rx0 - inner * 2 - sd(16)) / 3;
        int ry = yr + sd(31);
        for (i = 0; i < 3; i++)
            g_lo.seg_rate[i] = cs(rx0 + inner + i * (cw + sd(8)), ry,
                                  rx0 + inner + i * (cw + sd(8)) + cw, ry + sd(34));
        ry = yr + sd(88);
        for (i = 0; i < 3; i++)
            g_lo.seg_buf[i] = cs(rx0 + inner + i * (cw + sd(8)), ry,
                                 rx0 + inner + i * (cw + sd(8)) + cw, ry + sd(34));
        g_lo.card_audio = cs(rx0, g_lo.hdr_audio.top + g_ui.scroll, rx1, yr + sd(194));
        yr += sd(194);
    }
    /* R-24：收起态默认占 48px，面板间距 16 会让右列下半段空白散成三截；
     * 收起时压到 8，空白集中到底部一处 */
    yr += g_ui.exp_audio ? gap : sd(8);

    g_lo.hdr_stats = cs(rx0, yr, rx1, yr + sd(48));
    g_lo.card_stats = g_lo.hdr_stats;
    yr += sd(48);
    if (g_ui.exp_stats) {
        g_lo.card_stats = cs(rx0, g_lo.hdr_stats.top + g_ui.scroll, rx1, yr + sd(182));
        yr += sd(182);
    }
    yr += g_ui.exp_stats ? gap : sd(8);

    g_lo.hdr_log = cs(rx0, yr, rx1, yr + sd(48));
    g_lo.card_log = g_lo.hdr_log;
    yr += sd(48);
    if (g_ui.exp_log) {
        g_lo.btn_copylog = cs(rx1 - inner - sd(72), yr + sd(12),
                              rx1 - inner, yr + sd(36));
        g_lo.card_log = cs(rx0, g_lo.hdr_log.top + g_ui.scroll, rx1, yr + sd(232));
        yr += sd(232);
    } else {
        SetRectEmpty(&g_lo.btn_copylog);
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
    }
}

int ui_hit_test(int x, int y)
{
    POINT p = { x, y };
    int i;
    /* 标题栏控件 */
    if (PtInRect(&g_lo.btn_close, p)) return ID_CLOSE;
    if (PtInRect(&g_lo.btn_max, p))   return ID_MAX;
    if (PtInRect(&g_lo.btn_min, p))   return ID_MIN;
    if (PtInRect(&g_lo.seg_theme[0], p)) return ID_THEME_LIGHT;
    if (PtInRect(&g_lo.seg_theme[1], p)) return ID_THEME_DARK;
    /* 左列 */
    if (PtInRect(&g_lo.btn_conn, p))      return ID_CONN;
    if (PtInRect(&g_lo.btn_openset, p))   return ID_OPENSET;
    if (PtInRect(&g_lo.btn_install, p))   return ID_INSTALL;
    if (PtInRect(&g_lo.btn_uninstall, p)) return ID_UNINSTALL;
    if (PtInRect(&g_lo.btn_help, p))      return ID_HELP;
    if (PtInRect(&g_lo.sw_mute, p))       return ID_MUTE;
    /* 右列 */
    if (PtInRect(&g_lo.hdr_audio, p)) return ID_EXP_AUDIO;
    if (PtInRect(&g_lo.hdr_stats, p)) return ID_EXP_STATS;
    if (PtInRect(&g_lo.hdr_log, p))   return ID_EXP_LOG;
    if (PtInRect(&g_lo.btn_copylog, p)) return ID_COPYLOG;
    for (i = 0; i < 4; i++) if (PtInRect(&g_lo.seg_trans[i], p)) return ID_TRANS0 + i;
    for (i = 0; i < 3; i++) if (PtInRect(&g_lo.seg_rate[i], p)) return ID_RATE0 + i;
    for (i = 0; i < 3; i++) if (PtInRect(&g_lo.seg_buf[i], p))  return ID_BUF0 + i;
    {
        RECT s = g_lo.slider;
        if (!IsRectEmpty(&s)) {
            InflateRect(&s, 0, sd(4));       /* 命中区 24px（D-06） */
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
