#ifndef SPEED_TRACE_H
#define SPEED_TRACE_H

/* 车速从解析到屏幕可见的耗时追踪，纯计数逻辑，不含 HAL/RTOS。
 *   提交 → 应用：GPS 任务提交到 Dashboard 任务把新数值交给 LVGL
 *   应用 → 刷屏完成：LVGL 渲染并把最后一块帧缓冲写完（SSD1963 的 GRAM 写入完成）
 * 只统计整数 km/h 真正变化的那些提交。刷屏完成不等于屏幕上人眼可见：IPS 面板响应和
 * SSD1963 扫描的延迟在 MCU 侧测不到。 */

#include <stdint.h>

typedef struct {
    uint32_t count;           /* 已应用的变化数 */
    uint32_t sum_ms;
    uint32_t max_ms;
    uint32_t last_ms;
} speed_trace_stage_t;

typedef struct {
    speed_trace_stage_t submit_to_apply;
    speed_trace_stage_t apply_to_frame;
    uint32_t superseded;      /* 提交后还没应用就被新值覆盖的次数：界面慢于 GNSS 输出 */
} speed_trace_t;

void SpeedTrace_Reset(void);
void SpeedTrace_Superseded(void);
/* 界面把一个变化了的车速交给 LVGL。submit_tick 是该值被提交的时刻。 */
void SpeedTrace_Applied(uint32_t submit_tick, uint32_t now);
/* LVGL 一帧的最后一块刷屏完成（任意帧都可调用，只有紧跟在 Applied 之后的第一帧被计入）。 */
void SpeedTrace_FrameDone(uint32_t now);
void SpeedTrace_Get(speed_trace_t * out);

#endif
