/* ui_voicemeter.h — 输入电平卡（D-05/D-06/D-07/D-08）
 *
 * 一张卡内四要素（定稿图 win-final.png 实测几何）：
 *   左柱：环形电平（270° 轨道 + 数值弧 + 弧头圆点 + 峰值线 + 目标刻度）+ 图例
 *   右柱：hint / 60 段横条 / 最近 10 秒历史 / 本地增益（dB 刻度滑杆）/ 静音行
 */
#ifndef MICLINK_UI_VOICEMETER_H
#define MICLINK_UI_VOICEMETER_H

#include "app.h"

/* 0 = 无信号，1 = 正常，2 = 过载（主循环据此决定是否高频重绘） */
int  ui_meter_state(void);

/* 每帧调用：release 平滑（attack 在引擎侧，系数 0.35）+ 峰值保持 1.2 s */
void ui_meter_update(const EngineStats *st, int running);

/* 当前电平 dBFS（-96 = 静音）；历史采样用（D-08 柱高按 dB 映射） */
float ui_meter_cur_db(void);

/* 整卡绘制（r = 电平卡矩形） */
void ui_meter_draw(HDC dc, RECT r);

#endif /* MICLINK_UI_VOICEMETER_H */
