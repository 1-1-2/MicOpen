/* ui_draw.c — 绘制原语与控件
 *
 * 视觉权威：design-demos/round2/win-final.html（定稿图即验收截图）。
 * 排版/圆角/间距/颜色全部取自《定稿规范》§0 与定稿图实测值，过 sd() 支持高 DPI。
 */
#include "ui_draw.h"
#include "ui_layout.h"
#include "ui_voicemeter.h"
#include "drvinst.h"
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------- GDI+ 动态绑定
 * 不用 gdiplus.h（含 C++ 类，C 编不过），只手写需要的 flat API 并动态加载。
 * 好处：不增加导入表、不引入链接期依赖，gdiplus.dll 每台 Windows 都有。
 */
typedef struct { UINT32 ver; void *cb; BOOL no_thread; BOOL no_codecs; } GpStartupInput;
typedef int   GpStatus;
typedef void *GpGraphics, *GpPath, *GpBrush, *GpPen;
typedef float REAL;

static struct {
    HMODULE mod;
    GpStatus (WINAPI *startup)(ULONG_PTR *, const GpStartupInput *, void **);
    GpStatus (WINAPI *shutdown)(ULONG_PTR);
    GpStatus (WINAPI *from_hdc)(HDC, GpGraphics *);
    GpStatus (WINAPI *del_graphics)(GpGraphics);
    GpStatus (WINAPI *set_smooth)(GpGraphics, int);
    GpStatus (WINAPI *create_path)(int, GpPath *);
    GpStatus (WINAPI *add_arc)(GpPath, REAL, REAL, REAL, REAL, REAL, REAL);
    GpStatus (WINAPI *add_line)(GpPath, REAL, REAL, REAL, REAL);
    GpStatus (WINAPI *close_fig)(GpPath);
    GpStatus (WINAPI *solid_fill)(UINT32, GpBrush *);
    GpStatus (WINAPI *fill_path)(GpGraphics, GpBrush, GpPath);
    GpStatus (WINAPI *draw_path)(GpGraphics, GpPen, GpPath);
    GpStatus (WINAPI *create_pen)(UINT32, REAL, int, GpPen *);
    GpStatus (WINAPI *del_brush)(GpBrush);
    GpStatus (WINAPI *del_pen)(GpPen);
    GpStatus (WINAPI *del_path)(GpPath);
    ULONG_PTR token;
    int ready;
} gp;

#define GPA(field, sym, type) gp.field = (type)(void *)GetProcAddress(gp.mod, "Gdip" sym)

void ui_gdiplus_init(void)
{
    gp.mod = LoadLibraryW(L"gdiplus.dll");
    if (!gp.mod) return;
    GPA(from_hdc,     "CreateFromHDC",    GpStatus (WINAPI *)(HDC, GpGraphics *));
    GPA(del_graphics, "DeleteGraphics",   GpStatus (WINAPI *)(GpGraphics));
    GPA(set_smooth,   "SetSmoothingMode", GpStatus (WINAPI *)(GpGraphics, int));
    GPA(create_path,  "CreatePath",       GpStatus (WINAPI *)(int, GpPath *));
    GPA(add_arc,      "AddPathArc",       GpStatus (WINAPI *)(GpPath, REAL, REAL, REAL, REAL, REAL, REAL));
    GPA(add_line,     "AddPathLine",      GpStatus (WINAPI *)(GpPath, REAL, REAL, REAL, REAL));
    GPA(close_fig,    "ClosePathFigure",  GpStatus (WINAPI *)(GpPath));
    GPA(solid_fill,   "CreateSolidFill",  GpStatus (WINAPI *)(UINT32, GpBrush *));
    GPA(fill_path,    "FillPath",         GpStatus (WINAPI *)(GpGraphics, GpBrush, GpPath));
    GPA(draw_path,    "DrawPath",         GpStatus (WINAPI *)(GpGraphics, GpPen, GpPath));
    GPA(create_pen,   "CreatePen1",       GpStatus (WINAPI *)(UINT32, REAL, int, GpPen *));
    GPA(del_brush,    "DeleteBrush",      GpStatus (WINAPI *)(GpBrush));
    GPA(del_pen,      "DeletePen",        GpStatus (WINAPI *)(GpPen));
    GPA(del_path,     "DeletePath",       GpStatus (WINAPI *)(GpPath));
    gp.startup  = (GpStatus (WINAPI *)(ULONG_PTR *, const GpStartupInput *, void **))
                  (void *)GetProcAddress(gp.mod, "GdiplusStartup");
    gp.shutdown = (GpStatus (WINAPI *)(ULONG_PTR))
                  (void *)GetProcAddress(gp.mod, "GdiplusShutdown");
    if (!gp.from_hdc || !gp.create_path || !gp.fill_path || !gp.startup) { gp.mod = NULL; return; }
    {
        GpStartupInput in = { 1, NULL, FALSE, FALSE };
        if (gp.startup(&gp.token, &in, NULL) != 0) { gp.mod = NULL; return; }
    }
    gp.ready = 1;
}

int ui_gdiplus_ready(void) { return gp.ready; }

/* ---------------------------------------------------------------- DPI / 字体 */
int sd(int v) { return MulDiv(v, (int)g_ui.dpi, 96); }

typedef struct { int px, weight, mono, dpi; HFONT f; } FontEnt;
static FontEnt g_fonts[32];
static int     g_nfont;

/* ------------------------------------------------ 字体族探测（R-25）
 * 只用系统自带字体，且**必须逐级探测**：缺哪级退哪级，绝不让 GDI 自己猜替身
 * （猜出来的替身可能没有中文字形，整屏方框）。
 *   界面/中文：Microsoft YaHei UI → Microsoft YaHei → DengXian → SimHei
 *              →（Wine/精简系统兜底）Noto Sans CJK SC/TC/JP、文泉驿、Droid
 *              →（最后）全量枚举，取第一个真有汉字字形的族
 *   读数/时间戳：Consolas → Lucida Console → Courier New（等宽）
 *   纯拉丁/数字：Segoe UI
 * 探测用 GetGlyphIndicesW(GGI_MARK_NONEXISTING_GLYPHS) 做**字形级**确认：
 * 族名被枚举出来不代表真有汉字字形（Wine 上就踩过，只查族名照样出方框）。
 * 字重只用命名档（雅黑 UI 只有 300/400/600/700，GDI 无可变轴）：
 *   标题/卡片标题 600，正文/按钮 400，次要说明 400（明度降档，不用 Light）。
 */
/* 该字体是否真有某个字形（ch 传 0x4E2D「中」查中文，0x30「0」查数字） */
static int font_has_glyph(const wchar_t *face, wchar_t ch)
{
    HDC dc = GetDC(NULL);
    HFONT f, old;
    WORD idx = 0xFFFF;
    int ok = 0;
    if (dc) {
        f = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
        old = (HFONT)SelectObject(dc, f);
        if (GetGlyphIndicesW(dc, &ch, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS) == 1)
            ok = (idx != 0xFFFF);
        SelectObject(dc, old);
        DeleteObject(f);
        ReleaseDC(NULL, dc);
    }
    return ok;
}


static int CALLBACK font_enum_cjk_cb(const LOGFONTW *lf, const TEXTMETRICW *tm,
                                     DWORD type, LPARAM lp)
{
    (void)tm; (void)type;
    if (lf && font_has_glyph(lf->lfFaceName, 0x4E2D)) {
        wcscpy((wchar_t *)lp, lf->lfFaceName);
        return 0;                                /* 找到即停 */
    }
    return 1;                                    /* 继续 */
}

static void font_scan_cjk(wchar_t *out)
{
    LOGFONTW lf;
    HDC dc;
    out[0] = 0;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;              /* 不限族名：枚举全部 */
    dc = GetDC(NULL);
    if (dc) {
        EnumFontFamiliesExW(dc, &lf, font_enum_cjk_cb, (LPARAM)out, 0);
        ReleaseDC(NULL, dc);
    }
}


/* 顺序取链上第一个「真有目标字形」的字体；全缺返回 0（cache 置空） */
static void face_pick(const wchar_t *const *chain, int n, wchar_t glyph,
                      wchar_t *cache)
{
    int i;
    cache[0] = 0;
    for (i = 0; i < n; i++)
        if (font_has_glyph(chain[i], glyph)) { wcscpy(cache, chain[i]); break; }
}

/* 三类字体的回退链与探测字形；face_get() 统一「探测+缓存」，省三份样板 */
static const wchar_t *const chain_ui[] = {
    L"Microsoft YaHei UI", L"Microsoft YaHei", L"DengXian", L"SimHei",
    /* Wine/精简系统的开源 CJK 兜底（真机 Windows 用不到，多几级不伤） */
    L"Noto Sans CJK SC", L"Noto Sans CJK JP", L"WenQuanYi Zen Hei",
    L"Droid Sans Fallback"
};
static const wchar_t *const chain_mono[] = { L"Consolas", L"Lucida Console", L"Courier New" };
static const wchar_t *const chain_latin[] = { L"Segoe UI", L"Tahoma" };
static const struct {
    const wchar_t *const *chain; int n; wchar_t glyph;   /* 探测用的代表字形 */
} face_tab[3] = {
    { chain_ui,    8, 0x4E2D },   /* 「中」：必须有汉字字形 */
    { chain_mono,  3, 0x30 },     /* 「0」：数字等宽 */
    { chain_latin, 2, 0x41 },     /* 「A」 */
};
static wchar_t g_face_cache[3][LF_FACESIZE];
static int     g_face_done[3];

static const wchar_t *face_get(int kind)
{
    if (!g_face_done[kind]) {
        face_pick(face_tab[kind].chain, face_tab[kind].n, face_tab[kind].glyph,
                  g_face_cache[kind]);
        if (!g_face_cache[kind][0] && kind == 0) {
            font_scan_cjk(g_face_cache[0]);      /* 最后兜底：全量枚举 */
            if (!g_face_cache[0][0])
                wcscpy(g_face_cache[0], chain_ui[0]);   /* 给个名字让 GDI 兜底 */
        }
        if (!g_face_cache[kind][0]) wcscpy(g_face_cache[kind], face_tab[kind].chain[0]);
        g_face_done[kind] = 1;
    }
    return g_face_cache[kind];
}

/* mono：0 = 界面（中文链）/ 1 = 等宽读数 / 2 = 纯拉丁（Segoe UI） */
HFONT ui_font(int px, int weight, int mono)
{
    LOGFONTW lf;
    int i, slot;
    if (px < 6) px = 6;
    for (i = 0; i < g_nfont; i++)
        if (g_fonts[i].px == px && g_fonts[i].weight == weight &&
            g_fonts[i].mono == mono && g_fonts[i].dpi == (int)g_ui.dpi)
            return g_fonts[i].f;

    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -MulDiv(px, (int)g_ui.dpi, 96);   /* 逻辑 px → 设备 px */
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy(lf.lfFaceName,
           face_get(mono == 1 ? 1 : mono == 2 ? 2 : 0));
    slot = g_nfont < 32 ? g_nfont++ : 0;
    if (g_fonts[slot].f) DeleteObject(g_fonts[slot].f);
    g_fonts[slot].px = px;
    g_fonts[slot].weight = weight;
    g_fonts[slot].mono = mono;
    g_fonts[slot].dpi = (int)g_ui.dpi;
    g_fonts[slot].f = CreateFontIndirectW(&lf);
    return g_fonts[slot].f;
}

void ui_fonts_reset(void)
{
    int i;
    for (i = 0; i < g_nfont; i++) if (g_fonts[i].f) DeleteObject(g_fonts[i].f);
    memset(g_fonts, 0, sizeof(g_fonts));
    g_nfont = 0;
}

/* 卡片内距（R-24：16 → 14）。卡片标题/正文/子控件的左右内缩都用它。 */
int ui_pad(void) { return sd(14); }

/* 子控件裁剪（R-08）。IntersectClipRect 是「与当前裁剪区求交」，所以嵌套安全：
 * 卡片裁剪永远出不了 ui_draw_all 设下的可视区（标题栏以下、状态栏以上）。 */
int ui_clip_push(HDC dc, RECT r)
{
    int saved = SaveDC(dc);
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    return saved;
}

void ui_clip_pop(HDC dc, int saved)
{
    RestoreDC(dc, saved);
}

int ui_measure_text(const wchar_t *s, HFONT f)
{
    HDC dc = CreateCompatibleDC(NULL);
    SIZE sz = { 0, 0 };
    HFONT old;
    if (!dc || !s) return 0;
    old = (HFONT)SelectObject(dc, f);
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, old);
    DeleteDC(dc);
    return sz.cx;
}

/* ---------------------------------------------------------------- 原语 */
static UINT32 argb(COLORREF c, int a)
{
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return ((UINT32)a << 24) | (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
}

/* 颜色混合：返回 a 与 b 按 pct%（b 的占比）混合的结果 */
static COLORREF mix_c(COLORREF a, COLORREF b, int pct)
{
    int r, g, bb;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    r  = (GetRValue(a) * (100 - pct) + GetRValue(b) * pct) / 100;
    g  = (GetGValue(a) * (100 - pct) + GetGValue(b) * pct) / 100;
    bb = (GetBValue(a) * (100 - pct) + GetBValue(b) * pct) / 100;
    return RGB(r, g, bb);
}

/* 对外暴露同一公式（ui_voicemeter 的 dim 段派生色用，避免两套实现） */
COLORREF ui_mix(COLORREF a, COLORREF b, int pct) { return mix_c(a, b, pct); }

static void path_round(GpPath p, RECT r, int rad)
{
    REAL x = (REAL)r.left, y = (REAL)r.top;
    REAL w = (REAL)(r.right - r.left) - 1, h = (REAL)(r.bottom - r.top) - 1;
    REAL d = (REAL)(rad * 2);
    if (d > w) d = w;
    if (d > h) d = h;
    gp.add_arc(p, x, y, d, d, 180, 90);
    gp.add_arc(p, x + w - d, y, d, d, 270, 90);
    gp.add_arc(p, x + w - d, y + h - d, d, d, 0, 90);
    gp.add_arc(p, x, y + h - d, d, d, 90, 90);
    gp.close_fig(p);
}

void ui_fill_round(HDC dc, RECT r, int rad, COLORREF c, int alpha)
{
    if (r.right <= r.left || r.bottom <= r.top) return;
    if (!gp.ready) {
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &r, b);
        DeleteObject(b);
        return;
    }
    {
        GpGraphics gfx = NULL; GpPath p = NULL; GpBrush b = NULL;
        int done = 0;
        gp.from_hdc(dc, &gfx);
        if (gfx) {
            gp.set_smooth(gfx, 4);
            if (rad <= 0 && !gp.add_line) {
                /* add_line 缺失时的退化路径：纯 GDI 填充（无 alpha） */
                HBRUSH gb = CreateSolidBrush(c);
                FillRect(dc, &r, gb);
                DeleteObject(gb);
                done = 1;
            }
            else {
                gp.create_path(0, &p);
                if (p) {
                    if (rad <= 0) {
                        /* 直角矩形：四条直线闭合路径（零面积 arc 无法填充，
                         * 走 GDI+ 路径以保留 alpha 混合） */
                        REAL x0 = (REAL)r.left, y0 = (REAL)r.top;
                        REAL x1 = (REAL)r.right, y1 = (REAL)r.bottom;
                        gp.add_line(p, x0, y0, x1, y0);
                        gp.add_line(p, x1, y0, x1, y1);
                        gp.add_line(p, x1, y1, x0, y1);
                        gp.add_line(p, x0, y1, x0, y0);
                    }
                    else path_round(p, r, rad);
                    gp.close_fig(p);
                    gp.solid_fill(argb(c, alpha), &b);
                    if (b) { gp.fill_path(gfx, b, p); gp.del_brush(b); }
                    gp.del_path(p);
                    done = 1;
                }
            }
            gp.del_graphics(gfx);
        }
        if (!done) {
            /* GDI+ 完全不可用的最后兜底 */
            HBRUSH gb = CreateSolidBrush(c);
            FillRect(dc, &r, gb);
            DeleteObject(gb);
        }
    }
}

void ui_stroke_round(HDC dc, RECT r, int rad, COLORREF c, int alpha, int width)
{
    if (!gp.ready || r.right <= r.left || r.bottom <= r.top) return;
    {
        GpGraphics gfx = NULL; GpPath p = NULL; GpPen pen = NULL;
        gp.from_hdc(dc, &gfx);
        if (!gfx) return;
        gp.set_smooth(gfx, 4);
        gp.create_path(0, &p);
        if (!p) { gp.del_graphics(gfx); return; }
        path_round(p, r, rad);
        gp.create_pen(argb(c, alpha), (REAL)width, 0, &pen);
        if (pen) { gp.draw_path(gfx, pen, p); gp.del_pen(pen); }
        gp.del_path(p);
        gp.del_graphics(gfx);
    }
}

void ui_bg(HDC dc, RECT r)
{
    HBRUSH b = CreateSolidBrush(g_th.bg);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void ui_text(HDC dc, RECT r, const wchar_t *s, HFONT f, COLORREF c, UINT fmt)
{
    HFONT old = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt);
    SelectObject(dc, old);
}

void ui_text_u8(HDC dc, RECT r, const char *s, HFONT f, COLORREF c, UINT fmt)
{
    wchar_t w[512];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 512);
    w[511] = 0;
    ui_text(dc, r, w, f, c, fmt);
}

/* 屏幕坐标圆弧：角度 0=正右，顺时针为正（与 GDI+ 一致，y 向下） */
void ui_arc(HDC dc, int cx, int cy, int rad, double a0, double a1,
            COLORREF c, int width, int alpha)
{
    if (rad <= 0) return;
    if (gp.ready) {
        GpGraphics gfx = NULL; GpPath p = NULL; GpPen pen = NULL;
        gp.from_hdc(dc, &gfx);
        if (!gfx) return;
        gp.set_smooth(gfx, 4);
        gp.create_path(0, &p);
        if (p) {
            REAL d = (REAL)(rad * 2);
            gp.add_arc(p, (REAL)(cx - rad), (REAL)(cy - rad), d, d,
                       (REAL)a0, (REAL)(a1 - a0));
            gp.create_pen(argb(c, alpha), (REAL)width, 0, &pen);
            if (pen) { gp.draw_path(gfx, pen, p); gp.del_pen(pen); }
            gp.del_path(p);
        }
        gp.del_graphics(gfx);
    } else {
        /* GDI 退化：无抗锯齿 */
        HPEN pen = CreatePen(PS_SOLID, width < 1 ? 1 : width, c);
        HPEN old = (HPEN)SelectObject(dc, pen);
        double r0 = a0 * 3.14159265358979 / 180.0, r1 = a1 * 3.14159265358979 / 180.0;
        int x0 = cx + (int)(rad * cos(r0)), y0 = cy + (int)(rad * sin(r0));
        int x1 = cx + (int)(rad * cos(r1)), y1 = cy + (int)(rad * sin(r1));
        Arc(dc, cx - rad, cy - rad, cx + rad, cy + rad, x0, y0, x1, y1);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

void ui_line(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, double width, int alpha)
{
    if (gp.ready && gp.add_line) {
        GpGraphics gfx = NULL; GpPath p = NULL; GpPen pen = NULL;
        gp.from_hdc(dc, &gfx);
        if (!gfx) return;
        gp.set_smooth(gfx, 4);
        gp.create_path(0, &p);
        if (p) {
            gp.add_line(p, (REAL)x0, (REAL)y0, (REAL)x1, (REAL)y1);
            gp.create_pen(argb(c, alpha), (REAL)width, 0, &pen);
            if (pen) { gp.draw_path(gfx, pen, p); gp.del_pen(pen); }
            gp.del_path(p);
        }
        gp.del_graphics(gfx);
    } else {
        HPEN pen = CreatePen(PS_SOLID, width < 1 ? 1 : (int)(width + 0.5), c);
        HPEN old = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, x0, y0, NULL);
        LineTo(dc, x1, y1);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

/* ---------------------------------------------------------------- 图标 */
/* 标题栏三键 glyph（10×10，1.2px）与三步勾（10px 路径，1.8px 圆头） */
void ui_icon(HDC dc, RECT r, int kind, COLORREF c, int width)
{
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int hx = (r.right - r.left) / 2, hy = (r.bottom - r.top) / 2;
    switch (kind) {
    case IC_MIN:
        ui_line(dc, cx - hx, cy, cx + hx, cy, c, 1.2, 255);
        break;
    case IC_MAX:
        ui_line(dc, cx - hx, cy - hy, cx + hx, cy - hy, c, 1.2, 255);
        ui_line(dc, cx + hx, cy - hy, cx + hx, cy + hy, c, 1.2, 255);
        ui_line(dc, cx + hx, cy + hy, cx - hx, cy + hy, c, 1.2, 255);
        ui_line(dc, cx - hx, cy + hy, cx - hx, cy - hy, c, 1.2, 255);
        break;
    case IC_CLOSE:
        ui_line(dc, cx - hx, cy - hy, cx + hx, cy + hy, c, 1.2, 255);
        ui_line(dc, cx + hx, cy - hy, cx - hx, cy + hy, c, 1.2, 255);
        break;
    case IC_CHECK:
        ui_line(dc, cx - hx * 70 / 100, cy + hy * 10 / 100,
                cx - hx * 20 / 100, cy + hy * 60 / 100, c, 1.8, 255);
        ui_line(dc, cx - hx * 20 / 100, cy + hy * 60 / 100,
                cx + hx * 70 / 100, cy - hy * 60 / 100, c, 1.8, 255);
        break;
    default: break;
    }
}

/* ---------------------------------------------------------------- 控件 */
void ui_card(HDC dc, RECT r)
{
    ui_fill_round(dc, r, sd(12), g_th.card, 255);
    ui_stroke_round(dc, r, sd(12), g_th.stroke, 255, 1);
}

void ui_card_title(HDC dc, RECT r, const wchar_t *title)
{
    /* R-24：卡片标题 15/20，600（中文无可变字重轴，用命名档 Semibold） */
    RECT tr = { r.left + ui_pad(), r.top + sd(12), r.right - ui_pad(), r.top + sd(32) };
    ui_text(dc, tr, title, ui_font(15, FW_SEMIBOLD, 0), g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void ui_button(HDC dc, RECT r, const wchar_t *label, int id, int style, int enabled)
{
    /* D-10：hover 描边 border→accent 40%（T1 120ms 过渡）；press 内缩 1px 重绘 */
    int fade = (id == g_ui.hover_fade_id) ? g_ui.hover_fade : 0;   /* 0..1000 */
    int down = (g_ui.pressed == id) && enabled;
    RECT rr = r;
    HFONT f;
    COLORREF fg;

    if (down) InflateRect(&rr, -sd(1), -sd(1));   /* press：矩形内缩 1px */

    if (!enabled) {
        /* 可见禁用（诚实性纪律：不静默失效） */
        ui_fill_round(dc, rr, sd(8), g_th.card_hi, 255);
        ui_stroke_round(dc, rr, sd(8), g_th.stroke, 255, 1);
        ui_text(dc, rr, label, ui_font(14, FW_NORMAL, 0), g_th.text_mute,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    switch (style) {
    case BTN_PRIMARY:
        ui_fill_round(dc, rr, sd(8),
                      fade ? mix_c(g_th.accent, RGB(255, 255, 255), 12 * fade / 1000)
                           : g_th.accent, 255);
        fg = g_th.on_accent;
        f = ui_font(14, FW_NORMAL, 0);            /* R-25：按钮 400，不加粗 */
        break;
    case BTN_SECONDARY:
        ui_fill_round(dc, rr, sd(8), g_th.card, 255);
        ui_stroke_round(dc, rr, sd(8),
                        fade ? mix_c(g_th.stroke, g_th.accent, 40 * fade / 1000)
                             : g_th.stroke, 255, 1);
        fg = g_th.text;
        f = ui_font(14, FW_NORMAL, 0);
        break;
    case BTN_DANGER:
        /* C-05 破坏性：透明底 + err 1px 描边 + err 文字 */
        ui_stroke_round(dc, rr, sd(8),
                        fade ? mix_c(g_th.err, g_th.text, 30 * fade / 1000)
                             : g_th.err, 255, 1);
        fg = g_th.err;
        f = ui_font(14, FW_NORMAL, 0);
        break;
    case BTN_ACCENT_OUT:
        /* 连接中：accent 描边 + accent 文字 */
        ui_stroke_round(dc, rr, sd(8), g_th.accent, 255, 1);
        fg = g_th.accent;
        f = ui_font(14, FW_NORMAL, 0);
        break;
    case BTN_TEXT:
    default:
        /* 小文字按钮（11/15）：text-3 → hover text-1 */
        ui_text(dc, rr, label, ui_font(11, FW_NORMAL, 0),
                fade ? mix_c(g_th.text_mute, g_th.text, 100 * fade / 1000) : g_th.text_mute,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    ui_text(dc, rr, label, f, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void ui_seg_group(HDC dc, RECT r, const wchar_t * const *labels, int n,
                  int active, int id_base, const int *enabled)
{
    int i;
    if (n <= 0 || r.right <= r.left) return;
    /* 共享外框 overflow:hidden：用圆角裁剪区实现选中段满格不溢出 */
    {
        int sc = SaveDC(dc);                 /* 裁剪区必须成对保存/恢复：
                                              * SelectClipRgn(NULL) 会清掉外层
                                              * （ui_draw_all）的视图裁剪，导致
                                              * 滚动时右列卡片盖住标题栏 */
        HRGN clip = CreateRoundRectRgn(r.left, r.top, r.right, r.bottom,
                                       sd(8) * 2, sd(8) * 2);
        if (clip) SelectClipRgn(dc, clip);
        ui_fill_round(dc, r, sd(8), g_th.card, 255);
        for (i = 0; i < n; i++) {
            int cw = (r.right - r.left) / n;
            RECT cell = { r.left + i * cw, r.top, r.left + (i + 1) * cw, r.bottom };
            int en = enabled ? enabled[i] : 1;
            if (i == active) {
                ui_fill_round(dc, cell, 0, g_th.accent, 255);
                ui_text(dc, cell, labels[i], ui_font(12, FW_SEMIBOLD, 0), g_th.on_accent,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                int fade = (id_base + i == g_ui.hover_fade_id) ? g_ui.hover_fade : 0;
                if (fade && en)
                    ui_fill_round(dc, cell, 0, g_th.card_hi, (int)(230 * fade / 1000));
                ui_text(dc, cell, labels[i], ui_font(12, FW_NORMAL, 0),
                        en ? g_th.text_dim : g_th.text_mute,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        if (clip) DeleteObject(clip);
        RestoreDC(dc, sc);
    }
    /* 外框 + 内分隔 1px border */
    ui_stroke_round(dc, r, sd(8), g_th.stroke, 255, 1);
    for (i = 1; i < n; i++) {
        int x = r.left + (r.right - r.left) * i / n;
        RECT dv = { x, r.top + 1, x + 1, r.bottom - 1 };
        ui_fill_round(dc, dv, 0, g_th.stroke, 255);
    }
}

void ui_slider(HDC dc, RECT r, int pos, int id)
{
    /* r = 16px 高控制区；轨道 6 胶囊 bg-sunken、填充 accent、拇指 16 白底 Rfull */
    int cx = r.left + (r.right - r.left) * pos / 1000;
    RECT track = { r.left, r.top + sd(5), r.right, r.top + sd(11) };
    RECT fillr = { r.left, track.top, cx, track.bottom };
    RECT th    = { cx - sd(8), r.top, cx + sd(8), r.top + sd(16) };
    (void)id;
    if (pos < 0) pos = 0;
    if (pos > 1000) pos = 1000;
    ui_fill_round(dc, track, sd(3), g_th.sunken, 255);
    if (fillr.right > fillr.left) ui_fill_round(dc, fillr, sd(3), g_th.accent, 255);
    ui_fill_round(dc, th, sd(8), RGB(255, 255, 255), 255);
    ui_stroke_round(dc, th, sd(8), g_th.stroke, 255, 1);
}

void ui_switch(HDC dc, RECT r, int on)
{
    int rad = (r.bottom - r.top) / 2;
    int d = sd(12);
    RECT knob;
    ui_fill_round(dc, r, rad, on ? g_th.accent : g_th.sunken, 255);
    if (on) knob.left = r.left + sd(24);
    else    knob.left = r.left + sd(4);
    knob.top = r.top + sd(4);
    knob.right = knob.left + d;
    knob.bottom = knob.top + d;
    ui_fill_round(dc, knob, d / 2, on ? RGB(255, 255, 255) : g_th.text_mute, 255);
}

void ui_badge(HDC dc, RECT r, const wchar_t *text, COLORREF fg, COLORREF bg)
{
    /* C-11：12% 语义底 + 6px 圆点 + 11/15 Semibold 文字 */
    int rad = (r.bottom - r.top) / 2;
    int d = sd(6);
    RECT dr = { r.left + sd(8), (r.top + r.bottom - d) / 2,
                r.left + sd(8) + d, (r.top + r.bottom + d) / 2 };
    RECT tr = { dr.right + sd(4), r.top, r.right - sd(8), r.bottom };
    ui_fill_round(dc, r, rad, bg, 255);
    ui_fill_round(dc, dr, d / 2, fg, 255);
    ui_text(dc, tr, text, ui_font(11, FW_SEMIBOLD, 0), fg,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void ui_pill(HDC dc, RECT r, const wchar_t *key, const wchar_t *val)
{
    /* D-09：高 24、Rfull、accent 8% 底、1px border；键 sans + 值 Consolas */
    HFONT kf = ui_font(12, FW_NORMAL, 0), vf = ui_font(12, FW_NORMAL, 1);
    int x = r.left + sd(10);
    RECT kr = { x, r.top, x + sd(140), r.bottom };
    RECT vr;
    ui_fill_round(dc, r, (r.bottom - r.top) / 2, g_th.pill, 255);
    ui_stroke_round(dc, r, (r.bottom - r.top) / 2, g_th.stroke, 255, 1);
    ui_text(dc, kr, key, kf, g_th.text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(key, kf) + sd(6);
    vr.left = x; vr.top = r.top; vr.right = r.right - sd(10); vr.bottom = r.bottom;
    ui_text(dc, vr, val, vf, g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void ui_panel_hdr(HDC dc, RECT r, const wchar_t *title, const wchar_t *sum,
                  int expanded, int id)
{
    /* 收起态标题行（40 + 垂直内边距 4 各 = 48）：标题 14/20 Semibold +
     * 右摘要 12/16 text-2（Consolas）+ chevron（展开旋转 90°，T3） */
    int fade = (id == g_ui.hover_fade_id) ? g_ui.hover_fade : 0;
    int cw = sd(10), chx, chy;
    RECT tr = { r.left + ui_pad(), r.top, r.left + sd(160), r.bottom };
    if (fade) ui_fill_round(dc, r, sd(12), g_th.card_hi, (int)(255 * fade / 1000));
    ui_text(dc, tr, title, ui_font(15, FW_SEMIBOLD, 0), g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    chx = r.right - ui_pad() - cw;
    chy = (r.top + r.bottom - cw) / 2;
    if (expanded) {
        ui_line(dc, chx + sd(1), chy + sd(3), chx + sd(5), chy + sd(7), g_th.text_dim, 1.5, 255);
        ui_line(dc, chx + sd(5), chy + sd(7), chx + sd(9), chy + sd(3), g_th.text_dim, 1.5, 255);
    } else {
        ui_line(dc, chx + sd(3), chy + sd(1), chx + sd(7), chy + sd(5), g_th.text_dim, 1.5, 255);
        ui_line(dc, chx + sd(7), chy + sd(5), chx + sd(3), chy + sd(9), g_th.text_dim, 1.5, 255);
    }
    if (sum) {
        RECT sr = { r.left + sd(160), r.top, chx - sd(12), r.bottom };
        ui_text(dc, sr, sum, ui_font(12, FW_NORMAL, 1), g_th.text_dim,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void ui_breath_glow(HDC dc, RECT r)
{
    /* D-04 / D-22：运行态 err 呼吸外发光。
     * 共享参数（与 Android infiniteTransition 逐项一致，P-5 跨端同形态）：
     *   周期 1200 ms；alpha 0.08 → 0.30 → 0.08；扩散半径 6 → 10 → 6 px；
     *   仅会话运行/连接中启用，断开即停。帧相位挂现有 30fps 帧循环，不开新线程。 */
    double p = (double)(GetTickCount() % 1200u) / 1200.0;
    double wave = sin(3.14159265358979 * p);                 /* 0→1→0 */
    int spread = sd(6) + (int)((double)sd(4) * wave);        /* 6→10→6 px */
    int a0 = (int)((0.08 + 0.22 * wave) * 255.0);            /* 0.08→0.30→0.08 */
    int k;
    if (a0 <= 0 || spread <= 0) return;
    for (k = 3; k >= 0; k--) {
        int infl = 1 + spread * k / 3;
        int a = a0 * (4 - k) / 5;                            /* 外淡内浓 */
        RECT g = r;
        InflateRect(&g, infl, infl);
        ui_stroke_round(dc, g, sd(8) + infl, g_th.err, a, sd(2));
    }
}

/* ------------------------------------------------------- 增益 dB 刻度 */
/* R-31：内部一律 dB（0.1 dB 精度），不再存百分比。
 *   pos(0..1000) ↔ dB 线性：dB = -60 + 66 * pos/1000
 *   倍率 = pow(10, dB/20) —— -60 dB 是 0.001 倍，不是 0（不再是静音）
 * 旧实现存百分比，并在 pos→gain 里 `if (v <= 1) v = 0`：-38 dB 就掉成静音，
 * 而且 -60…-36 dB 整段被压进 0.1%…1.6%，半条轨道不可用。 */
static const int g_snap_db10[6]   = { 0, -120, -240, -360, -480, -600 };
static const int g_snap_radius[6] = { 15, 8, 8, 8, 8, 8 };   /* 1.5 dB / 0.75 dB */

int ui_pos_to_db10(int pos)
{
    int db10, i;
    if (pos < 0) pos = 0;
    if (pos > 1000) pos = 1000;
    db10 = -600 + (660 * pos) / 1000;
    /* R-32：只对"落点"吸附。每个新 pos 都重新算，所以继续拖就能离开吸附区，
     * 不会粘死在刻度上（不做累积吸附）。 */
    for (i = 0; i < 6; i++)
        if (db10 - g_snap_db10[i] <= g_snap_radius[i] &&
            g_snap_db10[i] - db10 <= g_snap_radius[i])
            return g_snap_db10[i];
    return db10;
}

int ui_gain_pos(int db10)
{
    int p;
    if (db10 < GAIN_DB10_MIN) db10 = GAIN_DB10_MIN;
    if (db10 > GAIN_DB10_MAX) db10 = GAIN_DB10_MAX;
    p = ((db10 + 600) * 1000) / 660;
    if (p < 0) p = 0;
    if (p > 1000) p = 1000;
    return p;
}

int ui_gain_snap_target(int db10)
{
    int i;
    for (i = 0; i < 6; i++)
        if (db10 == g_snap_db10[i]) return g_snap_db10[i];
    return 1000;                       /* 未吸附（1000 不是合法 dB10） */
}

void ui_gain_reading(wchar_t *out, int cap, int db10)
{
    double db = (double)db10 / 10.0;
    double pct = pow(10.0, db / 20.0) * 100.0;
    if (!out || cap <= 0) return;
    /* % 只用于显示：由 dB 反算；小于 10% 时给一位小数，别把 0.1% 显示成 0% */
    if (pct < 10.0) swprintf(out, cap, L"增益 %.1f%% · %+.1f dB", pct, db);
    else            swprintf(out, cap, L"增益 %d%% · %+.1f dB", (int)(pct + 0.5), db);
    if (db10 == 0) swprintf(out, cap, L"增益 100%% · 0.0 dB");
}

/* ------------------------------------------------------------ 业务卡片 */
static const wchar_t *drv_state_text(DrvState s)
{
    switch (s) {
    case DRV_READY: return L"驱动已就绪";
    case DRV_NEED_REBOOT: return L"驱动需重启";
    case DRV_NOT_INSTALLED: return L"驱动未安装";
    default: return L"状态未知";
    }
}

static COLORREF drv_state_color(DrvState s)
{
    switch (s) {
    case DRV_READY: return g_th.ok;
    case DRV_NEED_REBOOT: return g_th.warn;
    case DRV_NOT_INSTALLED: return g_th.err;
    default: return g_th.text_mute;
    }
}

static COLORREF drv_state_bg(DrvState s)
{
    switch (s) {
    case DRV_READY: return g_th.ok_12;
    case DRV_NEED_REBOOT: return g_th.warn_12;
    case DRV_NOT_INSTALLED: return g_th.err_12;
    default: return g_th.sunken;
    }
}

/* ------------------------------------------------------- D-01 标题栏 */
void ui_draw_titlebar(HDC dc)
{
    RECT r = g_lo.title;
    ui_bg(dc, r);
    {
        RECT ln = { r.left, r.bottom - 1, r.right, r.bottom };
        ui_fill_round(dc, ln, 0, g_th.stroke, 255);      /* 标题栏底 1px border */
    }
    /* 品牌：MicLink 18/24 Semibold text-1 + v1.0.0 pill（Rfull/高20/bg-sunken/text-2） */
    {
        HFONT f = ui_font(18, FW_SEMIBOLD, 2);   /* 纯拉丁品牌字 → Segoe UI */
        HFONT pf = ui_font(11, FW_NORMAL, 1);
        int tx = sd(20);
        int tw = ui_measure_text(L"MicLink", f);
        int pw = ui_measure_text(L"v" APP_VERSION, pf) + sd(16);
        RECT tr = { tx, 0, tx + tw + sd(8), ui_title_h() };
        RECT pr = { tr.right, (ui_title_h() - sd(20)) / 2,
                    tr.right + pw, (ui_title_h() - sd(20)) / 2 + sd(20) };
        wchar_t ver[32];
        MultiByteToWideChar(CP_UTF8, 0, "v" APP_VERSION, -1, ver, 32);
        ui_text(dc, tr, L"MicLink", f, g_th.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        ui_fill_round(dc, pr, sd(10), g_th.sunken, 255);
        ui_text(dc, pr, ver, pf, g_th.text_dim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    /* 主题切换：两段 segmented（浅色/深色，高 28、R2、共享 1px border） */
    {
        static const wchar_t *lb[2] = { L"浅色", L"深色" };
        RECT frame = { g_lo.seg_theme[0].left, g_lo.seg_theme[0].top,
                       g_lo.seg_theme[1].right, g_lo.seg_theme[1].bottom };
        int i;
        ui_fill_round(dc, frame, sd(8), g_th.bg, 255);
        for (i = 0; i < 2; i++) {
            int on = ((g_ui.dark == 1) == (i == 1));
            int hov = (g_ui.hover == (i ? ID_THEME_DARK : ID_THEME_LIGHT));
            if (hov && !on) {
                int fade = (g_ui.hover_fade_id == (i ? ID_THEME_DARK : ID_THEME_LIGHT))
                           ? g_ui.hover_fade : 0;
                if (fade) ui_fill_round(dc, g_lo.seg_theme[i], 0, g_th.card_hi,
                                        (int)(230 * fade / 1000));
            }
            if (on) ui_fill_round(dc, g_lo.seg_theme[i], 0, g_th.sunken, 255);
            ui_text(dc, g_lo.seg_theme[i], lb[i], ui_font(12, FW_NORMAL, 0),
                    on ? g_th.text : g_th.text_dim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        {
            RECT dv = { g_lo.seg_theme[1].left, frame.top + 1,
                        g_lo.seg_theme[1].left + 1, frame.bottom - 1 };
            ui_fill_round(dc, dv, 0, g_th.stroke, 255);
        }
        ui_stroke_round(dc, frame, sd(8), g_th.stroke, 255, 1);
    }
    /* 最小化/最大化/关闭（各 46×48，glyph 1.2px text-2） */
    if (g_ui.hover == ID_MIN || g_ui.hover == ID_MAX)
        ui_fill_round(dc, g_ui.hover == ID_MIN ? g_lo.btn_min : g_lo.btn_max,
                      0, g_th.card_hi, 255);
    if (g_ui.hover == ID_CLOSE)
        ui_fill_round(dc, g_lo.btn_close, 0, g_th.err, 255);
    {
        RECT g;
        int gw = sd(10);
        g.left = (g_lo.btn_min.left + g_lo.btn_min.right) / 2 - gw / 2;
        g.top = (g_lo.btn_min.top + g_lo.btn_min.bottom) / 2 - gw / 2;
        g.right = g.left + gw; g.bottom = g.top + gw;
        ui_icon(dc, g, IC_MIN, g_ui.hover == ID_MIN ? g_th.text : g_th.text_dim, sd(1));
        g.left = (g_lo.btn_max.left + g_lo.btn_max.right) / 2 - gw / 2;
        g.right = g.left + gw;
        ui_icon(dc, g, IC_MAX, g_ui.hover == ID_MAX ? g_th.text : g_th.text_dim, sd(1));
        g.left = (g_lo.btn_close.left + g_lo.btn_close.right) / 2 - gw / 2;
        g.right = g.left + gw;
        ui_icon(dc, g, IC_CLOSE, g_ui.hover == ID_CLOSE ? RGB(255, 255, 255) : g_th.text_dim, sd(1));
    }
}

/* ------------------------------------------------------- D-02 三步进度 */
static void draw_steps(HDC dc, RECT card)
{
    int s1 = (g_ui.drv.state == DRV_READY);
    int s2 = (g_ui.running && g_ui.st.state != ST_IDLE && g_ui.st.state != ST_CONNECTING);
    int s3 = (g_ui.running && g_ui.st.state == ST_STREAMING);
    int done[3] = { s1, s2, s3 };
    int cur = 3, k;
    for (k = 0; k < 3; k++) if (!done[k]) { cur = k; break; }

    for (k = 0; k < 3; k++) {
        RECT d = g_lo.step[k];
        int rad = (d.bottom - d.top) / 2;
        int lw = ui_measure_text(ui_step_label(k), ui_font(11, FW_NORMAL, 0));
        RECT lr = { d.right + sd(6), d.top - sd(1), d.right + sd(6) + lw, d.bottom + sd(1) };
        if (done[k]) {
            /* 完成 = ok 底 + 白勾（on-accent 10px 路径，1.8px 圆头） */
            ui_fill_round(dc, d, rad, g_th.ok, 255);
            ui_icon(dc, d, IC_CHECK, g_th.on_accent, sd(1));
            ui_text(dc, lr, ui_step_label(k), ui_font(11, FW_NORMAL, 0), g_th.text_dim,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else if (cur == k) {
            /* 当前步 = accent 2px 环 */
            ui_fill_round(dc, d, rad, g_th.card, 255);
            ui_stroke_round(dc, d, rad, g_th.accent, 255, sd(2));
            ui_text(dc, lr, ui_step_label(k), ui_font(11, FW_NORMAL, 0), g_th.text_dim,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            /* 未开始 = border 1px 环 + text-3 */
            ui_stroke_round(dc, d, rad, g_th.stroke, 255, 1);
            ui_text(dc, lr, ui_step_label(k), ui_font(11, FW_NORMAL, 0), g_th.text_mute,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    /* 步间连接线：1px border，标签尾 +8 到下一圆 -8，y = 圆心 */
    for (k = 0; k < 2; k++) {
        int lw = ui_measure_text(ui_step_label(k), ui_font(11, FW_NORMAL, 0));
        int x0 = g_lo.step[k].right + sd(6) + lw + sd(8);
        int x1 = g_lo.step[k + 1].left - sd(8);
        int y = (g_lo.step[k].top + g_lo.step[k].bottom) / 2;
        if (x1 > x0) ui_line(dc, x0, y, x1, y, g_th.stroke, 1.0, 255);
    }
    (void)card;
}

/* ------------------------------------------------------- 连接卡 */
static void draw_input(HDC dc, RECT box, const wchar_t *label)
{
    /* C-06 输入框：bg-sunken + R2 + 1px border；标签在框上方 4 */
    RECT lr = { box.left, box.top - sd(19), box.left + sd(220), box.top - sd(4) };
    ui_fill_round(dc, box, sd(8), g_th.sunken, 255);
    ui_stroke_round(dc, box, sd(8), g_th.stroke, 255, 1);
    ui_text(dc, lr, label, ui_font(11, FW_NORMAL, 0), g_th.text_dim,
            DT_LEFT | DT_SINGLELINE);
}

void ui_draw_conn_card(HDC dc)
{
    RECT r = g_lo.card_conn;
    int T = g_lo.tight;
    int t_tlab = T ? sd(76) : sd(90);

    ui_card(dc, r);
    ui_card_title(dc, r, L"连接");
    draw_steps(dc, r);

    /* 传输方式标签 + 四段 segmented（D-03：仅 Wi-Fi 已实现，其余可见禁用） */
    {
        RECT lr = { r.left + ui_pad(), r.top + t_tlab, r.right - ui_pad(), r.top + t_tlab + sd(15) };
        static const wchar_t *lb[4] = { L"Wi-Fi", L"Wi-Fi 直连", L"蓝牙", L"USB" };
        int en[4] = { 1, 0, 0, 0 };
        RECT frame = { g_lo.seg_trans[0].left, g_lo.seg_trans[0].top,
                       g_lo.seg_trans[3].right, g_lo.seg_trans[0].bottom };
        ui_text(dc, lr, L"传输方式", ui_font(11, FW_NORMAL, 0), g_th.text_dim,
                DT_LEFT | DT_SINGLELINE);
        ui_seg_group(dc, frame, lb, 4, g_ui.transport, ID_TRANS0, en);
    }

    /* 手机 IP / 控制端口 / 媒体端口（C-06，可编辑） */
    draw_input(dc, g_lo.ed_host, L"手机 IP");
    draw_input(dc, g_lo.ed_port, L"控制端口");
    draw_input(dc, g_lo.ed_media, L"媒体端口（留空 = 随机）");

    /* 主按钮：状态机着色 + D-04 运行态呼吸 */
    {
        const wchar_t *lbl = L"连接";
        int style = BTN_PRIMARY;
        int enabled = (g_ui.drv.state == DRV_READY);
        if (g_ui.running) {
            if (g_ui.st.state == ST_STREAMING) {
                lbl = L"断开连接"; style = BTN_DANGER;
            } else {
                lbl = L"连接中…"; style = BTN_ACCENT_OUT;
            }
            enabled = 1;
        }
        if (g_ui.running && g_ui.st.state == ST_STREAMING)
            ui_breath_glow(dc, g_lo.btn_conn);
        ui_button(dc, g_lo.btn_conn, lbl, ID_CONN, style, enabled);
    }
}

/* ------------------------------------------------------- 驱动卡 */
void ui_draw_drv_card(HDC dc)
{
    RECT r = g_lo.card_drv;
    DrvStatus *d = &g_ui.drv;
    int ready = (d->state == DRV_READY);

    ui_card(dc, r);
    ui_card_title(dc, r, L"驱动");

    /* 标题行右侧：状态徽标（C-11 三通道）+ 两个小文字按钮 */
    {
        HFONT bf = ui_font(11, FW_SEMIBOLD, 0);
        const wchar_t *txt = drv_state_text(d->state);
        int bw = ui_measure_text(txt, bf) + sd(26);
        RECT br = { g_lo.btn_uninstall.left - sd(8) - bw, r.top + sd(12),
                    g_lo.btn_uninstall.left - sd(8), r.top + sd(32) };
        ui_badge(dc, br, txt, drv_state_color(d->state), drv_state_bg(d->state));
    }
    /* 文案由布局决定（窄窗自动缩为「卸载」/「说明」），宽度也是布局算好的，
     * 两者必须同源，否则文字会溢出矩形被卡片边缘裁掉。 */
    ui_button(dc, g_lo.btn_uninstall, ui_lbl_uninstall(), ID_UNINSTALL, BTN_TEXT,
              d->state != DRV_NOT_INSTALLED);
    ui_button(dc, g_lo.btn_help, ui_lbl_help(), ID_HELP, BTN_TEXT, 1);

    if (ready) {
        /* 只读次级信息 pill 化（D-09）：真实设备名 + 真实格式 */
        ui_pill(dc, g_lo.pill_dev, L"虚拟设备",
                d->friendly[0] ? d->friendly : L"WO Mic Device");
        ui_pill(dc, g_lo.pill_fmt, L"格式", L"48 kHz · 单声道 · 16 bit");
        ui_button(dc, g_lo.btn_openset, L"打开声音设置", ID_OPENSET, BTN_SECONDARY, 1);
    } else {
        /* 未就绪：原因 + 安装驱动（此时为主按钮；连接按钮置灰，P-2 一屏一个 primary） */
        RECT sr = { r.left + ui_pad(), r.top + sd(46), r.right - ui_pad(), r.top + sd(94) };
        const wchar_t *why = (d->state == DRV_NEED_REBOOT)
            ? L"驱动已安装但设备未就绪，通常重启电脑后恢复；详见事件面板。"
            : L"未检测到虚拟麦克风驱动。先装驱动，才能把手机麦克风送进 Windows。";
        ui_text(dc, sr, why, ui_font(12, FW_NORMAL, 0), g_th.text_dim,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
        ui_button(dc, g_lo.btn_install, L"安装驱动", ID_INSTALL, BTN_PRIMARY, 1);
    }
}

/* ------------------------------------------------------- 音频参数面板 */
void ui_draw_audio_card(HDC dc)
{
    static const wchar_t *rate_lbl[3] = { L"48 kHz", L"16 kHz", L"8 kHz" };
    static const wchar_t *buf_lbl[3]  = { L"26 ms", L"60 ms", L"120 ms" };
    wchar_t sum[64];
    int en[3] = { 1, 1, 1 };

    swprintf(sum, 64, L"48 kHz · 单声道 · 16 bit · %d ms",
             g_ui.buf_idx == 0 ? 26 : g_ui.buf_idx == 1 ? 60 : 120);
    ui_card(dc, g_lo.card_audio);
    ui_panel_hdr(dc, g_lo.hdr_audio, L"音频参数", sum, g_ui.exp_audio, ID_EXP_AUDIO);
    if (!g_ui.exp_audio) return;

    {
        RECT c = g_lo.card_audio;
        RECT lr = { c.left + ui_pad(), c.top + sd(60), c.right - ui_pad(), c.top + sd(75) };
        ui_text(dc, lr, L"采样率（灌给驱动的一律是 48 kHz）", ui_font(11, FW_NORMAL, 0),
                g_th.text_dim, DT_LEFT | DT_SINGLELINE);
    }
    ui_seg_group(dc, (RECT){ g_lo.seg_rate[0].left, g_lo.seg_rate[0].top,
                             g_lo.seg_rate[2].right, g_lo.seg_rate[0].bottom },
                 rate_lbl, 3, g_ui.seg_rate, ID_RATE0, en);
    {
        RECT c = g_lo.card_audio;
        RECT lr = { c.left + ui_pad(), c.top + sd(117), c.right - ui_pad(), c.top + sd(132) };
        ui_text(dc, lr, L"环形缓冲（26 ms 低延迟 / 120 ms 更抗抖动）", ui_font(11, FW_NORMAL, 0),
                g_th.text_dim, DT_LEFT | DT_SINGLELINE);
    }
    ui_seg_group(dc, (RECT){ g_lo.seg_buf[0].left, g_lo.seg_buf[0].top,
                             g_lo.seg_buf[2].right, g_lo.seg_buf[0].bottom },
                 buf_lbl, 3, g_ui.buf_idx, ID_BUF0, en);
    /* 只读信息（保持清单 #5：灰底只读，2×2） */
    {
        static const wchar_t *k[4] = { L"输出", L"声道", L"位深", L"缓冲" };
        wchar_t v[4][24];
        RECT c = g_lo.card_audio;
        int cw = (c.right - c.left - sd(32) - sd(8)) / 2;
        int i;
        swprintf(v[0], 24, L"48000 Hz");
        swprintf(v[1], 24, L"单声道");
        swprintf(v[2], 24, L"16 bit");
        swprintf(v[3], 24, L"%d ms", g_ui.buf_idx == 0 ? 26 : g_ui.buf_idx == 1 ? 60 : 120);
        for (i = 0; i < 4; i++) {
            int col = i % 2, row = i / 2;
            RECT cell = { c.left + ui_pad() + col * (cw + sd(8)), c.top + sd(174) + row * sd(32),
                          c.left + ui_pad() + col * (cw + sd(8)) + cw, c.top + sd(198) + row * sd(32) };
            int x = cell.left + sd(10);
            ui_fill_round(dc, cell, sd(8), g_th.sunken, 255);
            ui_stroke_round(dc, cell, sd(8), g_th.stroke, 255, 1);
            ui_text(dc, (RECT){ x, cell.top, x + sd(70), cell.bottom }, k[i],
                    ui_font(11, FW_NORMAL, 0), g_th.text_dim,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            ui_text(dc, (RECT){ x + sd(76), cell.top, cell.right - sd(10), cell.bottom }, v[i],
                    ui_font(12, FW_NORMAL, 1), g_th.text,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

/* ------------------------------------------------------- 会话统计面板 */
void ui_draw_stats_card(HDC dc)
{
    EngineStats *s = &g_ui.st_disp;
    int live = g_ui.running && s->state != ST_IDLE;
    wchar_t sum[96], v[3][32], cur[3][96];
    int i;

    /* 丢包率 = 跳号数 / 期望总数（engine 侧已按 seq 统计）；
     * 没有任何期望帧时显示「—」，不要拿 0.0% 冒充"测过且很好"。 */
    if (live && s->state == ST_STREAMING && s->expected) {
        int dur = (int)((GetTickCount() - s->started_at) / 1000);
        swprintf(sum, 96, L"%d:%02d · %d ms · 丢包 %.1f%%",
                 dur / 60, dur % 60, s->jitter_ms,
                 (double)s->lost * 100.0 / (double)s->expected);
    } else {
        swprintf(sum, 96, L"— · — · 丢包 —");
    }
    ui_card(dc, g_lo.card_stats);
    ui_panel_hdr(dc, g_lo.hdr_stats, L"会话统计", sum, g_ui.exp_stats, ID_EXP_STATS);
    if (!g_ui.exp_stats) return;

    /* 三行指标条：码率 / 抖动 / 丢包（D-12）
     * 「抖动」不是「延迟」：读数是 UDP 帧到达间隔的 EMA，只反映网络抖动，
     * 不含编码/解码/驱动缓冲，叫延迟是错名。 */
    {
        static const wchar_t *lb[3] = { L"码率", L"抖动", L"丢包" };
        double frac[3] = { 0, 0, 0 };
        COLORREF fill[3] = { g_th.ok, g_th.ok, g_th.ok };
        double thr[3][2] = { { 0, 0 }, { 0.20, 0.50 }, { 0.10, 0.50 } };  /* 80/200ms、1%/5% */
        double lost_pct = s->expected
                          ? (double)s->lost * 100.0 / (double)s->expected : 0.0;
        RECT c = g_lo.card_stats;
        int W = c.right - c.left - sd(32);
        int bx0 = c.left + ui_pad() + sd(152);
        int bx1 = c.right - ui_pad() - sd(150);
        int bw = bx1 - bx0;

        if (live) {
            swprintf(v[0], 32, L"%d kbps", s->kbps);
            frac[0] = s->kbps / 512.0;
            swprintf(cur[0], 96, L"当前 %d · 峰值 %d", s->kbps, g_ui.kbps_peak);
            /* 抖动只在真有帧间隔样本时才有意义（否则 0 ms 是假读数） */
            if (s->jitter_ms > 0) {
                swprintf(v[1], 32, L"%d ms", s->jitter_ms);
                frac[1] = s->jitter_ms / 400.0;
                swprintf(cur[1], 96, L"当前 %d · 峰值 %d", s->jitter_ms, g_ui.jit_peak);
            } else {
                swprintf(v[1], 32, L"—");
                swprintf(cur[1], 96, L"—");
            }
        } else {
            for (i = 0; i < 2; i++) {
                swprintf(v[i], 32, L"—");
                swprintf(cur[i], 96, L"—");
            }
        }
        /* 丢包：一帧都没期望到时显示「—」，不拿 0.0% 冒充"测过且很好" */
        if (live && s->expected) {
            swprintf(v[2], 32, L"%.1f%%", lost_pct);
            frac[2] = lost_pct / 10.0;
            swprintf(cur[2], 96, L"当前 %.1f · 峰值 %.1f", lost_pct, g_ui.lost_peak);
        } else {
            swprintf(v[2], 32, L"—");
            swprintf(cur[2], 96, L"—");
        }
        if (live) {
            if (s->jitter_ms > 200)      fill[1] = g_th.err;
            else if (s->jitter_ms >= 80) fill[1] = g_th.warn;
            if (lost_pct > 5.0)           fill[2] = g_th.err;
            else if (lost_pct >= 1.0)     fill[2] = g_th.warn;
        }

        for (i = 0; i < 3; i++) {
            int y = c.top + sd(60) + i * sd(26);
            RECT lr = { c.left + ui_pad(), y, c.left + ui_pad() + sd(44), y + sd(20) };
            RECT vr = { c.left + sd(64), y, c.left + sd(64) + sd(88), y + sd(20) };
            RECT br = { bx0, y + sd(7), bx1, y + sd(13) };
            RECT cr = { bx1 + sd(6), y, c.right - ui_pad(), y + sd(20) };
            ui_text(dc, lr, lb[i], ui_font(14, FW_NORMAL, 0), g_th.text_dim,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            ui_text(dc, vr, v[i], ui_font(14, FW_NORMAL, 1), g_th.text,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            /* 横向进度条：轨道 6 胶囊 bg-sunken，填充随健康度 */
            ui_fill_round(dc, br, sd(3), g_th.sunken, 255);
            if (live && frac[i] > 0.0) {
                RECT fr = br;
                fr.right = br.left + (int)(bw * (frac[i] > 1.0 ? 1.0 : frac[i]));
                if (fr.right > fr.left) ui_fill_round(dc, fr, sd(3), fill[i], 255);
            }
            /* 阈值刻度 1px text-3 */
            {
                int t;
                for (t = 0; t < 2; t++) {
                    if (thr[i][t] <= 0.0) continue;
                    {
                        int tx = br.left + (int)(bw * thr[i][t]);
                        ui_line(dc, tx, br.top - sd(2), tx, br.bottom + sd(2),
                                g_th.text_mute, 1.0, 255);
                    }
                }
            }
            ui_text(dc, cr, cur[i], ui_font(11, FW_NORMAL, 0), g_th.text_mute,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        (void)W;
    }

    /* 第二区块：会话时长 / 重连次数 / 对端 IP / 传输方式（只读 Consolas） */
    {
        static const wchar_t *k[4] = { L"会话时长", L"重连次数", L"对端 IP", L"传输方式" };
        wchar_t v[4][40];
        RECT c = g_lo.card_stats;
        int cw = (c.right - c.left - sd(32) - sd(8)) / 2;
        int i;
        if (live && s->state == ST_STREAMING) {
            int dur = (int)((GetTickCount() - s->started_at) / 1000);
            swprintf(v[0], 40, L"%d:%02d", dur / 60, dur % 60);
        } else {
            swprintf(v[0], 40, L"—");
        }
        swprintf(v[1], 40, L"%d", g_ui.conn_count > 0 ? g_ui.conn_count - 1 : 0);
        if (live && s->peer[0]) {
            /* peer = "host:port"，只取主机部分 */
            char host[64];
            strncpy(host, s->peer, sizeof(host) - 1);
            host[sizeof(host) - 1] = 0;
            {
                char *colon = strrchr(host, ':');
                if (colon) *colon = 0;
            }
            MultiByteToWideChar(CP_UTF8, 0, host, -1, v[2], 40);
            v[2][39] = 0;
        } else {
            swprintf(v[2], 40, L"—");
        }
        swprintf(v[3], 40, L"Wi-Fi");
        for (i = 0; i < 4; i++) {
            int col = i % 2, row = i / 2;
            int x = c.left + ui_pad() + col * (cw + sd(8));
            int y = c.top + sd(144) + row * sd(41);
            ui_text(dc, (RECT){ x, y, x + cw, y + sd(15) }, k[i],
                    ui_font(11, FW_NORMAL, 0), g_th.text_dim, DT_LEFT | DT_SINGLELINE);
            ui_text(dc, (RECT){ x, y + sd(15), x + cw, y + sd(33) }, v[i],
                    ui_font(14, FW_NORMAL, 1), g_th.text,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
}

/* ------------------------------------------------------- 事件面板 */
void ui_draw_log_card(HDC dc)
{
    static const wchar_t *lv_name[3] = { L"INFO", L"ERROR", L"OK" };
    COLORREF lv_col[3];
    int y, i, line_h = sd(22);
    RECT c = g_lo.card_log;
    wchar_t sum[160];
    HFONT tf, lf, mf;

    lv_col[0] = g_th.text_dim;
    lv_col[1] = g_th.err;
    lv_col[2] = g_th.ok;

    if (ui_log_count() > 0) {
        const LogLine *L = ui_log_get(0);
        wchar_t msg[LOG_LEN];
        MultiByteToWideChar(CP_UTF8, 0, L->text, -1, msg, LOG_LEN);
        msg[LOG_LEN - 1] = 0;
        swprintf(sum, 160, L"最近 %.12ls · %.64ls", L->ts, msg);
    } else {
        swprintf(sum, 160, L"暂无事件");
    }
    ui_card(dc, c);
    ui_panel_hdr(dc, g_lo.hdr_log, L"事件", sum, g_ui.exp_log, ID_EXP_LOG);
    if (!g_ui.exp_log) return;

    /* 「复制全部」secondary 按钮置于面板右上（Ctrl+C 同效） */
    ui_button(dc, g_lo.btn_copylog, L"复制全部", ID_COPYLOG, BTN_SECONDARY, 1);

    tf = ui_font(11, FW_NORMAL, 1);       /* 等宽时间戳 HH:MM:SS.mmm */
    lf = ui_font(11, FW_SEMIBOLD, 0);     /* 级别标签 */
    mf = ui_font(12, FW_NORMAL, 0);       /* 消息正文 */
    y = c.top + sd(92);
    for (i = 0; i < ui_log_count(); i++) {
        const LogLine *L = ui_log_get(i);
        wchar_t msg[LOG_LEN];
        RECT tr, mr, lr;
        COLORREF lc;
        if (!L) break;
        if (y + line_h > c.bottom - sd(8)) break;
        MultiByteToWideChar(CP_UTF8, 0, L->text, -1, msg, LOG_LEN);
        msg[LOG_LEN - 1] = 0;
        lc = lv_col[L->kind < 0 || L->kind > 2 ? 0 : L->kind];
        /* 左侧 3px 级别竖条 */
        {
            RECT bar = { c.left + ui_pad(), y + sd(3), c.left + ui_pad() + sd(3), y + line_h - sd(3) };
            ui_fill_round(dc, bar, sd(1), lc, 255);
        }
        /* 等宽时间戳（固定宽度） */
        SetRect(&tr, c.left + sd(26), y, c.left + sd(26) + sd(96), y + line_h);
        ui_text(dc, tr, L->ts, tf, g_th.text_mute, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        /* 级别标签（着色） */
        SetRect(&lr, c.left + sd(126), y, c.left + sd(126) + sd(52), y + line_h);
        ui_text(dc, lr, lv_name[L->kind < 0 || L->kind > 2 ? 0 : L->kind], lf, lc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        /* 消息正文 text-1 */
        SetRect(&mr, c.left + sd(182), y, c.right - ui_pad(), y + line_h);
        ui_text(dc, mr, msg, mf, g_th.text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += line_h;
    }
    if (!ui_log_count()) {
        RECT tr = { c.left + ui_pad(), c.top + sd(92), c.right - ui_pad(), c.top + sd(114) };
        ui_text(dc, tr, L"暂无事件", ui_font(12, FW_NORMAL, 0), g_th.text_mute,
                DT_LEFT | DT_SINGLELINE);
    }
}

/* ------------------------------------------------------- D-14 状态栏 */
void ui_draw_statusbar(HDC dc)
{
    RECT r = { 0, g_ui.h - ui_status_h(), g_ui.w, g_ui.h };
    RECT ln = { 0, r.top, g_ui.w, r.top + 1 };
    EngineStats *s = &g_ui.st_disp;
    int live = g_ui.running && s->state == ST_STREAMING;
    const wchar_t *badge_txt;
    COLORREF badge_fg, badge_bg;
    wchar_t item[128];
    int x = sd(16);
    HFONT f = ui_font(12, FW_NORMAL, 0);
    HFONT mf = ui_font(12, FW_NORMAL, 1);
    RECT ir;

    ui_fill_round(dc, r, 0, g_th.sunken, 255);
    ui_fill_round(dc, ln, 0, g_th.stroke, 255);

    /* [状态徽标] · 传输方式 · 抖动 XX ms · 丢包 X.X% · 音量 ±dB · 时长 MM:SS（缺项「—」） */
    if (g_ui.running && s->state == ST_STREAMING) {
        badge_txt = L"已连接"; badge_fg = g_th.ok; badge_bg = g_th.ok_12;
    } else if (g_ui.running) {
        badge_txt = L"连接中"; badge_fg = g_th.accent; badge_bg = g_th.acc_12;
    } else if (g_ui.running == 0 && s->state == ST_ERROR) {
        badge_txt = L"连接错误"; badge_fg = g_th.err; badge_bg = g_th.err_12;
    } else {
        badge_txt = L"未连接"; badge_fg = g_th.text_dim; badge_bg = g_th.sunken;
    }
    {
        int bw = ui_measure_text(badge_txt, ui_font(11, FW_SEMIBOLD, 0)) + sd(26);
        ir.left = x; ir.top = r.top + sd(4); ir.right = x + bw; ir.bottom = r.bottom - sd(4);
        ui_badge(dc, ir, badge_txt, badge_fg, badge_bg);
        x = ir.right + sd(8);
    }
    /* 分隔 + 传输方式（当前仅 Wi-Fi 一种已实现，其余为可见禁用态，D-03） */
    ui_text(dc, (RECT){ x, r.top, x + sd(8), r.bottom }, L"·", f, g_th.text_mute,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += sd(8) + sd(8);
    ui_text(dc, (RECT){ x, r.top, x + sd(80), r.bottom },
            L"Wi-Fi", ui_font(12, FW_NORMAL, 2), g_th.text_dim,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(L"Wi-Fi", f) + sd(8);
    /* 抖动（旧名「延迟」是错名：这是帧到达间隔 EMA，不含编解码与驱动缓冲） */
    ui_text(dc, (RECT){ x, r.top, x + sd(8), r.bottom }, L"·", f, g_th.text_mute,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += sd(16);
    ui_text(dc, (RECT){ x, r.top, x + sd(40), r.bottom }, L"抖动", f, g_th.text_dim,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(L"抖动", f) + sd(4);
    if (live && s->jitter_ms > 0) swprintf(item, 128, L"%d ms", s->jitter_ms);
    else swprintf(item, 128, L"—");
    ui_text(dc, (RECT){ x, r.top, x + sd(80), r.bottom }, item, mf, g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(item, mf) + sd(8);
    /* 丢包 */
    ui_text(dc, (RECT){ x, r.top, x + sd(8), r.bottom }, L"·", f, g_th.text_mute,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += sd(16);
    ui_text(dc, (RECT){ x, r.top, x + sd(40), r.bottom }, L"丢包", f, g_th.text_dim,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(L"丢包", f) + sd(4);
    if (live && s->expected) {
        double pct = (double)s->lost * 100.0 / (double)s->expected;
        swprintf(item, 128, L"%.1f%%", pct);
    } else {
        swprintf(item, 128, L"—");          /* 没数据时是「—」，不是 0.0% */
    }
    ui_text(dc, (RECT){ x, r.top, x + sd(80), r.bottom }, item, mf, g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(item, mf) + sd(8);
    /* 手机端推流音量（协议 vol 字节）：0=静音，5=原样，其余 ±6 dB/档。
     * 放在状态栏是为了让"手机上调音量"在 PC 端有可见落点。 */
    ui_text(dc, (RECT){ x, r.top, x + sd(8), r.bottom }, L"·", f, g_th.text_mute,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += sd(16);
    ui_text(dc, (RECT){ x, r.top, x + sd(40), r.bottom }, L"音量", f, g_th.text_dim,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(L"音量", f) + sd(4);
    if (live && s->vol >= 0) {
        static const wchar_t *vd[10] = { L"静音", L"−24 dB", L"−18 dB", L"−12 dB",
                                         L"−6 dB", L"0 dB", L"+6 dB", L"+12 dB",
                                         L"+18 dB", L"+24 dB" };
        swprintf(item, 128, L"%ls", (s->vol <= 9) ? vd[s->vol] : L"—");
    } else {
        swprintf(item, 128, L"—");
    }
    ui_text(dc, (RECT){ x, r.top, x + sd(80), r.bottom }, item, mf, g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(item, mf) + sd(8);
    /* 时长 */
    ui_text(dc, (RECT){ x, r.top, x + sd(8), r.bottom }, L"·", f, g_th.text_mute,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += sd(16);
    ui_text(dc, (RECT){ x, r.top, x + sd(40), r.bottom }, L"时长", f, g_th.text_dim,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += ui_measure_text(L"时长", f) + sd(4);
    if (live && s->started_at) {
        int dur = (int)((GetTickCount() - s->started_at) / 1000);
        swprintf(item, 128, L"%d:%02d", dur / 60, dur % 60);
    } else {
        swprintf(item, 128, L"—");
    }
    ui_text(dc, (RECT){ x, r.top, x + sd(80), r.bottom }, item, mf, g_th.text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

/* ------------------------------------------------------- 整窗绘制 */
void ui_draw_all(HDC dc)
{
    RECT full = { 0, 0, g_ui.w, g_ui.h };
    int saved;

    ui_bg(dc, full);
    ui_draw_titlebar(dc);

    /* 卡片流只画在「标题栏与状态栏之间」的可视区里：展开面板后内容超出窗口时，
     * 超出的部分必须被裁掉，而不是画到状态栏下面去 */
    saved = SaveDC(dc);
    IntersectClipRect(dc, 0, ui_title_h(), g_ui.w, g_ui.h - ui_status_h());
    /* R-08：每个卡片再各自裁一次。可视区裁剪只保证不出窗口，卡片内的子控件
     * （段控/滑杆/历史柱/开关）在滚动时同样可能越出自己的卡片边界——
     * 上一版电平卡的增益滑杆和静音行就浮到了标题栏上。卡片裁剪与可视区
     * 裁剪求交，嵌套安全。 */
    {
        RECT cr;
        cr = g_lo.card_conn;  { int s = ui_clip_push(dc, cr);
            ui_draw_conn_card(dc);  ui_clip_pop(dc, s); }
        cr = g_lo.card_drv;   { int s = ui_clip_push(dc, cr);
            ui_draw_drv_card(dc);   ui_clip_pop(dc, s); }
        cr = g_lo.card_meter; { int s = ui_clip_push(dc, cr);
            ui_meter_draw(dc, cr);  ui_clip_pop(dc, s); }
        cr = g_lo.card_audio; { int s = ui_clip_push(dc, cr);
            ui_draw_audio_card(dc); ui_clip_pop(dc, s); }
        cr = g_lo.card_stats; { int s = ui_clip_push(dc, cr);
            ui_draw_stats_card(dc); ui_clip_pop(dc, s); }
        cr = g_lo.card_log;   { int s = ui_clip_push(dc, cr);
            ui_draw_log_card(dc);   ui_clip_pop(dc, s); }
    }
    RestoreDC(dc, saved);

    ui_draw_statusbar(dc);

    /* 有滚动空间时画一条细指示，避免用户以为内容就这么多 */
    if (g_lo.content_h > ui_title_h() + g_lo.view_h) {
        int track_y = ui_title_h() + sd(4);
        int track_h = g_lo.view_h - sd(8);
        int total = g_lo.content_h;
        int th = track_h * g_lo.view_h / (total ? total : 1);
        int ty = track_y + (track_h - th) * g_ui.scroll /
                 ((total - ui_title_h() - g_lo.view_h) > 0 ? (total - ui_title_h() - g_lo.view_h) : 1);
        RECT sb = { g_ui.w - sd(6), track_y, g_ui.w - sd(2), track_y + track_h };
        RECT kn = { g_ui.w - sd(6), ty, g_ui.w - sd(2), ty + (th < sd(16) ? sd(16) : th) };
        ui_fill_round(dc, sb, sd(2), g_th.stroke, 120);
        ui_fill_round(dc, kn, sd(2), g_th.accent, 200);
    }
}

/* ---------------------------------------------------------------- 日志 */
static LogLine g_logs[LOG_LINES];
static int g_head, g_count;

void ui_log_add(const char *s, int kind)
{
    LogLine *L = &g_logs[g_head];
    strncpy(L->text, s, LOG_LEN - 1);
    L->text[LOG_LEN - 1] = 0;
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        /* D-13：等宽时间戳 HH:MM:SS.mmm */
        swprintf(L->ts, 16, L"%02u:%02u:%02u.%03u",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }
    L->kind = kind;
    g_head = (g_head + 1) % LOG_LINES;
    if (g_count < LOG_LINES) g_count++;
}

int ui_log_count(void) { return g_count; }

const LogLine *ui_log_get(int i)
{
    if (i < 0 || i >= g_count) return NULL;
    return &g_logs[(g_head - 1 - i + LOG_LINES * 2) % LOG_LINES];
}

int ui_log_copy_all(void)
{
    /* D-13：复制全部（Ctrl+C 同效）。按时间正序（旧→新）写入剪贴板。 */
    int n = ui_log_count();
    size_t cap = (size_t)(n > 0 ? n : 1) * (LOG_LEN + 48) + 1;
    wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t));
    HGLOBAL hgl;
    int i, ok = 0;
    if (!buf) return 0;
    buf[0] = 0;
    for (i = n - 1; i >= 0; i--) {
        const LogLine *L = ui_log_get(i);
        const wchar_t *lv = (L->kind == 1) ? L"ERROR" : (L->kind == 2) ? L"OK" : L"INFO";
        wchar_t msg[LOG_LEN];
        size_t used = wcslen(buf);
        if (!L) continue;
        MultiByteToWideChar(CP_UTF8, 0, L->text, -1, msg, LOG_LEN);
        msg[LOG_LEN - 1] = 0;
        if (used + LOG_LEN + 48 >= cap) break;
        swprintf(buf + used, cap - used, L"%ls %ls %ls\r\n", L->ts, lv, msg);
    }
    if (!OpenClipboard(g_ui.hwnd)) { free(buf); return 0; }
    {
        size_t bytes = (wcslen(buf) + 1) * sizeof(wchar_t);
        hgl = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hgl) {
            void *dst = GlobalLock(hgl);
            if (dst) {
                memcpy(dst, buf, bytes);
                GlobalUnlock(hgl);
                if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, hgl)) ok = 1;
            }
            if (!ok) GlobalFree(hgl);
        }
    }
    CloseClipboard();
    free(buf);
    return ok;
}
