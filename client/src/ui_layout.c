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
static int g_small_gainext;              /* R-47：1 = 电平卡底行放不下三开关全称 */

const wchar_t *ui_lbl_uninstall(void) { return g_small_hdr ? L"卸载" : L"卸载驱动"; }
const wchar_t *ui_lbl_help(void)      { return g_small_hdr ? L"说明" : L"命令行说明书"; }
/* R-47 扩展增益开关的标签：窄窗缩短为「扩展」（与顶栏同一套口径）。 */
const wchar_t *ui_lbl_gainext(void)   { return g_small_gainext ? L"扩展" : L"扩展增益"; }

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
        int t_step  = T ? sd(38)  : sd(49);   /* 三步进度 */
        int t_tlab  = T ? sd(64)  : sd(90);   /* 传输方式标签 */
        int t_seg   = T ? sd(76)  : sd(108);  /* 四段 segmented */
        int h_seg   = T ? sd(28)  : sd(36);   /* R-24：按钮高 32→36（tight 保持 30 不破版） */
        int t_hlab  = T ? sd(108) : sd(153);  /* 手机 IP 标签 */
        int t_host  = T ? sd(127) : sd(170);  /* 手机 IP 输入框 */
        int h_in    = T ? sd(28)  : sd(32);   /* 输入框高度不变（R-10 只改对齐） */
        int h_btn   = T ? sd(28)  : sd(36);   /* 主按钮：R-24 32→36 */
        int t_plab  = T ? sd(159) : sd(216);  /* 端口标签 */
        int t_port  = T ? sd(178) : sd(234);  /* 端口输入框 */
        int t_sw    = T ? sd(212) : sd(274);  /* R-44：自动重连开关行 */
        int h_sw    = T ? sd(28)  : sd(32);   /* 开关高 */
        int t_btn   = T ? sd(244) : sd(314);  /* 主按钮 */
        int bpad    = T ? sd(12)  : sd(16);   (void)bpad;

        /* 卡片总高：最后一块（主按钮）+ 底部内边距 */
        g_lo.card_conn = raw(lx0, yl, lx1, yl + (T ? sd(284) : sd(362)));
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
            /* R-44：自动重连开关归位到**连接卡**（它本来就是连接行为，
             * 放在电平卡里既不合语义、也和"连接/断开"状态离得太远）。
             * 位置：端口输入与主按钮之间的一行，右对齐到卡片内距，
             * 标签「自动重连」在开关左侧（同电平卡静音/监听的排法）。 */
            {
                int sw = sd(52);
                g_lo.sw_autorec = raw(cx1 - sw, yl + t_sw, cx1, yl + t_sw + h_sw);
            }
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
                /* R-43：徽标 / 卸载驱动 / 命令行说明书 三件同排。
                 * 口径统一：11px Semibold、同一行高（top+12 … top+32）、垂直居中；
                 * 按钮宽 = 文字 + 左右各 8px（文字居中，hover 不改宽）；
                 * 三者间距 8px；整行右端留 12px 卡片内距。 */
                int bh = sd(20);
                int g8 = sd(8);
                int pad = sd(16);                        /* 8 + 8 */
                int hdrw = cx1 - cx0;
                int titlew, badgew, hw, uw;
                HFONT lf = ui_font(11, FW_SEMIBOLD, 0);  /* 与徽标/BTN_TEXT 同款 */
                HFONT sf = ui_font(11, FW_SEMIBOLD, 0);
                int rright = lx1 - sd(12);
                /* 标题「驱动」占位 + 徽标最宽文案（驱动已就绪/驱动需重启/驱动未安装） */
                titlew = ui_measure_text(L"驱动", ui_font(15, FW_SEMIBOLD, 0));
                badgew = ui_measure_text(L"驱动未安装", sf) + sd(26);   /* 徽标内含圆点+间隙 */
                g_small_hdr = 0;
                hw = ui_measure_text(ui_lbl_help(), lf) + pad;
                uw = ui_measure_text(ui_lbl_uninstall(), lf) + pad;
                if (titlew + sd(12) + badgew + g8 * 2 + hw + uw > hdrw) {
                    g_small_hdr = 1;                     /* 窄窗：短标签（同字号同基线） */
                    hw = ui_measure_text(ui_lbl_help(), lf) + pad;
                    uw = ui_measure_text(ui_lbl_uninstall(), lf) + pad;
                }
                g_lo.btn_help = raw(rright - hw, yl + sd(12), rright, yl + sd(12) + bh);
                g_lo.btn_uninstall = raw(rright - hw - g8 - uw, yl + sd(12),
                                         rright - hw - g8, yl + sd(12) + bh);
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
            /* R-39：底行开关（静音 / 监听），统一 52×32。
             * R-44：自动重连已移出电平卡，归位到左侧连接卡。
             * R-47：末尾再加一个「扩展增益」，共三个。
             * 从 x0 顺序排，标签画在各自开关左侧。 */
            {
                HFONT lf = ui_font(14, FW_NORMAL, 0);
                int sw = sd(52), sh = sd(32);
                int gap = sd(14), lg = sd(6);
                int x = rx, y = c.top + sd(330);
                int avail = (c.right - inner) - rx;
                /* R-47 窄窗退化：三个开关 + 全标签放不下时，把「扩展增益」缩短为
                 * 「扩展」。与 R-43 顶栏三件同排同一套口径：宁可短标签，也不让
                 * 控件越出卡片右内边距。 */
                int lw_mute = ui_measure_text(L"静音", lf);
                int lw_mon  = ui_measure_text(L"监听", lf);
                int lw_ext  = ui_measure_text(L"扩展增益", lf);
                int need = lw_mute + lw_mon + lw_ext + sw * 3 + lg * 3 + gap * 2;
                int used_ext = 0;
                if (need > avail) {
                    lw_ext = ui_measure_text(L"扩展", lf);
                    g_small_gainext = 1;
                } else {
                    g_small_gainext = 0;
                }
                g_lo.sw_mute = cs(x + lw_mute + lg, y, x + lw_mute + lg + sw, y + sh);
                used_ext = 0; (void)used_ext;
                x = g_lo.sw_mute.right + gap + lw_mon + lg;
                g_lo.sw_mon = cs(x, y, x + sw, y + sh);
                x = g_lo.sw_mon.right + gap + lw_ext + lg;
                g_lo.sw_gainext = cs(x, y, x + sw, y + sh);
                (void)sw; (void)sh;
            }
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
    /* 安装驱动 / 打开声音设置 **共用同一格**（ui_layout 里 btn_install = btn_openset）：
     * 未就绪画「安装驱动」，就绪画「打开声音设置」。命中必须与绘制同源，否则
     * 未装驱动的机器上点「安装驱动」会返回 ID_OPENSET——打开 Windows 声音设置
     * （Win11 真机实测踩到，等于装不了驱动）。 */
    if (g_ui.drv.state == DRV_READY) {
        if (PtInRect(&g_lo.btn_openset, p)) return ID_OPENSET;
    } else {
        if (PtInRect(&g_lo.btn_install, p)) return ID_INSTALL;
    }
    if (PtInRect(&g_lo.btn_uninstall, p)) {
        /* 未安装时该按钮画成禁用（ui_draw 里 enabled = state != NOT_INSTALLED），
         * 命中也要一致：否则点它会真的去拉起卸载器。 */
        return (g_ui.drv.state != DRV_NOT_INSTALLED) ? ID_UNINSTALL : ID_NONE;
    }
    if (PtInRect(&g_lo.btn_help, p))      return ID_HELP;
    if (PtInRect(&g_lo.sw_mute, p))       return ID_MUTE;
    if (PtInRect(&g_lo.sw_mon, p))        return ID_MONITOR;
    if (PtInRect(&g_lo.sw_gainext, p))    return ID_GAINEXT;
    if (PtInRect(&g_lo.sw_autorec, p))    return ID_AUTOREC;
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
