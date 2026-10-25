/* ui_voicemeter.c — 输入电平卡（D-05/D-06/D-07/D-08）
 *
 * 视觉权威：design-demos/round2/win-final.html（定稿图即验收截图）。
 * 全部几何/颜色为该 HTML 在 1060×680 的实测值，过 sd() 支持高 DPI。
 *
 * 四要素同卡（D-06）：
 *   ① 环形电平 + 横条 + 历史（上区）
 *   ② 本地增益滑杆（中区，24px 分隔线上方）
 *   ③ 静音行（底行）
 *   区间用 24px 留白 + 1px border 分隔线。
 *
 * 环形电平（D-05）：270° 扫掠、起角 135°（7:30）顺时针；映射 −60…0 dBFS；
 *   弧头当前值圆点 = 6px 实心（分区着色）+ 1.5px bg-card 描边 + 5.5px 25% 光晕；
 *   峰值保持线 2px text-2（保持 1.2 s）；−12/−6 dB 目标刻度 1.5px text-3。
 * 横条（C-12）：60 段覆盖 −60…0 dBFS，段心 dB = −59.5+i；点亮条件 段心 < 当前 dB；
 *   段心 < −48 → accent 35%（dim）；段心 ≥ −3 → warn。
 * 历史（D-08）：最近 10 秒约 48 柱、高 56、R1、bg-sunken、accent 柱、间距 2。
 * 增益（D-07）：dB 刻度（−60…+6 dB，0 dB 位于 90.9%），读数「增益 N% · ±X.X dB」。
 */
#include "ui_voicemeter.h"
#include "ui_draw.h"
#include "ui_layout.h"
#include <math.h>
#include <wchar.h>

#define PI 3.14159265358979

#define BAR_SEGS     60        /* 横条段数（C-12） */
#define SILENT_FRAMES 40       /* 连续静音帧 → 无信号（20ms/帧 ≈ 0.8 s） */
#define OVER_LEVEL   0.86f     /* 过载阈值（≈ −1.3 dBFS） */
#define PEAK_HOLD_MS 1200      /* 峰值保持 1.2 s（D-05） */
#define PEAK_FALL_DBS 24.0f    /* 保持结束后的回落速度 dB/s */

static float  s_level = 0.0f;
static float  s_db    = -96.0f;   /* 当前 dBFS */
static float  s_peak_db = -96.0f; /* 峰值 dBFS（1.2 s 保持） */
static DWORD  s_peak_until = 0;
static DWORD  s_last = 0;
static int    s_state = 0;

int ui_meter_state(void) { return s_state; }

float ui_meter_cur_db(void) { return s_db; }

/* dBFS → 环形角度（度，0=正右、顺时针为正）：−60 dB → 135°，0 dB → 405° */
static double db_to_ang(double db)
{
    double t = (db + 60.0) / 60.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return 135.0 + t * 270.0;
}

static void polar(int cx, int cy, int rad, double ang, int *x, int *y)
{
    double r = ang * PI / 180.0;
    *x = cx + (int)((double)rad * cos(r));
    *y = cy + (int)((double)rad * sin(r));
}

void ui_meter_update(const EngineStats *st, int running)
{
    DWORD now = GetTickCount();
    float dt = s_last ? (now - s_last) / 1000.0f : 0.016f;
    if (dt > 0.5f) dt = 0.5f;
    if (dt < 0.0f) dt = 0.0f;
    s_last = now;

    if (running && st->silent_frames < SILENT_FRAMES) {
        /* attack 由引擎完成（level += (rms-level)*0.35），这里直接取 */
        s_level = st->level;
    } else {
        /* 停止推流 / 连续静音 → release 每秒 ×0.72，2 s 内基本归零 */
        s_level *= (float)pow(0.72, (double)dt);
        if (s_level < 0.0005f) s_level = 0.0f;
    }
    if (s_level < 0.0f) s_level = 0.0f;
    if (s_level > 1.0f) s_level = 1.0f;

    s_db = (s_level > 0.0005f) ? 20.0f * (float)log10((double)s_level) : -96.0f;
    if (s_db < -96.0f) s_db = -96.0f;
    if (s_db > 0.0f) s_db = 0.0f;

    /* 峰值：新高值立即刷新并重新保持 1.2 s；保持结束后按 24 dB/s 回落 */
    if (s_db > s_peak_db) {
        s_peak_db = s_db;
        s_peak_until = now + PEAK_HOLD_MS;
    } else if (now > s_peak_until) {
        s_peak_db -= PEAK_FALL_DBS * dt;
        if (s_peak_db < s_db) s_peak_db = s_db;
        if (s_peak_db < -96.0f) s_peak_db = -96.0f;
    }

    if (!running) s_state = 0;
    else if (s_level >= OVER_LEVEL) s_state = 2;
    else if (st->silent_frames >= SILENT_FRAMES) s_state = 0;
    else s_state = 1;
}

/* ---------------------------------------------------------------- 环形电平 */
static void arc_cap(HDC dc, int cx, int cy, int rad, double ang, int half, COLORREF c)
{
    /* GDI+ 笔帽为平帽，用等径实心圆补圆头（定稿图 stroke-linecap:round） */
    int x, y;
    RECT r;
    polar(cx, cy, rad, ang, &x, &y);
    SetRect(&r, x - half, y - half, x + half, y + half);
    ui_fill_round(dc, r, half, c, 255);
}

static void draw_ring(HDC dc)
{
    RECT r = g_lo.ring;
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int rad = sd(70), w = sd(8), half = w / 2;
    int muted = g_ui.mute;
    COLORREF arc_c;
    wchar_t buf[32];
    RECT tr;

    /* 分区着色：≥ −3 dB 进 warn 区（与横条一致）；静音时整环降 text-3 */
    if (muted) arc_c = g_th.text_mute;
    else arc_c = (s_db >= -3.0f) ? g_th.warn : g_th.accent;

    /* 轨道：270°，135° → 405°（7:30 顺时针到 4:30），bg-sunken，圆头 */
    ui_arc(dc, cx, cy, rad, 135.0, 405.0, g_th.sunken, w, 255);
    arc_cap(dc, cx, cy, rad, 135.0, half, g_th.sunken);
    arc_cap(dc, cx, cy, rad, 405.0, half, g_th.sunken);

    /* 目标刻度：−12 / −6 dB，径向 62→78，1.5px text-3（C-12） */
    {
        static const double tgt[2] = { -12.0, -6.0 };
        int k;
        for (k = 0; k < 2; k++) {
            int x0, y0, x1, y1;
            double a = db_to_ang(tgt[k]);
            polar(cx, cy, sd(62), a, &x0, &y0);
            polar(cx, cy, sd(78), a, &x1, &y1);
            ui_line(dc, x0, y0, x1, y1, g_th.text_mute, 1.5, 255);
        }
    }

    /* 数值弧：当前值（−60 dB 以下不画） */
    if (s_db > -60.0f) {
        double a = db_to_ang((double)s_db);
        ui_arc(dc, cx, cy, rad, 135.0, a, arc_c, w, 255);
        arc_cap(dc, cx, cy, rad, 135.0, half, arc_c);
        arc_cap(dc, cx, cy, rad, a, half, arc_c);
    }

    /* 峰值保持线：2px text-2，径向 62→78，圆头（保持 1.2 s） */
    if (s_peak_db > -60.0f) {
        int x0, y0, x1, y1;
        double a = db_to_ang((double)s_peak_db);
        polar(cx, cy, sd(62), a, &x0, &y0);
        polar(cx, cy, sd(78), a, &x1, &y1);
        ui_line(dc, x0, y0, x1, y1, g_th.text_dim, 2.0, 255);
        arc_cap(dc, cx, cy, sd(78), a, 1, g_th.text_dim);
    }

    /* 弧头当前值圆点：5.5px 25% 光晕 + 3px 实心 + 1.5px bg-card 描边（D-05） */
    if (s_db > -60.0f) {
        int x, y;
        double a = db_to_ang((double)s_db);
        RECT hr, dr;
        polar(cx, cy, rad, a, &x, &y);
        SetRect(&hr, x - sd(11) / 2, y - sd(11) / 2, x + sd(11) / 2, y + sd(11) / 2);
        ui_fill_round(dc, hr, sd(11) / 2, arc_c, 64);            /* 25% alpha */
        SetRect(&dr, x - sd(3), y - sd(3), x + sd(3), y + sd(3));
        ui_fill_round(dc, dr, sd(3), arc_c, 255);
        ui_stroke_round(dc, dr, sd(3), g_th.card, 255, 1);
    }

    /* 中心读数：30px Consolas + dBFS 11px text-3 */
    if (s_db > -60.0f)
        swprintf(buf, 32, L"%.1f", (double)s_db);
    else
        wcscpy(buf, L"—");
    SetRect(&tr, cx - sd(56), cy - sd(26), cx + sd(56), cy + sd(2));
    ui_text(dc, tr, buf, ui_font(30, FW_SEMIBOLD, 1), g_th.text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetRect(&tr, cx - sd(56), cy + sd(7), cx + sd(56), cy + sd(17));
    ui_text(dc, tr, L"dBFS", ui_font(11, FW_NORMAL, 0), g_th.text_mute,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* 图例：● 当前值 ┃ 峰值 1.2 s ┃ 目标（11/15 text-3，项间距 8） */
static void draw_ring_legend(HDC dc)
{
    RECT r = g_lo.ring_legend;
    HFONT f = ui_font(11, FW_NORMAL, 0);
    int x = r.left, cy = (r.top + r.bottom) / 2;
    int i;
    static const wchar_t *txt[3] = { L"当前值", L"峰值 1.2 s", L"目标" };
    for (i = 0; i < 3; i++) {
        RECT ir;
        if (i == 0) {                          /* 6px 实心圆点 = accent */
            SetRect(&ir, x, cy - sd(3), x + sd(6), cy + sd(3));
            ui_fill_round(dc, ir, sd(3), g_th.accent, 255);
            x += sd(6) + sd(4);
        } else {                               /* 2px / 1.5px 竖条 */
            int bw2 = (i == 1) ? 2 : 1;
            COLORREF c = (i == 1) ? g_th.text_dim : g_th.text_mute;
            ui_line(dc, x + bw2 / 2, cy - sd(4), x + bw2 / 2, cy + sd(4), c,
                    (double)bw2, 255);
            x += bw2 + sd(4);
        }
        ui_text(dc, (RECT){ x, r.top, x + sd(120), r.bottom }, txt[i], f,
                g_th.text_mute, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x += ui_measure_text(txt[i], f) + sd(8);
    }
}

/* ---------------------------------------------------------------- 横条电平 */
/* dim 段 = accent 35% 混合 bg-card（定稿图 .lit.dim{opacity:.35}）。
 * mix_c 在 ui_draw.c 为文件内静态，这里经 ui_mix 复用同一公式。 */
static COLORREF dim_accent(void)
{
    return ui_mix(g_th.card, g_th.accent, 35);
}

static void draw_bar(HDC dc)
{
    int x0 = g_lo.bar_x0, x1 = g_lo.bar_x1, y = g_lo.bar_y;
    int gap = sd(2), h = sd(12);
    int bw = ((x1 - x0) - gap * (BAR_SEGS - 1)) / BAR_SEGS;
    int muted = g_ui.mute;
    COLORREF dim_c = dim_accent();
    int i;

    if (bw < 2) bw = 2;
    for (i = 0; i < BAR_SEGS; i++) {
        double c = -59.5 + (double)i;          /* 段心 dB */
        RECT br;
        COLORREF col = g_th.meter_off;
        if (c < (double)s_db) {                /* 点亮条件：段心 < 当前 dB */
            if (muted) col = g_th.text_mute;
            else if (c < -48.0) col = dim_c;
            else if (c >= -3.0) col = g_th.warn;
            else col = g_th.accent;
        }
        SetRect(&br, x0 + i * (bw + gap), y, x0 + i * (bw + gap) + bw, y + h);
        ui_fill_round(dc, br, bw / 2, col, 255);
    }
    /* 阈值刻度：80% / 90%，1.5×5px text-3 */
    {
        int t;
        for (t = 0; t < 2; t++) {
            int tx = x0 + (x1 - x0) * (t == 0 ? 80 : 90) / 100;
            ui_line(dc, tx, y + h + sd(1), tx, y + h + sd(6), g_th.text_mute, 1.5, 255);
        }
    }
    /* dB 刻度：-48/-24/-12/-6/0 @ 20/60/80/90/100%（11/14 mono text-3） */
    {
        static const wchar_t *sc[5] = { L"-48", L"-24", L"-12", L"-6", L"0" };
        static const int pos[5] = { 20, 60, 80, 90, 100 };
        HFONT f = ui_font(11, FW_NORMAL, 1);
        int k;
        for (k = 0; k < 5; k++) {
            int sx = x0 + (x1 - x0) * pos[k] / 100;
            ui_text(dc, (RECT){ sx - sd(30), y + h + sd(7), sx + sd(30), y + h + sd(21) },
                    sc[k], f, g_th.text_mute, DT_CENTER | DT_SINGLELINE);
        }
    }
}

/* 目标区标签（横条上方 85%，11/14 text-3） */
static void draw_target_cap(HDC dc)
{
    int x0 = g_lo.bar_x0, x1 = g_lo.bar_x1, y = g_lo.bar_y;
    int tx = x0 + (x1 - x0) * 85 / 100;
    ui_text(dc, (RECT){ tx - sd(40), y - sd(14), tx + sd(40), y },
            L"目标", ui_font(11, FW_NORMAL, 0), g_th.text_mute,
            DT_CENTER | DT_SINGLELINE);
}

/* ---------------------------------------------------------------- 历史 */
/* 缓冲约定（main.c 采样端与这里共用）：hist[0] 最旧、hist[hist_count-1] 最新；
 * 满 48 柱后整体左移一格再写入新值。柱高 = dB 映射的 0..1 比例。 */
static void draw_history(HDC dc)
{
    RECT c = g_lo.hist;
    RECT lr;
    int i, n = HIST_BARS, gap = sd(2), pad = sd(6);
    int bw = ((c.right - c.left) - pad * 2 - gap * (n - 1)) / n;
    int start = (g_ui.hist_count >= n) ? g_ui.hist_count - n : 0;

    SetRect(&lr, c.left, c.top - sd(18), c.left + sd(160), c.top - sd(2));
    ui_text(dc, lr, L"最近 10 秒", ui_font(12, FW_NORMAL, 0), g_th.text_dim,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ui_fill_round(dc, c, sd(4), g_th.sunken, 255);
    if (bw < 1) bw = 1;
    for (i = 0; i < n; i++) {
        int src = start + i;                   /* 从左到右 = 时间正序 */
        float v = (src < g_ui.hist_count) ? g_ui.hist[src] : 0.0f;
        int bh = (int)(v * (c.bottom - c.top));
        RECT br;
        if (bh < sd(2)) bh = sd(2);             /* min-height 2px */
        if (bh > c.bottom - c.top) bh = c.bottom - c.top;
        SetRect(&br, c.left + pad + i * (bw + gap), c.bottom - bh,
                c.left + pad + i * (bw + gap) + bw, c.bottom);
        ui_fill_round(dc, br, sd(4), g_th.accent, 255);
    }
}

/* ---------------------------------------------------------------- 增益 */
static void draw_gain(HDC dc, RECT card)
{
    int x0 = g_lo.bar_x0, x1 = g_lo.bar_x1;
    int gy = card.top + sd(257);
    wchar_t rd[48];
    RECT lr = { x0, gy, x0 + sd(280), gy + sd(18) };
    RECT rr = { x1 - sd(220), gy, x1, gy + sd(18) };

    ui_gain_reading(rd, 48, g_ui.gain);
    ui_text(dc, lr, L"电脑端本地增益（0–200%）", ui_font(13, FW_NORMAL, 0),
            g_th.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ui_text(dc, rr, rd, ui_font(13, FW_NORMAL, 1), g_th.text,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    /* 滑杆：轨道 6 胶囊 bg-sunken、填充 accent、拇指 16 白底 Rfull */
    ui_slider(dc, g_lo.slider, ui_gain_pos(g_ui.gain), ID_SLIDER);
    /* 0 dB 刻度：90.9% 处 1.5×12 accent 竖线（D-07） */
    {
        int zx = x0 + (x1 - x0) * 909 / 1000;
        ui_line(dc, zx, g_lo.slider.top + sd(2), zx, g_lo.slider.top + sd(14),
                g_th.accent, 1.5, 255);
    }
    /* dB 刻度：-60/-36/-24/-12/0 dB/+6 @ 0/36.4/54.5/72.7/90.9/100% */
    {
        static const wchar_t *sc[6] = { L"-60", L"-36", L"-24", L"-12", L"0 dB", L"+6" };
        static const int pos[6] = { 0, 364, 545, 727, 909, 1000 };
        HFONT f = ui_font(11, FW_NORMAL, 1);
        int sy = g_lo.slider.bottom + sd(2);
        int k;
        for (k = 0; k < 6; k++) {
            int sx = x0 + (x1 - x0) * pos[k] / 1000;
            ui_text(dc, (RECT){ sx - sd(30), sy, sx + sd(30), sy + sd(14) },
                    sc[k], f, (k == 4) ? g_th.text_dim : g_th.text_mute,
                    DT_CENTER | DT_SINGLELINE);
        }
    }
}

/* ---------------------------------------------------------------- 静音行 */
static void draw_mute_row(HDC dc)
{
    RECT sw = g_lo.sw_mute;
    RECT lr = { g_lo.bar_x0, sw.top, g_lo.bar_x0 + sd(200), sw.bottom };
    ui_text(dc, lr, L"静音", ui_font(13, FW_NORMAL, 0), g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ui_switch(dc, sw, g_ui.mute);
}

/* ---------------------------------------------------------------- 整卡 */
void ui_meter_draw(HDC dc, RECT r)
{
    ui_card(dc, r);

    /* 标题行：输入电平 14/20 Semibold + 右侧峰值读数（13/18，值 Consolas） */
    ui_text(dc, (RECT){ r.left + sd(16), r.top + sd(12), r.left + sd(200), r.top + sd(32) },
            L"输入电平", ui_font(14, FW_SEMIBOLD, 0), g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    {
        wchar_t pv[48];
        HFONT vf = ui_font(13, FW_NORMAL, 1);
        int vw, pw;
        if (s_peak_db > -60.0f)
            swprintf(pv, 48, L"%.1f dBFS", (double)s_peak_db);
        else
            wcscpy(pv, L"— dBFS");
        vw = ui_measure_text(pv, vf);
        pw = ui_measure_text(L"峰值 ", ui_font(13, FW_NORMAL, 0));
        ui_text(dc, (RECT){ r.right - sd(16) - vw - pw, r.top + sd(12),
                            r.right - sd(16) - vw, r.top + sd(32) },
                L"峰值 ", ui_font(13, FW_NORMAL, 0), g_th.text_dim,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        ui_text(dc, (RECT){ r.right - sd(16) - vw, r.top + sd(12),
                            r.right - sd(16), r.top + sd(32) },
                pv, vf, g_th.text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    /* hint（12/16 text-2） */
    ui_text(dc, (RECT){ g_lo.bar_x0, r.top + sd(52), g_lo.bar_x1, r.top + sd(68) },
            L"说话试一下：让电平峰值停在「目标」区间。",
            ui_font(12, FW_NORMAL, 0), g_th.text_dim, DT_LEFT | DT_SINGLELINE);

    draw_ring(dc);
    draw_ring_legend(dc);
    draw_target_cap(dc);
    draw_bar(dc);
    draw_history(dc);

    /* 区间分隔：24 留白 + 1px border + 24 留白（D-06） */
    {
        int dy = r.top + sd(233);
        ui_line(dc, g_lo.bar_x0, dy, g_lo.bar_x1, dy, g_th.stroke, 1.0, 255);
    }
    draw_gain(dc, r);
    draw_mute_row(dc);
}
