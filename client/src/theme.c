/* theme.c — 两套色板（《定稿规范》§0 令牌速查 + 派生色公式）
 *
 * 深色 = win-final.png 定稿图取值；浅色 = 手册 §4.1 浅色列。
 * 派生色（禁止手调，按 §0 公式）：
 *   徽标底 = 语义色 12% 混合 bg-card   → ok_12 / warn_12 / acc_12 / err_12
 *   pill 底 = accent 8% 混合 bg-card    → pill
 * 规范同时给出这四个派生值的字面结果，这里逐字采用（已用公式复算核对：
 * 深色 ok #23352E / accent #28303D / err #372B2F、pill #242B35 全部吻合；
 * 浅色 pill #EEF3FE 吻合；浅色徽标 ok/err 吻合，浅色 accent 徽标按规范
 * 字面值 #EEF3FF 采用）。
 */
#include "theme.h"

Theme g_th;

void theme_apply(int dark)
{
    if (dark) {
        g_th.bg        = RGB(0x14, 0x17, 0x19);   /* bg-app   #141719 */
        g_th.bg2       = g_th.bg;                 /* 定稿为纯色底 */
        g_th.card      = RGB(0x1C, 0x20, 0x23);   /* bg-card  #1C2023 */
        g_th.card_hi   = RGB(0x26, 0x2B, 0x30);   /* hover 提亮（卡片与 text-3 之间） */
        g_th.sunken    = RGB(0x10, 0x13, 0x15);   /* bg-sunken #101315 */
        g_th.stroke    = RGB(0x2C, 0x32, 0x36);   /* border   #2C3236 */
        g_th.text      = RGB(0xE8, 0xEB, 0xEF);   /* text-1   #E8EBEF */
        g_th.text_dim  = RGB(0xA6, 0xB0, 0xBC);   /* text-2   #A6B0BC */
        g_th.text_mute = RGB(0x6E, 0x78, 0x83);   /* text-3   #6E7883 */
        g_th.accent    = RGB(0x7E, 0xA8, 0xFF);   /* accent   #7EA8FF */
        g_th.accent_dim= RGB(0x28, 0x30, 0x3D);
        g_th.on_accent = RGB(0x10, 0x13, 0x15);   /* 深色 accent 上的近黑文字 */
        g_th.ok        = RGB(0x54, 0xCE, 0x7F);   /* ok       #54CE7F */
        g_th.warn      = RGB(0xFF, 0xB0, 0x67);   /* warn     #FFB067 */
        g_th.err       = RGB(0xFF, 0x7A, 0x85);   /* err      #FF7A85 */
        g_th.ok_12     = RGB(0x23, 0x35, 0x2E);   /* 徽标底 ok 12% */
        g_th.warn_12   = RGB(0x37, 0x2B, 0x2B);   /* 徽标底 warn 12%（同公式派生） */
        g_th.acc_12    = RGB(0x28, 0x30, 0x3D);   /* 徽标底 accent 12% */
        g_th.err_12    = RGB(0x37, 0x2B, 0x2F);   /* 徽标底 err 12% */
        g_th.pill      = RGB(0x24, 0x2B, 0x35);   /* pill 底 accent 8% */
        g_th.meter_off = g_th.sunken;             /* 未点亮段 = bg-sunken */
        g_th.dark      = 1;
    } else {
        g_th.bg        = RGB(0xF4, 0xF6, 0xF9);   /* bg-app   #F4F6F9 */
        g_th.bg2       = g_th.bg;
        g_th.card      = RGB(0xFF, 0xFF, 0xFF);   /* bg-card  #FFFFFF */
        g_th.card_hi   = RGB(0xEC, 0xF0, 0xF5);   /* hover 提亮 */
        g_th.sunken    = RGB(0xEE, 0xF1, 0xF5);   /* bg-sunken #EEF1F5 */
        g_th.stroke    = RGB(0xDD, 0xE3, 0xEA);   /* border   #DDE3EA */
        g_th.text      = RGB(0x1A, 0x1D, 0x21);   /* text-1   #1A1D21 */
        g_th.text_dim  = RGB(0x5A, 0x64, 0x70);   /* text-2   #5A6470 */
        g_th.text_mute = RGB(0x8A, 0x94, 0xA0);   /* text-3   #8A94A0 */
        g_th.accent    = RGB(0x2A, 0x6D, 0xF4);   /* accent   #2A6DF4 */
        g_th.accent_dim= RGB(0xEE, 0xF3, 0xFF);
        g_th.on_accent = RGB(0xFF, 0xFF, 0xFF);   /* 浅色 accent 上的白字 */
        g_th.ok        = RGB(0x0E, 0x7C, 0x3A);   /* ok       #0E7C3A */
        g_th.warn      = RGB(0xC2, 0x52, 0x0C);   /* warn     #C2520C */
        g_th.err       = RGB(0xC4, 0x26, 0x2E);   /* err      #C4262E */
        g_th.ok_12     = RGB(0xE2, 0xEF, 0xE7);   /* 徽标底 ok 12% */
        g_th.warn_12   = RGB(0xF8, 0xEA, 0xE2);   /* 徽标底 warn 12%（同公式派生） */
        g_th.acc_12    = RGB(0xEE, 0xF3, 0xFF);   /* 徽标底 accent 12%（规范字面值） */
        g_th.err_12    = RGB(0xF8, 0xE5, 0xE6);   /* 徽标底 err 12% */
        g_th.pill      = RGB(0xEE, 0xF3, 0xFE);   /* pill 底 accent 8% */
        g_th.meter_off = g_th.sunken;
        g_th.dark      = 0;
    }
}
