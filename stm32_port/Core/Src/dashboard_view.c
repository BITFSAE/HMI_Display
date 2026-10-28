/* USER CODE BEGIN Dashboard_View */
/* Dashboard view for the 800x480 SSD1963 panel (RGB565, partial FSMC flush).
 *
 * Layout (px):
 *   0..52    top bar: logo image | BEST | LAST | delta | LAP | LEFT | ODO | GPS
 *   52..376  left 0..200: mode tile, slip, tyre map, night button
 *            centre 200..600: lap time, alert banner, speed, pedal bars
 *            right 600..800: SOC, HV, power, max temp, LV bus, fans
 *   376..480 four motor columns FL | FR | RL | RR
 *
 * Performance rules: black background with solid fills only (no gradients,
 * shadows or opacity), shared styles instead of per-object local styles,
 * fixed-size labels so text changes invalidate a known small area, and bars
 * drawn by draw callbacks (one object each) rather than stacks of objects. */
#include "dashboard_view.h"

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "../lvgl/lvgl.h"
#endif

#include <string.h>

LV_FONT_DECLARE(dash_font_speed)  /* Bold 180, digits */
LV_FONT_DECLARE(dash_font_xl)     /* SemiBold 56, digits % - A C D E F N X */
LV_FONT_DECLARE(dash_font_l)      /* SemiBold 36, digits : . + - */
LV_FONT_DECLARE(dash_font_m)      /* SemiBold 22, ASCII */
LV_FONT_DECLARE(dash_font_s)      /* Medium 18, ASCII + eye/bolt/warning */
LV_FONT_DECLARE(dash_font_xs)     /* SemiBold 14, ASCII */
extern const lv_image_dsc_t dash_logo; /* 140x29 A8, tools/gen_dash_logo.sh */

#define DEG "\xC2\xB0"

/* Palette */
#define C_BG      0x000000
#define C_PANEL   0x14181E
#define C_LINE    0x2A3038
#define C_TRACK   0x262B33
#define C_TEXT    0xFFFFFF
#define C_DIM     0x8C96A3
#define C_RED     0xFF453A
#define C_GREEN   0x30D158
#define C_AMBER   0xFFA620
#define C_YELLOW  0xFFD60A
#define C_BLUE    0x0A84FF
#define C_ALERT   0xE0262B

/* Motor/inverter/max temperature text colour thresholds, degC. */
#define TEMP_WARN_C 70
#define TEMP_HOT_C  85
#define SOC_LOW_PCT  20
#define SOC_MID_PCT  50
#define DELTA_FULL_SCALE 500   /* 5.00 s fills half the delta bar */

/* Geometry */
#define TOP_H        52
#define MID_Y        TOP_H
#define BOTTOM_Y     376
#define LEFT_W       200
#define RIGHT_X      600

/* 顶栏左端到分隔线（x=164）为 Logo 区：点它重放开屏动画，不触发计时。 */
#define LOGO_ZONE_W  164

#define ALERT_X      214
#define ALERT_Y      110
#define ALERT_W      372
#define ALERT_H      38

/* 触摸范围比 84x84 色块外扩 8 px，便于戴手套或行驶中点击；
 * 仍与顶栏、SLIP 标签保持距离。 */
#define MODE_X       14
#define MODE_Y       64
#define MODE_W       84
#define MODE_H       84
#define MODE_TOUCH_PAD 8

#define PEDAL_W      32
#define PEDAL_H      162
#define PEDAL_Y      156
#define REGEN_X      209
#define MECH_X       243
#define BRAKE_PEDAL_W 26
#define THROTTLE_X   542

#define TIRE_W       40
#define TIRE_H       62
#define TIRE_LX      50
#define TIRE_RX      110
#define TIRE_FY      182
#define TIRE_RY      258

#define NIGHT_X      80
#define NIGHT_Y      330
#define NIGHT_W      40
#define NIGHT_H      36

/* SOP limits sit under the speed, to the right of the two brake bars and
 * left of the throttle readout. */
#define SOP_X        318
#define SOP_Y        332
#define SOP_ROW_H    22
#define SOP_CAP_W    42
#define SOP_POWER_X  (SOP_X + SOP_CAP_W)
#define SOP_POWER_W  70
#define SOP_CURRENT_X (SOP_POWER_X + SOP_POWER_W + 4)
#define SOP_CURRENT_W 60

#define RIGHT_ROW_H  22   /* right-column row pitch; 24 would not fit the PK row */
#define RIGHT_COL_X  614
#define RIGHT_VAL_X  656
#define RIGHT_END_X  786

typedef struct {
    int32_t value;
    int32_t max;
    uint32_t color;
    uint8_t vertical;
    uint8_t ticks;
} dash_bar_t;

typedef struct {
    lv_obj_t * name;
    lv_obj_t * bolt;
    lv_obj_t * diag;
    lv_obj_t * torque;
    lv_obj_t * rpm;
    lv_obj_t * power;
    lv_obj_t * peak;
    lv_obj_t * motor_temp;
    lv_obj_t * inverter_temp;
} dash_motor_ui_t;

static struct {
    lv_obj_t * best;
    lv_obj_t * last;
    lv_obj_t * delta;
    lv_obj_t * delta_bar;
    lv_obj_t * lap_caption;
    lv_obj_t * lap_count;
    lv_obj_t * left_caption;
    lv_obj_t * left_value;
    lv_obj_t * odometer;
    lv_obj_t * signal;
    lv_obj_t * mode_tile;
    lv_obj_t * mode_letter;
    lv_obj_t * mode_name;
    lv_obj_t * slip_value;
    lv_obj_t * slip_meter;
    lv_obj_t * tires[4];
    lv_obj_t * tire_labels[4];
    lv_obj_t * night_button;
    lv_obj_t * night_icon;
    lv_obj_t * time_caption;
    lv_obj_t * time_value;
    lv_obj_t * timing_dot;
    lv_obj_t * alert_box;
    lv_obj_t * alert_label;
    lv_obj_t * speed;
    lv_obj_t * brake_bar;
    lv_obj_t * brake_pct;
    lv_obj_t * regen_bar;
    lv_obj_t * regen_pct;
    lv_obj_t * throttle_bar;
    lv_obj_t * throttle_pct;
    lv_obj_t * soc;
    lv_obj_t * soc_bar;
    lv_obj_t * hv_voltage;
    lv_obj_t * hv_current;
    lv_obj_t * power_live;
    lv_obj_t * power_peak;
    lv_obj_t * current_peak;
    lv_obj_t * max_temp;
    lv_obj_t * lv_voltage;
    lv_obj_t * lv_current;
    lv_obj_t * lv_power;
    lv_obj_t * sop_power[2];     /* [0]=discharge [1]=charge */
    lv_obj_t * sop_current[2];
    lv_obj_t * fan_duty[3];
    lv_obj_t * fan_rpm[3];
    dash_motor_ui_t motors[4];
} ui;

static lv_style_t st_caption;   /* small grey uppercase captions */
static lv_style_t st_value;     /* 18 px white values */
static lv_style_t st_aux_value; /* 14 px values for the compact brake bars */
static lv_style_t st_value_m;   /* 22 px white values */
static lv_style_t st_large;     /* 36 px */
static lv_style_t st_xl;        /* 56 px */
static lv_style_t st_mode_name; /* 14 px drive-mode name inside the tile */
static lv_style_t st_speed;     /* 180 px digits */
static lv_style_t st_unit;      /* 18 px grey units */
static lv_style_t st_right;
static lv_style_t st_center;
static lv_style_t st_line;      /* 1 px divider */

static dash_bar_t s_brake_bar = {0, 100, C_RED, 1, 1};
static dash_bar_t s_regen_bar = {0, 100, C_BLUE, 1, 1};
static dash_bar_t s_throttle_bar = {0, 100, C_GREEN, 1, 1};
static dash_bar_t s_soc_bar = {0, 100, C_GREEN, 0, 0};
static int32_t s_delta;
static int32_t s_slip = -1;
static int32_t s_signal = -1;
static int32_t s_speed = -1;
static dash_mode_t s_mode = DASH_MODE_ACCELERATION;
static uint8_t s_mode_valid;
static int32_t s_lap_count = -1;
static int32_t s_lap_remaining = -1;
static int32_t s_tire_temp[4][4];
static uint8_t s_night = 0xFFU;

/* ---------------------------------------------------------------- helpers */

static void set_text(lv_obj_t * label, const char * text)
{
    if(strcmp(lv_label_get_text(label), text) == 0) return;
    lv_label_set_text(label, text);
}

static void set_text_color(lv_obj_t * obj, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);
    if(lv_color_eq(lv_obj_get_style_text_color(obj, 0), c)) return;
    lv_obj_set_style_text_color(obj, c, 0);
}

static void set_bg_color(lv_obj_t * obj, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);
    if(lv_color_eq(lv_obj_get_style_bg_color(obj, 0), c)) return;
    lv_obj_set_style_bg_color(obj, c, 0);
}

static void set_hidden(lv_obj_t * obj, uint8_t hidden)
{
    if((hidden != 0U) == lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if(hidden != 0U) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static uint32_t temp_text_color(int32_t temp_c)
{
    if(temp_c >= TEMP_HOT_C) return C_RED;
    if(temp_c >= TEMP_WARN_C) return C_AMBER;
    return C_TEXT;
}

/* High-saturation tyre map. Cyan/yellow waypoints avoid the grey midpoint of
 * mixing complementary blue and yellow directly. */
static lv_color_t tire_color(int32_t temp_centi)
{
    static const int32_t stops[] = {2000, 4000, 6500, 8000, 9500, 11000};
    static const uint32_t colors[] = {0x005CFF, 0x00DFFF, 0x00D060, 0xFFE000, 0xFF7800, 0xFF2020};
    uint32_t i;

    if(temp_centi <= stops[0]) return lv_color_hex(colors[0]);
    for(i = 1U; i < sizeof(stops) / sizeof(stops[0]); i++) {
        if(temp_centi < stops[i]) {
            uint8_t mix = (uint8_t)(((temp_centi - stops[i - 1U]) * 255) / (stops[i] - stops[i - 1U]));
            return lv_color_mix(lv_color_hex(colors[i]), lv_color_hex(colors[i - 1U]), mix);
        }
    }
    return lv_color_hex(colors[i - 1U]);
}

static void format_time(char * buf, size_t size, int32_t hundredths)
{
    if(hundredths < 0) hundredths = 0;
    lv_snprintf(buf, size, "%ld:%02ld.%02ld", (long)(hundredths / 6000),
                (long)((hundredths / 100) % 60), (long)(hundredths % 100));
}

/* Signed deci value with one decimal, minus sign kept out of the fraction. */
static void format_deci(char * buf, size_t size, int32_t deci, const char * unit)
{
    int32_t magnitude = deci < 0 ? -deci : deci;
    lv_snprintf(buf, size, "%s%ld.%ld%s", deci < 0 ? "-" : "",
                (long)(magnitude / 10), (long)(magnitude % 10), unit);
}

static void fill_rect(lv_layer_t * layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      uint32_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_area_t area = {x1, y1, x2, y2};

    if((x2 < x1) || (y2 < y1)) return;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 0;
    lv_draw_rect(layer, &dsc, &area);
}

static lv_obj_t * make_box(lv_obj_t * parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    return obj;
}

static void make_line(lv_obj_t * parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_add_style(make_box(parent, x, y, w, h), &st_line, 0);
}

/* width > 0 gives a fixed-width label aligned by align_style (may be NULL). */
static lv_obj_t * make_label(lv_obj_t * parent, lv_style_t * style, int32_t x, int32_t y,
                             int32_t width, lv_style_t * align_style, const char * text)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_add_style(label, style, 0);
    if(align_style != NULL) lv_obj_add_style(label, align_style, 0);
    lv_obj_set_pos(label, x, y);
    if(width > 0) {
        lv_obj_set_width(label, width);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    }
    lv_label_set_text_static(label, text);
    return label;
}

static void init_style(lv_style_t * style, const lv_font_t * font, uint32_t color)
{
    lv_style_init(style);
    lv_style_set_text_font(style, font);
    lv_style_set_text_color(style, lv_color_hex(color));
}

/* ------------------------------------------------------- draw callbacks */

/* Generic bar: track + proportional fill, optional 25/50/75 % gaps. */
static void bar_draw_cb(lv_event_t * e)
{
    const dash_bar_t * bar = lv_event_get_user_data(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t a;
    int32_t w, h, value = bar->value;

    lv_obj_get_coords(lv_event_get_current_target_obj(e), &a);
    w = lv_area_get_width(&a);
    h = lv_area_get_height(&a);
    if(value < 0) value = 0;
    if(value > bar->max) value = bar->max;

    if(bar->vertical != 0U) {
        int32_t fill = (value * h) / bar->max;
        fill_rect(layer, a.x1, a.y1, a.x2, a.y2 - fill, C_TRACK);
        fill_rect(layer, a.x1, a.y2 - fill + 1, a.x2, a.y2, bar->color);
        if(bar->ticks != 0U) {
            for(int32_t q = 1; q < 4; q++) {
                int32_t y = a.y2 - (q * h) / 4;
                fill_rect(layer, a.x1, y, a.x2, y + 1, C_BG);
            }
        }
    }
    else {
        int32_t fill = (value * w) / bar->max;
        fill_rect(layer, a.x1 + fill, a.y1, a.x2, a.y2, C_TRACK);
        fill_rect(layer, a.x1, a.y1, a.x1 + fill - 1, a.y2, bar->color);
    }
}

/* Delta bar: faster (negative) grows green to the right of centre, slower
 * grows red to the left; +/-DELTA_FULL_SCALE fills one half. */
static void delta_draw_cb(lv_event_t * e)
{
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t a;
    int32_t half, centre, fill, magnitude = s_delta < 0 ? -s_delta : s_delta;

    lv_obj_get_coords(lv_event_get_current_target_obj(e), &a);
    half = lv_area_get_width(&a) / 2;
    centre = a.x1 + half;
    if(magnitude > DELTA_FULL_SCALE) magnitude = DELTA_FULL_SCALE;
    fill = (magnitude * half) / DELTA_FULL_SCALE;
    if((fill == 0) && (s_delta != 0)) fill = 1;

    fill_rect(layer, a.x1, a.y1, a.x2, a.y2, C_TRACK);
    if(s_delta < 0) fill_rect(layer, centre + 1, a.y1, centre + fill, a.y2, C_GREEN);
    else if(s_delta > 0) fill_rect(layer, centre - fill, a.y1, centre - 1, a.y2, C_RED);
    fill_rect(layer, centre - 1, a.y1 - 3, centre + 1, a.y2 + 3, C_TEXT);
}

static void slip_draw_cb(lv_event_t * e)
{
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t a;
    const int32_t gap = 4;
    int32_t seg_w;

    lv_obj_get_coords(lv_event_get_current_target_obj(e), &a);
    seg_w = (lv_area_get_width(&a) - 6 * gap) / 7;
    for(int32_t i = 0; i < 7; i++) {
        int32_t x = a.x1 + i * (seg_w + gap);
        fill_rect(layer, x, a.y1, x + seg_w - 1, a.y2, i < s_slip ? C_TEXT : C_TRACK);
    }
}

static void signal_draw_cb(lv_event_t * e)
{
    static const uint32_t level_color[5] = {C_TRACK, C_RED, C_AMBER, C_GREEN, C_GREEN};
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t a;
    int32_t h, level = s_signal < 0 ? 0 : s_signal;

    lv_obj_get_coords(lv_event_get_current_target_obj(e), &a);
    h = lv_area_get_height(&a);
    for(int32_t i = 0; i < 4; i++) {
        int32_t x = a.x1 + i * 12;
        int32_t bar_h = (h * (i + 1)) / 4;
        fill_rect(layer, x, a.y2 - bar_h + 1, x + 7, a.y2,
                  i < level ? level_color[level] : C_TRACK);
    }
}

/* Four tread bands inside the tyre outline, outside edge facing outward. */
static void tire_draw_cb(lv_event_t * e)
{
    uint32_t wheel = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t a;
    int32_t inner_w;

    lv_obj_get_coords(lv_event_get_current_target_obj(e), &a);
    a.x1 += 3; a.y1 += 3; a.x2 -= 3; a.y2 -= 3;
    inner_w = lv_area_get_width(&a);
    for(uint32_t segment = 0U; segment < 4U; segment++) {
        uint32_t visual = (wheel < 2U) ? segment : (3U - segment);
        lv_draw_rect_dsc_t dsc;
        lv_area_t band = {
            a.x1 + (int32_t)((visual * (uint32_t)inner_w) / 4U), a.y1,
            a.x1 + (int32_t)(((visual + 1U) * (uint32_t)inner_w) / 4U) - 1, a.y2
        };
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = tire_color(s_tire_temp[wheel][segment]);
        dsc.bg_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &dsc, &band);
    }
}

/* Team logo: A8 coverage map from flash, painted white as a mask. */
static void logo_draw_cb(lv_event_t * e)
{
    lv_draw_image_dsc_t dsc;
    lv_area_t a;

    lv_obj_get_coords(lv_event_get_current_target_obj(e), &a);
    lv_draw_image_dsc_init(&dsc);
    dsc.src = &dash_logo;
    dsc.recolor = lv_color_hex(C_TEXT);
    dsc.recolor_opa = LV_OPA_COVER;
    lv_draw_image(lv_event_get_layer(e), &dsc, &a);
}

/* ------------------------------------------------------------- creation */

static lv_obj_t * top_block(lv_obj_t * scr, int32_t x, int32_t w, const char * caption,
                            lv_obj_t ** caption_out)
{
    lv_obj_t * cap = make_label(scr, &st_caption, x, 7, w, NULL, caption);
    if(caption_out != NULL) *caption_out = cap;
    return make_label(scr, &st_value_m, x, 23, w, NULL, "");
}

static void create_top(lv_obj_t * scr)
{
    lv_obj_t * logo = make_box(scr, 12, (TOP_H - (int32_t)dash_logo.header.h) / 2,
                               dash_logo.header.w, dash_logo.header.h);
    lv_obj_add_event_cb(logo, logo_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    make_line(scr, 164, 10, 1, TOP_H - 20);

    ui.best = top_block(scr, 176, 86, "BEST", NULL);
    ui.last = top_block(scr, 266, 86, "LAST", NULL);

    ui.delta = make_label(scr, &st_value_m, 356, 4, 148, &st_center, "");
    ui.delta_bar = make_box(scr, 364, 33, 132, 9);
    lv_obj_add_event_cb(ui.delta_bar, delta_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    ui.lap_count = top_block(scr, 516, 46, "LAP", &ui.lap_caption);
    ui.left_value = top_block(scr, 566, 56, "LEFT", &ui.left_caption);
    ui.odometer = top_block(scr, 630, 84, "ODO KM", NULL);

    make_label(scr, &st_caption, 730, 7, 0, NULL, "GPS");
    ui.signal = make_box(scr, 730, 24, 44, 20);
    lv_obj_add_event_cb(ui.signal, signal_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    make_line(scr, 0, TOP_H, DASH_SCREEN_W, 1);
}

static void create_left(lv_obj_t * scr)
{
    static const int32_t tire_x[4] = {TIRE_LX, TIRE_LX, TIRE_RX, TIRE_RX};
    static const int32_t tire_y[4] = {TIRE_FY, TIRE_RY, TIRE_FY, TIRE_RY};

    ui.mode_tile = make_box(scr, MODE_X, MODE_Y, MODE_W, MODE_H);
    lv_obj_set_style_radius(ui.mode_tile, 8, 0);
    lv_obj_set_style_bg_opa(ui.mode_tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui.mode_tile, 1, 0);
    lv_obj_set_style_border_color(ui.mode_tile, lv_color_hex(C_LINE), 0);
    /* 56 px 字库的实际墨迹只有 39 px 高；两块一起下移并收紧间距，
     * 让缩写与全称在 84 px 色块内上下留白接近。 */
    ui.mode_letter = make_label(ui.mode_tile, &st_xl, 0, 11, MODE_W, &st_center, "AC");
    ui.mode_name = make_label(ui.mode_tile, &st_mode_name, 0, 58, MODE_W, &st_center,
                              "Acceleration");

    make_label(scr, &st_caption, 114, 66, 0, NULL, "SLIP");
    ui.slip_value = make_label(scr, &st_xl, 114, 92, 0, NULL, "0");
    ui.slip_meter = make_box(scr, 14, 158, 172, 8);
    lv_obj_add_event_cb(ui.slip_meter, slip_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    for(uint32_t wheel = 0U; wheel < 4U; wheel++) {
        ui.tires[wheel] = make_box(scr, tire_x[wheel], tire_y[wheel], TIRE_W, TIRE_H);
        lv_obj_set_style_radius(ui.tires[wheel], 6, 0);
        lv_obj_set_style_bg_color(ui.tires[wheel], lv_color_hex(C_TRACK), 0);
        lv_obj_set_style_bg_opa(ui.tires[wheel], LV_OPA_COVER, 0);
        lv_obj_add_event_cb(ui.tires[wheel], tire_draw_cb, LV_EVENT_DRAW_MAIN,
                            (void *)(uintptr_t)wheel);
        /* Max tread temperature beside the outer edge of each tyre. */
        ui.tire_labels[wheel] = make_label(scr, &st_value,
                                           wheel < 2U ? 2 : TIRE_RX + TIRE_W + 6,
                                           tire_y[wheel] + (TIRE_H - 19) / 2, 42,
                                           wheel < 2U ? &st_right : NULL, "");
    }
    /* Chassis hint between the tyres. */
    make_line(scr, TIRE_LX + TIRE_W + 9, TIRE_FY + 8, 2, TIRE_RY + TIRE_H - TIRE_FY - 16);

    ui.night_button = make_box(scr, NIGHT_X, NIGHT_Y, NIGHT_W, NIGHT_H);
    lv_obj_set_style_radius(ui.night_button, 6, 0);
    lv_obj_set_style_bg_opa(ui.night_button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui.night_button, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_width(ui.night_button, 1, 0);
    lv_obj_set_style_border_color(ui.night_button, lv_color_hex(C_LINE), 0);
    ui.night_icon = make_label(ui.night_button, &st_value, 0, 0, 0, NULL, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(ui.night_icon);

    make_line(scr, LEFT_W, MID_Y, 1, BOTTOM_Y - MID_Y);
}

static lv_obj_t * pedal(lv_obj_t * scr, int32_t x, int32_t width, dash_bar_t * bar,
                        const char * caption, uint32_t color, lv_style_t *value_style,
                        int32_t label_x, int32_t label_w, lv_obj_t ** pct_out)
{
    lv_obj_t * obj = make_box(scr, x, PEDAL_Y, width, PEDAL_H);
    lv_obj_add_event_cb(obj, bar_draw_cb, LV_EVENT_DRAW_MAIN, bar);
    *pct_out = make_label(scr, value_style, label_x, PEDAL_Y + PEDAL_H + 6, label_w,
                          &st_center, "0%");
    lv_obj_set_style_text_color(*pct_out, lv_color_hex(color), 0);
    make_label(scr, &st_caption, label_x, PEDAL_Y + PEDAL_H + 28, label_w,
               &st_center, caption);
    return obj;
}

static void create_centre(lv_obj_t * scr)
{
    ui.time_caption = make_label(scr, &st_caption, 300, 60, 200, &st_center, "LAP TIME");
    ui.time_value = make_label(scr, &st_large, 300, 76, 200, &st_center, "0:00.00");
    ui.timing_dot = make_box(scr, 318, 84, 12, 12);
    lv_obj_set_style_radius(ui.timing_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ui.timing_dot, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.timing_dot, LV_OBJ_FLAG_HIDDEN);

    /* 三格车速的墨迹约 263 px 宽；右移 6 px 后与左侧 REG 条和右侧
     * THROTTLE 条各留约 5 px，不再因数字宽度变化压住竖条。 */
    ui.speed = make_label(scr, &st_speed, 268, 166, 276, &st_center, "--");
    make_label(scr, &st_unit, 268, 302, 276, &st_center, "km/h");

    {
        static const char * const sop_caption[2] = {"DISCH", "CHG"};
        for(uint32_t row = 0U; row < 2U; row++) {
            const int32_t y = SOP_Y + (int32_t)row * SOP_ROW_H;
            make_label(scr, &st_caption, SOP_X, y + 4, SOP_CAP_W, &st_center,
                       sop_caption[row]);
            ui.sop_power[row] = make_label(scr, &st_value, SOP_POWER_X, y, SOP_POWER_W,
                                           &st_center, "");
            ui.sop_current[row] = make_label(scr, &st_value, SOP_CURRENT_X, y, SOP_CURRENT_W,
                                             &st_center, "");
        }
    }

    /* 两条制动条放在车速数字左侧；三格车速的墨迹会伸到约 x268，
     * 条块一旦进入该范围就会压住数字。 */
    ui.regen_bar = pedal(scr, REGEN_X, BRAKE_PEDAL_W, &s_regen_bar, "REG", C_BLUE,
                         &st_aux_value, 200, 44, &ui.regen_pct);
    ui.brake_bar = pedal(scr, MECH_X, BRAKE_PEDAL_W, &s_brake_bar, "MECH", C_RED,
                         &st_aux_value, 234, 44, &ui.brake_pct);
    ui.throttle_bar = pedal(scr, THROTTLE_X, PEDAL_W, &s_throttle_bar, "THROTTLE", C_GREEN,
                            &st_value, THROTTLE_X - 26, 84, &ui.throttle_pct);

    /* Created last so the banner paints over the lap-time row edges. */
    ui.alert_box = make_box(scr, ALERT_X, ALERT_Y, ALERT_W, ALERT_H);
    lv_obj_set_style_radius(ui.alert_box, 6, 0);
    lv_obj_set_style_bg_opa(ui.alert_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui.alert_box, lv_color_hex(C_ALERT), 0);
    ui.alert_label = make_label(ui.alert_box, &st_value_m, 10, (ALERT_H - 23) / 2,
                                ALERT_W - 20, &st_center, "");
    lv_obj_add_flag(ui.alert_box, LV_OBJ_FLAG_HIDDEN);
}

/* Right column row: caption plus one value at RIGHT_VAL_X and an optional
 * second value right-aligned to RIGHT_END_X. */
static lv_obj_t * right_row(lv_obj_t * scr, int32_t y, const char * caption, lv_obj_t ** second)
{
    make_label(scr, &st_caption, RIGHT_COL_X, y + 4, 0, NULL, caption);
    if(second != NULL) {
        *second = make_label(scr, &st_value, RIGHT_END_X - 66, y, 66, &st_right, "");
    }
    return make_label(scr, &st_value, RIGHT_VAL_X, y, 64, NULL, "");
}

static void create_right(lv_obj_t * scr)
{
    make_line(scr, RIGHT_X, MID_Y, 1, BOTTOM_Y - MID_Y);

    make_label(scr, &st_caption, RIGHT_COL_X, 60, 0, NULL, "SOC");
    ui.soc = make_label(scr, &st_xl, RIGHT_COL_X, 74, 0, NULL, "0%");
    ui.soc_bar = make_box(scr, RIGHT_COL_X, 126, RIGHT_END_X - RIGHT_COL_X, 10);
    lv_obj_add_event_cb(ui.soc_bar, bar_draw_cb, LV_EVENT_DRAW_MAIN, &s_soc_bar);

    ui.hv_voltage = right_row(scr, 148, "HV", &ui.hv_current);
    ui.power_live = right_row(scr, 148 + RIGHT_ROW_H, "PWR", NULL);
    /* PK: highest power and highest discharge current since power-on. */
    ui.power_peak = right_row(scr, 148 + 2 * RIGHT_ROW_H, "PK", &ui.current_peak);
    ui.max_temp = right_row(scr, 148 + 3 * RIGHT_ROW_H, "TMAX", NULL);
    make_line(scr, RIGHT_COL_X, 240, RIGHT_END_X - RIGHT_COL_X, 1);
    ui.lv_voltage = right_row(scr, 247, "LV", &ui.lv_current);
    ui.lv_power = right_row(scr, 247 + RIGHT_ROW_H, "", NULL);
    make_line(scr, RIGHT_COL_X, 295, RIGHT_END_X - RIGHT_COL_X, 1);
    {
        static const char * const fan_caption[3] = {"FAN1", "FAN2", "FAN3"};
        for(uint32_t fan = 0U; fan < 3U; fan++) {
            ui.fan_duty[fan] = right_row(scr, 302 + (int32_t)fan * RIGHT_ROW_H, fan_caption[fan],
                                         &ui.fan_rpm[fan]);
        }
    }
}

static void create_bottom(lv_obj_t * scr)
{
    static const char * const names[4] = {"FL", "RL", "FR", "RR"};
    static const uint8_t column[4] = {0, 2, 1, 3};   /* screen order FL FR RL RR */

    make_line(scr, 0, BOTTOM_Y, DASH_SCREEN_W, 1);
    for(uint32_t col = 1U; col < 4U; col++) {
        make_line(scr, (int32_t)col * 200, BOTTOM_Y + 10, 1, DASH_SCREEN_H - BOTTOM_Y - 20);
    }
    for(uint32_t wheel = 0U; wheel < 4U; wheel++) {
        dash_motor_ui_t * m = &ui.motors[wheel];
        int32_t x = (int32_t)column[wheel] * 200;
        int32_t y = BOTTOM_Y + 30;

        m->name = make_label(scr, &st_value_m, x + 12, BOTTOM_Y + 4, 0, NULL, names[wheel]);
        m->bolt = make_label(scr, &st_value, x + 44, BOTTOM_Y + 7, 0, NULL, LV_SYMBOL_CHARGE);
        m->diag = make_label(scr, &st_value, x + 110, BOTTOM_Y + 6, 80, &st_right, "OK");
        lv_obj_set_style_pad_hor(m->diag, 6, 0);
        lv_obj_set_style_radius(m->diag, 4, 0);
        lv_obj_set_style_bg_color(m->diag, lv_color_hex(C_ALERT), 0);

        /* Multi-line captions share the 24 px row pitch of the value labels. */
        lv_obj_t * cap = make_label(scr, &st_caption, x + 10, y + 4, 0, NULL, "TQ\nkW\nMOT");
        lv_obj_set_style_text_line_space(cap, 10, 0);
        cap = make_label(scr, &st_caption, x + 106, y + 4, 0, NULL, "RPM\nPK\nINV");
        lv_obj_set_style_text_line_space(cap, 10, 0);
        m->torque = make_label(scr, &st_value, x + 36, y, 62, &st_right, "");
        m->power = make_label(scr, &st_value, x + 36, y + 24, 62, &st_right, "");
        m->motor_temp = make_label(scr, &st_value, x + 36, y + 48, 62, &st_right, "");
        m->rpm = make_label(scr, &st_value, x + 140, y, 50, &st_right, "");
        m->peak = make_label(scr, &st_value, x + 140, y + 24, 50, &st_right, "");
        m->inverter_temp = make_label(scr, &st_value, x + 140, y + 48, 50, &st_right, "");
    }
}

void Dash_View_Create(void)
{
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_remove_style_all(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    init_style(&st_caption, &dash_font_xs, C_DIM);
    lv_style_set_text_letter_space(&st_caption, 1);
    init_style(&st_value, &dash_font_s, C_TEXT);
    init_style(&st_aux_value, &dash_font_xs, C_TEXT);
    init_style(&st_value_m, &dash_font_m, C_TEXT);
    init_style(&st_large, &dash_font_l, C_TEXT);
    init_style(&st_xl, &dash_font_xl, C_TEXT);
    init_style(&st_mode_name, &dash_font_xs, C_TEXT);
    init_style(&st_speed, &dash_font_speed, C_TEXT);
    init_style(&st_unit, &dash_font_s, C_DIM);
    lv_style_init(&st_right);
    lv_style_set_text_align(&st_right, LV_TEXT_ALIGN_RIGHT);
    lv_style_init(&st_center);
    lv_style_set_text_align(&st_center, LV_TEXT_ALIGN_CENTER);
    lv_style_init(&st_line);
    lv_style_set_bg_color(&st_line, lv_color_hex(C_LINE));
    lv_style_set_bg_opa(&st_line, LV_OPA_COVER);

    create_top(scr);
    create_left(scr);
    create_centre(scr);
    create_right(scr);
    create_bottom(scr);

    Dash_View_SetMode(DASH_MODE_ACCELERATION);
    Dash_View_SetSlip(0);
    Dash_View_SetSignal(0);
    Dash_View_SetSpeed(DASH_VIEW_INVALID);
    Dash_View_SetNight(0U);
    Dash_View_SetLapTimes(0, 0, 0);
    Dash_View_SetDelta(0);
    lv_screen_load(scr);
}

dash_zone_t Dash_View_HitTest(int32_t x, int32_t y)
{
    if((y >= 0) && (y < TOP_H) && (x >= 0) && (x < LOGO_ZONE_W)) return DASH_ZONE_LOGO;
    if((y >= 0) && (y < TOP_H) && (x >= LOGO_ZONE_W) && (x < DASH_SCREEN_W)) return DASH_ZONE_LAP;
    if((x >= MODE_X - MODE_TOUCH_PAD) && (x < MODE_X + MODE_W + MODE_TOUCH_PAD) &&
       (y >= MODE_Y - MODE_TOUCH_PAD) && (y < MODE_Y + MODE_H + MODE_TOUCH_PAD)) return DASH_ZONE_MODE;
    if((x >= ALERT_X - 8) && (x < ALERT_X + ALERT_W + 8) &&
       (y >= ALERT_Y - 8) && (y < ALERT_Y + ALERT_H + 8)) return DASH_ZONE_ALERT;
    if((x >= NIGHT_X - 10) && (x < NIGHT_X + NIGHT_W + 10) &&
       (y >= NIGHT_Y - 10) && (y < NIGHT_Y + NIGHT_H + 10)) return DASH_ZONE_NIGHT;
    return DASH_ZONE_NONE;
}

/* --------------------------------------------------------------- setters */

void Dash_View_SetSpeed(int32_t kmh)
{
    char buf[8];

    if(kmh == DASH_VIEW_INVALID) {
        if(s_speed == DASH_VIEW_INVALID) return;
        s_speed = DASH_VIEW_INVALID;
        set_text(ui.speed, "--");
        return;
    }
    if(kmh < 0) kmh = 0;
    if(kmh > 299) kmh = 299;
    if(kmh == s_speed) return;
    s_speed = kmh;
    lv_snprintf(buf, sizeof(buf), "%ld", (long)kmh);
    set_text(ui.speed, buf);
}

void Dash_View_SetMode(dash_mode_t mode)
{
    static const char * const letters[5] = {"DF", "AC", "AX", "8", "EN"};
    static const char * const names[5] = {"Default", "Acceleration", "Autocross", "Skidpad", "Endurance"};
    static const uint32_t tile[5] = {C_TRACK, C_RED, C_YELLOW, C_BLUE, C_GREEN};
    static const uint32_t text[5] = {C_TEXT, C_TEXT, C_BG, C_TEXT, C_BG};
    uint32_t index;
    uint8_t sprint;

    if((mode < DASH_MODE_DEFAULT) || (mode > DASH_MODE_ENDURANCE)) mode = DASH_MODE_DEFAULT;
    if((s_mode_valid != 0U) && (mode == s_mode)) return;
    s_mode = mode;
    s_mode_valid = 1U;
    sprint = (mode == DASH_MODE_ACCELERATION) ? 1U : 0U;
    index = (uint32_t)(mode + 1);

    set_bg_color(ui.mode_tile, tile[index]);
    set_text(ui.mode_letter, letters[index]);
    set_text_color(ui.mode_letter, text[index]);
    set_text(ui.mode_name, names[index]);
    set_text_color(ui.mode_name, text[index]);
    lv_label_set_text_static(ui.lap_caption, sprint ? "RUN" : "LAP");
    lv_label_set_text_static(ui.left_caption, sprint ? "DIST M" : "LEFT");
    lv_label_set_text_static(ui.time_caption, sprint ? "RUN TIME" : "LAP TIME");
    /* Counter text format depends on the mode: force a refresh. */
    {
        int32_t count = s_lap_count, remaining = s_lap_remaining;
        s_lap_count = -1;
        Dash_View_SetLapCounter(count < 0 ? 0 : count, remaining < 0 ? 0 : remaining);
    }
}

void Dash_View_SetSlip(int32_t level)
{
    char buf[4];

    if(level < 0) level = 0;
    if(level > 7) level = 7;
    if(level == s_slip) return;
    s_slip = level;
    lv_snprintf(buf, sizeof(buf), "%ld", (long)level);
    set_text(ui.slip_value, buf);
    lv_obj_invalidate(ui.slip_meter);
}

static void set_pedal(lv_obj_t * bar_obj, dash_bar_t * bar, lv_obj_t * label, int32_t pct)
{
    char buf[8];

    /* -1 marks "no data": empty bar and "--". */
    if(pct == DASH_VIEW_INVALID) pct = -1;
    else if(pct < 0) pct = 0;
    else if(pct > 100) pct = 100;
    if(pct == bar->value) return;
    bar->value = pct;
    lv_obj_invalidate(bar_obj);
    if(pct < 0) {
        set_text(label, "--");
        return;
    }
    lv_snprintf(buf, sizeof(buf), "%ld%%", (long)pct);
    set_text(label, buf);
}

void Dash_View_SetPedals(int32_t throttle_pct, int32_t mechanical_brake_pct,
                         int32_t regen_pct)
{
    set_pedal(ui.throttle_bar, &s_throttle_bar, ui.throttle_pct, throttle_pct);
    set_pedal(ui.brake_bar, &s_brake_bar, ui.brake_pct, mechanical_brake_pct);
    set_pedal(ui.regen_bar, &s_regen_bar, ui.regen_pct, regen_pct);
}

void Dash_View_SetBattery(int32_t soc_pct, int32_t voltage_v, int32_t current_a)
{
    char buf[16];
    uint32_t color;

    if(soc_pct == DASH_VIEW_INVALID) {
        if(s_soc_bar.value != 0) {
            s_soc_bar.value = 0;
            lv_obj_invalidate(ui.soc_bar);
        }
        set_text(ui.soc, "--%");
        set_text_color(ui.soc, C_DIM);
    }
    else {
        if(soc_pct < 0) soc_pct = 0;
        if(soc_pct > 100) soc_pct = 100;
        color = soc_pct < SOC_LOW_PCT ? C_RED : (soc_pct < SOC_MID_PCT ? C_AMBER : C_GREEN);
        if((soc_pct != s_soc_bar.value) || (color != s_soc_bar.color)) {
            s_soc_bar.value = soc_pct;
            s_soc_bar.color = color;
            lv_obj_invalidate(ui.soc_bar);
        }
        lv_snprintf(buf, sizeof(buf), "%ld%%", (long)soc_pct);
        set_text(ui.soc, buf);
        set_text_color(ui.soc, soc_pct < SOC_LOW_PCT ? C_RED : C_TEXT);
    }
    if(voltage_v == DASH_VIEW_INVALID) {
        set_text(ui.hv_voltage, "-- V");
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld V", (long)voltage_v);
        set_text(ui.hv_voltage, buf);
    }
    if(current_a == DASH_VIEW_INVALID) {
        set_text(ui.hv_current, "-- A");
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld A", (long)current_a);
        set_text(ui.hv_current, buf);
    }
}

void Dash_View_SetPower(int32_t live_kw, int32_t peak_kw)
{
    char buf[16];

    if(live_kw == DASH_VIEW_INVALID) {
        set_text(ui.power_live, "-- kW");
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld kW", (long)live_kw);
        set_text(ui.power_live, buf);
    }
    if(peak_kw == DASH_VIEW_INVALID) {
        set_text(ui.power_peak, "-- kW");
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld kW", (long)peak_kw);
        set_text(ui.power_peak, buf);
    }
}

void Dash_View_SetCurrentPeak(int32_t peak_a)
{
    char buf[12];

    if(peak_a == DASH_VIEW_INVALID) {
        set_text(ui.current_peak, "-- A");
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld A", (long)peak_a);
        set_text(ui.current_peak, buf);
    }
}

/* One SOP value. Zero discharge limit means the BMS forbids drive torque, so it
 * is red; any other valid value is white. */
static void set_sop_value(lv_obj_t * label, int32_t deci, const char * unit, uint8_t warn_zero)
{
    char buf[16];

    if(deci == DASH_VIEW_INVALID) {
        lv_snprintf(buf, sizeof(buf), "--%s", unit);
        set_text(label, buf);
        set_text_color(label, C_DIM);
        return;
    }
    format_deci(buf, sizeof(buf), deci, unit);
    set_text(label, buf);
    set_text_color(label, (warn_zero != 0U && deci == 0) ? C_RED : C_TEXT);
}

void Dash_View_SetSop(int32_t discharge_dkw, int32_t charge_dkw,
                      int32_t discharge_da, int32_t charge_da)
{
    const int32_t power[2] = {discharge_dkw, charge_dkw};
    const int32_t current[2] = {discharge_da, charge_da};

    for(uint32_t row = 0U; row < 2U; row++) {
        set_sop_value(ui.sop_power[row], power[row], " kW", (row == 0U) ? 1U : 0U);
        set_sop_value(ui.sop_current[row], current[row], " A", (row == 0U) ? 1U : 0U);
    }
}

void Dash_View_SetMaxTemp(int32_t temp_c)
{
    char buf[12];

    if(temp_c == DASH_VIEW_INVALID) {
        set_text(ui.max_temp, "--");
        set_text_color(ui.max_temp, C_DIM);
        return;
    }
    lv_snprintf(buf, sizeof(buf), "%ld" DEG "C", (long)temp_c);
    set_text(ui.max_temp, buf);
    set_text_color(ui.max_temp, temp_text_color(temp_c));
}

void Dash_View_SetLowVoltage(int32_t voltage_mv, int32_t current_ca, int32_t power_dw)
{
    char buf[16];

    if(voltage_mv == DASH_VIEW_INVALID) {
        set_text(ui.lv_voltage, "-- V");
        set_text(ui.lv_current, "-- A");
        set_text(ui.lv_power, "-- W");
        return;
    }
    format_deci(buf, sizeof(buf), voltage_mv / 100, " V");
    set_text(ui.lv_voltage, buf);
    format_deci(buf, sizeof(buf), current_ca / 10, " A");
    set_text(ui.lv_current, buf);
    lv_snprintf(buf, sizeof(buf), "%ld W", (long)(power_dw / 10));
    set_text(ui.lv_power, buf);
}

void Dash_View_SetFans(const int32_t rpm[3], const int32_t duty_pct[2])
{
    char buf[12];

    for(uint32_t fan = 0U; fan < 3U; fan++) {
        if(fan < 2U) {
            if(duty_pct[fan] == DASH_VIEW_INVALID) {
                set_text(ui.fan_duty[fan], "--%");
            }
            else {
                lv_snprintf(buf, sizeof(buf), "%ld%%", (long)duty_pct[fan]);
                set_text(ui.fan_duty[fan], buf);
            }
        }
        if(rpm[fan] == DASH_VIEW_INVALID) {
            set_text(ui.fan_rpm[fan], "-- rpm");
        }
        else {
            lv_snprintf(buf, sizeof(buf), "%ld rpm", (long)rpm[fan]);
            set_text(ui.fan_rpm[fan], buf);
        }
    }
}

void Dash_View_SetMotor(uint32_t wheel, const dash_motor_t * motor)
{
    dash_motor_ui_t * m;
    char buf[16];

    if((wheel >= 4U) || (motor == NULL)) return;
    m = &ui.motors[wheel];

    if((motor->invalid & DASH_MOTOR_NO_TORQUE) != 0U) {
        set_text(m->torque, "-- Nm");
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld Nm", (long)motor->torque);
        set_text(m->torque, buf);
    }
    if((motor->invalid & DASH_MOTOR_NO_RPM) != 0U) {
        set_text(m->rpm, "--");
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld", (long)motor->rpm);
        set_text(m->rpm, buf);
    }
    /* Power needs both torque and speed. */
    if((motor->invalid & (DASH_MOTOR_NO_TORQUE | DASH_MOTOR_NO_RPM)) != 0U) {
        set_text(m->power, "--");
    }
    else {
        format_deci(buf, sizeof(buf), motor->power_dkw, "");
        set_text(m->power, buf);
    }
    format_deci(buf, sizeof(buf), motor->power_peak_dkw, "");
    set_text(m->peak, buf);
    if((motor->invalid & DASH_MOTOR_NO_MOTOR_TEMP) != 0U) {
        set_text(m->motor_temp, "--");
        set_text_color(m->motor_temp, C_DIM);
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld" DEG, (long)motor->motor_temp);
        set_text(m->motor_temp, buf);
        set_text_color(m->motor_temp, temp_text_color(motor->motor_temp));
    }
    if((motor->invalid & DASH_MOTOR_NO_INV_TEMP) != 0U) {
        set_text(m->inverter_temp, "--");
        set_text_color(m->inverter_temp, C_DIM);
    }
    else {
        lv_snprintf(buf, sizeof(buf), "%ld" DEG, (long)motor->inverter_temp);
        set_text(m->inverter_temp, buf);
        set_text_color(m->inverter_temp, temp_text_color(motor->inverter_temp));
    }
    set_text_color(m->bolt, ((motor->enabled != 0U) &&
                             ((motor->invalid & DASH_MOTOR_NO_ENABLED) == 0U)) ? C_YELLOW : C_TRACK);

    if((motor->invalid & DASH_MOTOR_NO_DIAG) != 0U) {
        set_text(m->diag, "--");
        set_text_color(m->diag, C_DIM);
        if(lv_obj_get_style_bg_opa(m->diag, 0) != LV_OPA_TRANSP) {
            lv_obj_set_style_bg_opa(m->diag, LV_OPA_TRANSP, 0);
        }
    }
    else if(motor->diag != 0U) {
        lv_snprintf(buf, sizeof(buf), "E %04lu", (unsigned long)motor->diag);
        set_text(m->diag, buf);
        set_text_color(m->diag, C_TEXT);
        if(lv_obj_get_style_bg_opa(m->diag, 0) != LV_OPA_COVER) {
            lv_obj_set_style_bg_opa(m->diag, LV_OPA_COVER, 0);
        }
    }
    else {
        set_text(m->diag, "OK");
        set_text_color(m->diag, C_DIM);
        if(lv_obj_get_style_bg_opa(m->diag, 0) != LV_OPA_TRANSP) {
            lv_obj_set_style_bg_opa(m->diag, LV_OPA_TRANSP, 0);
        }
    }
}

void Dash_View_SetTire(uint32_t wheel, const int32_t temps_centi[4])
{
    int32_t maximum;
    char buf[12];

    if((wheel >= 4U) || (temps_centi == NULL)) return;
    if(memcmp(s_tire_temp[wheel], temps_centi, sizeof(s_tire_temp[wheel])) == 0 &&
       lv_label_get_text(ui.tire_labels[wheel])[0] != '\0') return;
    memcpy(s_tire_temp[wheel], temps_centi, sizeof(s_tire_temp[wheel]));
    lv_obj_invalidate(ui.tires[wheel]);

    maximum = temps_centi[0];
    for(uint32_t i = 1U; i < 4U; i++) {
        if(temps_centi[i] > maximum) maximum = temps_centi[i];
    }
    /* Integer display, rounded half away from zero. */
    lv_snprintf(buf, sizeof(buf), "%ld" DEG,
                (long)((maximum >= 0 ? maximum + 50 : maximum - 50) / 100));
    set_text(ui.tire_labels[wheel], buf);
}

void Dash_View_SetLapTimes(int32_t current, int32_t last, int32_t best)
{
    char buf[16];

    format_time(buf, sizeof(buf), current);
    set_text(ui.time_value, buf);
    format_time(buf, sizeof(buf), last);
    set_text(ui.last, buf);
    format_time(buf, sizeof(buf), best);
    set_text(ui.best, buf);
}

void Dash_View_SetLapCounter(int32_t count, int32_t remaining)
{
    char buf[12];

    if((count == s_lap_count) && (remaining == s_lap_remaining)) return;
    s_lap_count = count;
    s_lap_remaining = remaining;
    lv_snprintf(buf, sizeof(buf), "%02ld", (long)count);
    set_text(ui.lap_count, buf);
    lv_snprintf(buf, sizeof(buf), "%02ld", (long)remaining);
    set_text(ui.left_value, buf);
}

void Dash_View_SetDelta(int32_t delta)
{
    char buf[16];
    int32_t magnitude = delta < 0 ? -delta : delta;

    if((delta == s_delta) && (lv_label_get_text(ui.delta)[0] != '\0')) return;
    s_delta = delta;
    lv_snprintf(buf, sizeof(buf), "%s%ld.%02ld", delta > 0 ? "+" : (delta < 0 ? "-" : ""),
                (long)(magnitude / 100), (long)(magnitude % 100));
    set_text(ui.delta, buf);
    set_text_color(ui.delta, delta < 0 ? C_GREEN : (delta > 0 ? C_RED : C_TEXT));
    lv_obj_invalidate(ui.delta_bar);
}

void Dash_View_SetSignal(int32_t level)
{
    if(level < 0) level = 0;
    if(level > 4) level = 4;
    if(level == s_signal) return;
    s_signal = level;
    lv_obj_invalidate(ui.signal);
}

void Dash_View_SetTimingDot(uint8_t visible, uint32_t color_hex)
{
    if(visible != 0U) set_bg_color(ui.timing_dot, color_hex);
    set_hidden(ui.timing_dot, visible == 0U);
}

void Dash_View_SetAlert(const char * text, uint32_t count, uint8_t ready)
{
    char buf[48];
    uint32_t i = 0U;

    if(ready != 0U) {
        set_bg_color(ui.alert_box, C_GREEN);
        set_text_color(ui.alert_label, C_BG);
        set_text(ui.alert_label, "READY");
        set_hidden(ui.alert_box, 0U);
        return;
    }
    if((text == NULL) || (text[0] == '\0') || (count == 0U)) {
        set_hidden(ui.alert_box, 1U);
        return;
    }
    /* Upper-case for consistency; "+N" tells the driver more are queued. */
    for(; (text[i] != '\0') && (i < 32U); i++) {
        char c = text[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    buf[i] = '\0';
    if(count > 1U) lv_snprintf(buf + i, sizeof(buf) - i, "   +%lu", (unsigned long)(count - 1U));
    set_bg_color(ui.alert_box, C_ALERT);
    set_text_color(ui.alert_label, C_TEXT);
    set_text(ui.alert_label, buf);
    set_hidden(ui.alert_box, 0U);
}

void Dash_View_SetOdometer(int32_t tenths_km)
{
    char buf[12];

    if(tenths_km < 0) tenths_km = 0;
    if(tenths_km > 99999) tenths_km = 99999;
    lv_snprintf(buf, sizeof(buf), "%ld.%ld", (long)(tenths_km / 10), (long)(tenths_km % 10));
    set_text(ui.odometer, buf);
}

void Dash_View_SetNight(uint8_t night)
{
    night = night != 0U ? 1U : 0U;
    if(night == s_night) return;
    s_night = night;
    set_text(ui.night_icon, night ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    set_text_color(ui.night_icon, night ? C_AMBER : C_DIM);
}

/* USER CODE END Dashboard_View */
