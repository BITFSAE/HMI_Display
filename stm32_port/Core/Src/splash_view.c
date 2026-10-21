/* 开屏动画（800x480，约 2.6 s）。
 *
 * 画面：鲨鱼自右向左冲入，字母自左向右依次冲入，方格旗擦出，一道高光扫过，最后一条与字标斜体同角度
 * 的斜边从左向右扫屏，露出仪表。点 Logo 重放时先用同样的斜边把仪表盖住。
 *
 * 性能与内存约束（见 docs/dashboard_ui.md「开屏动画」）：
 *  - 整段动画只有一个 LVGL 对象 + 绘制回调；元素没有各自的对象，也没有 lv_anim。
 *  - 每个元素的位置/颜色/可见性都是时间 t 的纯函数 eval()；重绘回调和失效计算共用它，
 *    所以任何时候重绘同一区域结果一致，也不需要保存每个元素的上一帧状态。
 *  - t 按 33 ms 步进。每步只对发生变化的区域 invalidate：条带单独失效，鲨鱼和字母各合并成一个
 *    区域，斜边扫屏每 4 行一组，一步最多十几个区域，远低于 LVGL 失效缓冲（32）溢出后整屏重绘的门限。
 *  - 全黑保持阶段对象用不透明黑底声明自己完全覆盖，LVGL 不再绘制其下的仪表控件；扫屏阶段改为透明。
 *    切换底色时关掉失效：前后像素相同，不应触发整屏重绘。
 *  - 只用实色：高光和降温用颜色档位，扫屏边缘用三条实色带；位图是 A8 条带，直接从 Flash 做遮罩。
 *  - 时间用 Q10 定点和整数运算，不用浮点，主机回归和目标机的帧完全一致。 */
#include "splash_view.h"
#include "dash_splash_data.h"
#include "dashboard_view.h"

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "../lvgl/lvgl.h"
#endif

#include <stdbool.h>

#define W DASH_SCREEN_W
#define H DASH_SCREEN_H
#define Q 1024

/* 时间表（ms，开屏局部时间） */
#define STEP_MS      33
#define ENTER_MS     300    /* 仅重放：斜边扫屏盖住仪表 */
#define SHARK_DUR    600
#define LETTER_T0    260
#define LETTER_GAP   70     /* 最右的字母先发，B 最后落定，飞行中互不穿插 */
#define LETTER_DUR   520
#define FLAG_T0      900
#define FLAG_DUR     300
#define SHEEN_T0     1400
#define SHEEN_SPAN   420    /* 高光从屏幕左缘扫到右缘的时间 */
#define FLASH_MS     120
#define EXIT_T0      2150
#define EXIT_DUR     450

/* 斜边扫屏：每 16 px 一行的台阶，斜率 23/64 ≈ 0.36 取自字标的斜体角 */
#define ROW_H        16
#define ROWS         (H / ROW_H)
#define SKEW_NUM     23
#define SKEW_DEN     64
#define E_LO         (-100) /* 边缘基准位置：此时所有行的边都在屏外左侧 */
#define E_HI         884    /* 此时所有行的边都在屏外右侧 */
#define BAND_TOTAL   14
#define INV_GROUP    4      /* 扫屏每 4 行（64 px）合并成一个失效区域 */

enum { P_SKY, P_SKY_L, P_SKY_LL, P_SKY_LLL, P_SKY_W, P_SKY_D, P_WHITE };
static const uint32_t pal[] = {0x0EA5E9, 0x38BDF8, 0x7DD3FC, 0xBAE6FD, 0xE0F2FE, 0x0A5F8A, 0xFFFFFF};

/* 边带从亮到暗，宽度 3+5+6 = 14 px */
static const struct { uint8_t color, w; } bands[] = {{P_SKY_LLL, 3}, {P_SKY, 5}, {P_SKY_D, 6}};

/* 背景速度线：上半区向左（与鲨鱼同向），字母所在的下半区向右（与斜体同向）。 */
typedef struct {
    int16_t y;
    int8_t dir;
    uint16_t t0;
    uint16_t v;          /* px/s */
    uint16_t len;
    uint8_t th;
    uint8_t color;
} amb_t;
static const amb_t amb[] = {
    { 30, -1,  30, 2800, 170, 2, P_SKY},   { 54, -1, 110, 3400, 100, 2, P_SKY_D},  { 74, -1, 200, 2500, 220, 3, P_SKY_L},
    { 22, -1, 380, 3000, 140, 2, P_SKY_D}, { 66, -1, 520, 3600,  90, 2, P_SKY},
    {270,  1, 330, 3200, 200, 2, P_SKY_D}, {418,  1, 120, 2600, 180, 2, P_SKY},    {436,  1, 240, 3400, 120, 2, P_SKY_L},
    {452,  1, 400, 2900, 230, 3, P_SKY_D}, {466,  1, 560, 3500, 100, 2, P_SKY},    {410,  1, 700, 3000, 150, 2, P_SKY_D},
};
#define AMB_N ((uint32_t)(sizeof(amb) / sizeof(amb[0])))

/* 拖尾：{相对精灵高度的百分比, 起始长度, 颜色}；长度随速度（1-p)^3 缩短 */
typedef struct { uint8_t frac, color; uint16_t base; } trail_t;
static const trail_t shark_trail[] = {
    {10, P_SKY_D, 360}, {26, P_SKY, 240}, {42, P_SKY_L, 420}, {58, P_SKY_D, 200}, {74, P_SKY, 320}, {90, P_SKY_D, 150},
};
static const trail_t letter_trail[] = {{32, P_SKY, 240}, {74, P_SKY_D, 170}};
#define SHT_N 6U
#define LT_PER 2U

/* 元素编号同时是绘制顺序 */
#define E_AMB    0U
#define E_SHT    (E_AMB + AMB_N)
#define E_LT     (E_SHT + SHT_N)
#define E_SHARK  (E_LT + LT_PER * SPLASH_LETTER_COUNT)
#define E_LETTER (E_SHARK + 1U)
#define E_FLAG   (E_LETTER + SPLASH_LETTER_COUNT)
#define E_COUNT  (E_FLAG + 1U)

typedef struct {
    lv_area_t a;         /* 当前占用的屏幕区域（含 x2/y2） */
    uint32_t color;
} elem_t;

typedef enum { PH_ENTER, PH_HOLD, PH_EXIT } phase_t;

static lv_obj_t * s_obj;
static splash_mode_t s_mode;
static uint32_t s_t0;
static int32_t s_t = -1;       /* 当前步进时刻，-1 = 尚未开始 */
static uint8_t s_started;
static uint8_t s_skip;
static uint8_t s_cover = 0xFFU;   /* 覆盖层底色当前是否不透明；0xFF = 尚未设置 */

/* ------------------------------------------------------------ 数学 */

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int32_t lerp_q(int32_t a, int32_t b, int32_t e)
{
    return a + (b - a) * e / Q;
}

/* 1-(1-p)^n，p 与结果均为 Q10 */
static int32_t ease_out(int32_t p, int n)
{
    int32_t q = Q - p, r = Q;
    while(n-- > 0) r = r * q / Q;
    return Q - r;
}

static int32_t progress(int32_t tt, int32_t t0, int32_t dur)
{
    return clampi((tt - t0) * Q / dur, 0, Q);
}

static int32_t enter_ms(void)
{
    return s_mode == SPLASH_REPLAY ? ENTER_MS : 0;
}

static int32_t total_ms(void)
{
    return enter_ms() + EXIT_T0 + EXIT_DUR;
}

static phase_t phase_of(int32_t t)
{
    if(t < enter_ms()) return PH_ENTER;
    if(t - enter_ms() < EXIT_T0) return PH_HOLD;
    return PH_EXIT;
}

/* ------------------------------------------------------------ 元素时间表 */

static bool streak(elem_t * el, int32_t x1, int32_t x2, int32_t y, int32_t th, uint8_t color)
{
    x1 = clampi(x1, 0, W);
    x2 = clampi(x2, 0, W);
    if(x2 - x1 < 2) return false;
    el->a.x1 = x1;
    el->a.x2 = x2 - 1;
    el->a.y1 = y;
    el->a.y2 = y + th - 1;
    el->color = pal[color];
    return true;
}

/* 高光自左向右扫过：2 = 最亮档，1 = 次亮档，0 = 无 */
static int sheen(int32_t tt, int32_t cx)
{
    int32_t d = tt - (SHEEN_T0 + cx * SHEEN_SPAN / W);

    if((d < 0) || (d >= FLASH_MS)) return 0;
    return d < FLASH_MS / 2 ? 2 : 1;
}

static int32_t shark_x(int32_t tt)
{
    return lerp_q(W + 10, dash_splash_sprites[SPLASH_SPR_SHARK].x, ease_out(progress(tt, 0, SHARK_DUR), 4));
}

static int32_t letter_t0(uint32_t i)
{
    return LETTER_T0 + (int32_t)(SPLASH_LETTER_COUNT - 1U - i) * LETTER_GAP;
}

static int32_t letter_x(uint32_t i, int32_t tt)
{
    const splash_sprite_t * s = &dash_splash_sprites[SPLASH_SPR_LETTER0 + i];

    return lerp_q(-(int32_t)s->w - 12, s->x,
                  ease_out(progress(tt, letter_t0(i), LETTER_DUR), 4));
}

static void sprite_box(elem_t * el, const splash_sprite_t * s, int32_t x, int32_t w)
{
    el->a.x1 = x;
    el->a.x2 = x + w - 1;
    el->a.y1 = s->y;
    el->a.y2 = s->y + (int32_t)s->h - 1;
}

/* 返回元素在局部时间 tt 是否可见，并给出区域与颜色。tt < 0（入场扫屏期间）一律不可见。 */
static bool eval(uint32_t idx, int32_t tt, elem_t * el)
{
    if(tt < 0) return false;

    if(idx < E_SHT) {
        const amb_t * s = &amb[idx - E_AMB];
        int32_t d = tt - s->t0, dist, x;

        if(d < 0) return false;
        dist = (int32_t)s->v * d / 1000;
        x = s->dir < 0 ? W - dist : -(int32_t)s->len + dist;
        return streak(el, x, x + s->len, s->y, s->th, s->color);
    }
    if(idx < E_LT) {            /* 鲨鱼拖尾：锚在鲨鱼身后，随减速缩短 */
        const trail_t * t = &shark_trail[idx - E_SHT];
        const splash_sprite_t * s = &dash_splash_sprites[SPLASH_SPR_SHARK];
        int32_t q = Q - progress(tt, 0, SHARK_DUR), len, x;

        if(q <= 0) return false;
        len = (int32_t)t->base * q / Q * q / Q * q / Q;
        if(len < 6) return false;
        x = shark_x(tt) + (int32_t)s->w * 55 / 100;
        return streak(el, x, x + len, s->y + (int32_t)s->h * t->frac / 100, 3, t->color);
    }
    if(idx < E_SHARK) {         /* 字母拖尾：锚在字母左侧 30% 处，向左延伸 */
        uint32_t i = (idx - E_LT) / LT_PER;
        const trail_t * t = &letter_trail[(idx - E_LT) % LT_PER];
        const splash_sprite_t * s = &dash_splash_sprites[SPLASH_SPR_LETTER0 + i];
        int32_t q = Q - progress(tt, letter_t0(i), LETTER_DUR), len, x;

        if((tt < letter_t0(i)) || (q <= 0)) return false;
        len = (int32_t)t->base * q / Q * q / Q * q / Q;
        if(len < 6) return false;
        x = letter_x(i, tt) + (int32_t)s->w * 3 / 10;
        return streak(el, x - len, x, s->y + (int32_t)s->h * t->frac / 100, 3, t->color);
    }
    if(idx == E_SHARK) {        /* 落定后由亮白降温到品牌蓝，高光扫过时再提亮 */
        const splash_sprite_t * s = &dash_splash_sprites[SPLASH_SPR_SHARK];
        int32_t x = shark_x(tt), lt = tt - SHARK_DUR;
        uint8_t c = lt >= 140 ? P_SKY : lt >= 70 ? P_SKY_L : lt >= 0 ? P_SKY_LLL : P_SKY_W;
        int sh = sheen(tt, s->x + (int32_t)s->w / 2);

        if(sh != 0) c = sh == 2 ? P_SKY_W : P_SKY_LL;
        sprite_box(el, s, x, s->w);
        el->color = pal[c];
        return true;
    }
    if(idx < E_FLAG) {
        uint32_t i = idx - E_LETTER;
        const splash_sprite_t * s = &dash_splash_sprites[SPLASH_SPR_LETTER0 + i];
        int32_t p = progress(tt, letter_t0(i), LETTER_DUR);
        uint8_t c = p < Q ? P_SKY_LLL : P_WHITE;
        int sh = sheen(tt, s->x + (int32_t)s->w / 2);

        if(tt < letter_t0(i)) return false;
        if(sh != 0) c = sh == 2 ? P_SKY_LL : P_SKY_LLL;
        sprite_box(el, s, letter_x(i, tt), s->w);
        el->color = pal[c];
        return true;
    }
    {                           /* 方格旗：自左向右擦出 */
        const splash_sprite_t * s = &dash_splash_sprites[SPLASH_SPR_FLAG];
        int32_t cw;
        uint8_t c = P_SKY;
        int sh = sheen(tt, s->x + (int32_t)s->w / 2);

        if(tt < FLAG_T0) return false;
        cw = (int32_t)s->w * ease_out(progress(tt, FLAG_T0, FLAG_DUR), 3) / Q;
        if(cw <= 0) return false;
        if(sh != 0) c = sh == 2 ? P_SKY_W : P_SKY_LL;
        sprite_box(el, s, s->x, cw);
        el->color = pal[c];
        return true;
    }
}

/* ------------------------------------------------------------ 扫屏边缘 */

/* 扫屏每一步要重绘的面积正比于边缘速度，所以入场匀速、退场只做轻微加速，避免首尾几步面积激增。 */
static int32_t edge_enter(int32_t t)
{
    return lerp_q(E_LO, E_HI, clampi(t * Q / ENTER_MS, 0, Q));
}

static int32_t edge_exit(int32_t tt)
{
    int32_t p = progress(tt, EXIT_T0, EXIT_DUR);

    return lerp_q(E_LO, E_HI, (p + p * p / Q) / 2);
}

static int32_t edge_x(int32_t e0, uint32_t row)
{
    return e0 + (H / 2 - (int32_t)(row * ROW_H + ROW_H / 2)) * SKEW_NUM / SKEW_DEN;
}

/* ------------------------------------------------------------ 绘制 */

static bool on_clip(const lv_layer_t * layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    const lv_area_t * c = &layer->_clip_area;

    return (x1 <= c->x2) && (x2 >= c->x1) && (y1 <= c->y2) && (y2 >= c->y1);
}

static void fill(lv_layer_t * layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color)
{
    lv_draw_fill_dsc_t dsc;
    lv_area_t a = {x1, y1, x2, y2};

    if((x2 < x1) || (y2 < y1) || !on_clip(layer, x1, y1, x2, y2)) return;
    lv_draw_fill_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.opa = LV_OPA_COVER;
    lv_draw_fill(layer, &dsc, &a);
}

/* 条带在 Flash 里是带常量描述符的 A8，LVGL 以 recolor 色做遮罩直接混合。 */
static void draw_sprite(lv_layer_t * layer, const splash_sprite_t * s, const elem_t * el)
{
    lv_draw_image_dsc_t dsc;
    lv_area_t saved = layer->_clip_area;
    int32_t x = el->a.x1;
    uint32_t i;

    if(el->a.x2 - el->a.x1 + 1 < (int32_t)s->w) {      /* 擦出中的方格旗：裁掉右侧未到的部分 */
        if(el->a.x2 < layer->_clip_area.x2) layer->_clip_area.x2 = el->a.x2;
        if(layer->_clip_area.x2 < layer->_clip_area.x1) {
            layer->_clip_area = saved;
            return;
        }
    }
    for(i = 0U; i < s->count; i++) {
        const splash_strip_t * st = &s->strips[i];
        lv_area_t a;

        a.x1 = x + st->x;
        a.y1 = s->y + st->y;
        a.x2 = a.x1 + (int32_t)st->img.header.w - 1;
        a.y2 = a.y1 + (int32_t)st->img.header.h - 1;
        if(!on_clip(layer, a.x1, a.y1, a.x2, a.y2)) continue;
        lv_draw_image_dsc_init(&dsc);
        dsc.src = &st->img;
        dsc.recolor = lv_color_hex(el->color);
        dsc.recolor_opa = LV_OPA_COVER;
        lv_draw_image(layer, &dsc, &a);
    }
    layer->_clip_area = saved;
}

static void draw_content(lv_layer_t * layer, int32_t tt)
{
    uint32_t idx;
    elem_t el;

    for(idx = 0U; idx < E_COUNT; idx++) {
        if(!eval(idx, tt, &el)) continue;
        if(idx < E_SHARK) fill(layer, el.a.x1, el.a.y1, el.a.x2, el.a.y2, el.color);
        else if(idx == E_SHARK) draw_sprite(layer, &dash_splash_sprites[SPLASH_SPR_SHARK], &el);
        else if(idx < E_FLAG) draw_sprite(layer, &dash_splash_sprites[SPLASH_SPR_LETTER0 + idx - E_LETTER], &el);
        else draw_sprite(layer, &dash_splash_sprites[SPLASH_SPR_FLAG], &el);
    }
}

static void draw_band(lv_layer_t * layer, int32_t x, int32_t y1, int32_t y2, bool reversed)
{
    uint32_t k;

    for(k = 0U; k < sizeof(bands) / sizeof(bands[0]); k++) {
        uint32_t n = reversed ? (uint32_t)(sizeof(bands) / sizeof(bands[0])) - 1U - k : k;
        int32_t x1 = clampi(x, 0, W), x2 = clampi(x + bands[n].w, 0, W);

        fill(layer, x1, y1, x2 - 1, y2, pal[bands[n].color]);
        x += bands[n].w;
    }
}

/* 入场：黑色在边左侧、边带在右；退场：边带在左、黑色（和开屏内容）在右。 */
static void draw_scan(lv_layer_t * layer, int32_t t)
{
    bool enter = t < enter_ms();
    int32_t tt = t - enter_ms();
    int32_t e0 = enter ? edge_enter(t) : edge_exit(tt);
    uint32_t row;

    for(row = 0U; row < ROWS; row++) {
        int32_t y1 = (int32_t)(row * ROW_H), y2 = y1 + ROW_H - 1;
        int32_t ex = edge_x(e0, row);

        if(!on_clip(layer, 0, y1, W - 1, y2)) continue;
        if(enter) {
            fill(layer, 0, y1, clampi(ex, 0, W) - 1, y2, 0x000000);
            draw_band(layer, ex, y1, y2, true);
        }
        else {
            int32_t bx = clampi(ex + BAND_TOTAL, 0, W);

            if(bx < W) {
                lv_area_t saved = layer->_clip_area;

                /* 开屏内容只显示在黑色一侧，按行裁剪 */
                if(layer->_clip_area.x1 < bx) layer->_clip_area.x1 = bx;
                if(layer->_clip_area.x1 <= layer->_clip_area.x2) {
                    fill(layer, bx, y1, W - 1, y2, 0x000000);
                    if(layer->_clip_area.y1 < y1) layer->_clip_area.y1 = y1;
                    if(layer->_clip_area.y2 > y2) layer->_clip_area.y2 = y2;
                    if(layer->_clip_area.y1 <= layer->_clip_area.y2) draw_content(layer, tt);
                }
                layer->_clip_area = saved;
            }
            draw_band(layer, ex, y1, y2, false);
        }
    }
}

/* 在 DRAW_POST 里画：LVGL 自己的底色（保持阶段的黑）在 DRAW_MAIN 之后才画，内容必须在它上面。 */
static void draw_cb(lv_event_t * e)
{
    lv_layer_t * layer = lv_event_get_layer(e);
    int32_t t = s_t < 0 ? 0 : s_t;

    if(phase_of(t) == PH_HOLD) draw_content(layer, t - enter_ms());   /* 黑底由对象底色负责 */
    else draw_scan(layer, t);
}

/* 只有全黑保持阶段才整屏覆盖；扫屏时要露出仪表，必须让 LVGL 画下面的控件。
 * LVGL 的覆盖判定只认不透明底色（用户回调只能把结果改成"不覆盖"，不能改成"覆盖"）。 */
static void set_cover(bool on)
{
    lv_display_t * disp;

    if(s_cover == (uint8_t)on) return;
    s_cover = on ? 1U : 0U;
    disp = lv_obj_get_display(s_obj);
    lv_display_enable_invalidation(disp, false);
    lv_obj_set_style_bg_opa(s_obj, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_display_enable_invalidation(disp, true);
}

/* ------------------------------------------------------------ 失效 */

static void inv_area(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_area_t a = {clampi(x1, 0, W - 1), clampi(y1, 0, H - 1), clampi(x2, 0, W - 1), clampi(y2, 0, H - 1)};

    if((x2 < 0) || (y2 < 0) || (x1 > W - 1) || (y1 > H - 1) || (x2 < x1) || (y2 < y1)) return;
    lv_obj_invalidate_area(s_obj, &a);
}

typedef struct {
    bool have;
    lv_area_t a;
} lane_t;

static void lane_add(lane_t * lane, const lv_area_t * a)
{
    if(!lane->have) {
        lane->a = *a;
        lane->have = true;
        return;
    }
    if(a->x1 < lane->a.x1) lane->a.x1 = a->x1;
    if(a->y1 < lane->a.y1) lane->a.y1 = a->y1;
    if(a->x2 > lane->a.x2) lane->a.x2 = a->x2;
    if(a->y2 > lane->a.y2) lane->a.y2 = a->y2;
}

static void invalidate_elements(int32_t t_prev, int32_t t_now)
{
    lane_t shark = {0}, word = {0};
    uint32_t idx;

    for(idx = 0U; idx < E_COUNT; idx++) {
        elem_t p, n;
        bool vp = t_prev >= 0 && eval(idx, t_prev - enter_ms(), &p);
        bool vn = eval(idx, t_now - enter_ms(), &n);
        lv_area_t u;

        if(!vp && !vn) continue;
        if(vp && vn && (p.color == n.color) && (p.a.x1 == n.a.x1) && (p.a.x2 == n.a.x2) &&
           (p.a.y1 == n.a.y1) && (p.a.y2 == n.a.y2)) continue;
        if(vp && vn) {
            u.x1 = p.a.x1 < n.a.x1 ? p.a.x1 : n.a.x1;
            u.y1 = p.a.y1 < n.a.y1 ? p.a.y1 : n.a.y1;
            u.x2 = p.a.x2 > n.a.x2 ? p.a.x2 : n.a.x2;
            u.y2 = p.a.y2 > n.a.y2 ? p.a.y2 : n.a.y2;
        }
        else {
            u = vn ? n.a : p.a;
        }
        if((idx < E_SHT) || (idx == E_FLAG)) inv_area(u.x1, u.y1, u.x2, u.y2);       /* 速度线、方格旗各自一块 */
        else if(((idx >= E_SHT) && (idx < E_LT)) || (idx == E_SHARK)) lane_add(&shark, &u);  /* 鲨鱼与它的拖尾 */
        else lane_add(&word, &u);                                                    /* 字母与它们的拖尾 */
    }
    if(shark.have) inv_area(shark.a.x1, shark.a.y1, shark.a.x2, shark.a.y2);
    if(word.have) inv_area(word.a.x1, word.a.y1, word.a.x2, word.a.y2);
}

/* 某一行在时刻 t 被扫屏画成黑色的区间 B 和三色边带占的区间 A，均为半开区间，空 = lo >= hi。 */
typedef struct { int32_t b0, b1, a0, a1; } row_state_t;

static row_state_t row_state(int32_t t, uint32_t row)
{
    row_state_t r = {0, 0, 0, 0};
    phase_t ph = phase_of(t);

    if(ph == PH_ENTER) {
        int32_t ex = edge_x(edge_enter(t), row);

        r.b1 = clampi(ex, 0, W);
        r.a0 = r.b1;
        r.a1 = clampi(ex + BAND_TOTAL, 0, W);
    }
    else if(ph == PH_HOLD) {
        r.b1 = W;
    }
    else {
        int32_t ex = edge_x(edge_exit(t - enter_ms()), row);

        r.a0 = clampi(ex, 0, W);
        r.a1 = clampi(ex + BAND_TOTAL, 0, W);
        r.b0 = r.a1;
        r.b1 = W;
    }
    return r;
}

static void hull_add(int32_t * lo, int32_t * hi, int32_t a, int32_t b)
{
    if(a >= b) return;
    if(a < *lo) *lo = a;
    if(b > *hi) *hi = b;
}

/* 两个半开区间的对称差，取包络（保守）。 */
static void hull_xor(int32_t * lo, int32_t * hi, int32_t p0, int32_t p1, int32_t q0, int32_t q1)
{
    bool pe = p0 >= p1, qe = q0 >= q1;

    if(pe && qe) return;
    if(pe || qe) {
        hull_add(lo, hi, pe ? q0 : p0, pe ? q1 : p1);
        return;
    }
    hull_add(lo, hi, p0 < q0 ? p0 : q0, p0 < q0 ? q0 : p0);
    hull_add(lo, hi, p1 < q1 ? p1 : q1, p1 < q1 ? q1 : p1);
}

static void invalidate_scan(int32_t t_prev, int32_t t_now)
{
    uint32_t row, group;

    for(group = 0U; group < ROWS; group += INV_GROUP) {
        int32_t lo = W, hi = 0;

        for(row = group; row < group + INV_GROUP; row++) {
            row_state_t n = row_state(t_now, row), p;

            /* 第一步之前：上电时 Start 已整屏失效，视为与现在相同；重放时屏上还是原来的仪表，没有任何扫屏像素。 */
            if(t_prev >= 0) p = row_state(t_prev, row);
            else if(s_mode == SPLASH_BOOT) p = n;
            else {
                p.b0 = p.b1 = p.a0 = p.a1 = 0;
            }
            hull_xor(&lo, &hi, p.b0, p.b1, n.b0, n.b1);
            /* 边带内部的三种颜色随位置整体平移，位置变了整条带都要重画 */
            if((p.a0 != n.a0) || (p.a1 != n.a1)) {
                hull_add(&lo, &hi, p.a0, p.a1);
                hull_add(&lo, &hi, n.a0, n.a1);
            }
        }
        if(hi > lo) inv_area(lo, (int32_t)(group * ROW_H), hi - 1, (int32_t)((group + INV_GROUP) * ROW_H - 1));
    }
}

/* ------------------------------------------------------------ 对外接口 */

static void finish(void)
{
    lv_display_t * disp = lv_obj_get_display(s_obj);

    /* 删除对象会失效它的整个区域（整屏）。此时最后一步的扫屏区域已经失效，其余部分早已是仪表，
     * 关掉失效避免一次整屏重绘。 */
    lv_display_enable_invalidation(disp, false);
    lv_obj_delete(s_obj);
    lv_display_enable_invalidation(disp, true);
    s_obj = NULL;
    s_t = -1;
    s_cover = 0xFFU;
}

void Splash_View_Start(splash_mode_t mode)
{
    if(s_obj != NULL) return;
    s_mode = mode;
    s_t = -1;
    s_started = 0U;
    s_skip = 0U;
    s_obj = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_obj);
    lv_obj_remove_flag(s_obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_obj, 0, 0);
    lv_obj_set_size(s_obj, W, H);
    lv_obj_set_style_bg_color(s_obj, lv_color_black(), 0);
    lv_obj_add_event_cb(s_obj, draw_cb, LV_EVENT_DRAW_POST, NULL);
    s_cover = 0xFFU;
    set_cover(phase_of(0) == PH_HOLD);
    if(mode == SPLASH_BOOT) lv_obj_invalidate(s_obj);   /* 上电：首帧就是整屏黑 */
}

uint8_t Splash_View_Step(uint32_t now_ms)
{
    int32_t elapsed, t, exit_start, prev;

    if(s_obj == NULL) return 0U;
    if(s_started == 0U) {
        s_started = 1U;
        s_t0 = now_ms;
    }
    elapsed = (int32_t)(now_ms - s_t0);
    exit_start = enter_ms() + EXIT_T0;
    if(s_skip != 0U) {
        s_skip = 0U;
        if(elapsed < exit_start) {
            s_t0 -= (uint32_t)(exit_start - elapsed);
            elapsed = exit_start;
        }
    }
    t = elapsed >= total_ms() ? total_ms() : elapsed - elapsed % STEP_MS;
    if(t == s_t) return 1U;

    prev = s_t;
    s_t = t;
    set_cover(phase_of(t) == PH_HOLD);
    invalidate_elements(prev, t);
    invalidate_scan(prev, t);
    if(t >= total_ms()) {
        finish();
        return 0U;
    }
    return 1U;
}

void Splash_View_Skip(void)
{
    if(s_obj != NULL) s_skip = 1U;
}

uint8_t Splash_View_IsActive(void)
{
    return s_obj != NULL ? 1U : 0U;
}

uint32_t Splash_View_DurationMs(splash_mode_t mode)
{
    return (uint32_t)((mode == SPLASH_REPLAY ? ENTER_MS : 0) + EXIT_T0 + EXIT_DUR);
}
