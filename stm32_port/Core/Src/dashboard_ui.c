/* USER CODE BEGIN dashboard_ui */
#include "dashboard_ui.h"

#include "main.h"

#include "../lvgl/lvgl.h"

#include "can.h"
#include "gps.h"
#include "gps_lap.h"

#include <string.h>

LV_FONT_DECLARE(dashboard_mode_font);
static volatile uint8_t g_dashboard_initialized;

/* Integer/temperature rounding often leaves text unchanged even when raw
 * telemetry changes. Avoid label allocation, layout and invalidation then. */
static void dashboard_set_label_text(lv_obj_t *label, const char *text)
{
    if(text != NULL && strcmp(lv_label_get_text(label), text) == 0) return;
    lv_label_set_text(label, text);
}

typedef struct {
    lv_obj_t * speed_digit_container;
    lv_obj_t * speed_segments[2][7];
    lv_obj_t * lap_current_label;
    lv_obj_t * lap_current_value;
    lv_obj_t * lap_last_label;
    lv_obj_t * lap_last_value;
    lv_obj_t * lap_best_label;
    lv_obj_t * lap_best_value;
    lv_obj_t * delta_bar_track;
    lv_obj_t * delta_bar_fill;
    lv_obj_t * delta_bar_center;
    lv_obj_t * delta_value;
    lv_obj_t * mode_tile;
    lv_obj_t * mode_value;
    lv_obj_t * slip_value;
    lv_obj_t * slip_bars[7];
    lv_obj_t * soc_value;
    lv_obj_t * battery_fill;
    lv_obj_t * throttle_bar_fill;
    lv_obj_t * brake_bar_fill;
    lv_obj_t * wheel_fl;
    lv_obj_t * wheel_fr;
    lv_obj_t * wheel_rl;
    lv_obj_t * wheel_rr;
    lv_obj_t * tire_max_labels[4];
    lv_obj_t * lightning_fl;
    lv_obj_t * lightning_fr;
    lv_obj_t * lightning_rl;
    lv_obj_t * lightning_rr;
    lv_obj_t * total_voltage_value;
    lv_obj_t * total_current_value;
    lv_obj_t * max_temp_value;
    lv_obj_t * power_live_value;
    lv_obj_t * power_peak_value;
    lv_obj_t * motor_fl_torque;
    lv_obj_t * motor_fl_speed;
    lv_obj_t * motor_fl_power_live;
    lv_obj_t * motor_fl_power_peak;
    lv_obj_t * motor_fl_temp;
    lv_obj_t * motor_fr_torque;
    lv_obj_t * motor_fr_speed;
    lv_obj_t * motor_fr_power_live;
    lv_obj_t * motor_fr_power_peak;
    lv_obj_t * motor_fr_temp;
    lv_obj_t * motor_rl_torque;
    lv_obj_t * motor_rl_speed;
    lv_obj_t * motor_rl_power_live;
    lv_obj_t * motor_rl_power_peak;
    lv_obj_t * motor_rl_temp;
    lv_obj_t * motor_rr_torque;
    lv_obj_t * motor_rr_speed;
    lv_obj_t * motor_rr_power_live;
    lv_obj_t * motor_rr_power_peak;
    lv_obj_t * motor_rr_temp;
    lv_obj_t * laps_current_label;
    lv_obj_t * laps_current_value;
    lv_obj_t * laps_left_label;
    lv_obj_t * laps_left_value;
    lv_obj_t * signal_bars[4];
    lv_obj_t * alert_circle;
    lv_obj_t * alert_label;
    lv_obj_t * odometer_label;
    /* DBC BO_1440 PDM_LowVoltageBus 0x5A0: right-panel low-voltage readouts */
    lv_obj_t * lv_voltage_value;
    lv_obj_t * lv_current_value;
    lv_obj_t * lv_power_value;
    /* DBC BO_1442 FanController_Status 0x5A2: "F1 85% 3200" rows */
    lv_obj_t * fan_value_labels[3];
    /* DBC BO_1287 Debug7: per-wheel inverter temperature "Ti" */
    lv_obj_t * motor_fl_inverter;
    lv_obj_t * motor_fr_inverter;
    lv_obj_t * motor_rl_inverter;
    lv_obj_t * motor_rr_inverter;
    /* DBC BO_1283/1284: per-wheel diagnostic number "E", red when non-zero */
    lv_obj_t * motor_fl_error;
    lv_obj_t * motor_fr_error;
    lv_obj_t * motor_rl_error;
    lv_obj_t * motor_rr_error;
    /* Large centre-panel pedal bars with live percentage captions */
    lv_obj_t * throttle_pct_label;
    lv_obj_t * brake_pct_label;
    /* Night-mode toggle: circular EYE icon, dims backlight to 60% */
    lv_obj_t * night_icon;
    lv_obj_t * night_icon_label;
} dashboard_ui_t;

typedef enum {
    DRIVE_MODE_S = 0,
    DRIVE_MODE_Q,
    DRIVE_MODE_C,
    DRIVE_MODE_E
} drive_mode_t;

static dashboard_ui_t g_dashboard;
static volatile drive_mode_t g_drive_mode = DRIVE_MODE_S;
static int32_t g_speed = 24;
static int32_t g_soc = 24;
static volatile int32_t g_mode_index = 0;
static int32_t g_slip_level = 0;
static int32_t g_torque[4] = {24, 24, 24, 24};
static int32_t g_rpm[4] = {24, 24, 24, 24};
static int32_t g_sum_voltage = 24;
static int32_t g_top_temperature = 24;
static int32_t g_sum_current = 24;
static int32_t g_current_lap_time = 0;
static int32_t g_last_lap_time = 0;
static int32_t g_best_lap_time = 0;
static int32_t g_lap_delta = 0;
static int32_t g_laps_current = 0;
static int32_t g_laps_left = 75;
static int32_t g_power_live = 12;
static int32_t g_power_peak = 36;
static int32_t g_motor_power_live[4] = {10, 10, 9, 9};
static int32_t g_motor_power_peak[4] = {0, 0, 0, 0};
static int32_t g_motor_temp[4] = {48, 47, 49, 50};
/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR; segment order is outside-to-inside.
 * Tire temperatures are stored in 0.01 degC to preserve the CAN precision. */
static int32_t g_tire_temp[4][4] = {
    {5800, 6100, 6400, 6700},
    {5900, 6200, 6500, 6800},
    {5700, 6000, 6300, 6600},
    {6000, 6300, 6600, 6900}
};
static volatile int32_t g_pending_tire_temp[4][4];
static bool g_motor_fl_online = true;
static bool g_motor_fr_online = true;
static bool g_motor_rl_online = true;
static bool g_motor_rr_online = true;
static dashboard_data_t g_dashboard_data = {
    .speed = 24,
    .soc = 24,
    .mode_index = 0,
    .torque = {24, 24, 24, 24},
    .motor_enable = {1, 1, 1, 1},
    .rpm = {24, 24, 24, 24},
    .sum_voltage = 24,
    .sum_current = 24,
    .max_temperature = 24,
};
static volatile dashboard_data_t g_pending_dashboard_data;
static volatile uint32_t g_dashboard_dirty_mask = 0U;
static volatile int32_t g_pending_speed = 24;
static volatile uint8_t g_speed_dirty = 0U;
static volatile int32_t g_pending_signal_level = 0;
static volatile uint8_t g_signal_dirty = 0U;
static volatile int32_t g_pending_lap_delta = 0;
static volatile uint8_t g_lap_delta_dirty = 0U;
static volatile int32_t g_pending_lap_current = 0;
static volatile int32_t g_pending_lap_last = 0;
static volatile int32_t g_pending_lap_best = 0;
static volatile int32_t g_pending_lap_count = 0;
static volatile uint8_t g_lap_times_dirty = 0U;
static volatile int32_t g_pending_sprint_remaining_m = 75;
static volatile uint8_t g_sprint_remaining_dirty = 0U;
static uint32_t g_speed_ui_last_tick = 0U;
static uint32_t g_dashboard_fast_ui_last_tick = 0U;
static uint32_t g_dashboard_slow_ui_last_tick = 0U;
static volatile uint8_t g_lap_analysis_active = 0U;
static uint8_t g_lap_analysis_blink_visible = 0U;
static uint8_t g_lap_analysis_indicator_visible = 0xFFU;
static GPS_LapDiagState_t g_lap_analysis_indicator_state = (GPS_LapDiagState_t)0xFFU;
static uint32_t g_lap_analysis_blink_tick = 0U;

#define DASHBOARD_ALERT_MAX 6U
#define DASHBOARD_ALERT_TEXT_MAX 32U
#define DASHBOARD_UI_FAST_PERIOD_MS 50U
#define DASHBOARD_UI_SLOW_PERIOD_MS 200U
#define LAP_START_MIN_SPEED_KMH     5.0f

#define DASH_DIRTY_BATTERY  (1UL << 0)
#define DASH_DIRTY_MODE     (1UL << 1)
#define DASH_DIRTY_PEDALS   (1UL << 2)
#define DASH_DIRTY_MOTOR_0  (1UL << 3)
#define DASH_DIRTY_MOTOR_1  (1UL << 4)
#define DASH_DIRTY_MOTOR_2  (1UL << 5)
#define DASH_DIRTY_MOTOR_3  (1UL << 6)
#define DASH_DIRTY_ODOMETER (1UL << 7)
#define DASH_DIRTY_SLIP     (1UL << 8)
#define DASH_DIRTY_TIRE_0   (1UL << 9)
#define DASH_DIRTY_TIRE_1   (1UL << 10)
#define DASH_DIRTY_TIRE_2   (1UL << 11)
#define DASH_DIRTY_TIRE_3   (1UL << 12)
#define DASH_DIRTY_LOWVOLTAGE (1UL << 13)
#define DASH_DIRTY_FANS     (1UL << 14)
#define DASH_DIRTY_MOTORS   (DASH_DIRTY_MOTOR_0 | DASH_DIRTY_MOTOR_1 | \
                             DASH_DIRTY_MOTOR_2 | DASH_DIRTY_MOTOR_3)
#define DASH_DIRTY_TIRES    (DASH_DIRTY_TIRE_0 | DASH_DIRTY_TIRE_1 | \
                             DASH_DIRTY_TIRE_2 | DASH_DIRTY_TIRE_3)
#define DASH_DIRTY_FAST     (DASH_DIRTY_MODE | DASH_DIRTY_PEDALS | DASH_DIRTY_MOTORS | \
                             DASH_DIRTY_SLIP | DASH_DIRTY_TIRES | DASH_DIRTY_FANS)
#define DASH_DIRTY_SLOW     (DASH_DIRTY_BATTERY | DASH_DIRTY_ODOMETER | \
                             DASH_DIRTY_LOWVOLTAGE)
#define DASH_DIRTY_ALL      (DASH_DIRTY_FAST | DASH_DIRTY_SLOW)
static char g_alert_queue[DASHBOARD_ALERT_MAX][DASHBOARD_ALERT_TEXT_MAX];
static uint8_t g_alert_count = 0U;
static char g_pending_alert_queue[DASHBOARD_ALERT_MAX][DASHBOARD_ALERT_TEXT_MAX];
static volatile uint8_t g_pending_alert_count = 0U;
static volatile uint8_t g_pending_alert_pop_count = 0U;
static volatile uint8_t g_pending_lap_toggle = 0U;
static volatile uint8_t g_pending_alert_clear = 0U;
static volatile uint8_t g_pending_mode_lock_alert = 0U;
static volatile uint8_t g_pending_sprint_ready = 0U;
static uint8_t g_sprint_ready_dirty = 0U;
static uint8_t g_sprint_ready_visible = 0U;
static uint32_t g_mode_lock_alert_last_tick = 0U;
/* Night mode: 0 = day (100% backlight), 1 = night (60% backlight). */
static volatile uint8_t g_pending_night_toggle = 0U;
static uint8_t g_night_mode = 0U;

#define DASHBOARD_FONT_SMALL (&lv_font_montserrat_18)
#define DASHBOARD_FONT_MEDIUM (&lv_font_montserrat_18)
#define DASHBOARD_FONT_LARGE (&dashboard_mode_font)

static const uint8_t g_speed_digit_map[10][7] = {
    {1, 1, 1, 1, 1, 1, 0},
    {0, 1, 1, 0, 0, 0, 0},
    {1, 1, 0, 1, 1, 0, 1},
    {1, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 1, 1},
    {1, 0, 1, 1, 0, 1, 1},
    {1, 0, 1, 1, 1, 1, 1},
    {1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 0, 1, 1},
};

#define UI_BG_COLOR lv_color_hex(0xFFFFFF)
#define UI_TEXT_COLOR lv_color_hex(0x000000)
#define UI_BORDER_COLOR lv_color_hex(0x000000)
#define UI_SEGMENT_ON_COLOR lv_color_hex(0x000000)
#define UI_SEGMENT_OFF_COLOR lv_color_hex(0xD8D8D8)

#define SIM_HOR_RES 800
#define SIM_VER_RES 480

#define UI_TOP_HEIGHT 54
#define UI_MIDDLE_Y (UI_TOP_HEIGHT + 1)
#define UI_MIDDLE_HEIGHT 342
#define UI_BOTTOM_Y (UI_MIDDLE_Y + UI_MIDDLE_HEIGHT + 1)
#define UI_BOTTOM_HEIGHT (SIM_VER_RES - UI_BOTTOM_Y)

#define UI_LEFT_PANEL_WIDTH 180
#define UI_CENTER_PANEL_X UI_LEFT_PANEL_WIDTH
#define UI_CENTER_PANEL_WIDTH 440
#define UI_RIGHT_PANEL_X (UI_CENTER_PANEL_X + UI_CENTER_PANEL_WIDTH)
#define UI_RIGHT_PANEL_WIDTH (SIM_HOR_RES - UI_RIGHT_PANEL_X)

#define LAP_TOUCH_X_MIN 0
#define LAP_TOUCH_X_MAX SIM_HOR_RES
#define LAP_TOUCH_Y_MIN 0
#define LAP_TOUCH_Y_MAX UI_TOP_HEIGHT

#define ALERT_TOUCH_X_MIN 322
#define ALERT_TOUCH_X_MAX 580
#define ALERT_TOUCH_Y_MIN UI_MIDDLE_Y
#define ALERT_TOUCH_Y_MAX (UI_MIDDLE_Y + 72)

/* Night-mode toggle icon in the free bottom area of the left vehicle panel
 * (absolute screen coordinates: vehicle panel starts at UI_MIDDLE_Y). */
#define NIGHT_ICON_X 72
#define NIGHT_ICON_Y 292
#define NIGHT_ICON_SIZE 40
#define NIGHT_TOUCH_X_MIN (NIGHT_ICON_X - 6)
#define NIGHT_TOUCH_X_MAX (NIGHT_ICON_X + NIGHT_ICON_SIZE + 6)
#define NIGHT_TOUCH_Y_MIN (UI_MIDDLE_Y + NIGHT_ICON_Y - 6)
#define NIGHT_TOUCH_Y_MAX (UI_MIDDLE_Y + NIGHT_ICON_Y + NIGHT_ICON_SIZE + 6)

/* Backlight dimming: PF9 is reconfigured from plain GPIO output to TIM14_CH1
 * PWM (AF9). 84 MHz / 84 / 1000 = 1 kHz; night compare = 45% duty. */
#define BACKLIGHT_PWM_PERIOD 1000U
#define BACKLIGHT_NIGHT_COMPARE 450U

/* Boot-stage LED breadcrumbs implemented in main.c. Stage 3 = this init is
 * running (RED on), stage 4 = past the night-icon allocation (RED+GREEN). */
void LED_Diag_SetBootStage(uint8_t stage);

#define DELTA_BAR_X 450
#define DELTA_BAR_Y 17
#define DELTA_BAR_W 84
#define DELTA_BAR_H 16
#define DELTA_BAR_INNER_X 6
#define DELTA_BAR_CENTER_X 42
#define DELTA_BAR_RIGHT_X 78
#define DELTA_BAR_FILL_H 15
#define DELTA_BAR_FULL_SCALE_HUNDREDTHS 500
#define SLIP_BAR_COUNT 7U
#define DELTA_TEXT_LIMIT_HUNDREDTHS 3000

/* Pedal bars live beside the speed digits so they stay in the driver's primary
 * sight line. The bars are children of the full-width middle panel: the speed
 * digit container spans x307..493, so the bars sit at x222..270 (brake) and
 * x530..578 (throttle), inside the 180..620 centre column but clear of the
 * digits and the km/h caption. Percentages are drawn under each bar. */
#define PEDAL_BAR_W 48
#define PEDAL_BAR_H 196
#define PEDAL_BAR_TOP_Y 84
#define PEDAL_BRAKE_X 222
#define PEDAL_THROTTLE_X 530
#define PEDAL_PCT_LABEL_Y 286
#define PEDAL_NAME_LABEL_Y 310

#define UI_BATTERY_FILL_MAX_W 50
#define UI_SPEED_DIGIT_W 84
#define UI_SPEED_DIGIT_H 140
#define UI_SPEED_SEG_THICKNESS 12
#define UI_SPEED_DIGIT_GAP 18
#define UI_SPEED_BOX_Y 72
#define UI_SPEED_BOX_HEIGHT 210
#define UI_TIRE_WIDTH 42
#define UI_TIRE_HEIGHT 54
#define UI_TIRE_LEFT_X 34
#define UI_TIRE_RIGHT_X 104
#define UI_TIRE_FRONT_Y 148
#define UI_TIRE_REAR_Y 208
#define UI_TIRE_TEMP_LABEL_Y -14
#define UI_TIRE_LIGHTNING_Y 14
#define UI_TIRE_LIGHTNING_SCALE 352

/* High-saturation thermal map. The cyan and yellow waypoints avoid the grey
 * midpoint produced by directly mixing complementary blue and yellow. */
static lv_color_t temp_to_color(int32_t temp_centi)
{
    const lv_color_t cold = lv_color_hex(0x005CFF);
    const lv_color_t cool = lv_color_hex(0x00DFFF);
    const lv_color_t working = lv_color_hex(0x00D060);
    const lv_color_t warm = lv_color_hex(0xFFE000);
    const lv_color_t hot = lv_color_hex(0xFF7800);
    const lv_color_t overheat = lv_color_hex(0xFF2020);
    uint8_t mix;

    if(temp_centi <= 2000) return cold;
    if(temp_centi < 4000) {
        mix = (uint8_t)(((temp_centi - 2000) * 255) / 2000);
        return lv_color_mix(cool, cold, mix);
    }
    if(temp_centi < 6500) {
        mix = (uint8_t)(((temp_centi - 4000) * 255) / 2500);
        return lv_color_mix(working, cool, mix);
    }
    if(temp_centi < 8000) {
        mix = (uint8_t)(((temp_centi - 6500) * 255) / 1500);
        return lv_color_mix(warm, working, mix);
    }
    if(temp_centi < 9500) {
        mix = (uint8_t)(((temp_centi - 8000) * 255) / 1500);
        return lv_color_mix(hot, warm, mix);
    }
    if(temp_centi < 11000) {
        mix = (uint8_t)(((temp_centi - 9500) * 255) / 1500);
        return lv_color_mix(overheat, hot, mix);
    }
    return overheat;
}

/* Draw the four tread-temperature bands inside the existing wheel object.
 * Keeping the bands as drawing primitives avoids 16 extra LVGL objects on the
 * STM32, whose former 32 KiB LVGL heap reached its initialization limit. */
static void tire_draw_event_cb(lv_event_t * event)
{
    lv_obj_t * wheel = lv_event_get_current_target_obj(event);
    lv_layer_t * layer = lv_event_get_layer(event);
    lv_area_t wheel_coords;
    lv_draw_rect_dsc_t rect_dsc;
    int32_t wheel_index = -1;

    if(wheel == g_dashboard.wheel_fl) wheel_index = 0;
    else if(wheel == g_dashboard.wheel_rl) wheel_index = 1;
    else if(wheel == g_dashboard.wheel_fr) wheel_index = 2;
    else if(wheel == g_dashboard.wheel_rr) wheel_index = 3;
    if((wheel_index < 0) || (layer == NULL)) return;

    lv_obj_get_coords(wheel, &wheel_coords);
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_opa = LV_OPA_TRANSP;
    rect_dsc.radius = 0;

    for(uint32_t segment = 0U; segment < 4U; segment++) {
        uint32_t visual_segment = (wheel_index < 2) ? segment : (3U - segment);
        lv_coord_t inner_x1 = wheel_coords.x1 + 1;
        lv_coord_t inner_width = (wheel_coords.x2 - wheel_coords.x1 + 1) - 2;
        lv_area_t segment_coords = {
            .x1 = inner_x1 + (lv_coord_t)((visual_segment * (uint32_t)inner_width) / 4U),
            .y1 = wheel_coords.y1 + 1,
            .x2 = inner_x1 + (lv_coord_t)(((visual_segment + 1U) * (uint32_t)inner_width) / 4U) - 1,
            .y2 = wheel_coords.y2 - 1
        };
        rect_dsc.bg_color = temp_to_color(g_tire_temp[wheel_index][segment]);
        lv_draw_rect(layer, &rect_dsc, &segment_coords);
    }
}

/* Backlight PWM on PF9 = TIM14_CH1 (AF9). gpio.c brings PF9 up as a plain
 * push-pull output (backlight on); this reconfigures the pin to alternate
 * function and starts a 1 kHz PWM so brightness can be dimmed. */
static TIM_HandleTypeDef s_backlight_tim;

/* PWM unavailable: put PF9 back to a plain GPIO output and light the panel.
 * The screen must never stay dark just because dimming could not start. */
static void backlight_gpio_fallback(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    s_backlight_tim.Instance = NULL;   /* night toggle keeps skipping PWM writes */
    gpio_init.Pin = GPIO_PIN_9;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOF, &gpio_init);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_SET);
}

static void dashboard_backlight_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    TIM_OC_InitTypeDef oc_config = {0};

    __HAL_RCC_TIM14_CLK_ENABLE();

    gpio_init.Pin = GPIO_PIN_9;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = GPIO_AF9_TIM14;
    HAL_GPIO_Init(GPIOF, &gpio_init);

    s_backlight_tim.Instance = TIM14;
    s_backlight_tim.Init.Prescaler = 83U;                 /* 84 MHz / 84 = 1 MHz */
    s_backlight_tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_backlight_tim.Init.Period = BACKLIGHT_PWM_PERIOD - 1U; /* -> 1 kHz */
    s_backlight_tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    if(HAL_TIM_PWM_Init(&s_backlight_tim) != HAL_OK) {
        backlight_gpio_fallback();
        return;
    }

    oc_config.OCMode = TIM_OCMODE_PWM1;
    oc_config.Pulse = BACKLIGHT_PWM_PERIOD;               /* start fully lit */
    oc_config.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc_config.OCFastMode = TIM_OCFAST_DISABLE;
    if(HAL_TIM_PWM_ConfigChannel(&s_backlight_tim, &oc_config, TIM_CHANNEL_1) != HAL_OK) {
        backlight_gpio_fallback();
        return;
    }
    if(HAL_TIM_PWM_Start(&s_backlight_tim, TIM_CHANNEL_1) != HAL_OK) {
        backlight_gpio_fallback();
        return;
    }
}

static void dashboard_backlight_set(uint8_t night_mode)
{
    /* PWM1 with CCR >= period keeps the output high, i.e. full brightness. */
    uint32_t compare = (night_mode != 0U) ? BACKLIGHT_NIGHT_COMPARE : BACKLIGHT_PWM_PERIOD;
    /* update_night_mode_ui() runs once inside Dashboard_UI_Init before
     * dashboard_backlight_init(); a NULL Instance would write CCR1 at 0x28
     * (flash alias) and hardfault. PWM starts fully lit, so skipping is fine. */
    if(s_backlight_tim.Instance == NULL) return;
    __HAL_TIM_SET_COMPARE(&s_backlight_tim, TIM_CHANNEL_1, compare);
}

static void update_night_mode_ui(void)
{
    if(g_dashboard.night_icon_label != NULL) {
        dashboard_set_label_text(g_dashboard.night_icon_label,
                          (g_night_mode != 0U) ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }
    if(g_dashboard.night_icon != NULL) {
        lv_obj_set_style_bg_color(g_dashboard.night_icon,
                                  (g_night_mode != 0U) ? lv_color_hex(0x555555) : UI_BG_COLOR, 0);
    }
    dashboard_backlight_set(g_night_mode);
}

/* Draw 25/50/75% tick lines inside a pedal bar track without extra objects,
 * same trick as the tire bands. The fill child covers ticks below the level. */
static void pedal_bar_draw_event_cb(lv_event_t * event)
{
    static const uint32_t ticks[3] = {25U, 50U, 75U};
    lv_obj_t * bar = lv_event_get_current_target_obj(event);
    lv_layer_t * layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_draw_rect_dsc_t tick_dsc;
    lv_coord_t bar_h;
    uint32_t i;

    if(layer == NULL) return;
    lv_obj_get_coords(bar, &coords);
    bar_h = coords.y2 - coords.y1 + 1;
    lv_draw_rect_dsc_init(&tick_dsc);
    tick_dsc.bg_opa = LV_OPA_30;
    tick_dsc.bg_color = lv_color_hex(0x000000);
    tick_dsc.border_opa = LV_OPA_TRANSP;
    tick_dsc.radius = 0;
    for(i = 0U; i < 3U; i++) {
        lv_coord_t tick_y = coords.y2 - (lv_coord_t)((ticks[i] * (uint32_t)bar_h) / 100U);
        lv_area_t tick_area = {
            .x1 = coords.x1 + 1, .x2 = coords.x2 - 1,
            .y1 = tick_y, .y2 = tick_y
        };
        lv_draw_rect(layer, &tick_dsc, &tick_area);
    }
}

static lv_obj_t * create_panel(lv_obj_t * parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                               lv_color_t bg_color, lv_opa_t bg_opa)
{    lv_obj_t * panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, bg_color, 0);
    lv_obj_set_style_bg_opa(panel, bg_opa, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, UI_BORDER_COLOR, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    return panel;
}

static lv_obj_t * create_value(lv_obj_t * parent, const char * text, lv_color_t color, const lv_font_t * font)
{
    lv_obj_t * label = lv_label_create(parent);
    dashboard_set_label_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

static lv_obj_t * create_segment(lv_obj_t * parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t * segment = lv_obj_create(parent);
    lv_obj_remove_style_all(segment);
    lv_obj_set_pos(segment, x, y);
    lv_obj_set_size(segment, w, h);
    lv_obj_set_style_radius(segment, 3, 0);
    lv_obj_set_style_bg_color(segment, UI_SEGMENT_OFF_COLOR, 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_40, 0);
    return segment;
}

static void update_lap_delta_ui(void);

static void update_slip_level_ui(void)
{
    char text_buf[16];

    if(g_dashboard.slip_value == NULL) return;
    lv_snprintf(text_buf, sizeof(text_buf), "SLIP %ld", (long)g_slip_level);
    dashboard_set_label_text(g_dashboard.slip_value, text_buf);
    for(uint32_t index = 0U; index < SLIP_BAR_COUNT; index++) {
        if(g_dashboard.slip_bars[index] == NULL) continue;
        lv_obj_set_style_bg_color(g_dashboard.slip_bars[index],
                                  (index < (uint32_t)g_slip_level) ?
                                  UI_TEXT_COLOR : UI_SEGMENT_OFF_COLOR, 0);
        lv_obj_set_style_bg_opa(g_dashboard.slip_bars[index], LV_OPA_COVER, 0);
    }
}

static void set_speed_digit_segment_state(lv_obj_t * segment, bool enabled)
{
    lv_obj_set_style_bg_color(segment, enabled ? UI_SEGMENT_ON_COLOR : UI_SEGMENT_OFF_COLOR, 0);
    lv_obj_set_style_bg_opa(segment, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void create_speed_digits(lv_obj_t * parent)
{
    const lv_coord_t digit_width = UI_SPEED_DIGIT_W;
    const lv_coord_t digit_height = UI_SPEED_DIGIT_H;
    const lv_coord_t segment_thickness = UI_SPEED_SEG_THICKNESS;
    const lv_coord_t digit_gap = UI_SPEED_DIGIT_GAP;
    const lv_coord_t mid_y = (digit_height - segment_thickness) / 2;
    const lv_coord_t bottom_y = digit_height - segment_thickness;

    g_dashboard.speed_digit_container = lv_obj_create(parent);
    lv_obj_remove_style_all(g_dashboard.speed_digit_container);
    lv_obj_set_size(g_dashboard.speed_digit_container,
                    (digit_width * 2) + digit_gap,
                    digit_height);
    lv_obj_align(g_dashboard.speed_digit_container, LV_ALIGN_CENTER, 0, -24);

    for(uint32_t digit_index = 0; digit_index < 2U; digit_index++) {
        lv_coord_t base_x = (lv_coord_t)digit_index * (digit_width + digit_gap);
        lv_obj_t ** segments = g_dashboard.speed_segments[digit_index];

        segments[0] = create_segment(g_dashboard.speed_digit_container, base_x + segment_thickness, 0,
                                     digit_width - (segment_thickness * 2), segment_thickness);
        segments[1] = create_segment(g_dashboard.speed_digit_container, base_x + digit_width - segment_thickness,
                                     segment_thickness, segment_thickness, mid_y - segment_thickness / 2);
        segments[2] = create_segment(g_dashboard.speed_digit_container, base_x + digit_width - segment_thickness,
                                     mid_y + segment_thickness / 2, segment_thickness,
                                     mid_y - segment_thickness / 2);
        segments[3] = create_segment(g_dashboard.speed_digit_container, base_x + segment_thickness, bottom_y,
                                     digit_width - (segment_thickness * 2), segment_thickness);
        segments[4] = create_segment(g_dashboard.speed_digit_container, base_x, mid_y + segment_thickness / 2,
                                     segment_thickness, mid_y - segment_thickness / 2);
        segments[5] = create_segment(g_dashboard.speed_digit_container, base_x, segment_thickness,
                                     segment_thickness, mid_y - segment_thickness / 2);
        segments[6] = create_segment(g_dashboard.speed_digit_container, base_x + segment_thickness, mid_y,
                                     digit_width - (segment_thickness * 2), segment_thickness);
    }
}

static void set_speed_digits(int32_t speed)
{
    static uint8_t segment_state[2][7] = {
        {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU},
        {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU}
    };
    uint8_t digits[2];

    if(speed < 0) speed = 0;
    if(speed > 99) speed = 99;

    digits[0] = (uint8_t)((speed / 10) % 10);
    digits[1] = (uint8_t)(speed % 10);

    for(uint32_t digit_index = 0; digit_index < 2U; digit_index++) {
        bool hide_digit = (digit_index == 0U) && (digits[digit_index] == 0U);

        for(uint32_t segment_index = 0; segment_index < 7U; segment_index++) {
            uint8_t enabled = hide_digit ? 0U : g_speed_digit_map[digits[digit_index]][segment_index];
            if(segment_state[digit_index][segment_index] == enabled) {
                continue;
            }
            segment_state[digit_index][segment_index] = enabled;
            set_speed_digit_segment_state(
                g_dashboard.speed_segments[digit_index][segment_index],
                enabled != 0U);
        }
    }
}

static void format_lap_time(char * buf, size_t buf_size, int32_t hundredths)
{
    int32_t minutes;
    int32_t seconds;
    int32_t centiseconds;

    if(hundredths < 0) hundredths = 0;

    minutes = hundredths / 6000;
    seconds = (hundredths / 100) % 60;
    centiseconds = hundredths % 100;
    lv_snprintf(buf, buf_size, "%ld:%02ld.%02ld", (long)minutes, (long)seconds, (long)centiseconds);
}

static void update_alert_ui(void)
{
    char alert_buf[48];

    if(g_dashboard.alert_label == NULL) {
        return;
    }

    if(g_sprint_ready_visible != 0U) {
        lv_obj_set_style_text_color(g_dashboard.alert_label, lv_palette_main(LV_PALETTE_GREEN), 0);
        dashboard_set_label_text(g_dashboard.alert_label, "READY");
        lv_obj_clear_flag(g_dashboard.alert_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_style_text_color(g_dashboard.alert_label, lv_palette_main(LV_PALETTE_RED), 0);

    if(g_alert_count == 0U) {
        dashboard_set_label_text(g_dashboard.alert_label, "");
        lv_obj_add_flag(g_dashboard.alert_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_snprintf(alert_buf, sizeof(alert_buf), "%u %s", (unsigned)g_alert_count, g_alert_queue[0]);
    dashboard_set_label_text(g_dashboard.alert_label, alert_buf);
    lv_obj_clear_flag(g_dashboard.alert_label, LV_OBJ_FLAG_HIDDEN);
}

static void dashboard_push_alert_local(const char * text)
{
    uint32_t text_len;

    if((text == NULL) || (text[0] == '\0')) {
        return;
    }

    for(uint32_t index = 0U; index < g_alert_count; index++) {
        if(strncmp(g_alert_queue[index], text, DASHBOARD_ALERT_TEXT_MAX) == 0) {
            update_alert_ui();
            return;
        }
    }

    if(g_alert_count >= DASHBOARD_ALERT_MAX) {
        for(uint32_t index = 1U; index < DASHBOARD_ALERT_MAX; index++) {
            (void)strncpy(g_alert_queue[index - 1U], g_alert_queue[index], DASHBOARD_ALERT_TEXT_MAX);
            g_alert_queue[index - 1U][DASHBOARD_ALERT_TEXT_MAX - 1U] = '\0';
        }
        g_alert_count = DASHBOARD_ALERT_MAX - 1U;
    }

    text_len = strlen(text);
    if(text_len >= DASHBOARD_ALERT_TEXT_MAX) {
        text_len = DASHBOARD_ALERT_TEXT_MAX - 1U;
    }
    (void)memcpy(g_alert_queue[g_alert_count], text, text_len);
    g_alert_queue[g_alert_count][text_len] = '\0';
    g_alert_count++;
    update_alert_ui();
}

static void dashboard_pop_alert(void)
{
    if(g_alert_count == 0U) {
        update_alert_ui();
        return;
    }

    for(uint32_t index = 1U; index < g_alert_count; index++) {
        (void)strncpy(g_alert_queue[index - 1U], g_alert_queue[index], DASHBOARD_ALERT_TEXT_MAX);
        g_alert_queue[index - 1U][DASHBOARD_ALERT_TEXT_MAX - 1U] = '\0';
    }

    g_alert_count--;
    if(g_alert_count < DASHBOARD_ALERT_MAX) {
        g_alert_queue[g_alert_count][0] = '\0';
    }
    update_alert_ui();
}

static void dashboard_clear_alerts(void)
{
    (void)memset(g_alert_queue, 0, sizeof(g_alert_queue));
    g_alert_count = 0U;
    update_alert_ui();
}

static void dashboard_reset_lap_ui(void)
{
    char buf[16];

    g_current_lap_time = 0;
    g_last_lap_time = 0;
    g_best_lap_time = 0;
    g_lap_delta = 0;
	g_pending_lap_delta = 0;
	g_lap_delta_dirty = 0U;
	g_pending_lap_current = 0;
	g_pending_lap_last = 0;
	g_pending_lap_best = 0;
	g_pending_lap_count = 0;
	g_lap_times_dirty = 0U;
    g_laps_current = 0;
    g_laps_left = (g_drive_mode == DRIVE_MODE_S) ? 75 : 0;
	g_pending_sprint_remaining_m = g_laps_left;
	g_sprint_remaining_dirty = 0U;

    format_lap_time(buf, sizeof(buf), g_best_lap_time);
    dashboard_set_label_text(g_dashboard.lap_best_value, buf);
    format_lap_time(buf, sizeof(buf), g_last_lap_time);
    dashboard_set_label_text(g_dashboard.lap_last_value, buf);
    format_lap_time(buf, sizeof(buf), g_current_lap_time);
    dashboard_set_label_text(g_dashboard.lap_current_value, buf);
    dashboard_set_label_text(g_dashboard.laps_current_value, "00");
    lv_snprintf(buf, sizeof(buf), "%02ld", (long)g_laps_left);
    dashboard_set_label_text(g_dashboard.laps_left_value, buf);
    update_lap_delta_ui();
}

static lv_color_t lap_diagnostic_color(GPS_LapDiagState_t state)
{
    switch(state) {
        case GPS_LAP_DIAG_ARMED:
            return lv_color_make(0x20, 0xD0, 0x50);
        case GPS_LAP_DIAG_APPROACHING:
            return lv_color_make(0xFF, 0xD0, 0x20);
        case GPS_LAP_DIAG_GATE_MISS:
            return lv_color_make(0xFF, 0x20, 0xD0);
        case GPS_LAP_DIAG_REVERSE_PASS:
            return lv_color_make(0x20, 0x80, 0xFF);
        case GPS_LAP_DIAG_CROSSED:
            return lv_color_make(0x20, 0xF0, 0xF0);
        case GPS_LAP_DIAG_WAIT_ARM:
        default:
            return lv_color_make(0xFF, 0x1A, 0x1A);
    }
}

static void update_lap_analysis_indicator(uint32_t now)
{
    GPS_LapDiagnostic_t diagnostic;

    if(g_dashboard.alert_circle == NULL) {
        return;
    }

    if((g_drive_mode == DRIVE_MODE_S) &&
       (g_lap_analysis_active != 0U) &&
       (GPS_Sprint_IsActive() == 0U)) {
        g_lap_analysis_active = 0U;
        g_lap_analysis_blink_visible = 0U;
    }

    if(g_lap_analysis_active == 0U) {
        g_lap_analysis_blink_visible = 0U;
        g_lap_analysis_indicator_state = (GPS_LapDiagState_t)0xFFU;
        if(g_lap_analysis_indicator_visible != 0U) {
            lv_obj_add_flag(g_dashboard.alert_circle, LV_OBJ_FLAG_HIDDEN);
            g_lap_analysis_indicator_visible = 0U;
        }
        return;
    }

    GPS_Lap_GetDiagnostic(&diagnostic);
    if(g_lap_analysis_indicator_state != diagnostic.state) {
        g_lap_analysis_indicator_state = diagnostic.state;
        lv_obj_set_style_bg_color(g_dashboard.alert_circle,
                                  lap_diagnostic_color(diagnostic.state), 0);
    }

    if((g_lap_analysis_blink_tick == 0U) || ((now - g_lap_analysis_blink_tick) >= 500U)) {
        g_lap_analysis_blink_tick = now;
        g_lap_analysis_blink_visible = (g_lap_analysis_blink_visible == 0U) ? 1U : 0U;
    }

    if(g_lap_analysis_indicator_visible == g_lap_analysis_blink_visible) {
        return;
    }

    g_lap_analysis_indicator_visible = g_lap_analysis_blink_visible;
    if(g_lap_analysis_indicator_visible != 0U) {
        lv_obj_clear_flag(g_dashboard.alert_circle, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(g_dashboard.alert_circle, LV_OBJ_FLAG_HIDDEN);
    }
}

static void dashboard_toggle_lap_analysis(void)
{
	GPS_Data_t gps_data;
    float line_heading;

    if((g_drive_mode == DRIVE_MODE_S) && (GPS_Sprint_IsActive() != 0U)) {
        GPS_Sprint_Cancel();
        g_lap_analysis_active = 0U;
        g_lap_analysis_blink_visible = 0U;
        update_lap_analysis_indicator(HAL_GetTick());
        Dashboard_UI_PushAlert("75m run cancelled");
        return;
    }

    if((g_drive_mode != DRIVE_MODE_S) && (g_lap_analysis_active != 0U)) {
        g_lap_analysis_active = 0U;
        g_lap_analysis_blink_visible = 0U;
        GPS_Lap_SetAnalysisActive(0U);
        update_lap_analysis_indicator(HAL_GetTick());
        return;
    }

    GPS_GetData(&gps_data);
	if(gps_data.valid == 0U) {
		Dashboard_UI_PushAlert("gps not fixed");
		return;
	}
    if(gps_data.signal_level <= GPS_SIG_LEVEL_NONE) {
        Dashboard_UI_PushAlert("gps no signal");
        return;
    }
	/* A valid dual-antenna heading is available even while stationary.  RMC
	 * course remains the fallback, so lap timing still works with one antenna. */
    if(gps_data.heading_valid != 0U) {
        line_heading = gps_data.heading_angle + GPS_HEADING_INSTALL_OFFSET_DEG;
        while(line_heading < 0.0f) line_heading += 360.0f;
        while(line_heading >= 360.0f) line_heading -= 360.0f;
    }
    else {
        if(gps_data.speed_kmh < LAP_START_MIN_SPEED_KMH) {
            Dashboard_UI_PushAlert("wait heading or move");
            return;
        }
        line_heading = gps_data.track_angle;
    }

    if(g_drive_mode == DRIVE_MODE_S) {
        if(gps_data.speed_kmh >= GPS_SPRINT_START_SPEED_KMH) {
            Dashboard_UI_PushAlert("stop before ready");
            return;
        }
        if(GPS_Sprint_StartAtCurrent(&gps_data, line_heading) == 0U) {
            Dashboard_UI_PushAlert("gps position invalid");
            return;
        }
        g_lap_analysis_active = 1U;
        g_lap_analysis_blink_visible = 1U;
        g_lap_analysis_blink_tick = HAL_GetTick();
        update_lap_analysis_indicator(g_lap_analysis_blink_tick);
        return;
    }

    GPS_Lap_Reset();
    GPS_Lap_SetStartLine(gps_data.latitude,
                         gps_data.longitude,
                         line_heading);
    GPS_Lap_SetFinishLine(gps_data.latitude,
                          gps_data.longitude,
                          line_heading);
    if(GPS_Lap_StartAtCurrent(&gps_data) == 0U) {
        Dashboard_UI_PushAlert("gps position invalid");
        return;
    }
    dashboard_reset_lap_ui();
    g_lap_analysis_active = 1U;
    g_lap_analysis_blink_visible = 1U;
    g_lap_analysis_blink_tick = HAL_GetTick();
    GPS_Lap_SetAnalysisActive(1U);
    update_lap_analysis_indicator(g_lap_analysis_blink_tick);
}

static void update_lap_delta_ui(void)
{
    char delta_buf[16];
    int32_t delta_abs;
    int32_t max_abs = DELTA_BAR_FULL_SCALE_HUNDREDTHS;
    lv_coord_t center_tick_x = DELTA_BAR_CENTER_X;
    lv_coord_t right_tick_x = DELTA_BAR_RIGHT_X;
    lv_coord_t fill_width;

    if((g_dashboard.delta_bar_fill == NULL) || (g_dashboard.delta_value == NULL)) {
        return;
    }

    delta_abs = g_lap_delta < 0 ? -g_lap_delta : g_lap_delta;
    if(g_lap_delta > 0) {
        lv_snprintf(delta_buf, sizeof(delta_buf), "+%ld.%02lds", (long)(delta_abs / 100), (long)(delta_abs % 100));
        lv_obj_set_style_text_color(g_dashboard.delta_value, lv_palette_main(LV_PALETTE_RED), 0);
    }
    else if(g_lap_delta < 0) {
        lv_snprintf(delta_buf, sizeof(delta_buf), "-%ld.%02lds", (long)(delta_abs / 100), (long)(delta_abs % 100));
        lv_obj_set_style_text_color(g_dashboard.delta_value, lv_palette_main(LV_PALETTE_GREEN), 0);
    }
    else {
        lv_snprintf(delta_buf, sizeof(delta_buf), "0.00s");
        lv_obj_set_style_text_color(g_dashboard.delta_value, UI_TEXT_COLOR, 0);
    }
    dashboard_set_label_text(g_dashboard.delta_value, delta_buf);

    if(delta_abs > max_abs) delta_abs = max_abs;
    fill_width = (lv_coord_t)((delta_abs * (right_tick_x - center_tick_x)) / max_abs);
    if((fill_width < 1) && (g_lap_delta != 0)) fill_width = 1;

    if(g_lap_delta < 0) {
        lv_obj_set_pos(g_dashboard.delta_bar_fill, center_tick_x, 0);
        lv_obj_set_size(g_dashboard.delta_bar_fill, fill_width, DELTA_BAR_FILL_H);
        lv_obj_set_style_bg_color(g_dashboard.delta_bar_fill, lv_palette_main(LV_PALETTE_GREEN), 0);
    }
    else if(g_lap_delta > 0) {
        lv_obj_set_pos(g_dashboard.delta_bar_fill, center_tick_x - fill_width, 0);
        lv_obj_set_size(g_dashboard.delta_bar_fill, fill_width, DELTA_BAR_FILL_H);
        lv_obj_set_style_bg_color(g_dashboard.delta_bar_fill, lv_palette_main(LV_PALETTE_RED), 0);
    }
    else {
        lv_obj_set_pos(g_dashboard.delta_bar_fill, center_tick_x, 0);
        lv_obj_set_size(g_dashboard.delta_bar_fill, 1, DELTA_BAR_FILL_H);
        lv_obj_set_style_bg_color(g_dashboard.delta_bar_fill, lv_color_hex(0x808080), 0);
    }
}

static void apply_drive_mode_ui(void)
{
    const char * mode_text = "S";
    lv_color_t tile_color = lv_palette_main(LV_PALETTE_RED);
    lv_color_t text_color = lv_color_hex(0xFFFFFF);

    switch(g_drive_mode) {
        case DRIVE_MODE_S:
            mode_text = "S";
            tile_color = lv_palette_main(LV_PALETTE_RED);
            break;
        case DRIVE_MODE_Q:
            mode_text = "Q";
            tile_color = lv_color_hex(0xFFD400);
            break;
        case DRIVE_MODE_C:
            mode_text = "C";
            tile_color = lv_palette_main(LV_PALETTE_BLUE);
            break;
        case DRIVE_MODE_E:
            mode_text = "E";
            tile_color = lv_palette_main(LV_PALETTE_GREEN);
            break;
        default:
            mode_text = "S";
            tile_color = lv_palette_main(LV_PALETTE_RED);
            break;
    }

    if(g_dashboard.mode_tile != NULL) {
        lv_obj_set_style_bg_color(g_dashboard.mode_tile, tile_color, 0);
    }
    if(g_dashboard.mode_value != NULL) {
        dashboard_set_label_text(g_dashboard.mode_value, mode_text);
        lv_obj_set_style_text_color(g_dashboard.mode_value, text_color, 0);
    }
    if(g_dashboard.laps_current_label != NULL) {
        dashboard_set_label_text(g_dashboard.laps_current_label,
                          (g_drive_mode == DRIVE_MODE_S) ? "Run" : "Lap");
    }
    if(g_dashboard.laps_left_label != NULL) {
        dashboard_set_label_text(g_dashboard.laps_left_label,
                          (g_drive_mode == DRIVE_MODE_S) ? "Dist" : "Left");
    }
}

static void sync_mode_from_index(void)
{
    switch(g_mode_index) {
        case 0:
            g_drive_mode = DRIVE_MODE_S;
            break;
        case 1:
            g_drive_mode = DRIVE_MODE_Q;
            break;
        case 2:
            g_drive_mode = DRIVE_MODE_C;
            break;
        case 3:
            g_drive_mode = DRIVE_MODE_E;
            break;
        default:
            g_drive_mode = DRIVE_MODE_S;
            break;
    }
}

static void apply_vehicle_motor_ui(uint32_t index);

static void apply_vehicle_ui(void)
{
    for(uint32_t index = 0U; index < 4U; index++) {
        apply_vehicle_motor_ui(index);
    }
}

static uint8_t dashboard_timing_mode_locked(void)
{
    if(g_drive_mode == DRIVE_MODE_S) {
        return GPS_Sprint_IsActive();
    }
    return g_lap_analysis_active;
}

static void dashboard_request_mode_lock_alert(void)
{
    g_pending_mode_lock_alert = 1U;
}

static void dashboard_handle_timing_mode_change(drive_mode_t previous_mode)
{
    if((previous_mode != DRIVE_MODE_S) && (g_drive_mode != DRIVE_MODE_S)) {
        apply_drive_mode_ui();
        return;
    }
    g_lap_analysis_active = 0U;
    g_lap_analysis_blink_visible = 0U;
    GPS_Lap_SetAnalysisActive(0U);
    GPS_Sprint_Cancel();
    if(g_drive_mode == DRIVE_MODE_S) {
        GPS_Sprint_Reset();
    }
    else {
        GPS_Lap_Reset();
    }
    dashboard_reset_lap_ui();
    apply_drive_mode_ui();
    update_lap_analysis_indicator(HAL_GetTick());
}

static void update_signal_bars(int32_t level)
{
    static const lv_color_t grey  = { 0x55, 0x55, 0x55 };
    static const lv_color_t red   = { 0x1A, 0x1A, 0xFF };
    static const lv_color_t yellow = { 0x00, 0xD4, 0xFF };
    static const lv_color_t green = { 0x5A, 0xD4, 0x2F };
    lv_color_t bar_color[4];

    if(level < 0) level = 0;
    if(level > 4) level = 4;
    switch(level) {
    case 0:
        bar_color[0] = grey; bar_color[1] = grey; bar_color[2] = grey; bar_color[3] = grey;
        break;
    case 1:
        bar_color[0] = red;  bar_color[1] = grey; bar_color[2] = grey; bar_color[3] = grey;
        break;
    case 2:
        bar_color[0] = yellow; bar_color[1] = yellow; bar_color[2] = grey; bar_color[3] = grey;
        break;
    case 3:
        bar_color[0] = green; bar_color[1] = green; bar_color[2] = green; bar_color[3] = grey;
        break;
    default:
        bar_color[0] = green; bar_color[1] = green; bar_color[2] = green; bar_color[3] = green;
        break;
    }
    for(uint32_t i = 0U; i < 4U; i++) {
        if(g_dashboard.signal_bars[i] != NULL) {
            lv_obj_set_style_bg_color(g_dashboard.signal_bars[i], bar_color[i], 0);
        }
    }
}

/* Format a signed deci-scaled value (e.g. cA -> A, mV -> V) with one decimal
 * and a unit suffix, keeping the minus sign out of the fractional part. */
static void format_deci_value(char * buf, size_t buf_size, int32_t deci, const char * unit)
{
    int32_t magnitude = deci < 0 ? -deci : deci;
    lv_snprintf(buf, buf_size, "%s%ld.%01ld%s",
                (deci < 0) ? "-" : "", (long)(magnitude / 10),
                (long)(magnitude % 10), unit);
}

static void dashboard_apply_data(uint32_t dirty_mask)
{
    static char text_buf[16];

    if((dirty_mask & DASH_DIRTY_BATTERY) != 0U) {
    g_soc = g_dashboard_data.soc;                     /* DBC BO_1200 BatterySOC */
    g_sum_voltage = g_dashboard_data.sum_voltage;     /* DBC BO_1200 BatteryVoltage (V) */
    g_sum_current = g_dashboard_data.sum_current;     /* DBC BO_1200 BatteryCurrent (A) */
    g_top_temperature = g_dashboard_data.max_temperature;  /* can.c: max motor/inverter/IGBT */
    }

    for(uint32_t index = 0; index < 4U; index++) {
        if((dirty_mask & (DASH_DIRTY_MOTOR_0 << index)) == 0U) continue;
        g_torque[index] = g_dashboard_data.torque[index];    /* DBC BO_1282 Debug2: ActualTorque */
        g_rpm[index] = g_dashboard_data.rpm[index];          /* DBC BO_1285 Debug5: ActualVelocity */
        g_motor_temp[index] = g_dashboard_data.motor_temp[index]; /* DBC BO_1286 Debug6: Motor_temperature */
    }
    if((dirty_mask & DASH_DIRTY_MOTOR_0) != 0U) g_motor_fl_online = g_dashboard_data.motor_enable[0] != 0U;
    if((dirty_mask & DASH_DIRTY_MOTOR_1) != 0U) g_motor_rl_online = g_dashboard_data.motor_enable[1] != 0U;
    if((dirty_mask & DASH_DIRTY_MOTOR_2) != 0U) g_motor_fr_online = g_dashboard_data.motor_enable[2] != 0U;
    if((dirty_mask & DASH_DIRTY_MOTOR_3) != 0U) g_motor_rr_online = g_dashboard_data.motor_enable[3] != 0U;

    if((dirty_mask & DASH_DIRTY_MODE) != 0U) {
        drive_mode_t previous_mode = g_drive_mode;
        g_mode_index = g_dashboard_data.mode_index;
        sync_mode_from_index();
        if(previous_mode != g_drive_mode) {
            dashboard_handle_timing_mode_change(previous_mode);
        }
        else {
            apply_drive_mode_ui();
        }
    }
    if((dirty_mask & DASH_DIRTY_SLIP) != 0U) {
        g_slip_level = g_dashboard_data.slip_level;
        if(g_slip_level < 0) g_slip_level = 0;
        if(g_slip_level > 7) g_slip_level = 7;
        update_slip_level_ui();
    }

    int32_t display_speed = g_speed;
    if(display_speed < 0) display_speed = 0;
    if(display_speed > 99) display_speed = 99;

    if((dirty_mask & DASH_DIRTY_ALL) == DASH_DIRTY_ALL) set_speed_digits(display_speed);

    if((dirty_mask & DASH_DIRTY_BATTERY) != 0U) {
    lv_snprintf(text_buf, sizeof(text_buf), "%ld%%", (long)g_soc);
    dashboard_set_label_text(g_dashboard.soc_value, text_buf);
    lv_obj_set_width(g_dashboard.battery_fill, g_soc == 0 ? 1 : (g_soc * 30 / 100));

    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_sum_voltage);
    dashboard_set_label_text(g_dashboard.total_voltage_value, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_sum_current);
    dashboard_set_label_text(g_dashboard.total_current_value, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_top_temperature);
    dashboard_set_label_text(g_dashboard.max_temp_value, text_buf);
    g_power_live = (g_sum_voltage * g_sum_current) / 1000;
    if(g_power_live < 0) g_power_live = 0;
    if(g_power_live > g_power_peak) g_power_peak = g_power_live;
    if(g_dashboard.power_live_value != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ldkW", (long)g_power_live);
        dashboard_set_label_text(g_dashboard.power_live_value, text_buf);
    }
    if(g_dashboard.power_peak_value != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ldkW", (long)g_power_peak);
        dashboard_set_label_text(g_dashboard.power_peak_value, text_buf);
    }

    }

    if(((dirty_mask & DASH_DIRTY_PEDALS) != 0U) && (g_dashboard.throttle_bar_fill != NULL)) {
        int32_t throttle_height = g_dashboard_data.aps_open_pct == 0 ? 1 : (g_dashboard_data.aps_open_pct * PEDAL_BAR_H / 100);
        lv_obj_set_pos(g_dashboard.throttle_bar_fill, 0, PEDAL_BAR_H - throttle_height);
        lv_obj_set_size(g_dashboard.throttle_bar_fill, PEDAL_BAR_W, throttle_height);
        if(g_dashboard.throttle_pct_label != NULL) {
            lv_snprintf(text_buf, sizeof(text_buf), "%ld%%", (long)g_dashboard_data.aps_open_pct);
            dashboard_set_label_text(g_dashboard.throttle_pct_label, text_buf);
        }
    }
    if(((dirty_mask & DASH_DIRTY_PEDALS) != 0U) && (g_dashboard.brake_bar_fill != NULL)) {
        int32_t brake_height = g_dashboard_data.brake_pct == 0 ? 1 : (g_dashboard_data.brake_pct * PEDAL_BAR_H / 100);
        lv_obj_set_pos(g_dashboard.brake_bar_fill, 0, PEDAL_BAR_H - brake_height);
        lv_obj_set_size(g_dashboard.brake_bar_fill, PEDAL_BAR_W, brake_height);
        if(g_dashboard.brake_pct_label != NULL) {
            lv_snprintf(text_buf, sizeof(text_buf), "%ld%%", (long)g_dashboard_data.brake_pct);
            dashboard_set_label_text(g_dashboard.brake_pct_label, text_buf);
        }
    }

    if((dirty_mask & DASH_DIRTY_LOWVOLTAGE) != 0U) {
        /* DBC BO_1440 PDM_LowVoltageBus: mV/cA/dW raw engineering units */
        if(g_dashboard.lv_voltage_value != NULL) {
            format_deci_value(text_buf, sizeof(text_buf),
                              g_dashboard_data.lv_bus_voltage_mV / 100, "V");
            dashboard_set_label_text(g_dashboard.lv_voltage_value, text_buf);
        }
        if(g_dashboard.lv_current_value != NULL) {
            format_deci_value(text_buf, sizeof(text_buf),
                              g_dashboard_data.lv_bus_current_cA, "A");
            dashboard_set_label_text(g_dashboard.lv_current_value, text_buf);
        }
        if(g_dashboard.lv_power_value != NULL) {
            lv_snprintf(text_buf, sizeof(text_buf), "%ldW",
                        (long)(g_dashboard_data.lv_bus_power_dW / 10));
            dashboard_set_label_text(g_dashboard.lv_power_value, text_buf);
        }
    }

    if((dirty_mask & DASH_DIRTY_FANS) != 0U) {
        /* DBC BO_1442: three RPM values, two measured duty cycles; fan 3 has
         * no independent duty signal in the DBC, so its slot shows "--". */
        for(uint32_t fan = 0U; fan < 3U; fan++) {
            if(g_dashboard.fan_value_labels[fan] == NULL) continue;
            const char * duty_text = (fan < 2U) ? NULL : "--";
            char duty_buf[8];
            if(duty_text == NULL) {
                lv_snprintf(duty_buf, sizeof(duty_buf), "%ld%%",
                            (long)g_dashboard_data.fan_pwm_duty[fan]);
                duty_text = duty_buf;
            }
            lv_snprintf(text_buf, sizeof(text_buf), "%s %ld",
                        duty_text, (long)g_dashboard_data.fan_rpm[fan]);
            dashboard_set_label_text(g_dashboard.fan_value_labels[fan], text_buf);
        }
    }

    if((dirty_mask & DASH_DIRTY_MOTORS) != 0U) {
    g_motor_power_live[0] = (g_torque[0] * g_rpm[0]) / 12000;
    g_motor_power_live[1] = (g_torque[1] * g_rpm[1]) / 12000;
    g_motor_power_live[2] = (g_torque[2] * g_rpm[2]) / 12000;
    g_motor_power_live[3] = (g_torque[3] * g_rpm[3]) / 12000;
    for(uint32_t i = 0; i < 4U; i++) {
        if(g_motor_power_live[i] <= 0) {
            /* Power dropped to zero (or regen): reset the peak so it tracks
             * the current drive cycle, not a stale historical max. */
            g_motor_power_peak[i] = 0;
        } else if(g_motor_power_live[i] > g_motor_power_peak[i]) {
            g_motor_power_peak[i] = g_motor_power_live[i];
        }
    }

    if((dirty_mask & DASH_DIRTY_MOTOR_0) != 0U) {
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_torque[0]);
    dashboard_set_label_text(g_dashboard.motor_fl_torque, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_rpm[0]);
    dashboard_set_label_text(g_dashboard.motor_fl_speed, text_buf);
    if(g_dashboard.motor_fl_power_live != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_power_live[0]);
        dashboard_set_label_text(g_dashboard.motor_fl_power_live, text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_power_peak[0]);
        dashboard_set_label_text(g_dashboard.motor_fl_power_peak, text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_temp[0]);
        dashboard_set_label_text(g_dashboard.motor_fl_temp, text_buf);
    }
    if(g_dashboard.motor_fl_inverter != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_dashboard_data.inverter_temp[0]);
        dashboard_set_label_text(g_dashboard.motor_fl_inverter, text_buf);
        lv_obj_set_style_text_color(g_dashboard.motor_fl_inverter,
            temp_to_color(g_dashboard_data.inverter_temp[0] * 100), 0);
    }
    if(g_dashboard.motor_fl_error != NULL) {
        if(g_dashboard_data.diag_num[0] != 0U) {
            lv_snprintf(text_buf, sizeof(text_buf), "0x%04lX",
                        (long)(g_dashboard_data.diag_num[0] & 0xFFFFU));
            lv_obj_set_style_text_color(g_dashboard.motor_fl_error,
                                        lv_palette_main(LV_PALETTE_RED), 0);
        }
        else {
            lv_snprintf(text_buf, sizeof(text_buf), "OK");
            lv_obj_set_style_text_color(g_dashboard.motor_fl_error,
                                        lv_palette_main(LV_PALETTE_GREY), 0);
        }
        dashboard_set_label_text(g_dashboard.motor_fl_error, text_buf);
    }
    }
    if((dirty_mask & DASH_DIRTY_MOTOR_1) != 0U) {
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_torque[1]);
    dashboard_set_label_text(g_dashboard.motor_rl_torque, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_rpm[1]);
    dashboard_set_label_text(g_dashboard.motor_rl_speed, text_buf);
    if(g_dashboard.motor_rl_power_live != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_power_live[1]);
        dashboard_set_label_text(g_dashboard.motor_rl_power_live, text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_power_peak[1]);
        dashboard_set_label_text(g_dashboard.motor_rl_power_peak, text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_temp[1]);
        dashboard_set_label_text(g_dashboard.motor_rl_temp, text_buf);
    }
    if(g_dashboard.motor_rl_inverter != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_dashboard_data.inverter_temp[1]);
        dashboard_set_label_text(g_dashboard.motor_rl_inverter, text_buf);
        lv_obj_set_style_text_color(g_dashboard.motor_rl_inverter,
            temp_to_color(g_dashboard_data.inverter_temp[1] * 100), 0);
    }
    if(g_dashboard.motor_rl_error != NULL) {
        if(g_dashboard_data.diag_num[1] != 0U) {
            lv_snprintf(text_buf, sizeof(text_buf), "0x%04lX",
                        (long)(g_dashboard_data.diag_num[1] & 0xFFFFU));
            lv_obj_set_style_text_color(g_dashboard.motor_rl_error,
                                        lv_palette_main(LV_PALETTE_RED), 0);
        }
        else {
            lv_snprintf(text_buf, sizeof(text_buf), "OK");
            lv_obj_set_style_text_color(g_dashboard.motor_rl_error,
                                        lv_palette_main(LV_PALETTE_GREY), 0);
        }
        dashboard_set_label_text(g_dashboard.motor_rl_error, text_buf);
    }
    }
    if((dirty_mask & DASH_DIRTY_MOTOR_2) != 0U) {
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_torque[2]);
    dashboard_set_label_text(g_dashboard.motor_fr_torque, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_rpm[2]);
    dashboard_set_label_text(g_dashboard.motor_fr_speed, text_buf);
    if(g_dashboard.motor_fr_power_live != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_power_live[2]);
        dashboard_set_label_text(g_dashboard.motor_fr_power_live, text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_power_peak[2]);
        dashboard_set_label_text(g_dashboard.motor_fr_power_peak, text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_temp[2]);
        dashboard_set_label_text(g_dashboard.motor_fr_temp, text_buf);
    }
    if(g_dashboard.motor_fr_inverter != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_dashboard_data.inverter_temp[2]);
        dashboard_set_label_text(g_dashboard.motor_fr_inverter, text_buf);
        lv_obj_set_style_text_color(g_dashboard.motor_fr_inverter,
            temp_to_color(g_dashboard_data.inverter_temp[2] * 100), 0);
    }
    if(g_dashboard.motor_fr_error != NULL) {
        if(g_dashboard_data.diag_num[2] != 0U) {
            lv_snprintf(text_buf, sizeof(text_buf), "0x%04lX",
                        (long)(g_dashboard_data.diag_num[2] & 0xFFFFU));
            lv_obj_set_style_text_color(g_dashboard.motor_fr_error,
                                        lv_palette_main(LV_PALETTE_RED), 0);
        }
        else {
            lv_snprintf(text_buf, sizeof(text_buf), "OK");
            lv_obj_set_style_text_color(g_dashboard.motor_fr_error,
                                        lv_palette_main(LV_PALETTE_GREY), 0);
        }
        dashboard_set_label_text(g_dashboard.motor_fr_error, text_buf);
    }
    }
    if((dirty_mask & DASH_DIRTY_MOTOR_3) != 0U) {
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_torque[3]);
    dashboard_set_label_text(g_dashboard.motor_rr_torque, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_rpm[3]);
    dashboard_set_label_text(g_dashboard.motor_rr_speed, text_buf);
    if(g_dashboard.motor_rr_power_live != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_power_live[3]);
        dashboard_set_label_text(g_dashboard.motor_rr_power_live, text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_power_peak[3]);
        dashboard_set_label_text(g_dashboard.motor_rr_power_peak, text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_motor_temp[3]);
        dashboard_set_label_text(g_dashboard.motor_rr_temp, text_buf);
    }
    if(g_dashboard.motor_rr_inverter != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%ld", (long)g_dashboard_data.inverter_temp[3]);
        dashboard_set_label_text(g_dashboard.motor_rr_inverter, text_buf);
        lv_obj_set_style_text_color(g_dashboard.motor_rr_inverter,
            temp_to_color(g_dashboard_data.inverter_temp[3] * 100), 0);
    }
    if(g_dashboard.motor_rr_error != NULL) {
        if(g_dashboard_data.diag_num[3] != 0U) {
            lv_snprintf(text_buf, sizeof(text_buf), "0x%04lX",
                        (long)(g_dashboard_data.diag_num[3] & 0xFFFFU));
            lv_obj_set_style_text_color(g_dashboard.motor_rr_error,
                                        lv_palette_main(LV_PALETTE_RED), 0);
        }
        else {
            lv_snprintf(text_buf, sizeof(text_buf), "OK");
            lv_obj_set_style_text_color(g_dashboard.motor_rr_error,
                                        lv_palette_main(LV_PALETTE_GREY), 0);
        }
        dashboard_set_label_text(g_dashboard.motor_rr_error, text_buf);
    }
    }

    for(uint32_t i = 0U; i < 4U; i++) {
        if(((dirty_mask & (DASH_DIRTY_MOTOR_0 << i)) != 0U) ||
           ((dirty_mask & (DASH_DIRTY_TIRE_0 << i)) != 0U)) {
            apply_vehicle_motor_ui(i);
        }
    }
    }

    if((dirty_mask & DASH_DIRTY_ODOMETER) != 0U) {
        int32_t odo_tenths = g_dashboard_data.odometer_tenths;
        int32_t odo_int = odo_tenths / 10;
        int32_t odo_frac = odo_tenths % 10;
        if(odo_frac < 0) { odo_frac = -odo_frac; odo_int = -odo_int; }
        if(odo_int > 999) odo_int = 999;
        lv_snprintf(text_buf, sizeof(text_buf), "%ld.%01ld km", (long)odo_int, (long)odo_frac);
        dashboard_set_label_text(g_dashboard.odometer_label, text_buf);
    }

}

static void apply_vehicle_motor_ui(uint32_t index)
{
    static int32_t last_tire_temperatures[4][4];
    static uint8_t last_online[4];
    static uint8_t initialized_mask;
    uint8_t changed = (initialized_mask & (1U << index)) == 0U;
    lv_obj_t * lightning;
    lv_obj_t * wheel;
    bool online;
    int32_t maximum_temp;
    char text_buf[12];

    switch(index) {
        case 0U:
            lightning = g_dashboard.lightning_fl;
            wheel = g_dashboard.wheel_fl;
            online = g_motor_fl_online;
            break;
        case 1U:
            lightning = g_dashboard.lightning_rl;
            wheel = g_dashboard.wheel_rl;
            online = g_motor_rl_online;
            break;
        case 2U:
            lightning = g_dashboard.lightning_fr;
            wheel = g_dashboard.wheel_fr;
            online = g_motor_fr_online;
            break;
        default:
            lightning = g_dashboard.lightning_rr;
            wheel = g_dashboard.wheel_rr;
            online = g_motor_rr_online;
            break;
    }

    for(uint32_t segment = 0U; segment < 4U; segment++) {
        if(last_tire_temperatures[index][segment] != g_tire_temp[index][segment]) changed = 1U;
        last_tire_temperatures[index][segment] = g_tire_temp[index][segment];
    }
    if(last_online[index] != online) changed = 1U;
    last_online[index] = online;
    initialized_mask |= (uint8_t)(1U << index);
    if(changed == 0U) return;

    maximum_temp = g_tire_temp[index][0];
    for(uint32_t segment = 0U; segment < 4U; segment++) {
        if(g_tire_temp[index][segment] > maximum_temp) {
            maximum_temp = g_tire_temp[index][segment];
        }
    }
    lv_obj_invalidate(wheel);
    /* Keep the compact UI label integer-only; the stored value remains centi-degrees. */
    lv_snprintf(text_buf, sizeof(text_buf), "%ld°", (long)((maximum_temp + 50) / 100));
    bool label_changed = strcmp(lv_label_get_text(g_dashboard.tire_max_labels[index]), text_buf) != 0;
    dashboard_set_label_text(g_dashboard.tire_max_labels[index], text_buf);
    if(label_changed) {
        lv_coord_t label_x;
        lv_coord_t label_max_x;

        /* Re-anchor both sides after every text change, then clamp the label to
         * the 180 px vehicle panel so three-digit temperatures remain visible. */
        lv_obj_update_layout(g_dashboard.tire_max_labels[index]);
        lv_obj_align_to(g_dashboard.tire_max_labels[index], wheel,
                        (index < 2U) ? LV_ALIGN_OUT_LEFT_MID : LV_ALIGN_OUT_RIGHT_MID,
                        (index < 2U) ? -2 : 2, UI_TIRE_TEMP_LABEL_Y);
        label_x = lv_obj_get_x(g_dashboard.tire_max_labels[index]);
        label_max_x = UI_LEFT_PANEL_WIDTH - 1 -
                      lv_obj_get_width(g_dashboard.tire_max_labels[index]);
        if(label_max_x < 1) label_max_x = 1;
        if(label_x < 1) lv_obj_set_x(g_dashboard.tire_max_labels[index], 1);
        else if(label_x > label_max_x) {
            lv_obj_set_x(g_dashboard.tire_max_labels[index], label_max_x);
        }
    }
    if(online) lv_obj_clear_flag(lightning, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lightning, LV_OBJ_FLAG_HIDDEN);
}

void Dashboard_UI_SubmitData(const dashboard_data_t * data)
{
    uint32_t index;
    uint32_t fan;
    uint32_t dirty_mask = 0U;
    uint32_t primask;

    if(data == NULL) {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if((g_pending_dashboard_data.soc != data->soc) ||
       (g_pending_dashboard_data.sum_voltage != data->sum_voltage) ||
       (g_pending_dashboard_data.sum_current != data->sum_current) ||
       (g_pending_dashboard_data.max_temperature != data->max_temperature)) {
        dirty_mask |= DASH_DIRTY_BATTERY;
    }
    if((g_pending_dashboard_data.aps_open_pct != data->aps_open_pct) ||
       (g_pending_dashboard_data.brake_pct != data->brake_pct)) {
        dirty_mask |= DASH_DIRTY_PEDALS;
    }
    if((g_pending_dashboard_data.lv_bus_voltage_mV != data->lv_bus_voltage_mV) ||
       (g_pending_dashboard_data.lv_bus_current_cA != data->lv_bus_current_cA) ||
       (g_pending_dashboard_data.lv_bus_power_dW != data->lv_bus_power_dW)) {
        dirty_mask |= DASH_DIRTY_LOWVOLTAGE;
    }
    {
        uint32_t fan;
        for(fan = 0U; fan < 3U; fan++) {
            if(g_pending_dashboard_data.fan_rpm[fan] != data->fan_rpm[fan]) {
                dirty_mask |= DASH_DIRTY_FANS;
                break;
            }
        }
        if((dirty_mask & DASH_DIRTY_FANS) == 0U) {
            for(fan = 0U; fan < 2U; fan++) {
                if(g_pending_dashboard_data.fan_pwm_duty[fan] != data->fan_pwm_duty[fan]) {
                    dirty_mask |= DASH_DIRTY_FANS;
                    break;
                }
            }
        }
    }
    for(index = 0U; index < 4U; index++) {
        if((g_pending_dashboard_data.torque[index] != data->torque[index]) ||
           (g_pending_dashboard_data.motor_enable[index] != data->motor_enable[index]) ||
           (g_pending_dashboard_data.rpm[index] != data->rpm[index]) ||
           (g_pending_dashboard_data.motor_temp[index] != data->motor_temp[index]) ||
           (g_pending_dashboard_data.inverter_temp[index] != data->inverter_temp[index]) ||
           (g_pending_dashboard_data.diag_num[index] != data->diag_num[index])) {
            dirty_mask |= (DASH_DIRTY_MOTOR_0 << index);
        }
    }

    if(dirty_mask == 0U) {
        __set_PRIMASK(primask);
        return;
    }

    g_pending_dashboard_data.soc = data->soc;
    g_pending_dashboard_data.sum_voltage = data->sum_voltage;
    g_pending_dashboard_data.sum_current = data->sum_current;
    g_pending_dashboard_data.max_temperature = data->max_temperature;
    g_pending_dashboard_data.aps_open_pct = data->aps_open_pct;
    g_pending_dashboard_data.lv_bus_voltage_mV = data->lv_bus_voltage_mV;
    g_pending_dashboard_data.lv_bus_current_cA = data->lv_bus_current_cA;
    g_pending_dashboard_data.lv_bus_power_dW = data->lv_bus_power_dW;
    for(fan = 0U; fan < 3U; fan++) {
        g_pending_dashboard_data.fan_rpm[fan] = data->fan_rpm[fan];
    }
    for(fan = 0U; fan < 2U; fan++) {
        g_pending_dashboard_data.fan_pwm_duty[fan] = data->fan_pwm_duty[fan];
    }
    for(index = 0; index < 4U; index++) {
        g_pending_dashboard_data.torque[index] = data->torque[index];
        g_pending_dashboard_data.motor_enable[index] = data->motor_enable[index];
        g_pending_dashboard_data.rpm[index] = data->rpm[index];
        g_pending_dashboard_data.motor_temp[index] = data->motor_temp[index];
        g_pending_dashboard_data.inverter_temp[index] = data->inverter_temp[index];
        g_pending_dashboard_data.diag_num[index] = data->diag_num[index];
    }
    g_pending_dashboard_data.alert_active = data->alert_active;
    g_pending_dashboard_data.brake_pct = data->brake_pct;

    g_dashboard_dirty_mask |= dirty_mask;
    __set_PRIMASK(primask);
}

void Dashboard_UI_SubmitTireTemperatures(uint32_t wheel_index,
                                         const int32_t temperatures[4])
{
    int32_t temperatures_centi[4];

    if((wheel_index >= 4U) || (temperatures == NULL)) return;
    for(uint32_t segment = 0U; segment < 4U; segment++) {
        int32_t value = temperatures[segment];
        if(value < -99) value = -99;
        if(value > 200) value = 200;
        temperatures_centi[segment] = value * 100;
    }
    Dashboard_UI_SubmitTireTemperaturesCenti(wheel_index, temperatures_centi);
}

void Dashboard_UI_SubmitTireTemperaturesCenti(uint32_t wheel_index,
                                              const int32_t temperatures_centi[4])
{
    int32_t clamped[4];
    uint32_t primask;
    uint32_t dirty_bit;
    uint8_t changed = 0U;

    if((wheel_index >= 4U) || (temperatures_centi == NULL)) return;
    for(uint32_t segment = 0U; segment < 4U; segment++) {
        clamped[segment] = temperatures_centi[segment];
        if(clamped[segment] < -9900) clamped[segment] = -9900;
        if(clamped[segment] > 20000) clamped[segment] = 20000;
    }

    dirty_bit = DASH_DIRTY_TIRE_0 << wheel_index;
    primask = __get_PRIMASK();
    __disable_irq();
    for(uint32_t segment = 0U; segment < 4U; segment++) {
        int32_t current = ((g_dashboard_dirty_mask & dirty_bit) != 0U) ?
                          g_pending_tire_temp[wheel_index][segment] :
                          g_tire_temp[wheel_index][segment];
        if(current != clamped[segment]) changed = 1U;
        g_pending_tire_temp[wheel_index][segment] = clamped[segment];
    }
    if(changed != 0U) g_dashboard_dirty_mask |= dirty_bit;
    if(primask == 0U) __enable_irq();
}

void Dashboard_UI_SubmitSignalLevel(int32_t level)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if(level < 0) level = 0;
    if(level > 4) level = 4;
    if(((g_signal_dirty != 0U) && (g_pending_signal_level == level)) ||
       ((g_signal_dirty == 0U) && (g_dashboard_data.signal_level == level))) {
        __set_PRIMASK(primask);
        return;
    }
    g_pending_signal_level = level;
    g_signal_dirty = 1U;
    __set_PRIMASK(primask);
}

void Dashboard_UI_SubmitSpeed(int32_t speed)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if(speed < 0) speed = 0;
    if(speed > 300) speed = 300;
    if((g_speed_dirty != 0U) && (g_pending_speed == speed)) {
        __set_PRIMASK(primask);
        return;
    }
    g_pending_speed = speed;
    g_speed_dirty = 1U;
    __set_PRIMASK(primask);
}

void Dashboard_UI_SubmitOdometer(int32_t odometer_tenths)
{
    uint32_t primask;

    if(odometer_tenths < 0) odometer_tenths = 0;

    primask = __get_PRIMASK();
    __disable_irq();
    if(((g_dashboard_dirty_mask & DASH_DIRTY_ODOMETER) == 0U) &&
       (g_dashboard_data.odometer_tenths == odometer_tenths)) {
        if(primask == 0U) __enable_irq();
        return;
    }
    if(g_pending_dashboard_data.odometer_tenths != odometer_tenths) {
        g_pending_dashboard_data.odometer_tenths = odometer_tenths;
        g_dashboard_dirty_mask |= DASH_DIRTY_ODOMETER;
    }
    if(primask == 0U) __enable_irq();
}

void Dashboard_UI_SubmitDriveMode(int32_t mode_index)
{
    uint32_t primask;

    if((mode_index < 0) || (mode_index > 3)) return;
    if(mode_index == g_mode_index) return;
    if(dashboard_timing_mode_locked() != 0U) {
        dashboard_request_mode_lock_alert();
        return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if(((g_dashboard_dirty_mask & DASH_DIRTY_MODE) == 0U) &&
       (g_dashboard_data.mode_index == mode_index)) {
        if(primask == 0U) __enable_irq();
        return;
    }
    g_pending_dashboard_data.mode_index = mode_index;
    g_dashboard_dirty_mask |= DASH_DIRTY_MODE;
    if(primask == 0U) __enable_irq();
}

uint8_t Dashboard_UI_IsTimingModeLocked(void)
{
    return dashboard_timing_mode_locked();
}

void Dashboard_UI_SubmitSlipLevel(int32_t slip_level)
{
    uint32_t primask;

    if((slip_level < 0) || (slip_level > 7)) return;
    primask = __get_PRIMASK();
    __disable_irq();
    if(((g_dashboard_dirty_mask & DASH_DIRTY_SLIP) == 0U) &&
       (g_dashboard_data.slip_level == slip_level)) {
        if(primask == 0U) __enable_irq();
        return;
    }
    g_pending_dashboard_data.slip_level = slip_level;
    g_dashboard_dirty_mask |= DASH_DIRTY_SLIP;
    if(primask == 0U) __enable_irq();
}

void Dashboard_UI_RequestLapToggle(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_pending_lap_toggle = 1U;
    if(primask == 0U) __enable_irq();
}

void Dashboard_UI_RequestAlertClear(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_pending_alert_clear = 1U;
    if(primask == 0U) __enable_irq();
}

void Dashboard_UI_SubmitLapDelta(int32_t delta_hundredths)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if(delta_hundredths > DELTA_TEXT_LIMIT_HUNDREDTHS) {
        delta_hundredths = DELTA_TEXT_LIMIT_HUNDREDTHS;
    }
    if(delta_hundredths < -DELTA_TEXT_LIMIT_HUNDREDTHS) {
        delta_hundredths = -DELTA_TEXT_LIMIT_HUNDREDTHS;
    }
	if(((g_lap_delta_dirty != 0U) && (g_pending_lap_delta == delta_hundredths)) ||
	   ((g_lap_delta_dirty == 0U) && (g_lap_delta == delta_hundredths))) {
		__set_PRIMASK(primask);
        return;
	}
    g_pending_lap_delta = delta_hundredths;
    g_lap_delta_dirty = 1U;
    __set_PRIMASK(primask);
}

void Dashboard_UI_SubmitLapTimes(int32_t current_hundredths,
								 int32_t last_hundredths,
								 int32_t best_hundredths,
								 int32_t lap_count)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
	if((g_pending_lap_current == current_hundredths) &&
	   (g_pending_lap_last == last_hundredths) &&
	   (g_pending_lap_best == best_hundredths) &&
	   (g_pending_lap_count == lap_count)) {
		__set_PRIMASK(primask);
        return;
	}
	g_pending_lap_current = current_hundredths;
	g_pending_lap_last = last_hundredths;
	g_pending_lap_best = best_hundredths;
	g_pending_lap_count = lap_count;
	g_lap_times_dirty = 1U;
    __set_PRIMASK(primask);
}

void Dashboard_UI_SubmitSprintRemaining(int32_t remaining_m)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if(remaining_m < 0) remaining_m = 0;
    if(remaining_m > 75) remaining_m = 75;
    if(((g_sprint_remaining_dirty != 0U) &&
        (g_pending_sprint_remaining_m == remaining_m)) ||
       ((g_sprint_remaining_dirty == 0U) && (g_laps_left == remaining_m))) {
        __set_PRIMASK(primask);
        return;
    }
    g_pending_sprint_remaining_m = remaining_m;
    g_sprint_remaining_dirty = 1U;
    __set_PRIMASK(primask);
}

void Dashboard_UI_SetSprintReady(uint8_t ready)
{
    uint32_t primask;

    ready = (ready != 0U) ? 1U : 0U;
    primask = __get_PRIMASK();
    __disable_irq();
    if(((g_sprint_ready_dirty != 0U) && (g_pending_sprint_ready == ready)) ||
       ((g_sprint_ready_dirty == 0U) && (g_sprint_ready_visible == ready))) {
        if(primask == 0U) {
            __enable_irq();
        }
        return;
    }
    g_pending_sprint_ready = ready;
    g_sprint_ready_dirty = 1U;
    if(primask == 0U) {
        __enable_irq();
    }
}

void Dashboard_UI_PushAlert(const char * text)
{
    uint32_t primask;
    uint32_t text_len;

    if((text == NULL) || (text[0] == '\0')) {
        return;
    }

    text_len = strlen(text);
    if(text_len >= DASHBOARD_ALERT_TEXT_MAX) {
        text_len = DASHBOARD_ALERT_TEXT_MAX - 1U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if(g_pending_alert_count >= DASHBOARD_ALERT_MAX) {
        for(uint32_t index = 1U; index < DASHBOARD_ALERT_MAX; index++) {
            (void)memcpy(g_pending_alert_queue[index - 1U],
                         g_pending_alert_queue[index],
                         DASHBOARD_ALERT_TEXT_MAX);
        }
        g_pending_alert_count = DASHBOARD_ALERT_MAX - 1U;
    }
    (void)memcpy(g_pending_alert_queue[g_pending_alert_count], text, text_len);
    g_pending_alert_queue[g_pending_alert_count][text_len] = '\0';
    g_pending_alert_count++;
    if(primask == 0U) {
        __enable_irq();
    }
}

void Dashboard_UI_Process(void)
{
    uint32_t index;
    uint32_t primask;
    int32_t pending_speed;
    int32_t pending_signal_level;
    char pending_alert_queue[DASHBOARD_ALERT_MAX][DASHBOARD_ALERT_TEXT_MAX];
    char pending_alert_text[DASHBOARD_ALERT_TEXT_MAX];
    uint8_t pending_alert_count;
    uint32_t now;
    uint32_t dashboard_mask = 0U;

    now = HAL_GetTick();
    update_lap_analysis_indicator(now);
    GPS_Lap_Tick();
    GPS_Sprint_Tick();

	if((g_dashboard_dirty_mask == 0U) && (g_speed_dirty == 0U) &&
	   (g_signal_dirty == 0U) && (g_lap_delta_dirty == 0U) &&
	   (g_lap_times_dirty == 0U) && (g_sprint_remaining_dirty == 0U) &&
	   (g_sprint_ready_dirty == 0U) &&
	   (g_pending_alert_count == 0U) && (g_pending_alert_pop_count == 0U) &&
	   (g_pending_lap_toggle == 0U) && (g_pending_night_toggle == 0U) &&
       (g_pending_alert_clear == 0U) && (g_pending_mode_lock_alert == 0U)) {
        return;
    }

    if(g_pending_night_toggle != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        g_pending_night_toggle = 0U;
        if(primask == 0U) {
            __enable_irq();
        }
        g_night_mode = (g_night_mode == 0U) ? 1U : 0U;
        update_night_mode_ui();
    }

    if(g_pending_alert_clear != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        g_pending_alert_clear = 0U;
        g_pending_alert_count = 0U;
        g_pending_alert_pop_count = 0U;
        if(primask == 0U) {
            __enable_irq();
        }
        dashboard_clear_alerts();
    }

    if(g_pending_mode_lock_alert != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        g_pending_mode_lock_alert = 0U;
        if(primask == 0U) {
            __enable_irq();
        }
        if((g_mode_lock_alert_last_tick == 0U) ||
           ((now - g_mode_lock_alert_last_tick) >= 1000U)) {
            g_mode_lock_alert_last_tick = now;
            dashboard_push_alert_local("MODE LOCK: TIMER ACTIVE");
        }
    }

    if(g_signal_dirty != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        pending_signal_level = g_pending_signal_level;
        g_signal_dirty = 0U;
        if(primask == 0U) {
            __enable_irq();
        }

        if(pending_signal_level < 0) pending_signal_level = 0;
        if(pending_signal_level > 4) pending_signal_level = 4;
        g_dashboard_data.signal_level = pending_signal_level;
        update_signal_bars(pending_signal_level);
    }

    if(g_pending_lap_toggle != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        g_pending_lap_toggle = 0U;
        if(primask == 0U) {
            __enable_irq();
        }

        dashboard_toggle_lap_analysis();
    }

    if(g_sprint_ready_dirty != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        g_sprint_ready_visible = g_pending_sprint_ready;
        g_sprint_ready_dirty = 0U;
        if(primask == 0U) {
            __enable_irq();
        }
        update_alert_ui();
    }

    if(g_sprint_remaining_dirty != 0U) {
        int32_t remaining_m;
        char distance_buf[8];
        primask = __get_PRIMASK();
        __disable_irq();
        remaining_m = g_pending_sprint_remaining_m;
        g_sprint_remaining_dirty = 0U;
        if(primask == 0U) {
            __enable_irq();
        }
        if(remaining_m < 0) remaining_m = 0;
        if(remaining_m > 75) remaining_m = 75;
        g_laps_left = remaining_m;
        if(g_drive_mode == DRIVE_MODE_S) {
            lv_snprintf(distance_buf, sizeof(distance_buf), "%02ld", (long)remaining_m);
            dashboard_set_label_text(g_dashboard.laps_left_value, distance_buf);
        }
    }

    while(g_pending_alert_pop_count != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        g_pending_alert_pop_count--;
        if(primask == 0U) {
            __enable_irq();
        }

        dashboard_pop_alert();
    }

    if(g_pending_alert_count != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        pending_alert_count = g_pending_alert_count;
        if(pending_alert_count > DASHBOARD_ALERT_MAX) {
            pending_alert_count = DASHBOARD_ALERT_MAX;
        }
        for(uint32_t alert_index = 0U; alert_index < pending_alert_count; alert_index++) {
            (void)memcpy(pending_alert_queue[alert_index],
                         g_pending_alert_queue[alert_index],
                         DASHBOARD_ALERT_TEXT_MAX);
            pending_alert_queue[alert_index][DASHBOARD_ALERT_TEXT_MAX - 1U] = '\0';
        }
        g_pending_alert_count = 0U;
        if(primask == 0U) {
            __enable_irq();
        }

        for(uint32_t alert_index = 0U; alert_index < pending_alert_count; alert_index++) {
            (void)strncpy(pending_alert_text, pending_alert_queue[alert_index], sizeof(pending_alert_text));
            pending_alert_text[sizeof(pending_alert_text) - 1U] = '\0';
            dashboard_push_alert_local(pending_alert_text);
        }
    }

    if(((now - g_dashboard_fast_ui_last_tick) >= DASHBOARD_UI_FAST_PERIOD_MS) ||
       (g_dashboard_fast_ui_last_tick == 0U)) {
        dashboard_mask |= g_dashboard_dirty_mask & DASH_DIRTY_FAST;
        if((dashboard_mask & DASH_DIRTY_FAST) != 0U) {
            g_dashboard_fast_ui_last_tick = now;
        }
    }
    if(((now - g_dashboard_slow_ui_last_tick) >= DASHBOARD_UI_SLOW_PERIOD_MS) ||
       (g_dashboard_slow_ui_last_tick == 0U)) {
        dashboard_mask |= g_dashboard_dirty_mask & DASH_DIRTY_SLOW;
        if((dashboard_mask & DASH_DIRTY_SLOW) != 0U) {
            g_dashboard_slow_ui_last_tick = now;
        }
    }

    if(((dashboard_mask & DASH_DIRTY_MODE) != 0U) &&
       (g_pending_dashboard_data.mode_index != g_mode_index) &&
       (dashboard_timing_mode_locked() != 0U)) {
        dashboard_mask &= ~DASH_DIRTY_MODE;
        primask = __get_PRIMASK();
        __disable_irq();
        g_dashboard_dirty_mask &= ~DASH_DIRTY_MODE;
        g_pending_dashboard_data.mode_index = g_mode_index;
        if(primask == 0U) {
            __enable_irq();
        }
        dashboard_request_mode_lock_alert();
    }

    if(dashboard_mask != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        if((dashboard_mask & DASH_DIRTY_BATTERY) != 0U) {
            g_dashboard_data.soc = g_pending_dashboard_data.soc;
            g_dashboard_data.sum_voltage = g_pending_dashboard_data.sum_voltage;
            g_dashboard_data.sum_current = g_pending_dashboard_data.sum_current;
            g_dashboard_data.max_temperature = g_pending_dashboard_data.max_temperature;
        }
        if((dashboard_mask & DASH_DIRTY_MODE) != 0U) {
            g_dashboard_data.mode_index = g_pending_dashboard_data.mode_index;
        }
        if((dashboard_mask & DASH_DIRTY_SLIP) != 0U) {
            g_dashboard_data.slip_level = g_pending_dashboard_data.slip_level;
        }
        if((dashboard_mask & DASH_DIRTY_PEDALS) != 0U) {
            g_dashboard_data.aps_open_pct = g_pending_dashboard_data.aps_open_pct;
            g_dashboard_data.brake_pct = g_pending_dashboard_data.brake_pct;
        }
        for(index = 0; index < 4U; index++) {
            if((dashboard_mask & (DASH_DIRTY_MOTOR_0 << index)) != 0U) {
                g_dashboard_data.torque[index] = g_pending_dashboard_data.torque[index];
                g_dashboard_data.motor_enable[index] = g_pending_dashboard_data.motor_enable[index];
                g_dashboard_data.rpm[index] = g_pending_dashboard_data.rpm[index];
                g_dashboard_data.motor_temp[index] = g_pending_dashboard_data.motor_temp[index];
            }
            if((dashboard_mask & (DASH_DIRTY_TIRE_0 << index)) != 0U) {
                for(uint32_t segment = 0U; segment < 4U; segment++) {
                    g_tire_temp[index][segment] = g_pending_tire_temp[index][segment];
                }
            }
        }
        if((dashboard_mask & DASH_DIRTY_ODOMETER) != 0U) {
            g_dashboard_data.odometer_tenths = g_pending_dashboard_data.odometer_tenths;
        }
        g_dashboard_dirty_mask &= ~dashboard_mask;
        if(primask == 0U) {
            __enable_irq();
        }

        dashboard_apply_data(dashboard_mask);
    }

    if(g_speed_dirty != 0U) {
        if((g_speed_ui_last_tick == 0U) || ((now - g_speed_ui_last_tick) >= 100U)) {
            primask = __get_PRIMASK();
            __disable_irq();
            pending_speed = g_pending_speed;
            g_speed_dirty = 0U;
            if(primask == 0U) {
                __enable_irq();
            }

            if(pending_speed < 0) pending_speed = 0;
            if(pending_speed > 99) pending_speed = 99;
            g_dashboard_data.speed = pending_speed;
            if(pending_speed != g_speed) {
                g_speed = pending_speed;
                set_speed_digits(pending_speed);
            }
            g_speed_ui_last_tick = now;
        }
    }

    if(g_lap_delta_dirty != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        g_lap_delta_dirty = 0U;
        g_lap_delta = g_pending_lap_delta;
        __set_PRIMASK(primask);
        update_lap_delta_ui();
    }

    if(g_lap_times_dirty != 0U) {
        char lap_buf[16];
        int32_t pending_lap_current;
        int32_t pending_lap_last;
        int32_t pending_lap_best;
        int32_t pending_lap_count;

        primask = __get_PRIMASK();
        __disable_irq();
        g_lap_times_dirty = 0U;
        pending_lap_current = g_pending_lap_current;
        pending_lap_last = g_pending_lap_last;
        pending_lap_best = g_pending_lap_best;
        pending_lap_count = g_pending_lap_count;
        __set_PRIMASK(primask);

        if(g_best_lap_time != pending_lap_best) {
            g_best_lap_time = pending_lap_best;
            format_lap_time(lap_buf, sizeof(lap_buf), g_best_lap_time);
            dashboard_set_label_text(g_dashboard.lap_best_value, lap_buf);
        }
        if(g_last_lap_time != pending_lap_last) {
            g_last_lap_time = pending_lap_last;
            format_lap_time(lap_buf, sizeof(lap_buf), g_last_lap_time);
            dashboard_set_label_text(g_dashboard.lap_last_value, lap_buf);
        }
        if(g_current_lap_time != pending_lap_current) {
            g_current_lap_time = pending_lap_current;
            format_lap_time(lap_buf, sizeof(lap_buf), g_current_lap_time);
            dashboard_set_label_text(g_dashboard.lap_current_value, lap_buf);
        }
        if(g_laps_current != pending_lap_count) {
            g_laps_current = pending_lap_count;
            lv_snprintf(lap_buf, sizeof(lap_buf), "%02ld", (long)g_laps_current);
            dashboard_set_label_text(g_dashboard.laps_current_value, lap_buf);
        }
    }
}

void Dashboard_UI_Init(void)
{
    g_dashboard_initialized = 0U;
    lv_obj_t * screen;

    LED_Diag_SetBootStage(3);
    screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t * top_area = create_panel(screen, 0, 0, SIM_HOR_RES, UI_TOP_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(top_area, 0, 0);

    g_dashboard.lap_best_label = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.lap_best_label, "Best");
    lv_obj_set_style_text_color(g_dashboard.lap_best_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_best_label, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.lap_best_label, 16, 16);

    g_dashboard.lap_best_value = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.lap_best_value, "0:00.00");
    lv_obj_set_style_text_color(g_dashboard.lap_best_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_best_value, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.lap_best_value, 76, 16);

    g_dashboard.lap_last_label = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.lap_last_label, "Pre");
    lv_obj_set_style_text_color(g_dashboard.lap_last_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_last_label, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.lap_last_label, 164, 16);

    g_dashboard.lap_last_value = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.lap_last_value, "0:00.00");
    lv_obj_set_style_text_color(g_dashboard.lap_last_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_last_value, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.lap_last_value, 214, 16);

    g_dashboard.lap_current_label = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.lap_current_label, "Cur");
    lv_obj_set_style_text_color(g_dashboard.lap_current_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_current_label, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.lap_current_label, 316, 16);

    g_dashboard.lap_current_value = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.lap_current_value, "0:00.00");
    lv_obj_set_style_text_color(g_dashboard.lap_current_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_current_value, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.lap_current_value, 364, 16);

    g_dashboard.delta_bar_track = lv_obj_create(top_area);
    lv_obj_remove_style_all(g_dashboard.delta_bar_track);
    lv_obj_set_pos(g_dashboard.delta_bar_track, DELTA_BAR_X, DELTA_BAR_Y);
    lv_obj_set_size(g_dashboard.delta_bar_track, DELTA_BAR_W, DELTA_BAR_H);
    lv_obj_set_style_bg_color(g_dashboard.delta_bar_track, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(g_dashboard.delta_bar_track, LV_OPA_COVER, 0);

    lv_obj_t * delta_bottom_line = lv_obj_create(g_dashboard.delta_bar_track);
    lv_obj_remove_style_all(delta_bottom_line);
    lv_obj_set_pos(delta_bottom_line, DELTA_BAR_INNER_X, DELTA_BAR_FILL_H);
    lv_obj_set_size(delta_bottom_line, DELTA_BAR_RIGHT_X - DELTA_BAR_INNER_X, 1);
    lv_obj_set_style_bg_color(delta_bottom_line, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(delta_bottom_line, LV_OPA_COVER, 0);

    g_dashboard.delta_bar_fill = lv_obj_create(g_dashboard.delta_bar_track);
    lv_obj_remove_style_all(g_dashboard.delta_bar_fill);
    lv_obj_set_pos(g_dashboard.delta_bar_fill, DELTA_BAR_CENTER_X, 0);
    lv_obj_set_size(g_dashboard.delta_bar_fill, 1, DELTA_BAR_FILL_H);
    lv_obj_set_style_bg_color(g_dashboard.delta_bar_fill, lv_color_hex(0x808080), 0);
    lv_obj_set_style_bg_opa(g_dashboard.delta_bar_fill, LV_OPA_COVER, 0);

    for(uint32_t tick_index = 0; tick_index < 7; tick_index++) {
        lv_obj_t * delta_tick = lv_obj_create(g_dashboard.delta_bar_track);
        lv_obj_remove_style_all(delta_tick);
        lv_coord_t tick_x = (lv_coord_t)(DELTA_BAR_INNER_X + ((tick_index * (DELTA_BAR_RIGHT_X - DELTA_BAR_INNER_X)) / 6));
        lv_obj_set_pos(delta_tick, tick_x, 1);
        lv_obj_set_size(delta_tick, 2, DELTA_BAR_FILL_H - 1);
        lv_obj_set_style_bg_color(delta_tick, UI_BORDER_COLOR, 0);
        lv_obj_set_style_bg_opa(delta_tick, LV_OPA_COVER, 0);
        lv_obj_move_foreground(delta_tick);
    }

    g_dashboard.delta_bar_center = lv_obj_create(g_dashboard.delta_bar_track);
    lv_obj_remove_style_all(g_dashboard.delta_bar_center);
    lv_obj_set_pos(g_dashboard.delta_bar_center, DELTA_BAR_CENTER_X, 1);
    lv_obj_set_size(g_dashboard.delta_bar_center, 2, DELTA_BAR_FILL_H - 1);
    lv_obj_set_style_bg_color(g_dashboard.delta_bar_center, UI_TEXT_COLOR, 0);
    lv_obj_set_style_bg_opa(g_dashboard.delta_bar_center, LV_OPA_COVER, 0);

    g_dashboard.delta_value = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.delta_value, "0.00s");
    lv_obj_set_style_text_color(g_dashboard.delta_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.delta_value, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.delta_value, 546, 15);

    g_dashboard.laps_current_label = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.laps_current_label, "Lap");
    lv_obj_set_style_text_color(g_dashboard.laps_current_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.laps_current_label, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.laps_current_label, 628, 16);

    g_dashboard.laps_current_value = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.laps_current_value, "00");
    lv_obj_set_style_text_color(g_dashboard.laps_current_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.laps_current_value, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.laps_current_value, 664, 16);

    g_dashboard.laps_left_label = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.laps_left_label, "Left");
    lv_obj_set_style_text_color(g_dashboard.laps_left_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.laps_left_label, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.laps_left_label, 704, 16);

    g_dashboard.laps_left_value = lv_label_create(top_area);
    dashboard_set_label_text(g_dashboard.laps_left_value, "75");
    lv_obj_set_style_text_color(g_dashboard.laps_left_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.laps_left_value, DASHBOARD_FONT_MEDIUM, 0);
    lv_obj_set_pos(g_dashboard.laps_left_value, 748, 16);

    lv_obj_t * middle_panel = create_panel(screen, 0, UI_MIDDLE_Y, SIM_HOR_RES, UI_MIDDLE_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(middle_panel, 0, 0);

    lv_obj_t * mode_box = create_panel(middle_panel, UI_RIGHT_PANEL_X, 0, UI_RIGHT_PANEL_WIDTH, UI_MIDDLE_HEIGHT - 9, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(mode_box, 0, 0);

    lv_obj_t * battery_outline = create_panel(mode_box, 18, 14, 54, 24, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(battery_outline, 1, 0);
    lv_obj_set_style_pad_all(battery_outline, 0, 0);

    lv_obj_t * battery_cap = lv_obj_create(mode_box);
    lv_obj_remove_style_all(battery_cap);
    lv_obj_set_pos(battery_cap, 72, 20);
    lv_obj_set_size(battery_cap, 5, 12);
    lv_obj_set_style_bg_color(battery_cap, UI_TEXT_COLOR, 0);
    lv_obj_set_style_bg_opa(battery_cap, LV_OPA_COVER, 0);

    g_dashboard.battery_fill = lv_obj_create(battery_outline);
    lv_obj_remove_style_all(g_dashboard.battery_fill);
    lv_obj_set_pos(g_dashboard.battery_fill, 2, 2);
    lv_obj_set_size(g_dashboard.battery_fill, 36, 20);
    lv_obj_set_style_bg_color(g_dashboard.battery_fill, UI_TEXT_COLOR, 0);
    lv_obj_set_style_bg_opa(g_dashboard.battery_fill, LV_OPA_COVER, 0);

    g_dashboard.soc_value = lv_label_create(mode_box);
    dashboard_set_label_text(g_dashboard.soc_value, "24%");
    lv_obj_set_style_text_color(g_dashboard.soc_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.soc_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.soc_value, 86, 14);

    lv_obj_t * power_live_label = lv_label_create(mode_box);
    dashboard_set_label_text(power_live_label, "P NOW:");
    lv_obj_set_style_text_color(power_live_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(power_live_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(power_live_label, 16, 46);

    g_dashboard.power_live_value = lv_label_create(mode_box);
    dashboard_set_label_text(g_dashboard.power_live_value, "12kW");
    lv_obj_set_style_text_color(g_dashboard.power_live_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.power_live_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.power_live_value, 88, 46);

    lv_obj_t * power_peak_label = lv_label_create(mode_box);
    dashboard_set_label_text(power_peak_label, "P PEAK:");
    lv_obj_set_style_text_color(power_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(power_peak_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(power_peak_label, 16, 68);

    g_dashboard.power_peak_value = lv_label_create(mode_box);
    dashboard_set_label_text(g_dashboard.power_peak_value, "36kW");
    lv_obj_set_style_text_color(g_dashboard.power_peak_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.power_peak_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.power_peak_value, 88, 68);

    lv_obj_t * voltage_label = lv_label_create(mode_box);
    dashboard_set_label_text(voltage_label, "TOTAL V:");
    lv_obj_set_style_text_color(voltage_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(voltage_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(voltage_label, 16, 92);

    g_dashboard.total_voltage_value = lv_label_create(mode_box);
    dashboard_set_label_text(g_dashboard.total_voltage_value, "24");
    lv_obj_set_style_text_color(g_dashboard.total_voltage_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.total_voltage_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.total_voltage_value, 98, 92);

    lv_obj_t * current_label = lv_label_create(mode_box);
    dashboard_set_label_text(current_label, "TOTAL A:");
    lv_obj_set_style_text_color(current_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(current_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(current_label, 16, 114);

    g_dashboard.total_current_value = lv_label_create(mode_box);
    dashboard_set_label_text(g_dashboard.total_current_value, "24");
    lv_obj_set_style_text_color(g_dashboard.total_current_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.total_current_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.total_current_value, 98, 114);

    lv_obj_t * max_temp_label = lv_label_create(mode_box);
    dashboard_set_label_text(max_temp_label, "MAX T:");
    lv_obj_set_style_text_color(max_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(max_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(max_temp_label, 16, 136);

    g_dashboard.max_temp_value = lv_label_create(mode_box);
    dashboard_set_label_text(g_dashboard.max_temp_value, "24");
    lv_obj_set_style_text_color(g_dashboard.max_temp_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.max_temp_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.max_temp_value, 88, 136);

    /* Low-voltage section: DBC BO_1440 PDM_LowVoltageBus 0x5A0 */
    {
        lv_obj_t * lv_separator = lv_obj_create(mode_box);
        lv_obj_remove_style_all(lv_separator);
        lv_obj_set_pos(lv_separator, 12, 158);
        lv_obj_set_size(lv_separator, 156, 1);
        lv_obj_set_style_bg_color(lv_separator, UI_BORDER_COLOR, 0);
        lv_obj_set_style_bg_opa(lv_separator, LV_OPA_COVER, 0);

        lv_obj_t * lv_voltage_label = lv_label_create(mode_box);
        dashboard_set_label_text(lv_voltage_label, "LV V:");
        lv_obj_set_style_text_color(lv_voltage_label, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lv_voltage_label, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(lv_voltage_label, 16, 166);
        g_dashboard.lv_voltage_value = lv_label_create(mode_box);
        dashboard_set_label_text(g_dashboard.lv_voltage_value, "0.0V");
        lv_obj_set_style_text_color(g_dashboard.lv_voltage_value, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(g_dashboard.lv_voltage_value, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(g_dashboard.lv_voltage_value, 78, 166);

        lv_obj_t * lv_current_label = lv_label_create(mode_box);
        dashboard_set_label_text(lv_current_label, "LV A:");
        lv_obj_set_style_text_color(lv_current_label, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lv_current_label, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(lv_current_label, 16, 188);
        g_dashboard.lv_current_value = lv_label_create(mode_box);
        dashboard_set_label_text(g_dashboard.lv_current_value, "0.0A");
        lv_obj_set_style_text_color(g_dashboard.lv_current_value, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(g_dashboard.lv_current_value, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(g_dashboard.lv_current_value, 78, 188);

        lv_obj_t * lv_power_label = lv_label_create(mode_box);
        dashboard_set_label_text(lv_power_label, "LV W:");
        lv_obj_set_style_text_color(lv_power_label, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lv_power_label, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(lv_power_label, 16, 210);
        g_dashboard.lv_power_value = lv_label_create(mode_box);
        dashboard_set_label_text(g_dashboard.lv_power_value, "0W");
        lv_obj_set_style_text_color(g_dashboard.lv_power_value, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(g_dashboard.lv_power_value, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(g_dashboard.lv_power_value, 78, 210);

        /* Fan section: DBC BO_1442 FanController_Status 0x5A2 */
        lv_obj_t * fan_separator = lv_obj_create(mode_box);
        lv_obj_remove_style_all(fan_separator);
        lv_obj_set_pos(fan_separator, 12, 232);
        lv_obj_set_size(fan_separator, 156, 1);
        lv_obj_set_style_bg_color(fan_separator, UI_BORDER_COLOR, 0);
        lv_obj_set_style_bg_opa(fan_separator, LV_OPA_COVER, 0);

        for(uint32_t fan = 0U; fan < 3U; fan++) {
            char fan_label_text[4];
            lv_obj_t * fan_label = lv_label_create(mode_box);
            lv_snprintf(fan_label_text, sizeof(fan_label_text), "F%lu", (unsigned long)(fan + 1U));
            dashboard_set_label_text(fan_label, fan_label_text);
            lv_obj_set_style_text_color(fan_label, UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(fan_label, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(fan_label, 16, (lv_coord_t)(240 + fan * 22));
            g_dashboard.fan_value_labels[fan] = lv_label_create(mode_box);
            dashboard_set_label_text(g_dashboard.fan_value_labels[fan], "0% 0");
            lv_obj_set_style_text_color(g_dashboard.fan_value_labels[fan], UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(g_dashboard.fan_value_labels[fan], &lv_font_montserrat_16, 0);
            lv_obj_set_pos(g_dashboard.fan_value_labels[fan], 44, (lv_coord_t)(240 + fan * 22));
        }
    }

    lv_obj_t * speed_box = create_panel(middle_panel, UI_CENTER_PANEL_X, UI_SPEED_BOX_Y,
                                        UI_CENTER_PANEL_WIDTH, UI_SPEED_BOX_HEIGHT,
                                        UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(speed_box, 0, 0);

    create_speed_digits(speed_box);

    lv_obj_t * speed_unit = lv_label_create(speed_box);
    dashboard_set_label_text(speed_unit, "km/h");
    lv_obj_set_style_text_color(speed_unit, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_opa(speed_unit, LV_OPA_60, 0);
    lv_obj_set_style_text_font(speed_unit, DASHBOARD_FONT_SMALL, 0);
    lv_obj_align(speed_unit, LV_ALIGN_CENTER, 0, 72);

    /* Large pedal bars flanking the speed digits: brake left (red), throttle
     * right (green), each with a live percentage under the bar. */
    {
        lv_obj_t * brake_bar_track = lv_obj_create(middle_panel);
        lv_obj_remove_style_all(brake_bar_track);
        lv_obj_set_pos(brake_bar_track, PEDAL_BRAKE_X, PEDAL_BAR_TOP_Y);
        lv_obj_set_size(brake_bar_track, PEDAL_BAR_W, PEDAL_BAR_H);
        lv_obj_set_style_radius(brake_bar_track, 0, 0);
        lv_obj_set_style_bg_color(brake_bar_track, UI_BG_COLOR, 0);
        lv_obj_set_style_bg_opa(brake_bar_track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(brake_bar_track, 1, 0);
        lv_obj_set_style_border_color(brake_bar_track, UI_BORDER_COLOR, 0);
        lv_obj_add_event_cb(brake_bar_track, pedal_bar_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

        lv_obj_t * brake_limit = lv_obj_create(middle_panel);
        lv_obj_remove_style_all(brake_limit);
        lv_obj_set_pos(brake_limit, PEDAL_BRAKE_X, PEDAL_BAR_TOP_Y - 8);
        lv_obj_set_size(brake_limit, PEDAL_BAR_W, 5);
        lv_obj_set_style_bg_color(brake_limit, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_bg_opa(brake_limit, LV_OPA_COVER, 0);

        g_dashboard.brake_bar_fill = lv_obj_create(brake_bar_track);
        lv_obj_remove_style_all(g_dashboard.brake_bar_fill);
        lv_obj_set_pos(g_dashboard.brake_bar_fill, 0, PEDAL_BAR_H - 1);
        lv_obj_set_size(g_dashboard.brake_bar_fill, PEDAL_BAR_W, 1);
        lv_obj_set_style_bg_color(g_dashboard.brake_bar_fill, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_bg_opa(g_dashboard.brake_bar_fill, LV_OPA_COVER, 0);

        g_dashboard.brake_pct_label = lv_label_create(middle_panel);
        lv_obj_set_pos(g_dashboard.brake_pct_label, PEDAL_BRAKE_X - 12, PEDAL_PCT_LABEL_Y);
        lv_obj_set_width(g_dashboard.brake_pct_label, PEDAL_BAR_W + 24);
        lv_obj_set_style_text_align(g_dashboard.brake_pct_label, LV_TEXT_ALIGN_CENTER, 0);
        dashboard_set_label_text(g_dashboard.brake_pct_label, "0%");
        lv_obj_set_style_text_color(g_dashboard.brake_pct_label, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(g_dashboard.brake_pct_label, DASHBOARD_FONT_SMALL, 0);

        lv_obj_t * brake_name_label = lv_label_create(middle_panel);
        lv_obj_set_pos(brake_name_label, PEDAL_BRAKE_X - 12, PEDAL_NAME_LABEL_Y);
        lv_obj_set_width(brake_name_label, PEDAL_BAR_W + 24);
        lv_obj_set_style_text_align(brake_name_label, LV_TEXT_ALIGN_CENTER, 0);
        dashboard_set_label_text(brake_name_label, "BRAKE");
        lv_obj_set_style_text_color(brake_name_label, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(brake_name_label, &lv_font_montserrat_16, 0);

        lv_obj_t * throttle_bar_track = lv_obj_create(middle_panel);
        lv_obj_remove_style_all(throttle_bar_track);
        lv_obj_set_pos(throttle_bar_track, PEDAL_THROTTLE_X, PEDAL_BAR_TOP_Y);
        lv_obj_set_size(throttle_bar_track, PEDAL_BAR_W, PEDAL_BAR_H);
        lv_obj_set_style_radius(throttle_bar_track, 0, 0);
        lv_obj_set_style_bg_color(throttle_bar_track, UI_BG_COLOR, 0);
        lv_obj_set_style_bg_opa(throttle_bar_track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(throttle_bar_track, 1, 0);
        lv_obj_set_style_border_color(throttle_bar_track, UI_BORDER_COLOR, 0);
        lv_obj_add_event_cb(throttle_bar_track, pedal_bar_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

        lv_obj_t * throttle_limit = lv_obj_create(middle_panel);
        lv_obj_remove_style_all(throttle_limit);
        lv_obj_set_pos(throttle_limit, PEDAL_THROTTLE_X, PEDAL_BAR_TOP_Y - 8);
        lv_obj_set_size(throttle_limit, PEDAL_BAR_W, 5);
        lv_obj_set_style_bg_color(throttle_limit, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_bg_opa(throttle_limit, LV_OPA_COVER, 0);

        g_dashboard.throttle_bar_fill = lv_obj_create(throttle_bar_track);
        lv_obj_remove_style_all(g_dashboard.throttle_bar_fill);
        lv_obj_set_pos(g_dashboard.throttle_bar_fill, 0, PEDAL_BAR_H - 1);
        lv_obj_set_size(g_dashboard.throttle_bar_fill, PEDAL_BAR_W, 1);
        lv_obj_set_style_bg_color(g_dashboard.throttle_bar_fill, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_bg_opa(g_dashboard.throttle_bar_fill, LV_OPA_COVER, 0);

        g_dashboard.throttle_pct_label = lv_label_create(middle_panel);
        lv_obj_set_pos(g_dashboard.throttle_pct_label, PEDAL_THROTTLE_X - 12, PEDAL_PCT_LABEL_Y);
        lv_obj_set_width(g_dashboard.throttle_pct_label, PEDAL_BAR_W + 24);
        lv_obj_set_style_text_align(g_dashboard.throttle_pct_label, LV_TEXT_ALIGN_CENTER, 0);
        dashboard_set_label_text(g_dashboard.throttle_pct_label, "0%");
        lv_obj_set_style_text_color(g_dashboard.throttle_pct_label, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_text_font(g_dashboard.throttle_pct_label, DASHBOARD_FONT_SMALL, 0);

        lv_obj_t * throttle_name_label = lv_label_create(middle_panel);
        lv_obj_set_pos(throttle_name_label, PEDAL_THROTTLE_X - 12, PEDAL_NAME_LABEL_Y);
        lv_obj_set_width(throttle_name_label, PEDAL_BAR_W + 24);
        lv_obj_set_style_text_align(throttle_name_label, LV_TEXT_ALIGN_CENTER, 0);
        dashboard_set_label_text(throttle_name_label, "THRTL");
        lv_obj_set_style_text_color(throttle_name_label, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(throttle_name_label, &lv_font_montserrat_16, 0);
    }

    lv_obj_t * vehicle_box = create_panel(middle_panel, 0, 0, UI_LEFT_PANEL_WIDTH, UI_MIDDLE_HEIGHT - 9, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(vehicle_box, 0, 0);

    g_dashboard.mode_tile = create_panel(vehicle_box, 52, 10, 76, 76, lv_palette_main(LV_PALETTE_RED), LV_OPA_COVER);
    lv_obj_set_style_border_width(g_dashboard.mode_tile, 2, 0);
    lv_obj_set_style_radius(g_dashboard.mode_tile, 0, 0);

    g_dashboard.mode_value = lv_label_create(g_dashboard.mode_tile);
    lv_obj_set_style_text_font(g_dashboard.mode_value, DASHBOARD_FONT_LARGE, 0);
    lv_obj_align(g_dashboard.mode_value, LV_ALIGN_CENTER, 0, 0);
    apply_drive_mode_ui();

    g_dashboard.slip_value = create_value(vehicle_box, "SLIP 0", UI_TEXT_COLOR,
                                          DASHBOARD_FONT_SMALL);
    lv_obj_set_pos(g_dashboard.slip_value, 55, 101);
    for(uint32_t index = 0U; index < SLIP_BAR_COUNT; index++) {
        g_dashboard.slip_bars[index] =
            create_panel(vehicle_box, 12 + (lv_coord_t)(index * 24U), 132,
                         14, 6, UI_SEGMENT_OFF_COLOR, LV_OPA_COVER);
        lv_obj_set_style_border_width(g_dashboard.slip_bars[index], 0, 0);
    }
    update_slip_level_ui();

    g_dashboard.wheel_fl = create_panel(vehicle_box, UI_TIRE_LEFT_X, UI_TIRE_FRONT_Y,
                                        UI_TIRE_WIDTH, UI_TIRE_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_radius(g_dashboard.wheel_fl, 2, 0);
    g_dashboard.lightning_fl = lv_label_create(vehicle_box);
    dashboard_set_label_text(g_dashboard.lightning_fl, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(g_dashboard.lightning_fl, lv_color_hex(0xFFD400), 0);
    lv_obj_set_style_text_font(g_dashboard.lightning_fl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_transform_scale(g_dashboard.lightning_fl, UI_TIRE_LIGHTNING_SCALE, 0);
    lv_obj_align_to(g_dashboard.lightning_fl, g_dashboard.wheel_fl, LV_ALIGN_OUT_LEFT_MID, -4, UI_TIRE_LIGHTNING_Y);

    g_dashboard.wheel_fr = create_panel(vehicle_box, UI_TIRE_RIGHT_X, UI_TIRE_FRONT_Y,
                                        UI_TIRE_WIDTH, UI_TIRE_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_radius(g_dashboard.wheel_fr, 2, 0);
    g_dashboard.lightning_fr = lv_label_create(vehicle_box);
    dashboard_set_label_text(g_dashboard.lightning_fr, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(g_dashboard.lightning_fr, lv_color_hex(0xFFD400), 0);
    lv_obj_set_style_text_font(g_dashboard.lightning_fr, &lv_font_montserrat_18, 0);
    lv_obj_set_style_transform_scale(g_dashboard.lightning_fr, UI_TIRE_LIGHTNING_SCALE, 0);
    lv_obj_align_to(g_dashboard.lightning_fr, g_dashboard.wheel_fr, LV_ALIGN_OUT_RIGHT_MID, 4, UI_TIRE_LIGHTNING_Y);

    g_dashboard.wheel_rl = create_panel(vehicle_box, UI_TIRE_LEFT_X, UI_TIRE_REAR_Y,
                                        UI_TIRE_WIDTH, UI_TIRE_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_radius(g_dashboard.wheel_rl, 2, 0);
    g_dashboard.lightning_rl = lv_label_create(vehicle_box);
    dashboard_set_label_text(g_dashboard.lightning_rl, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(g_dashboard.lightning_rl, lv_color_hex(0xFFD400), 0);
    lv_obj_set_style_text_font(g_dashboard.lightning_rl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_transform_scale(g_dashboard.lightning_rl, UI_TIRE_LIGHTNING_SCALE, 0);
    lv_obj_align_to(g_dashboard.lightning_rl, g_dashboard.wheel_rl, LV_ALIGN_OUT_LEFT_MID, -4, UI_TIRE_LIGHTNING_Y);

    g_dashboard.wheel_rr = create_panel(vehicle_box, UI_TIRE_RIGHT_X, UI_TIRE_REAR_Y,
                                        UI_TIRE_WIDTH, UI_TIRE_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_radius(g_dashboard.wheel_rr, 2, 0);
    g_dashboard.lightning_rr = lv_label_create(vehicle_box);
    dashboard_set_label_text(g_dashboard.lightning_rr, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(g_dashboard.lightning_rr, lv_color_hex(0xFFD400), 0);
    lv_obj_set_style_text_font(g_dashboard.lightning_rr, &lv_font_montserrat_18, 0);
    lv_obj_set_style_transform_scale(g_dashboard.lightning_rr, UI_TIRE_LIGHTNING_SCALE, 0);
    lv_obj_align_to(g_dashboard.lightning_rr, g_dashboard.wheel_rr, LV_ALIGN_OUT_RIGHT_MID, 4, UI_TIRE_LIGHTNING_Y);

    {
        lv_obj_t * tire_wheels[4] = {
            g_dashboard.wheel_fl, g_dashboard.wheel_rl,
            g_dashboard.wheel_fr, g_dashboard.wheel_rr
        };
        for(uint32_t wheel = 0U; wheel < 4U; wheel++) {
            lv_obj_add_event_cb(tire_wheels[wheel], tire_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
            g_dashboard.tire_max_labels[wheel] =
                create_value(vehicle_box, "0°", UI_TEXT_COLOR, &lv_font_montserrat_16);
        }
        lv_obj_align_to(g_dashboard.tire_max_labels[0], g_dashboard.wheel_fl,
                        LV_ALIGN_OUT_LEFT_MID, -2, UI_TIRE_TEMP_LABEL_Y);
        lv_obj_align_to(g_dashboard.tire_max_labels[1], g_dashboard.wheel_rl,
                        LV_ALIGN_OUT_LEFT_MID, -2, UI_TIRE_TEMP_LABEL_Y);
        lv_obj_align_to(g_dashboard.tire_max_labels[2], g_dashboard.wheel_fr,
                        LV_ALIGN_OUT_RIGHT_MID, 2, UI_TIRE_TEMP_LABEL_Y);
        lv_obj_align_to(g_dashboard.tire_max_labels[3], g_dashboard.wheel_rr,
                        LV_ALIGN_OUT_RIGHT_MID, 2, UI_TIRE_TEMP_LABEL_Y);
    }

    apply_vehicle_ui();

    /* Night-mode toggle: circular EYE icon in the free area under the tires.
     * Touch is handled by Dashboard_UI_SubmitTouchState hit-testing this
     * rectangle; the EYE glyph flips and the backlight dims to 60%. Object
     * creation is belt-and-braces NULL-checked: the LVGL pool is tight, and
     * the icon is cosmetic while a NULL deref would kill the whole boot. */
    g_dashboard.night_icon = lv_obj_create(vehicle_box);
    if(g_dashboard.night_icon != NULL) {
        lv_obj_remove_style_all(g_dashboard.night_icon);
        lv_obj_set_pos(g_dashboard.night_icon, NIGHT_ICON_X, NIGHT_ICON_Y);
        lv_obj_set_size(g_dashboard.night_icon, NIGHT_ICON_SIZE, NIGHT_ICON_SIZE);
        lv_obj_set_style_radius(g_dashboard.night_icon, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_dashboard.night_icon, UI_BG_COLOR, 0);
        lv_obj_set_style_bg_opa(g_dashboard.night_icon, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_dashboard.night_icon, 2, 0);
        lv_obj_set_style_border_color(g_dashboard.night_icon, UI_BORDER_COLOR, 0);

        g_dashboard.night_icon_label = lv_label_create(g_dashboard.night_icon);
        if(g_dashboard.night_icon_label != NULL) {
            lv_obj_set_style_text_font(g_dashboard.night_icon_label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(g_dashboard.night_icon_label, UI_TEXT_COLOR, 0);
            lv_obj_align(g_dashboard.night_icon_label, LV_ALIGN_CENTER, 0, 0);
            dashboard_set_label_text(g_dashboard.night_icon_label, LV_SYMBOL_EYE_OPEN);
        }
        update_night_mode_ui();
    }
    LED_Diag_SetBootStage(4);

    lv_obj_t * bottom_info = create_panel(screen, 0, UI_BOTTOM_Y, SIM_HOR_RES, UI_BOTTOM_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(bottom_info, 0, 0);

    lv_obj_t * motor_fl_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fl_label, "LF T:");
    lv_obj_set_style_text_color(motor_fl_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fl_label, 14, 4);
    g_dashboard.motor_fl_torque = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fl_torque, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_torque, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_torque, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_torque, 68, 4);
    g_dashboard.motor_fl_speed = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fl_speed, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_speed, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_speed, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_speed, 68, 24);

    lv_obj_t * motor_fl_speed_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fl_speed_label, "LF N:");
    lv_obj_set_style_text_color(motor_fl_speed_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_speed_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fl_speed_label, 14, 24);

    lv_obj_t * motor_fl_power_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fl_power_label, "P:");
    lv_obj_set_style_text_color(motor_fl_power_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_power_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fl_power_label, 102, 4);
    g_dashboard.motor_fl_power_live = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fl_power_live, "10");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_power_live, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_power_live, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_power_live, 140, 4);

    lv_obj_t * motor_fl_peak_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fl_peak_label, "Pk:");
    lv_obj_set_style_text_color(motor_fl_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_peak_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fl_peak_label, 102, 24);
    g_dashboard.motor_fl_power_peak = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fl_power_peak, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_power_peak, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_power_peak, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_power_peak, 150, 24);

    lv_obj_t * motor_fl_temp_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fl_temp_label, "Tm:");
    lv_obj_set_style_text_color(motor_fl_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_temp_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fl_temp_label, 14, 44);
    g_dashboard.motor_fl_temp = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fl_temp, "48");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_temp, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_temp, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_temp, 68, 44);

    lv_obj_t * motor_fr_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fr_label, "RF T:");
    lv_obj_set_style_text_color(motor_fr_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fr_label, 214, 4);
    g_dashboard.motor_fr_torque = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fr_torque, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_torque, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_torque, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_torque, 268, 4);
    g_dashboard.motor_fr_speed = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fr_speed, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_speed, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_speed, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_speed, 268, 24);

    lv_obj_t * motor_fr_speed_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fr_speed_label, "RF N:");
    lv_obj_set_style_text_color(motor_fr_speed_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_speed_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fr_speed_label, 214, 24);

    lv_obj_t * motor_fr_power_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fr_power_label, "P:");
    lv_obj_set_style_text_color(motor_fr_power_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_power_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fr_power_label, 302, 4);
    g_dashboard.motor_fr_power_live = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fr_power_live, "10");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_power_live, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_power_live, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_power_live, 340, 4);

    lv_obj_t * motor_fr_peak_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fr_peak_label, "Pk:");
    lv_obj_set_style_text_color(motor_fr_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_peak_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fr_peak_label, 302, 24);
    g_dashboard.motor_fr_power_peak = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fr_power_peak, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_power_peak, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_power_peak, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_power_peak, 350, 24);

    lv_obj_t * motor_fr_temp_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_fr_temp_label, "Tm:");
    lv_obj_set_style_text_color(motor_fr_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_temp_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_fr_temp_label, 214, 44);
    g_dashboard.motor_fr_temp = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_fr_temp, "47");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_temp, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_temp, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_temp, 268, 44);

    lv_obj_t * motor_rl_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rl_label, "LR T:");
    lv_obj_set_style_text_color(motor_rl_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rl_label, 414, 4);
    g_dashboard.motor_rl_torque = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rl_torque, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_torque, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_torque, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_torque, 468, 4);
    g_dashboard.motor_rl_speed = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rl_speed, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_speed, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_speed, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_speed, 468, 24);

    lv_obj_t * motor_rl_speed_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rl_speed_label, "LR N:");
    lv_obj_set_style_text_color(motor_rl_speed_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_speed_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rl_speed_label, 414, 24);

    lv_obj_t * motor_rl_power_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rl_power_label, "P:");
    lv_obj_set_style_text_color(motor_rl_power_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_power_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rl_power_label, 502, 4);
    g_dashboard.motor_rl_power_live = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rl_power_live, "9");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_power_live, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_power_live, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_power_live, 540, 4);

    lv_obj_t * motor_rl_peak_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rl_peak_label, "Pk:");
    lv_obj_set_style_text_color(motor_rl_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_peak_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rl_peak_label, 502, 24);
    g_dashboard.motor_rl_power_peak = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rl_power_peak, "23");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_power_peak, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_power_peak, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_power_peak, 550, 24);

    lv_obj_t * motor_rl_temp_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rl_temp_label, "Tm:");
    lv_obj_set_style_text_color(motor_rl_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_temp_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rl_temp_label, 414, 44);
    g_dashboard.motor_rl_temp = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rl_temp, "49");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_temp, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_temp, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_temp, 468, 44);

    lv_obj_t * motor_rr_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rr_label, "RR T:");
    lv_obj_set_style_text_color(motor_rr_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rr_label, 614, 4);
    g_dashboard.motor_rr_torque = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rr_torque, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_torque, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_torque, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_torque, 668, 4);
    g_dashboard.motor_rr_speed = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rr_speed, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_speed, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_speed, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_speed, 668, 24);

    lv_obj_t * motor_rr_speed_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rr_speed_label, "RR N:");
    lv_obj_set_style_text_color(motor_rr_speed_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_speed_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rr_speed_label, 614, 24);

    lv_obj_t * motor_rr_power_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rr_power_label, "P:");
    lv_obj_set_style_text_color(motor_rr_power_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_power_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rr_power_label, 702, 4);
    g_dashboard.motor_rr_power_live = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rr_power_live, "9");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_power_live, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_power_live, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_power_live, 740, 4);

    lv_obj_t * motor_rr_peak_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rr_peak_label, "Pk:");
    lv_obj_set_style_text_color(motor_rr_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_peak_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rr_peak_label, 702, 24);
    g_dashboard.motor_rr_power_peak = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rr_power_peak, "23");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_power_peak, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_power_peak, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_power_peak, 750, 24);

    lv_obj_t * motor_rr_temp_label = lv_label_create(bottom_info);
    dashboard_set_label_text(motor_rr_temp_label, "Tm:");
    lv_obj_set_style_text_color(motor_rr_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_temp_label, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(motor_rr_temp_label, 614, 44);
    g_dashboard.motor_rr_temp = lv_label_create(bottom_info);
    dashboard_set_label_text(g_dashboard.motor_rr_temp, "50");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_temp, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_temp, DASHBOARD_FONT_SMALL, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_temp, 668, 44);

    /* Per-wheel inverter temperature "Ti" (DBC BO_1287 Debug7) and diagnostic
     * number "E" (DBC BO_1283/1284), one extra row each. Column order matches
     * the four blocks above: LF, RF, LR, RR. */
    {
        static const lv_coord_t column_x[4] = {0, 200, 400, 600};
        lv_obj_t ** inverter_labels[4] = {
            &g_dashboard.motor_fl_inverter, &g_dashboard.motor_fr_inverter,
            &g_dashboard.motor_rl_inverter, &g_dashboard.motor_rr_inverter
        };
        lv_obj_t ** error_labels[4] = {
            &g_dashboard.motor_fl_error, &g_dashboard.motor_fr_error,
            &g_dashboard.motor_rl_error, &g_dashboard.motor_rr_error
        };
        for(uint32_t col = 0U; col < 4U; col++) {
            lv_coord_t x = column_x[col];
            lv_obj_t * inverter_caption = lv_label_create(bottom_info);
            dashboard_set_label_text(inverter_caption, "Ti:");
            lv_obj_set_style_text_color(inverter_caption, UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(inverter_caption, DASHBOARD_FONT_SMALL, 0);
            lv_obj_set_pos(inverter_caption, x + 102, 44);
            *inverter_labels[col] = lv_label_create(bottom_info);
            dashboard_set_label_text(*inverter_labels[col], "0");
            lv_obj_set_style_text_color(*inverter_labels[col], UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(*inverter_labels[col], DASHBOARD_FONT_SMALL, 0);
            lv_obj_set_pos(*inverter_labels[col], x + 140, 44);

            lv_obj_t * error_caption = lv_label_create(bottom_info);
            dashboard_set_label_text(error_caption, "E:");
            lv_obj_set_style_text_color(error_caption, UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(error_caption, DASHBOARD_FONT_SMALL, 0);
            lv_obj_set_pos(error_caption, x + 14, 64);
            *error_labels[col] = lv_label_create(bottom_info);
            dashboard_set_label_text(*error_labels[col], "OK");
            lv_obj_set_style_text_color(*error_labels[col], lv_palette_main(LV_PALETTE_GREY), 0);
            lv_obj_set_style_text_font(*error_labels[col], DASHBOARD_FONT_SMALL, 0);
            lv_obj_set_pos(*error_labels[col], x + 38, 64);
        }
    }

    for(uint32_t separator_index = 1; separator_index < 4; separator_index++) {
        lv_obj_t * motor_separator = lv_obj_create(bottom_info);
        lv_obj_remove_style_all(motor_separator);
        lv_obj_set_pos(motor_separator, (lv_coord_t)(separator_index * 200), 0);
        lv_obj_set_size(motor_separator, 2, UI_BOTTOM_HEIGHT);
        lv_obj_set_style_bg_color(motor_separator, UI_BORDER_COLOR, 0);
        lv_obj_set_style_bg_opa(motor_separator, LV_OPA_COVER, 0);
        lv_obj_move_foreground(motor_separator);
    }

    lv_obj_t * separator_top = lv_obj_create(screen);
    lv_obj_remove_style_all(separator_top);
    lv_obj_set_pos(separator_top, 0, UI_TOP_HEIGHT);
    lv_obj_set_size(separator_top, SIM_HOR_RES, 1);
    lv_obj_set_style_bg_color(separator_top, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(separator_top, LV_OPA_COVER, 0);

    lv_obj_t * separator_bottom = lv_obj_create(screen);
    lv_obj_remove_style_all(separator_bottom);
    lv_obj_set_pos(separator_bottom, 0, UI_BOTTOM_Y - 1);
    lv_obj_set_size(separator_bottom, SIM_HOR_RES, 1);
    lv_obj_set_style_bg_color(separator_bottom, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(separator_bottom, LV_OPA_COVER, 0);

    lv_obj_t * separator_value_top = lv_obj_create(screen);
    lv_obj_remove_style_all(separator_value_top);
    lv_obj_set_pos(separator_value_top, UI_CENTER_PANEL_X, UI_MIDDLE_Y + 72);
    lv_obj_set_size(separator_value_top, UI_CENTER_PANEL_WIDTH, 1);
    lv_obj_set_style_bg_color(separator_value_top, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(separator_value_top, LV_OPA_COVER, 0);

    {
        static const int bar_heights[4] = {12, 24, 36, 48};
        const lv_coord_t bar_w = 8;
        const lv_coord_t bar_gap = 2;
        const lv_coord_t total_w = (lv_coord_t)(4 * bar_w + 3 * bar_gap);
        const lv_coord_t bar_x = UI_RIGHT_PANEL_X - total_w - 2;
        const lv_coord_t bar_base_y = (lv_coord_t)(UI_TOP_HEIGHT + 1 + 72 - 2);
        const lv_color_t grey  = { 0x55, 0x55, 0x55 };

        for(uint32_t i = 0U; i < 4U; i++) {
            g_dashboard.signal_bars[i] = lv_obj_create(screen);
            lv_obj_set_pos(g_dashboard.signal_bars[i],
                (lv_coord_t)(bar_x + i * (bar_w + bar_gap)),
                (lv_coord_t)(bar_base_y - bar_heights[i]));
            lv_obj_set_size(g_dashboard.signal_bars[i], bar_w, (lv_coord_t)bar_heights[i]);
            lv_obj_set_style_border_width(g_dashboard.signal_bars[i], 0, 0);
            lv_obj_set_style_pad_all(g_dashboard.signal_bars[i], 0, 0);
            lv_obj_set_style_radius(g_dashboard.signal_bars[i], 0, 0);
            lv_obj_set_style_bg_color(g_dashboard.signal_bars[i], grey, 0);
            lv_obj_set_style_bg_opa(g_dashboard.signal_bars[i], LV_OPA_COVER, 0);
        }
    }

    {
        const lv_coord_t circle_d = 24;
        const lv_coord_t circle_x = UI_CENTER_PANEL_X + 12;
        const lv_coord_t circle_y = (lv_coord_t)(UI_MIDDLE_Y + 1 + (72 - circle_d) / 2);
        g_dashboard.alert_circle = lv_obj_create(screen);
        lv_obj_set_pos(g_dashboard.alert_circle, circle_x, circle_y);
        lv_obj_set_size(g_dashboard.alert_circle, circle_d, circle_d);
        lv_obj_set_style_border_width(g_dashboard.alert_circle, 0, 0);
        lv_obj_set_style_pad_all(g_dashboard.alert_circle, 0, 0);
        lv_obj_set_style_radius(g_dashboard.alert_circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_dashboard.alert_circle, lv_color_make(0xFF, 0x1A, 0x1A), 0);
        lv_obj_set_style_bg_opa(g_dashboard.alert_circle, LV_OPA_COVER, 0);
        lv_obj_add_flag(g_dashboard.alert_circle, LV_OBJ_FLAG_HIDDEN);

        g_dashboard.odometer_label = lv_label_create(screen);
        lv_obj_set_pos(g_dashboard.odometer_label, circle_x + circle_d + 12, circle_y - 3);
        lv_obj_set_style_text_color(g_dashboard.odometer_label, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(g_dashboard.odometer_label, DASHBOARD_FONT_SMALL, 0);
        dashboard_set_label_text(g_dashboard.odometer_label, "41.2 km");

        g_dashboard.alert_label = lv_label_create(screen);
        lv_obj_set_pos(g_dashboard.alert_label, ALERT_TOUCH_X_MIN, circle_y - 3);
        lv_obj_set_size(g_dashboard.alert_label, ALERT_TOUCH_X_MAX - ALERT_TOUCH_X_MIN, circle_d + 6);
        lv_label_set_long_mode(g_dashboard.alert_label, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(g_dashboard.alert_label, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(g_dashboard.alert_label, DASHBOARD_FONT_SMALL, 0);
        dashboard_set_label_text(g_dashboard.alert_label, "");
        lv_obj_add_flag(g_dashboard.alert_label, LV_OBJ_FLAG_HIDDEN);
        update_alert_ui();
    }

    lv_obj_t * speed_left_separator = lv_obj_create(screen);
    lv_obj_remove_style_all(speed_left_separator);
    lv_obj_set_pos(speed_left_separator, UI_LEFT_PANEL_WIDTH, UI_MIDDLE_Y);
    lv_obj_set_size(speed_left_separator, 1, UI_MIDDLE_HEIGHT);
    lv_obj_set_style_bg_color(speed_left_separator, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(speed_left_separator, LV_OPA_COVER, 0);

    lv_obj_t * speed_right_separator = lv_obj_create(screen);
    lv_obj_remove_style_all(speed_right_separator);
    lv_obj_set_pos(speed_right_separator, UI_RIGHT_PANEL_X, UI_MIDDLE_Y);
    lv_obj_set_size(speed_right_separator, 1, UI_MIDDLE_HEIGHT);
    lv_obj_set_style_bg_color(speed_right_separator, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(speed_right_separator, LV_OPA_COVER, 0);

    lv_screen_load(screen);
    dashboard_apply_data(DASH_DIRTY_ALL);
    dashboard_backlight_init();
    g_dashboard_initialized = 1U;
    LED_Diag_SetBootStage(5);   /* both LEDs off: UI init fully complete */
}

void Dashboard_UI_SubmitTouchState(uint16_t x, uint16_t y, uint8_t pressed)
{
    static uint8_t last_pressed = 0U;
    static uint32_t last_lap_toggle_tick = 0U;
    static uint32_t last_alert_touch_tick = 0U;
    static uint32_t last_night_toggle_tick = 0U;
    uint32_t now = HAL_GetTick();
    uint8_t in_lap_area = (uint8_t)((x >= LAP_TOUCH_X_MIN) && (x < LAP_TOUCH_X_MAX) &&
                                    (y >= LAP_TOUCH_Y_MIN) && (y < LAP_TOUCH_Y_MAX));
    uint8_t in_alert_area = (uint8_t)((x >= ALERT_TOUCH_X_MIN) && (x < ALERT_TOUCH_X_MAX) &&
                                      (y >= ALERT_TOUCH_Y_MIN) && (y < ALERT_TOUCH_Y_MAX));
    uint8_t in_night_icon = (uint8_t)((x >= NIGHT_TOUCH_X_MIN) && (x < NIGHT_TOUCH_X_MAX) &&
                                      (y >= NIGHT_TOUCH_Y_MIN) && (y < NIGHT_TOUCH_Y_MAX));

    if((pressed != 0U) && (last_pressed == 0U)) {
        if((in_lap_area != 0U) && ((now - last_lap_toggle_tick) >= 200U)) {
            last_lap_toggle_tick = now;
            g_pending_lap_toggle = 1U;
        }
        else if((in_alert_area != 0U) && ((now - last_alert_touch_tick) >= 200U)) {
            last_alert_touch_tick = now;
            if(g_pending_alert_pop_count < 255U) {
                g_pending_alert_pop_count++;
            }
        }
        else if((in_night_icon != 0U) && ((now - last_night_toggle_tick) >= 200U)) {
            last_night_toggle_tick = now;
            g_pending_night_toggle = 1U;
        }
    }

    last_pressed = pressed != 0U ? 1U : 0U;
}

const dashboard_data_t * Dashboard_UI_GetCurrentData(void)
{
    return &g_dashboard_data;
}

uint8_t Dashboard_UI_IsStartupComplete(void)
{
    return g_dashboard_initialized;
}

/* USER CODE END dashboard_ui */
