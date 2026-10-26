/* switch_anim_probe.c — R-46 开关动画离屏取证
 *
 * 目的：在无 WM / 无真机的环境里，证明「开关不是贴图、且真的在插值」：
 *   同一控件矩形，pos = 0 / 125 / … / 1000 各渲染一帧，
 *   逐帧找出滑块圆心位置 —— 若为硬切（贴图两态），圆心只有两个位置。
 *
 * 用法：见 client/build.sh 的 probe 模式
 *   ./build.sh probe   → 产出 build/switch_anim_probe.exe 并在 wine 下跑
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* 复用真实实现，避免"复制一份假实现"导致验证失真 */
#include "../src/app.h"
#include "../src/ui_draw.h"
#include "../src/theme.h"
#include "../src/ui_layout.h"

Ui g_ui;

/* ui_draw.c 里除开关之外的卡片绘制会引用布局符号（g_lo / ui_title_h …），
 * 那是"绘制与命中必须同源"的架构要求。探针路径不会执行到那些函数，
 * 这里给最小 stub 满足链接即可 —— 不复制业务逻辑，只补符号。 */
UiLayout g_lo;
int ui_title_h(void)  { return 48; }
int ui_status_h(void) { return 28; }
const wchar_t *ui_step_label(int i) { (void)i; return L""; }
const wchar_t *ui_lbl_uninstall(void) { return L""; }
const wchar_t *ui_lbl_help(void) { return L""; }
void ui_meter_draw(HDC dc, RECT r) { (void)dc; (void)r; }

int main(void)
{
    HDC screen = GetDC(NULL);
    HDC mem;
    HBITMAP bmp, old;
    RECT r = { 100, 100, 152, 132 };   /* 52×32，与实际开关同尺寸 */
    int pos;
    int prev_cx = -1;
    int moved = 0;

    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.dpi = 96;
    theme_apply(1);                 /* 真实色板，不用手调值 */

    ui_gdiplus_init();

    mem = CreateCompatibleDC(screen);
    bmp = CreateCompatibleBitmap(screen, 400, 300);
    old = (HBITMAP)SelectObject(mem, bmp);

    for (pos = 0; pos <= 1000; pos += 125) {
        int cx = -1, x;
        HDC pm;
        HBITMAP pb, po;

        ui_fill_round(mem, r, 16, g_th.bg, 255);
        ui_switch(mem, r, pos >= 500, pos);

        pm = CreateCompatibleDC(mem);
        pb = CreateCompatibleBitmap(mem, 400, 300);
        po = (HBITMAP)SelectObject(pm, pb);
        BitBlt(pm, 0, 0, 400, 300, mem, 0, 0, SRCCOPY);

        /* 沿中线找滑块：滑块是轨道上最亮的一段。
         * 阈值取 70：关闭态滑块 = text_mute(0x6E=110) 已超过，
         * 轨道底（sunken 0x10 / card_hi 0x26）远低于它。 */
        {
            int lo = -1, hi = -1;
            for (x = r.left; x < r.right; x++) {
                COLORREF c = GetPixel(pm, x, (r.top + r.bottom) / 2);
                int lum = (GetRValue(c) + GetGValue(c) + GetBValue(c)) / 3;
                if (lum >= 70) { if (lo < 0) lo = x; hi = x; }
            }
            if (lo >= 0) cx = (lo + hi) / 2;
        }
        SelectObject(pm, po);
        DeleteObject(pb);
        DeleteDC(pm);

        printf("pos=%4d  knob_cx=%3d\n", pos, cx);
        if (prev_cx >= 0 && cx != prev_cx) moved++;
        prev_cx = cx;
    }

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);

    /* 期望：9 帧里滑块有 8 次位移（连续插值）。
     * 若 moved <= 1，说明是硬切（两态贴图），不合格。 */
    printf("%s: moved=%d (期望 >=8 表示连续插值，而非两态硬切)\n",
           moved >= 8 ? "PASS" : "FAIL", moved);
    return moved >= 8 ? 0 : 1;
}
