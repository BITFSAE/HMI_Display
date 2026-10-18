#include "speed_trace.h"

/* 提交在 GPS 任务，应用与刷屏在 Dashboard 任务，读取在 CLI 任务。统计字段只由 Dashboard
 * 任务写（superseded 除外，由提交方写），读取方允许看到相邻两项不同步，只用于诊断。 */
static volatile speed_trace_t g_trace;
static volatile uint32_t g_apply_tick;
static volatile uint8_t g_frame_pending;

static void stage_add(volatile speed_trace_stage_t * stage, uint32_t ms)
{
    stage->count++;
    stage->sum_ms += ms;
    stage->last_ms = ms;
    if(ms > stage->max_ms) stage->max_ms = ms;
}

void SpeedTrace_Reset(void)
{
    g_trace.submit_to_apply.count = 0U;
    g_trace.submit_to_apply.sum_ms = 0U;
    g_trace.submit_to_apply.max_ms = 0U;
    g_trace.submit_to_apply.last_ms = 0U;
    g_trace.apply_to_frame.count = 0U;
    g_trace.apply_to_frame.sum_ms = 0U;
    g_trace.apply_to_frame.max_ms = 0U;
    g_trace.apply_to_frame.last_ms = 0U;
    g_trace.superseded = 0U;
}

void SpeedTrace_Superseded(void)
{
    g_trace.superseded++;
}

void SpeedTrace_Applied(uint32_t submit_tick, uint32_t now)
{
    stage_add(&g_trace.submit_to_apply, now - submit_tick);
    g_apply_tick = now;
    g_frame_pending = 1U;
}

void SpeedTrace_FrameDone(uint32_t now)
{
    if(g_frame_pending == 0U) return;
    g_frame_pending = 0U;
    stage_add(&g_trace.apply_to_frame, now - g_apply_tick);
}

void SpeedTrace_Get(speed_trace_t * out)
{
    *out = *(const speed_trace_t *)&g_trace;
}
