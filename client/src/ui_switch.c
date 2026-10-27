/* ui_switch.c — 开关控件（R-46）：纯 GDI/GDI+ 实时矢量渲染 + 滑块动画
 *
 * 为什么单独成文件：开关是「连接板块」与「电平板块」共用的核心控件，
 * 也是唯一有**动画状态**的控件。独立成文件后可以脱离整个 UI 单独测：
 * tools/switch_anim_probe.c 逐帧渲染并比对滑块圆心位置，
 * 证明它是连续插值（矢量）而不是两态硬切（贴图）。
 *
 * 与 ui_draw.c 的分工：
 *   ui_draw.c  提供通用原语（ui_fill_round / sd / ui_mix）
 *   ui_switch.c 只做开关本身，不含任何业务布局知识
 */
#include "ui_draw.h"

/* 把某个颜色向白色提亮 pct%（渐变用） */
static COLORREF mix_c_white(COLORREF c, int pct)
{
    return ui_mix(c, RGB(255, 255, 255), pct);
}

/* 开关（C-08 形态 52×32）：滑块直径随控件高度走，40×20 / 52×32 都能用
 *
 * R-46：这里是**纯 GDI/GDI+ 实时矢量渲染**（不是贴图），所以做「手机上那种
 * 丝滑滑块 + 渐变」的成本很低 —— 难点不在画，在于状态。
 *   · 轨道底色：按 pos 在 sunken → accent 之间插值（渐变）
 *   · 轨道前景：pos 覆盖到的那一段用 accent，形成横向渐变（用裁剪 + 竖条画出）
 *   · 滑块：位置按 pos 插值，带一圈柔和阴影
 *   · pos 由 16ms 节拍推进（ui_switch_anim_step），120ms 走完 0↔1000
 * 结果是连续位移+连续变色，不是"啪"地跳到另一态。 */
void ui_switch(HDC dc, RECT r, int on, int pos)
{
    int h;
    /* Keep the original hit rectangle while drawing a slimmer track. */
    if (r.bottom - r.top > sd(24)) {
        int inset_y = (r.bottom - r.top - sd(24)) / 2;
        r.top += inset_y; r.bottom -= inset_y;
    }
    h = r.bottom - r.top;
    int rad = h / 2;
    int inset = sd(4);
    int d = h - inset * 2;
    RECT knob;
    COLORREF base, on_c;
    if (d < sd(8)) d = sd(8);
    if (pos < 0) pos = 0;
    if (pos > 1000) pos = 1000;

    base = ui_mix(g_th.sunken, g_th.card_hi, pos * 40 / 1000);
    on_c = g_th.accent;
    ui_fill_round(dc, r, rad, base, 255);

    /* 已"点亮"的那一段：横向渐变（多竖条插值，clip 在圆角轨道内） */
    if (pos > 0) {
        int sc = SaveDC(dc);
        HRGN clip = CreateRoundRectRgn(r.left, r.top, r.right, r.bottom,
                                       rad * 2, rad * 2);
        int w = r.right - r.left;
        int fx = r.left + w * pos / 1000;
        int i, n = 24;
        if (clip) SelectClipRgn(dc, clip);
        for (i = 0; i < n; i++) {
            int x0 = r.left + w * i / n;
            int x1 = r.left + w * (i + 1) / n + 1;
            /* 渐变方向：由 accent（本色）向**更亮**过渡。
             * 不能用 on_accent —— 深色主题里 on_accent 是近黑，混过去会让
             * 轨道右端发暗，看起来像"没点亮"。 */
            COLORREF c = ui_mix(on_c, mix_c_white(on_c, 45), i * 100 / n);
            RECT b = { x0, r.top, x1, r.bottom };
            if (b.right > fx) b.right = fx;
            if (b.right > b.left) {
                HBRUSH br = CreateSolidBrush(c);
                FillRect(dc, &b, br);
                DeleteObject(br);
            }
        }
        RestoreDC(dc, sc);
        if (clip) DeleteObject(clip);
    }

    /* 滑块：位置按 pos 插值；色随状态在 text_mute ↔ 白 之间过渡 */
    {
        int travel = (r.right - inset - d) - (r.left + inset);
        int kx = r.left + inset + (travel * pos / 1000);
        knob.top = r.top + inset;
        knob.bottom = knob.top + d;
        knob.left = kx;
        knob.right = kx + d;
        /* 阴影（比滑块大一圈、alpha 低） */
        {
            RECT sh = knob;
            int grow = sd(2);
            InflateRect(&sh, grow, grow);
            ui_fill_round(dc, sh, (sh.bottom - sh.top) / 2, RGB(0, 0, 0), 40);
        }
        ui_fill_round(dc, knob, d / 2,
                      ui_mix(g_th.text_mute, RGB(255, 255, 255), pos * 100 / 1000),
                      255);
    }
    (void)on;
}

/* ------------------------------------------------- 开关动画状态 */
static int s_sw_pos[SWI_COUNT];     /* 当前动画位置 0..1000 */
static int s_sw_tgt[SWI_COUNT];     /* 目标位置 0..1000 */

void ui_switch_anim_set(int which, int on)
{
    if (which < 0 || which >= SWI_COUNT) return;
    s_sw_tgt[which] = on ? 1000 : 0;
}

int ui_switch_anim_pos(int which)
{
    if (which < 0 || which >= SWI_COUNT) return 0;
    return s_sw_pos[which];
}

/* 位置直接对齐目标（不播动画）——用于初始化与状态被外部改写时 */
void ui_switch_anim_sync(void)
{
    int i;
    for (i = 0; i < SWI_COUNT; i++) s_sw_pos[i] = s_sw_tgt[i];
}

int ui_switch_anim_step(void)
{
    /* 120ms 走完 0↔1000（16ms/节拍 → 每拍约 133） */
    int step = 1000 * 16 / 120;
    int i, moving = 0;
    for (i = 0; i < SWI_COUNT; i++) {
        int p = s_sw_pos[i], t = s_sw_tgt[i];
        if (p < t) { p += step; if (p > t) p = t; }
        else if (p > t) { p -= step; if (p < t) p = t; }
        if (p != s_sw_pos[i]) { s_sw_pos[i] = p; moving = 1; }
    }
    return moving;
}
