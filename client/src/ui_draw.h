/* ui_draw.h — 绘制原语与通用控件
 *
 * 全部用 GDI / GDI+ 手绘（GDI+ 是运行时 LoadLibrary，缺失自动退化为纯 GDI），
 * 不引入任何 UI 框架、不嵌图片、不嵌字体 —— 这是「单文件几十 KB + 零依赖」的前提。
 * 排版数值（R-24「略饱满」档，Win px @96dpi）：
 *   应用标题 18/24 Semibold(600) · 卡片标题 15/20 Semibold(600) · 正文 14/18 ·
 *   次要 12/16 · 标签 11/15 · 读数/时间戳 Consolas 13/18。
 * 字体（R-25，全部系统自带，EnumFontFamiliesEx 探测后按链回退）：
 *   界面/中文 Microsoft YaHei UI（→YaHei→DengXian→SimHei）；
 *   读数/时间戳 Consolas；纯拉丁/数字 Segoe UI。字重只用 400/600 命名档。
 */
#ifndef MICLINK_UI_DRAW_H
#define MICLINK_UI_DRAW_H

#include "app.h"

/* DPI 缩放：所有尺寸都必须过一遍 sd()，否则 125%/150% 下会错位 */
int sd(int v);

/* 字体缓存：同一 (px,weight,mono,dpi) 只创建一次，DPI 变化时 ui_fonts_reset()。
 * 尺寸单位是逻辑像素（96dpi 下 1px = 1px），内部再按 DPI 放大。 */
/* mono：0 = 界面（中文回退链）/ 1 = Consolas 等宽 / 2 = Segoe UI 纯拉丁 */
HFONT ui_font(int px, int weight, int mono);
void  ui_fonts_reset(void);
/* 卡片内距（R-24：16 → 14）。绘制侧与布局侧必须同源，不要各写 sd(14)。 */
int   ui_pad(void);
/* 子控件裁剪（R-08）：push 保存当前 DC 状态并把裁剪夹到 r 内，
 * pop 恢复。所有卡片/滚动区内的子控件都必须先 push 再画。 */
int   ui_clip_push(HDC dc, RECT r);
void  ui_clip_pop(HDC dc, int saved);
int   ui_measure_text(const wchar_t *s, HFONT f);
/* 该字号一行文字的高度（设备像素）。R-30：单行 EDIT 不会自己垂直居中，
 * 要让它的高度=一行文字高再整体居中，所以得有这个测量。 */
int   ui_text_height(int px);

/* 颜色混合：a 与 b 按 pct%（b 的占比）混合。电平卡 dim 段等派生色复用。 */
COLORREF ui_mix(COLORREF a, COLORREF b, int pct);

/* GDI+：动态加载，失败自动退化（直角 + 无抗锯齿） */
void  ui_gdiplus_init(void);
int   ui_gdiplus_ready(void);

/* 原语 */
void  ui_fill_round(HDC dc, RECT r, int rad, COLORREF c, int alpha);
void  ui_stroke_round(HDC dc, RECT r, int rad, COLORREF c, int alpha, int width);
void  ui_bg(HDC dc, RECT r);                    /* 窗口纯色底（bg-app） */
void  ui_text(HDC dc, RECT r, const wchar_t *s, HFONT f, COLORREF c, UINT fmt);
void  ui_text_u8(HDC dc, RECT r, const char *s, HFONT f, COLORREF c, UINT fmt);
/* 屏幕坐标圆弧/直线（角度 0=正右，顺时针为正；alpha 0..255） */
void  ui_arc(HDC dc, int cx, int cy, int rad, double a0, double a1,
             COLORREF c, int width, int alpha);
void  ui_line(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, double width, int alpha);

/* 图标（标题栏三键 + 三步勾，手绘线条，不用字形字体） */
enum { IC_MIN = 0, IC_MAX, IC_CLOSE, IC_CHECK };
void  ui_icon(HDC dc, RECT r, int kind, COLORREF c, int width);

/* 控件 */
void  ui_card(HDC dc, RECT r);                  /* bg-card + R3 + 1px border */
void  ui_card_title(HDC dc, RECT r, const wchar_t *title);

/* BTN_DANGER_OUT（R-40）：与 BTN_ACCENT_OUT 同构的"危险描边"——err 1px 描边 +
 * err 文字、透明底。用于"进行态可取消"的按钮（取消连接 / 停止重连）。 */
enum { BTN_PRIMARY = 0, BTN_SECONDARY, BTN_DANGER, BTN_TEXT, BTN_ACCENT_OUT,
       BTN_DANGER_OUT };
/* D-10：hover 描边 border→accent 40%（T1 120ms 过渡，fade 由 g_ui.hover_fade 驱动）；
 *       press 矩形内缩 1px 重绘。enabled=0 为可见禁用（text-3）。 */
void  ui_button(HDC dc, RECT r, const wchar_t *label, int id, int style, int enabled);
/* 分段控件：共享外框 + 1px 内分隔；选中段 accent 底 + on-accent 文字；
 * enabled[i]=0 的段可见但禁用（D-03：仅 Wi-Fi 已实现）。 */
void  ui_seg_group(HDC dc, RECT r, const wchar_t * const *labels, int n,
                   int active, int id_base, const int *enabled);
void  ui_slider(HDC dc, RECT r, int pos, int id);       /* pos 0..1000 */
void  ui_switch(HDC dc, RECT r, int on);                /* C-08 开关 40×20 */
void  ui_badge(HDC dc, RECT r, const wchar_t *text, COLORREF fg, COLORREF bg);
void  ui_pill(HDC dc, RECT r, const wchar_t *key, const wchar_t *val);
void  ui_panel_hdr(HDC dc, RECT r, const wchar_t *title, const wchar_t *sum,
                   int expanded, int id);
/* D-04/D-22：运行态 err 呼吸外发光（周期 1200ms、alpha 0.08→0.30→0.08、扩散 6→10px）。
 * 与 Android infiniteTransition 同参数；只在会话运行/连接中调用。 */
void  ui_breath_glow(HDC dc, RECT r);

/* 增益（R-31/R-32）：内部一律 0.1 dB（-600..+60），% 只在读数里反算显示。
 * 轨道 pos(0..1000) ↔ dB 线性：dB = -60 + 66*pos/1000；倍率 = pow(10,dB/20)，
 * -60 dB 仍是 0.001 倍（不是静音）；静音只由静音开关表达。
 * 吸附：0 dB ±1.5 dB、-12/-24/-36/-48/-60 各 ±0.75 dB；只吸落点，继续拖即可脱离。 */
#define GAIN_DB10_MIN  (-600)
#define GAIN_DB10_MAX  (60)
int   ui_gain_pos(int db10);                 /* dB → 滑杆位置 0..1000 */
int   ui_pos_to_db10(int pos);               /* 位置 → dB（含吸附） */
int   ui_gain_snap_target(int db10);         /* 吸到哪个刻度（dB10）；未吸附返回 1000 */
void  ui_gain_reading(wchar_t *out, int cap, int db10);   /* 「增益 N% · −12.0 dB」 */

/* 各业务卡片的绘制（放在这里而不是 main.c，是为了让 main.c 保持成薄壳：
 * 事件分发 + 模块装配，不掺绘制细节） */
void  ui_draw_all(HDC dc);
void  ui_draw_titlebar(HDC dc);
void  ui_draw_conn_card(HDC dc);
void  ui_draw_drv_card(HDC dc);
void  ui_draw_audio_card(HDC dc);
void  ui_draw_stats_card(HDC dc);
void  ui_draw_log_card(HDC dc);
void  ui_draw_statusbar(HDC dc);

/* 日志环形缓冲（引擎线程 → UI 线程的字符串都先进这里） */
void  ui_log_add(const char *s, int kind);
int   ui_log_count(void);
const LogLine *ui_log_get(int i);
int   ui_log_copy_all(void);        /* 全部事件写入剪贴板（D-13，Ctrl+C 同效） */

#endif /* MICLINK_UI_DRAW_H */
