#ifndef DASH_SPLASH_DATA_H
#define DASH_SPLASH_DATA_H

/* 开屏动画的位图数据类型。数据本身由 tools/gen_dash_splash.sh 生成到 dash_splash_data.c。
 * 每个精灵由若干 A8 条带组成: 条带只存有效列范围, 由 LVGL 直接从 Flash 做遮罩绘制, 不占 RAM。
 * 每个条带带自己的常量 lv_image_dsc_t, 绘制任务不依赖栈上临时对象, 与 LV_USE_OS 无关。 */

#include <stdint.h>

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "../lvgl/lvgl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 精灵序号, 与生成脚本的输出顺序一致 */
enum {
    SPLASH_SPR_SHARK = 0,
    SPLASH_SPR_LETTER0,                          /* B I T F S A E 共 7 个, 依次递增 */
    SPLASH_SPR_FLAG = SPLASH_SPR_LETTER0 + 7,
    SPLASH_SPRITE_COUNT
};
#define SPLASH_LETTER_COUNT 7

typedef struct {
    uint16_t x, y;            /* 条带左上角, 相对精灵左上角 */
    lv_image_dsc_t img;       /* A8, 行距 = 宽 */
} splash_strip_t;

typedef struct {
    const splash_strip_t * strips;
    uint16_t count;
    int16_t x, y;             /* 精灵静止时在屏幕上的左上角 */
    uint16_t w, h;
} splash_sprite_t;

extern const splash_sprite_t dash_splash_sprites[SPLASH_SPRITE_COUNT];

#ifdef __cplusplus
}
#endif

#endif
