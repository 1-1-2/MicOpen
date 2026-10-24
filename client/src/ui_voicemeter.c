/* ui_voicemeter.c — VoiceMeter
 *
 * 规格（来自需求）：
 *   · 32–40 段条（这里 34 段）
 *   · 阈值 <0.66 accent / <0.86 warn / >=0.86 err
 *   · 峰值保持线 + dBFS（等宽字体）+ 峰值% + 状态徽标（无信号 / 正常 / 过载）
 *   · attack 0.35（在 engine 侧做）、release 每秒 ×0.72
 *   · 连续 N 帧低于阈值 → 转“无信号”
 *   · 断线后 2 s 内回落
 */
#include "ui_voicemeter.h"
#include "ui_draw.h"
#include <math.h>
#include <wchar.h>

#define METER_SEGS   34
#define SILENT_LEVEL 0.004f
#define SILENT_FRAMES 40        /* 20ms/帧 → 约 0.8 s 静音即判无信号 */
#define OVER_LEVEL   0.86f

static float  s_level = 0.0f;
static float  s_peak = 0.0f;
static DWORD  s_last = 0;
static int    s_state;

int ui_meter_state(void) { return s_state; }

static float decay_per_sec(float dt)
{
    /* 每秒 ×0.72：dt 秒的衰减系数 */
    return (float)pow(0.72, (double)dt);
}

void ui_meter_update(const EngineStats *st, int running)
{
    DWORD now = GetTickCount();
    float dt = s_last ? (now - s_last) / 1000.0f : 0.016f;
    float k;
    if (dt > 0.5f) dt = 0.5f;
    if (dt < 0.0f) dt = 0.0f;
    s_last = now;
    k = decay_per_sec(dt);

    if (running && st->silent_frames < SILENT_FRAMES) {
        /* attack 由引擎完成（level += (rms-level)*0.35），这里直接用 */
        s_level = st->level;
    } else {
        /* 停止推流 / 连续静音 → release 慢回落，2 s 内基本归零 */
        s_level *= k;
        if (s_level < 0.0005f) s_level = 0.0f;
    }
    if (s_level < 0.0f) s_level = 0.0f;
    if (s_level > 1.0f) s_level = 1.0f;

    s_peak *= k;
    if (s_level > s_peak) s_peak = s_level;
    if (s_peak < 0.002f) s_peak = 0.0f;

    if (!running) s_state = 0;
    else if (s_level >= OVER_LEVEL) s_state = 2;
    else if (st->silent_frames >= SILENT_FRAMES) s_state = 0;
    else s_state = 1;
}

void ui_meter_draw(HDC dc, RECT r)
{
    wchar_t buf[64];
    int i, lit, peak_idx;
    int bx0 = r.left + sd(200), bx1 = r.right - sd(16);
    int bar_top = r.top + sd(100), bar_bot = r.top + sd(124);
    int gap = sd(2);
    int bw;
    int cx = r.left + sd(96), cy = r.top + sd(98), rad = sd(54);
    COLORREF ring_c;

    /* 卡片 hint 让位给右上徽标；引导语放条下 */
    ui_card(dc, r, L"输入电平", NULL);
    if (bar_bot <= bar_top) return;

    bw = ((bx1 - bx0) - gap * (METER_SEGS - 1)) / METER_SEGS;
    if (bw < 2) bw = 2;
    lit = (int)(s_level * METER_SEGS + 0.5f);
    peak_idx = (int)(s_peak * METER_SEGS + 0.5f);

    /* ---- 环形电平（左）：弧色随状态（accent=正常 / err=过载 / 灰=无信号） ---- */
    ring_c = (s_state == 2) ? g_th.err : (s_state == 1) ? g_th.accent : g_th.text_mute;
    ui_ring_level(dc, cx, cy, rad, s_level, ring_c);
    {
        RECT tr = { cx - sd(52), cy - sd(22), cx + sd(52), cy };
        if (s_level > 0.0005f)
            swprintf(buf, 64, L"%.1f", (double)(20.0f * (float)log10((double)s_level)));
        else
            wcscpy(buf, L"-inf");
        ui_text(dc, tr, buf, ui_font(13, FW_SEMIBOLD, 1), g_th.text,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        RECT tr = { cx - sd(52), cy + sd(2), cx + sd(52), cy + sd(20) };
        ui_text(dc, tr, L"dBFS", ui_font(8, FW_NORMAL, 0), g_th.text_mute,
                DT_CENTER | DT_SINGLELINE);
    }

    /* ---- 状态徽标（右上） ---- */
    {
        RECT br = { r.right - sd(140), r.top + sd(10), r.right - sd(16), r.top + sd(36) };
        COLORREF fg, bg;
        const wchar_t *txt;
        if (s_state == 2) {
            txt = L"过载"; fg = RGB(255, 255, 255); bg = g_th.err;
        } else if (s_state == 1) {
            txt = L"正常";
            fg = g_th.dark ? RGB(10, 18, 24) : RGB(255, 255, 255);
            bg = g_th.ok;
        } else {
            txt = L"无信号"; fg = g_th.text_dim;
            bg = g_th.dark ? RGB(44, 48, 58) : RGB(232, 235, 241);
        }
        ui_badge(dc, br, txt, fg, bg);
    }

    /* ---- dB 读数（右上，徽标下方） ---- */
    {
        RECT tr = { bx0, r.top + sd(44), bx1 - sd(150), r.top + sd(72) };
        if (s_level > 0.0005f)
            swprintf(buf, 64, L"%6.1f dBFS", (double)(20.0f * (float)log10((double)s_level)));
        else
            wcscpy(buf, L"-inf dBFS");
        ui_text(dc, tr, buf, ui_font(13, FW_NORMAL, 1), g_th.text, DT_LEFT | DT_SINGLELINE);
    }
    {
        RECT tr = { r.right - sd(150) - sd(10) - sd(150), r.top + sd(50),
                    r.right - sd(160), r.top + sd(68) };
        swprintf(buf, 64, L"峰值 %3.0f%%", (double)(s_peak * 100.0f));
        ui_text(dc, tr, buf, ui_font(9, FW_NORMAL, 1), g_th.text_dim, DT_RIGHT | DT_SINGLELINE);
    }

    /* ---- 目标区标记（条上方 -12~-6 dBFS → 振幅 0.25~0.50） ---- */
    {
        int zw = bx1 - bx0;
        RECT tz = { bx0 + zw * 25 / 100, bar_top - sd(7), bx0 + zw * 50 / 100, bar_top - sd(3) };
        ui_fill_round(dc, tz, sd(2), g_th.ok, 110);
        {
            RECT tl = { tz.left - sd(30), bar_top - sd(22), tz.right + sd(30), bar_top - sd(6) };
            ui_text(dc, tl, L"目标区", ui_font(7, FW_NORMAL, 0), g_th.text_mute,
                    DT_CENTER | DT_SINGLELINE);
        }
    }

    /* ---- 横向分段电平条（右） ---- */
    for (i = 0; i < METER_SEGS; i++) {
        RECT br = { bx0 + i * (bw + gap), bar_top, bx0 + i * (bw + gap) + bw, bar_bot };
        COLORREF c = g_th.meter_off;
        if (i < lit) {
            float t = (float)i / (float)METER_SEGS;
            if (t >= 0.86f) c = g_th.err;
            else if (t >= 0.66f) c = g_th.warn;
            else c = g_th.accent;
        }
        ui_fill_round(dc, br, sd(2), c, 255);
    }
    /* 峰值保持线 */
    if (peak_idx > 0 && peak_idx <= METER_SEGS) {
        int px = bx0 + peak_idx * (bw + gap);
        RECT pr = { px - sd(1), bar_top - sd(2), px + bw + sd(1), bar_top + sd(1) };
        ui_fill_round(dc, pr, sd(1), g_th.text, 230);
    }
    /* 条下读数 */
    {
        RECT tr = { bx0, bar_bot + sd(6), bx1 - sd(10), bar_bot + sd(24) };
        swprintf(buf, 64, L"%ls · 峰值 %3.0f%% · 34 段", 
                 (s_state == 2) ? L"过载：离麦远一点或降增益" :
                 (s_state == 1) ? L"说话试一下：让峰值停在目标区" : L"无信号：确认手机端已启动采集",
                 (double)(s_peak * 100.0f));
        ui_text(dc, tr, buf, ui_font(8, FW_NORMAL, 0), g_th.text_mute, DT_LEFT | DT_SINGLELINE);
    }
}
