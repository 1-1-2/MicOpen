/* ui_layout.h — 两列布局与命中测试（定稿图 win-final.png 几何）
 *
 * 布局（1060×680 验收尺寸，全部逻辑像素、过 sd() 缩放）：
 *   标题栏 48 · 内容区（外边距 20）· 状态栏 28
 *   左列（clamp(w/3, 320, 400)，1060 时 353）：连接卡 338 + 驱动卡 177
 *   右列（可滚动）：电平卡 374（常驻）+ 音频参数/会话统计/事件（D-11 默认收起 48）
 * 窗口矮于左列总高时（如 900×560）进入 tight 垂直压缩，保证不重叠不裁切。
 * 左列用绝对坐标；右列经 cs() 减 g_ui.scroll。绘制与命中测试共用同一套矩形。
 */
#ifndef MICLINK_UI_LAYOUT_H
#define MICLINK_UI_LAYOUT_H

#include "app.h"

typedef struct {
    RECT title;
    RECT btn_min, btn_max, btn_close;
    RECT seg_theme[2];              /* 0 浅色 / 1 深色（D-01） */

    RECT card_conn;                 /* 左列：连接卡 */
    RECT step[3];                   /* 三步进度圆环（D-02） */
    RECT seg_trans[4];              /* 传输方式四段（D-03） */
    RECT ed_host, ed_port, ed_media;
    RECT btn_conn;                  /* 主按钮（D-04 呼吸） */

    RECT card_drv;                  /* 左列：驱动卡 */
    RECT pill_dev, pill_fmt;        /* 只读信息 pill（D-09） */
    RECT btn_install, btn_openset;  /* 安装驱动（未就绪）/ 打开声音设置（就绪） */
    RECT btn_uninstall, btn_help;   /* 标题行小文字按钮（宽度按文字实测自适应） */

    RECT card_meter;                /* 右列：电平卡（常驻） */
    RECT ring;                      /* 环形电平 180×180（D-05） */
    RECT ring_legend;               /* 环形下图例 */
    RECT hist;                      /* 最近 10 秒历史图区（D-08） */
    RECT slider;                    /* 本地增益滑杆（D-07） */
    RECT sw_mute;                   /* 静音开关（D-06） */
    RECT sw_mon;                    /* 监听开关（R-39） */
    RECT sw_gainext;                /* 扩展增益范围开关（R-47） */
    RECT sw_autorec;                /* 自动重连开关（R-27） */
    int  bar_x0, bar_x1, bar_y;     /* 横条电平（60 段，D-06/C-12） */

    RECT card_audio, hdr_audio, seg_rate[3], seg_buf[3];
    RECT card_stats, hdr_stats;
    RECT card_log, hdr_log, log_toolbar, log_body, log_track;
    RECT btn_copylog, btn_clearlog, btn_pauselog;

    int  content_h;      /* 右列内容总高（不含滚动） */
    int  view_h;         /* 可视区高度（窗口高 - 标题栏 - 状态栏） */
    int  tight;          /* 900×560 垂直压缩（左列不裁切） */
    int  left_h;         /* 左列内容总高 */
} UiLayout;

extern UiLayout g_lo;

/* 标题栏高度 / 状态栏高度，绘制与命中测试共用 */
int ui_title_h(void);
int ui_status_h(void);
void ui_layout_compute(void);
int  ui_hit_test(int x, int y);
const wchar_t *ui_step_label(int i);   /* 三步进度标签（D-02） */

/* 驱动卡标题行两个小文字按钮的文案。宽度由布局按实测字宽决定，
 * 窄窗（≤960，如 900×560）放不下全称时自动退化为短标签，
 * 保证「不裁切、不与标题/状态徽标重叠」。绘制方必须用这两个取值，
 * 不能各写一份字面量，否则文案与矩形宽度会漂移。 */
const wchar_t *ui_lbl_uninstall(void);  /* 「卸载驱动」/ 窄窗「卸载」 */
const wchar_t *ui_lbl_help(void);       /* 「命令行说明书」/ 窄窗「说明」 */
const wchar_t *ui_lbl_gainext(void);    /* R-47：「扩展增益」/ 窄窗「扩展」 */

#endif /* MICLINK_UI_LAYOUT_H */
