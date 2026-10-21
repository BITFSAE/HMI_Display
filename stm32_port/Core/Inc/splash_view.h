#ifndef SPLASH_VIEW_H
#define SPLASH_VIEW_H

/* 开屏动画：纯 LVGL，固件与模拟器共用，不含 HAL/RTOS 调用，所有函数只能在 LVGL 线程调用。
 * 时间由调用者传入（固件 HAL_GetTick，模拟器 lv_tick_get），动画是时间的纯函数，
 * 每个 33 ms 步进只对变化的区域调用 invalidate。 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPLASH_BOOT = 0,   /* 上电：整屏黑底开场，结束时斜边扫屏露出仪表 */
    SPLASH_REPLAY      /* 点 Logo 重放：先用斜边扫屏盖住仪表，再按上电的流程播放 */
} splash_mode_t;

/* 在当前活动屏幕最上层建立覆盖层；已在播放时忽略。第一次 Step 才开始计时。 */
void Splash_View_Start(splash_mode_t mode);
/* 推进动画并失效变化区域。返回 1 表示仍在播放，0 表示未播放或刚刚结束（覆盖层已删除）。 */
uint8_t Splash_View_Step(uint32_t now_ms);
/* 直接跳到退场扫屏。进入扫屏之后无效。 */
void Splash_View_Skip(void);
uint8_t Splash_View_IsActive(void);
/* 整段动画的时长（ms），含重放的入场扫屏。 */
uint32_t Splash_View_DurationMs(splash_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif
