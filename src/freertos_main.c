/**
 * @file    Freertos main file
 * @author  MootSeeker
 * @date    2024-09-02
 * @brief   Main file for FreeRTOS tasks and hooks.
 * @license MIT License
 */

#include "lvgl/lvgl.h"

#include "FreeRTOS.h"
#include "task.h"

#if LV_USE_OS == LV_OS_FREERTOS || defined(__APPLE__)

#include "hal/hal.h"
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#if defined(__APPLE__)
#include <unistd.h>
/* macOS: Cocoa 要求窗口创建与事件循环必须在主线程, 而 FreeRTOS POSIX 端口的任务跑在子线程。
 * 故 main() 直接在主线程运行 LVGL 循环而不启动调度器, 延时改用 usleep。 */
#define SIM_LVGL_LOOP_DELAY()   usleep(30 * 1000)
#else
#define SIM_LVGL_LOOP_DELAY()   vTaskDelay(pdMS_TO_TICKS(30)) /* Short delay for the RTOS scheduler */
#endif

typedef struct {
    lv_obj_t * speed_digit_container;
    lv_obj_t * speed_segments[2][7];
    lv_obj_t * speed_unit;
    lv_obj_t * mode_tile;
    lv_obj_t * mode_value;
    lv_obj_t * slip_value;
    lv_obj_t * slip_bars[7];
    lv_obj_t * soc_value;
    lv_obj_t * battery_fill;
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
    lv_obj_t * key_target;
    /* S mode specific UI elements */
    lv_obj_t * accel_label;
    lv_obj_t * accel_value;
    lv_obj_t * brake_label;
    lv_obj_t * brake_value;
    lv_obj_t * speed_box;
    /* Non-S mode top area elements */
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
    lv_obj_t * laps_current_label;
    lv_obj_t * laps_current_value;
    lv_obj_t * laps_left_label;
    lv_obj_t * laps_left_value;
    lv_obj_t * throttle_bar_fill;
    lv_obj_t * brake_bar_fill;
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
    /* Night-mode toggle: circular EYE icon, dims the screen via a 40% mask */
    lv_obj_t * night_icon;
    lv_obj_t * night_icon_label;
    lv_obj_t * night_mask;
} dashboard_ui_t;

static dashboard_ui_t g_dashboard;

typedef enum {
    DRIVE_MODE_S = 0,
    DRIVE_MODE_Q,
    DRIVE_MODE_C,
    DRIVE_MODE_E
} drive_mode_t;

static drive_mode_t g_drive_mode = DRIVE_MODE_S;
static int g_slip_level = 0;
static bool g_lap_recording_active = false;
static int speed = 24;
static int SOC = 24;
static int Mode_Index = 0;
static int g_aps_pct = 0;     /* accelerator pedal position 0-100 */
static int g_brake_pct = 10;  /* brake pedal position 0-100 */
static int current_lap_time = 0;  /* hundredths of a second */
static int last_lap_time = 0;     /* hundredths of a second */
static int best_lap_time = 0;     /* hundredths of a second */
static int lap_delta = 0;         /* hundredths of a second, positive means slower */
static int laps_current = 0;
static int laps_left = 75;
static int vehicle_distance_m = 0;
static int torque_M[4] = {24, 24, 24, 24};
static int RPM[4] = {24, 24, 24, 24};
static int Motor_Power_Live[4] = {0, 0, 0, 0};
static int Motor_Power_Peak[4] = {24, 24, 23, 23};
static int Motor_Temp[4] = {48, 47, 49, 50};
static int Sum_Voltage = 24;
static int Top_Temperature = 24;
static int Sum_I = 24;
static int Power_Live = 5;
static int Power_Peak = 36;
/* Demo values for the new readouts; MCU fills these from CAN. */
/* DBC BO_1287 Debug7: per-wheel inverter temperature, LF/LR/RF/RR order */
static int Inverter_Temp[4] = {52, 51, 53, 54};
/* DBC BO_1283/1284: per-wheel diagnostic number, 0 = OK */
static uint32_t Diag_Num[4] = {0, 0x0231, 0, 0};
/* DBC BO_1440 PDM_LowVoltageBus: 13821mV / -1240cA / 1712dW demo */
static int LV_Bus_Voltage_mV = 13821;
static int LV_Bus_Current_cA = -1240;
static int LV_Bus_Power_dW = 1712;
/* DBC BO_1442: three fan RPM + two measured PWM duties (fan3 none) */
static int Fan_RPM[3] = {3200, 3150, 2900};
static int Fan_PWM_Duty[2] = {85, 85};
/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR; segment order is outside-to-inside. */
static int Tire_Temp[4][4] = {
    {21, 23, 25, 27},
    {22, 24, 26, 28},
    {20, 22, 24, 26},
    {23, 25, 27, 29}
};
static uint16_t g_tire_demo_phase = 4U;
static bool Motor_FL_Online = true;
static bool Motor_FR_Online = true;
static bool Motor_RL_Online = true;
static bool Motor_RR_Online = true;
static int g_signal_level = 0;
static uint32_t g_demo_alert_index = 0U;
static volatile bool g_simulator_exit_requested = false;
/* Night mode: 0 = day, 1 = night. The simulator fakes the 60% backlight with
 * a 40% black overlay; the MCU dims the real backlight PWM instead. */
static uint8_t g_night_mode = 0U;

static void apply_main_dashboard_defaults(void);

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

#define UI_SPEED_BOX_Y 72
#define UI_SPEED_BOX_HEIGHT 210

#define UI_DELTA_BAR_X 450
#define UI_DELTA_BAR_Y 17
#define UI_DELTA_BAR_W 84
#define UI_DELTA_BAR_H 16
#define UI_DELTA_BAR_INNER_X 6
#define UI_DELTA_BAR_CENTER_X 42
#define UI_DELTA_BAR_RIGHT_X 78
#define UI_DELTA_BAR_FILL_H 15
#define UI_SLIP_BAR_COUNT 7U

#define UI_BATTERY_FILL_MAX_W 50
/* Pedal bars live beside the speed digits so they stay in the driver's primary
 * sight line. The bars are children of the full-width middle panel: the speed
 * digit container spans x307..493, so the bars sit at x222..270 (brake) and
 * x530..578 (throttle), inside the 180..620 centre column but clear of the
 * digits and the km/h caption. Percentages are drawn under each bar. */
#define UI_PEDAL_BAR_W 48
#define UI_PEDAL_BAR_H 196
#define UI_PEDAL_BAR_TOP_Y 84
#define UI_PEDAL_BRAKE_X 222
#define UI_PEDAL_THROTTLE_X 530
#define UI_PEDAL_PCT_LABEL_Y 286
#define UI_PEDAL_NAME_LABEL_Y 310

/* Night-mode toggle icon, bottom of the left vehicle panel (same spot as the
 * MCU build). The overlay dims the whole screen by 55% to fake 45% backlight. */
#define UI_NIGHT_ICON_X 72
#define UI_NIGHT_ICON_Y 292
#define UI_NIGHT_ICON_SIZE 40
#define UI_NIGHT_MASK_OPA ((lv_opa_t)140) /* 140/255 = 55% dimming */

#define UI_SPEED_DIGIT_W 84
#define UI_SPEED_DIGIT_H 140
#define UI_SPEED_SEG_THICKNESS 12
#define UI_SPEED_DIGIT_GAP 18
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
static lv_color_t temp_to_color(int32_t temp)
{
    const lv_color_t cold = lv_color_hex(0x005CFF);
    const lv_color_t cool = lv_color_hex(0x00DFFF);
    const lv_color_t working = lv_color_hex(0x00D060);
    const lv_color_t warm = lv_color_hex(0xFFE000);
    const lv_color_t hot = lv_color_hex(0xFF7800);
    const lv_color_t overheat = lv_color_hex(0xFF2020);
    uint8_t mix;

    if(temp <= 20) return cold;
    if(temp < 40) {
        mix = (uint8_t)(((temp - 20) * 255) / 20);
        return lv_color_mix(cool, cold, mix);
    }
    if(temp < 65) {
        mix = (uint8_t)(((temp - 40) * 255) / 25);
        return lv_color_mix(working, cool, mix);
    }
    if(temp < 80) {
        mix = (uint8_t)(((temp - 65) * 255) / 15);
        return lv_color_mix(warm, working, mix);
    }
    if(temp < 95) {
        mix = (uint8_t)(((temp - 80) * 255) / 15);
        return lv_color_mix(hot, warm, mix);
    }
    if(temp < 110) {
        mix = (uint8_t)(((temp - 95) * 255) / 15);
        return lv_color_mix(overheat, hot, mix);
    }
    return overheat;
}

/* Match the MCU implementation: draw the four bands without child objects. */
static void tire_draw_event_cb(lv_event_t * event)
{
    lv_obj_t * wheel = lv_event_get_current_target_obj(event);
    lv_layer_t * layer = lv_event_get_layer(event);
    lv_area_t wheel_coords;
    lv_draw_rect_dsc_t rect_dsc;
    int wheel_index = -1;

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
        rect_dsc.bg_color = temp_to_color(Tire_Temp[wheel_index][segment]);
        lv_draw_rect(layer, &rect_dsc, &segment_coords);
    }
}

/* Draw 25/50/75% tick lines inside a pedal bar track without extra objects,
 * mirroring the MCU implementation. The fill child covers ticks below level. */
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

/* Night mode: flip the EYE glyph and show/hide the dimming overlay. */
static void update_night_mode_ui(void)
{
    if(g_dashboard.night_icon_label != NULL) {
        lv_label_set_text(g_dashboard.night_icon_label,
                          g_night_mode ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }
    if(g_dashboard.night_icon != NULL) {
        lv_obj_set_style_bg_color(g_dashboard.night_icon,
                                  g_night_mode ? lv_color_hex(0x555555) : UI_BG_COLOR, 0);
    }
    if(g_dashboard.night_mask != NULL) {
        if(g_night_mode) lv_obj_clear_flag(g_dashboard.night_mask, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_dashboard.night_mask, LV_OBJ_FLAG_HIDDEN);
    }
}

static void night_icon_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    g_night_mode = g_night_mode ? 0U : 1U;
    update_night_mode_ui();
}

static void apply_vehicle_ui(void)
{
    lv_obj_t * lightning[4] = {
        g_dashboard.lightning_fl, g_dashboard.lightning_rl,
        g_dashboard.lightning_fr, g_dashboard.lightning_rr
    };
    bool online[4] = {
        Motor_FL_Online, Motor_RL_Online, Motor_FR_Online, Motor_RR_Online
    };
    char text_buf[12];

    for(uint32_t wheel = 0U; wheel < 4U; wheel++) {
        int maximum_temp = Tire_Temp[wheel][0];
        for(uint32_t segment = 0U; segment < 4U; segment++) {
            if(Tire_Temp[wheel][segment] > maximum_temp) {
                maximum_temp = Tire_Temp[wheel][segment];
            }
        }
        lv_obj_invalidate(wheel == 0U ? g_dashboard.wheel_fl :
                          wheel == 1U ? g_dashboard.wheel_rl :
                          wheel == 2U ? g_dashboard.wheel_fr : g_dashboard.wheel_rr);
        lv_snprintf(text_buf, sizeof(text_buf), "%d°", maximum_temp);
        lv_label_set_text(g_dashboard.tire_max_labels[wheel], text_buf);
        {
            lv_obj_t * tire = (wheel == 0U) ? g_dashboard.wheel_fl :
                              (wheel == 1U) ? g_dashboard.wheel_rl :
                              (wheel == 2U) ? g_dashboard.wheel_fr : g_dashboard.wheel_rr;
            lv_coord_t label_x;
            lv_coord_t label_max_x;

            lv_obj_update_layout(g_dashboard.tire_max_labels[wheel]);
            lv_obj_align_to(g_dashboard.tire_max_labels[wheel], tire,
                            (wheel < 2U) ? LV_ALIGN_OUT_LEFT_MID : LV_ALIGN_OUT_RIGHT_MID,
                            (wheel < 2U) ? -2 : 2, UI_TIRE_TEMP_LABEL_Y);
            label_x = lv_obj_get_x(g_dashboard.tire_max_labels[wheel]);
            label_max_x = UI_LEFT_PANEL_WIDTH - 1 -
                          lv_obj_get_width(g_dashboard.tire_max_labels[wheel]);
            if(label_max_x < 1) label_max_x = 1;
            if(label_x < 1) lv_obj_set_x(g_dashboard.tire_max_labels[wheel], 1);
            else if(label_x > label_max_x) {
                lv_obj_set_x(g_dashboard.tire_max_labels[wheel], label_max_x);
            }
        }
        if(online[wheel]) lv_obj_clear_flag(lightning[wheel], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(lightning[wheel], LV_OBJ_FLAG_HIDDEN);
    }

}

static void tire_demo_timer_cb(lv_timer_t * timer)
{
    static const int8_t temperature_offset[4][4] = {
        {-3, -1,  1,  3},
        {-2,  0,  2,  4},
        {-4, -2,  0,  2},
        {-1,  1,  3,  5}
    };
    int base_temp;

    LV_UNUSED(timer);
    if(g_tire_demo_phase <= 90U) {
        base_temp = 20 + (int)g_tire_demo_phase;
    }
    else {
        base_temp = 110 - (int)(g_tire_demo_phase - 90U);
    }

    for(uint32_t wheel = 0U; wheel < 4U; wheel++) {
        for(uint32_t segment = 0U; segment < 4U; segment++) {
            int temperature = base_temp + temperature_offset[wheel][segment];
            if(temperature < 20) temperature = 20;
            if(temperature > 110) temperature = 110;
            Tire_Temp[wheel][segment] = temperature;
        }
    }

    g_tire_demo_phase++;
    if(g_tire_demo_phase >= 180U) g_tire_demo_phase = 0U;

    /* Pedal/fan demo motion so the new readouts come alive in the simulator. */
    {
        static uint16_t pedal_phase = 0U;
        char text_buf[16];
        int aps = (pedal_phase < 90U) ? (int)pedal_phase : (180 - (int)pedal_phase);
        int brake = (pedal_phase >= 90U) ? (((int)pedal_phase - 90) * 100 / 90) : 0;
        uint32_t fan;

        g_aps_pct = aps * 100 / 90;
        g_brake_pct = brake;
        pedal_phase = (pedal_phase + 1U) % 180U;

        if(g_dashboard.throttle_bar_fill != NULL) {
            int height = g_aps_pct == 0 ? 1 : (g_aps_pct * UI_PEDAL_BAR_H / 100);
            lv_obj_set_pos(g_dashboard.throttle_bar_fill, 0, UI_PEDAL_BAR_H - height);
            lv_obj_set_size(g_dashboard.throttle_bar_fill, UI_PEDAL_BAR_W, height);
            lv_snprintf(text_buf, sizeof(text_buf), "%d%%", g_aps_pct);
            lv_label_set_text(g_dashboard.throttle_pct_label, text_buf);
        }
        if(g_dashboard.brake_bar_fill != NULL) {
            int height = g_brake_pct == 0 ? 1 : (g_brake_pct * UI_PEDAL_BAR_H / 100);
            lv_obj_set_pos(g_dashboard.brake_bar_fill, 0, UI_PEDAL_BAR_H - height);
            lv_obj_set_size(g_dashboard.brake_bar_fill, UI_PEDAL_BAR_W, height);
            lv_snprintf(text_buf, sizeof(text_buf), "%d%%", g_brake_pct);
            lv_label_set_text(g_dashboard.brake_pct_label, text_buf);
        }

        Fan_PWM_Duty[0] = 40 + g_aps_pct * 3 / 5;
        if(Fan_PWM_Duty[0] > 100) Fan_PWM_Duty[0] = 100;
        Fan_PWM_Duty[1] = Fan_PWM_Duty[0];
        Fan_RPM[0] = 1200 + Fan_PWM_Duty[0] * 24;
        Fan_RPM[1] = Fan_RPM[0] - 60;
        Fan_RPM[2] = Fan_RPM[0] - 180;
        for(fan = 0U; fan < 3U; fan++) {
            if(g_dashboard.fan_value_labels[fan] == NULL) continue;
            if(fan < 2U) {
                lv_snprintf(text_buf, sizeof(text_buf), "%d%% %d",
                            Fan_PWM_Duty[fan], Fan_RPM[fan]);
            }
            else {
                lv_snprintf(text_buf, sizeof(text_buf), "-- %d", Fan_RPM[fan]);
            }
            lv_label_set_text(g_dashboard.fan_value_labels[fan], text_buf);
        }
    }

    apply_vehicle_ui();
}

static void apply_drive_mode_ui(void)
{
    const char * mode_text = "S";
    lv_color_t tile_color = lv_palette_main(LV_PALETTE_RED);

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
            break;
    }

    if(g_dashboard.mode_tile != NULL) {
        lv_obj_set_style_bg_color(g_dashboard.mode_tile, tile_color, 0);
    }
    if(g_dashboard.mode_value != NULL) {
        lv_label_set_text(g_dashboard.mode_value, mode_text);
        lv_obj_set_style_text_color(g_dashboard.mode_value, lv_color_white(), 0);
    }
    if(g_dashboard.laps_current_label != NULL) {
        lv_label_set_text(g_dashboard.laps_current_label,
                          (g_drive_mode == DRIVE_MODE_S) ? "Run" : "Lap");
    }
    if(g_dashboard.laps_left_label != NULL) {
        lv_label_set_text(g_dashboard.laps_left_label,
                          (g_drive_mode == DRIVE_MODE_S) ? "Dist" : "Left");
    }
}

static void update_slip_level_ui(void)
{
    char text_buf[16];

    if(g_dashboard.slip_value == NULL) return;

    lv_snprintf(text_buf, sizeof(text_buf), "SLIP %d", g_slip_level);
    lv_label_set_text(g_dashboard.slip_value, text_buf);
    for(uint32_t index = 0U; index < UI_SLIP_BAR_COUNT; index++) {
        if(g_dashboard.slip_bars[index] == NULL) continue;
        lv_obj_set_style_bg_color(g_dashboard.slip_bars[index],
                                  (index < (uint32_t)g_slip_level) ?
                                  UI_TEXT_COLOR : UI_SEGMENT_OFF_COLOR, 0);
        lv_obj_set_style_bg_opa(g_dashboard.slip_bars[index], LV_OPA_COVER, 0);
    }
}

static void update_layout_by_mode(void)
{
    if(g_dashboard.accel_label != NULL) lv_obj_add_flag(g_dashboard.accel_label, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.accel_value != NULL) lv_obj_add_flag(g_dashboard.accel_value, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.brake_label != NULL) lv_obj_add_flag(g_dashboard.brake_label, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.brake_value != NULL) lv_obj_add_flag(g_dashboard.brake_value, LV_OBJ_FLAG_HIDDEN);

    if(g_dashboard.lap_current_label != NULL) lv_obj_clear_flag(g_dashboard.lap_current_label, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.lap_current_value != NULL) lv_obj_clear_flag(g_dashboard.lap_current_value, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.lap_last_label != NULL) lv_obj_clear_flag(g_dashboard.lap_last_label, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.lap_last_value != NULL) lv_obj_clear_flag(g_dashboard.lap_last_value, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.lap_best_label != NULL) lv_obj_clear_flag(g_dashboard.lap_best_label, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.lap_best_value != NULL) lv_obj_clear_flag(g_dashboard.lap_best_value, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.delta_bar_track != NULL) lv_obj_clear_flag(g_dashboard.delta_bar_track, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.delta_bar_fill != NULL) lv_obj_clear_flag(g_dashboard.delta_bar_fill, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.delta_bar_center != NULL) lv_obj_clear_flag(g_dashboard.delta_bar_center, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.delta_value != NULL) lv_obj_clear_flag(g_dashboard.delta_value, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.laps_current_label != NULL) lv_obj_clear_flag(g_dashboard.laps_current_label, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.laps_current_value != NULL) lv_obj_clear_flag(g_dashboard.laps_current_value, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.laps_left_label != NULL) lv_obj_clear_flag(g_dashboard.laps_left_label, LV_OBJ_FLAG_HIDDEN);
    if(g_dashboard.laps_left_value != NULL) lv_obj_clear_flag(g_dashboard.laps_left_value, LV_OBJ_FLAG_HIDDEN);
}

static void format_lap_time(char * buf, size_t buf_size, int hundredths)
{
    int minutes;
    int seconds;
    int fraction;

    if(hundredths < 0) hundredths = 0;

    minutes = hundredths / 6000;
    seconds = (hundredths / 100) % 60;
    fraction = hundredths % 100;
    lv_snprintf(buf, buf_size, "%d:%02d.%02d", minutes, seconds, fraction);
}

static void update_lap_delta_ui(void)
{
    char delta_buf[16];
    int delta_abs;
    int max_abs = 500;
    lv_coord_t center_tick_x = UI_DELTA_BAR_CENTER_X;
    lv_coord_t right_tick_x = UI_DELTA_BAR_RIGHT_X;
    lv_coord_t fill_y = 0;
    lv_coord_t fill_height = UI_DELTA_BAR_FILL_H;
    lv_coord_t fill_width;

    if(g_dashboard.delta_bar_track == NULL || g_dashboard.delta_bar_fill == NULL || g_dashboard.delta_value == NULL) {
        return;
    }

    delta_abs = lap_delta < 0 ? -lap_delta : lap_delta;

    if(lap_delta > 0) {
        lv_snprintf(delta_buf, sizeof(delta_buf), "+%d.%02ds", delta_abs / 100, delta_abs % 100);
        lv_obj_set_style_text_color(g_dashboard.delta_value, lv_palette_main(LV_PALETTE_RED), 0);
    }
    else if(lap_delta < 0) {
        lv_snprintf(delta_buf, sizeof(delta_buf), "-%d.%02ds", delta_abs / 100, delta_abs % 100);
        lv_obj_set_style_text_color(g_dashboard.delta_value, lv_palette_main(LV_PALETTE_GREEN), 0);
    }
    else {
        lv_snprintf(delta_buf, sizeof(delta_buf), "%d.%02ds", 0, 0);
        lv_obj_set_style_text_color(g_dashboard.delta_value, UI_TEXT_COLOR, 0);
    }

    lv_label_set_text(g_dashboard.delta_value, delta_buf);
    lv_obj_clear_flag(g_dashboard.delta_value, LV_OBJ_FLAG_HIDDEN);

    if(delta_abs > max_abs) delta_abs = max_abs;

    fill_width = (lv_coord_t)((delta_abs * (right_tick_x - center_tick_x)) / max_abs);
    if(fill_width < 1 && lap_delta != 0) fill_width = 1;

    if(lap_delta < 0) {
        lv_obj_set_pos(g_dashboard.delta_bar_fill, center_tick_x, fill_y);
        lv_obj_set_size(g_dashboard.delta_bar_fill, fill_width, fill_height);
        lv_obj_set_style_bg_color(g_dashboard.delta_bar_fill, lv_palette_main(LV_PALETTE_GREEN), 0);
    }
    else if(lap_delta > 0) {
        lv_obj_set_pos(g_dashboard.delta_bar_fill, center_tick_x - fill_width, fill_y);
        lv_obj_set_size(g_dashboard.delta_bar_fill, fill_width, fill_height);
        lv_obj_set_style_bg_color(g_dashboard.delta_bar_fill, lv_palette_main(LV_PALETTE_RED), 0);
    }
    else {
        lv_obj_set_pos(g_dashboard.delta_bar_fill, center_tick_x, fill_y);
        lv_obj_set_size(g_dashboard.delta_bar_fill, 1, fill_height);
        lv_obj_set_style_bg_color(g_dashboard.delta_bar_fill, lv_color_hex(0x808080), 0);
    }
}

static void sync_mode_from_index(void)
{
    switch(Mode_Index) {
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

static void simulator_set_drive_mode(int mode_index)
{
    char value_buf[8];

    if(mode_index < DRIVE_MODE_S || mode_index > DRIVE_MODE_E) return;

    Mode_Index = mode_index;
    sync_mode_from_index();
    current_lap_time = 0;
    last_lap_time = 0;
    best_lap_time = 0;
    lap_delta = 0;
    laps_current = 0;
    laps_left = (g_drive_mode == DRIVE_MODE_S) ? 75 : 0;
    apply_drive_mode_ui();
    update_layout_by_mode();
    format_lap_time(value_buf, sizeof(value_buf), current_lap_time);
    lv_label_set_text(g_dashboard.lap_current_value, value_buf);
    lv_label_set_text(g_dashboard.lap_last_value, value_buf);
    lv_label_set_text(g_dashboard.lap_best_value, value_buf);
    lv_label_set_text(g_dashboard.laps_current_value, "00");
    lv_snprintf(value_buf, sizeof(value_buf), "%02d", laps_left);
    lv_label_set_text(g_dashboard.laps_left_value, value_buf);
    update_lap_delta_ui();
}

static void simulator_set_slip_level(int slip_level)
{
    if(slip_level < 0 || slip_level > 7) return;

    g_slip_level = slip_level;
    update_slip_level_ui();
}

static void simulator_toggle_lap_recording(void)
{
    g_lap_recording_active = !g_lap_recording_active;
    if(g_dashboard.alert_circle == NULL) return;

    if(g_lap_recording_active) {
        lv_obj_set_style_bg_color(g_dashboard.alert_circle, lv_color_make(0xFF, 0x1A, 0x1A), 0);
        lv_obj_clear_flag(g_dashboard.alert_circle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_dashboard.alert_circle);
    }
    else {
        lv_obj_add_flag(g_dashboard.alert_circle, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(g_dashboard.alert_circle);
}

static void simulator_set_signal_level(int level)
{
    static const lv_color_t grey = {0x55, 0x55, 0x55};
    static const lv_color_t red = {0x1A, 0x1A, 0xFF};
    static const lv_color_t yellow = {0x00, 0xD4, 0xFF};
    static const lv_color_t green = {0x5A, 0xD4, 0x2F};
    lv_color_t colors[4];

    if(level < 0) level = 0;
    if(level > 4) level = 4;
    g_signal_level = level;

    switch(level) {
        case 0:
            colors[0] = grey; colors[1] = grey; colors[2] = grey; colors[3] = grey;
            break;
        case 1:
            colors[0] = red; colors[1] = grey; colors[2] = grey; colors[3] = grey;
            break;
        case 2:
            colors[0] = yellow; colors[1] = yellow; colors[2] = grey; colors[3] = grey;
            break;
        case 3:
            colors[0] = green; colors[1] = green; colors[2] = green; colors[3] = grey;
            break;
        default:
            colors[0] = green; colors[1] = green; colors[2] = green; colors[3] = green;
            break;
    }

    for(uint32_t index = 0U; index < 4U; index++) {
        if(g_dashboard.signal_bars[index] != NULL) {
            lv_obj_set_style_bg_color(g_dashboard.signal_bars[index], colors[index], 0);
            lv_obj_invalidate(g_dashboard.signal_bars[index]);
        }
    }
}

static void simulator_cycle_alert(void)
{
    static const char * const demo_alerts[] = {
        "",
        "1 GPS NOT FIXED",
        "1 CAN TIMEOUT",
        "1 MOTOR FAULT"
    };

    if(g_dashboard.alert_label == NULL) return;
    g_demo_alert_index = (g_demo_alert_index + 1U) %
                         (sizeof(demo_alerts) / sizeof(demo_alerts[0]));
    lv_label_set_text(g_dashboard.alert_label, demo_alerts[g_demo_alert_index]);
    if(g_demo_alert_index == 0U) {
        lv_obj_add_flag(g_dashboard.alert_label, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(g_dashboard.alert_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_dashboard.alert_label);
    }
    lv_obj_invalidate(g_dashboard.alert_label);
}

static void mode_key_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_KEY) return;

    /* S/Q/C/E or space: mode; 0..7: SLIP; R: recording; F: fault;
     * brackets/G: signal strength. */
    uint32_t key = lv_event_get_key(e);
    if(key == LV_KEY_ESC || key == 'x' || key == 'X') {
        g_simulator_exit_requested = true;
        return;
    }

    if(key == ' ') {
        // 空格键：循环切换模式
        simulator_set_drive_mode((Mode_Index + 1) % 4);
    }
    else if(key == 's' || key == 'S') {
        simulator_set_drive_mode(DRIVE_MODE_S);
    }
    else if(key == 'q' || key == 'Q') {
        simulator_set_drive_mode(DRIVE_MODE_Q);
    }
    else if(key == 'e' || key == 'E') {
        simulator_set_drive_mode(DRIVE_MODE_E);
    }
    else if(key == 'c' || key == 'C') {
        simulator_set_drive_mode(DRIVE_MODE_C);
    }
    else if(key >= '0' && key <= '7') {
        simulator_set_slip_level((int)(key - '0'));
    }
    else if(key == 'r' || key == 'R') {
        simulator_toggle_lap_recording();
    }
    else if(key == 'n' || key == 'N') {
        /* Same path as clicking the EYE icon: toggle night dimming. */
        g_night_mode = g_night_mode ? 0U : 1U;
        update_night_mode_ui();
    }
    else if(key == 'f' || key == 'F') {
        simulator_cycle_alert();
    }
    else if(key == '[') {
        simulator_set_signal_level(g_signal_level - 1);
    }
    else if(key == ']') {
        simulator_set_signal_level(g_signal_level + 1);
    }
    else if(key == 'g' || key == 'G') {
        simulator_set_signal_level((g_signal_level + 1) % 5);
    }
    else {
        return;
    }
}

// ........................................................................................................
/**
 * @brief   Malloc failed hook
 *
 * This function is called when a memory allocation (malloc) fails. It logs the available heap size and enters
 * an infinite loop to halt the system.
 *
 * @param   None
 * @return  None
 */
void vApplicationMallocFailedHook(void)
{
    printf("Malloc failed! Available heap: %ld bytes\n", xPortGetFreeHeapSize());
    fflush(stdout);
    exit(EXIT_FAILURE);
}

// ........................................................................................................
/**
 * @brief   Idle hook
 *
 * This function is called when the system is idle. It can be used for low-power mode operations or other
 * maintenance tasks that need to run when the CPU is not busy.
 *
 * @param   None
 * @return  None
 */
void vApplicationIdleHook(void) {}

// ........................................................................................................
/**
 * @brief   Stack overflow hook
 *
 * This function is called when a stack overflow is detected in a task. It logs the task name and enters
 * an infinite loop to halt the system.
 *
 * @param   xTask        Handle of the task that caused the stack overflow
 * @param   pcTaskName   Name of the task that caused the stack overflow
 * @return  None
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("Stack overflow in task %s\n", pcTaskName);
    LV_UNUSED(xTask);
    fflush(stdout);
    exit(EXIT_FAILURE);
}

// ........................................................................................................
/**
 * @brief   Tick hook
 *
 * This function is called on each tick interrupt. It can be used to execute periodic operations
 * that need to occur at a fixed time interval.
 *
 * @param   None
 * @return  None
 */
void vApplicationTickHook(void) {}

// ........................................................................................................
/**
 * @brief   Create main dashboard screen
 *
 * This function creates a simple LVGL screen with a "Hello, World!" label centered on the screen.
 *
 * @param   None
 * @return  None
 */
static lv_obj_t * create_panel(lv_obj_t * parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                               lv_color_t bg_color, lv_opa_t bg_opa)
{
    lv_obj_t * panel = lv_obj_create(parent);
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

static lv_obj_t * create_value(lv_obj_t * parent, const char * text, lv_color_t color, int32_t font_size)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font_size >= 28 ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
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

static void set_speed_digits(int32_t speed_value)
{
    uint8_t digits[2];

    if(speed_value < 0) speed_value = 0;
    if(speed_value > 99) speed_value = 99;

    digits[0] = (uint8_t)((speed_value / 10) % 10);
    digits[1] = (uint8_t)(speed_value % 10);

    for(uint32_t digit_index = 0; digit_index < 2U; digit_index++) {
        bool hide_digit = (digit_index == 0U) && (digits[digit_index] == 0U);

        for(uint32_t segment_index = 0; segment_index < 7U; segment_index++) {
            set_speed_digit_segment_state(
                g_dashboard.speed_segments[digit_index][segment_index],
                hide_digit ? false : (g_speed_digit_map[digits[digit_index]][segment_index] != 0U));
        }
    }
}

void create_main_dashboard_screen(void)
{
    lv_obj_t * screen = lv_obj_create(NULL);
    if(screen == NULL) {
        printf("Error: Failed to create dashboard screen\n");
        return;
    }

    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t * top_area = create_panel(screen, 0, 0, SIM_HOR_RES, UI_TOP_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(top_area, 0, 0);

    /* S mode: acceleration time info */
    g_dashboard.accel_label = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.accel_label, "0-100km\\h:");
    lv_obj_set_style_text_color(g_dashboard.accel_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.accel_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.accel_label, 16, 16);
    lv_obj_add_flag(g_dashboard.accel_label, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.accel_value = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.accel_value, "0.0s");
    lv_obj_set_style_text_color(g_dashboard.accel_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.accel_value, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.accel_value, 136, 16);
    lv_obj_add_flag(g_dashboard.accel_value, LV_OBJ_FLAG_HIDDEN);

    /* S mode: braking distance info */
    g_dashboard.brake_label = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.brake_label, "0-75m:");
    lv_obj_set_style_text_color(g_dashboard.brake_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.brake_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.brake_label, 248, 16);
    lv_obj_add_flag(g_dashboard.brake_label, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.brake_value = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.brake_value, "0.0m");
    lv_obj_set_style_text_color(g_dashboard.brake_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.brake_value, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.brake_value, 328, 16);
    lv_obj_add_flag(g_dashboard.brake_value, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.lap_current_label = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.lap_current_label, "Cur");
    lv_obj_set_style_text_color(g_dashboard.lap_current_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_current_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.lap_current_label, 316, 16);
    lv_obj_add_flag(g_dashboard.lap_current_label, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.lap_current_value = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.lap_current_value, "0:00.0");
    lv_obj_set_style_text_color(g_dashboard.lap_current_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_current_value, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.lap_current_value, 364, 16);
    lv_obj_add_flag(g_dashboard.lap_current_value, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.lap_last_label = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.lap_last_label, "Pre");
    lv_obj_set_style_text_color(g_dashboard.lap_last_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_last_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.lap_last_label, 164, 16);
    lv_obj_add_flag(g_dashboard.lap_last_label, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.lap_last_value = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.lap_last_value, "0:00.0");
    lv_obj_set_style_text_color(g_dashboard.lap_last_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_last_value, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.lap_last_value, 214, 16);
    lv_obj_add_flag(g_dashboard.lap_last_value, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.lap_best_label = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.lap_best_label, "Best");
    lv_obj_set_style_text_color(g_dashboard.lap_best_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_best_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.lap_best_label, 16, 16);
    lv_obj_add_flag(g_dashboard.lap_best_label, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.lap_best_value = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.lap_best_value, "0:00.0");
    lv_obj_set_style_text_color(g_dashboard.lap_best_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.lap_best_value, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.lap_best_value, 76, 16);
    lv_obj_add_flag(g_dashboard.lap_best_value, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.delta_bar_track = lv_obj_create(top_area);
    lv_obj_remove_style_all(g_dashboard.delta_bar_track);
    lv_obj_set_pos(g_dashboard.delta_bar_track, UI_DELTA_BAR_X, UI_DELTA_BAR_Y);
    lv_obj_set_size(g_dashboard.delta_bar_track, UI_DELTA_BAR_W, UI_DELTA_BAR_H);
    lv_obj_set_style_radius(g_dashboard.delta_bar_track, 0, 0);
    lv_obj_set_style_bg_color(g_dashboard.delta_bar_track, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(g_dashboard.delta_bar_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dashboard.delta_bar_track, 0, 0);
    lv_obj_add_flag(g_dashboard.delta_bar_track, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * delta_bottom_line = lv_obj_create(g_dashboard.delta_bar_track);
    lv_obj_remove_style_all(delta_bottom_line);
    lv_obj_set_pos(delta_bottom_line, UI_DELTA_BAR_INNER_X, UI_DELTA_BAR_FILL_H);
    lv_obj_set_size(delta_bottom_line, UI_DELTA_BAR_RIGHT_X - UI_DELTA_BAR_INNER_X, 1);
    lv_obj_set_style_bg_color(delta_bottom_line, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(delta_bottom_line, LV_OPA_COVER, 0);

    g_dashboard.delta_bar_fill = lv_obj_create(g_dashboard.delta_bar_track);
    lv_obj_remove_style_all(g_dashboard.delta_bar_fill);
    lv_obj_set_pos(g_dashboard.delta_bar_fill, UI_DELTA_BAR_CENTER_X, 0);
    lv_obj_set_size(g_dashboard.delta_bar_fill, 1, UI_DELTA_BAR_FILL_H);
    lv_obj_set_style_radius(g_dashboard.delta_bar_fill, 0, 0);
    lv_obj_set_style_bg_color(g_dashboard.delta_bar_fill, lv_color_hex(0x808080), 0);
    lv_obj_set_style_bg_opa(g_dashboard.delta_bar_fill, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_dashboard.delta_bar_fill, LV_OBJ_FLAG_HIDDEN);

    for(uint32_t tick_index = 0; tick_index < 7; tick_index++) {
        lv_obj_t * delta_tick = lv_obj_create(g_dashboard.delta_bar_track);
        lv_obj_remove_style_all(delta_tick);
        lv_coord_t tick_x = (lv_coord_t)(UI_DELTA_BAR_INNER_X +
                          ((tick_index * (UI_DELTA_BAR_RIGHT_X - UI_DELTA_BAR_INNER_X)) / 6));
        lv_obj_set_pos(delta_tick, tick_x, 1);
        lv_obj_set_size(delta_tick, 2, UI_DELTA_BAR_FILL_H - 1);
        lv_obj_set_style_bg_color(delta_tick, UI_BORDER_COLOR, 0);
        lv_obj_set_style_bg_opa(delta_tick, LV_OPA_COVER, 0);
        lv_obj_move_foreground(delta_tick);
    }

    g_dashboard.delta_bar_center = lv_obj_create(g_dashboard.delta_bar_track);
    lv_obj_remove_style_all(g_dashboard.delta_bar_center);
    lv_obj_set_pos(g_dashboard.delta_bar_center, UI_DELTA_BAR_CENTER_X, 1);
    lv_obj_set_size(g_dashboard.delta_bar_center, 2, UI_DELTA_BAR_FILL_H - 1);
    lv_obj_set_style_bg_color(g_dashboard.delta_bar_center, UI_TEXT_COLOR, 0);
    lv_obj_set_style_bg_opa(g_dashboard.delta_bar_center, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_dashboard.delta_bar_center, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.delta_value = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.delta_value, "+0.00s");
    lv_obj_set_style_text_color(g_dashboard.delta_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.delta_value, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(g_dashboard.delta_value, 546, 15);
    lv_obj_add_flag(g_dashboard.delta_value, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.laps_current_label = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.laps_current_label, "LAP");
    lv_obj_set_style_text_color(g_dashboard.laps_current_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.laps_current_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.laps_current_label, 628, 16);
    lv_obj_add_flag(g_dashboard.laps_current_label, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.laps_current_value = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.laps_current_value, "00");
    lv_obj_set_style_text_color(g_dashboard.laps_current_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.laps_current_value, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(g_dashboard.laps_current_value, 664, 16);
    lv_obj_add_flag(g_dashboard.laps_current_value, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.laps_left_label = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.laps_left_label, "LEFT");
    lv_obj_set_style_text_color(g_dashboard.laps_left_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.laps_left_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.laps_left_label, 704, 16);
    lv_obj_add_flag(g_dashboard.laps_left_label, LV_OBJ_FLAG_HIDDEN);

    g_dashboard.laps_left_value = lv_label_create(top_area);
    lv_label_set_text(g_dashboard.laps_left_value, "00");
    lv_obj_set_style_text_color(g_dashboard.laps_left_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.laps_left_value, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(g_dashboard.laps_left_value, 748, 16);
    lv_obj_add_flag(g_dashboard.laps_left_value, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * middle_panel = create_panel(screen, 0, UI_MIDDLE_Y, SIM_HOR_RES, UI_MIDDLE_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(middle_panel, 0, 0);

    lv_obj_t * mode_box = create_panel(middle_panel, UI_RIGHT_PANEL_X, 0, UI_RIGHT_PANEL_WIDTH, UI_MIDDLE_HEIGHT - 9, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(mode_box, 0, 0);
    lv_obj_set_style_border_side(mode_box, LV_BORDER_SIDE_RIGHT, 0);

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
    lv_label_set_text(g_dashboard.soc_value, "72%");
    lv_obj_set_style_text_color(g_dashboard.soc_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.soc_value, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(g_dashboard.soc_value, 86, 14);

    lv_obj_t * power_live_label = lv_label_create(mode_box);
    lv_label_set_text(power_live_label, "P NOW:");
    lv_obj_set_style_text_color(power_live_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(power_live_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(power_live_label, 16, 46);

    g_dashboard.power_live_value = lv_label_create(mode_box);
    lv_label_set_text(g_dashboard.power_live_value, "12kW");
    lv_obj_set_style_text_color(g_dashboard.power_live_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.power_live_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.power_live_value, 88, 46);

    lv_obj_t * power_peak_label = lv_label_create(mode_box);
    lv_label_set_text(power_peak_label, "P PEAK:");
    lv_obj_set_style_text_color(power_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(power_peak_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(power_peak_label, 16, 68);

    g_dashboard.power_peak_value = lv_label_create(mode_box);
    lv_label_set_text(g_dashboard.power_peak_value, "36kW");
    lv_obj_set_style_text_color(g_dashboard.power_peak_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.power_peak_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.power_peak_value, 88, 68);

    lv_obj_t * voltage_label = lv_label_create(mode_box);
    lv_label_set_text(voltage_label, "TOTAL V:");
    lv_obj_set_style_text_color(voltage_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(voltage_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(voltage_label, 16, 92);

    g_dashboard.total_voltage_value = lv_label_create(mode_box);
    lv_label_set_text(g_dashboard.total_voltage_value, "72");
    lv_obj_set_style_text_color(g_dashboard.total_voltage_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.total_voltage_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.total_voltage_value, 98, 92);

    lv_obj_t * current_label = lv_label_create(mode_box);
    lv_label_set_text(current_label, "TOTAL A:");
    lv_obj_set_style_text_color(current_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(current_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(current_label, 16, 114);

    g_dashboard.total_current_value = lv_label_create(mode_box);
    lv_label_set_text(g_dashboard.total_current_value, "15");
    lv_obj_set_style_text_color(g_dashboard.total_current_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.total_current_value, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.total_current_value, 98, 114);

    lv_obj_t * max_temp_label = lv_label_create(mode_box);
    lv_label_set_text(max_temp_label, "MAX T:");
    lv_obj_set_style_text_color(max_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(max_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(max_temp_label, 16, 136);

    g_dashboard.max_temp_value = lv_label_create(mode_box);
    lv_label_set_text(g_dashboard.max_temp_value, "46");
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
        lv_label_set_text(lv_voltage_label, "LV V:");
        lv_obj_set_style_text_color(lv_voltage_label, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lv_voltage_label, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(lv_voltage_label, 16, 166);
        g_dashboard.lv_voltage_value = lv_label_create(mode_box);
        lv_label_set_text(g_dashboard.lv_voltage_value, "0.0V");
        lv_obj_set_style_text_color(g_dashboard.lv_voltage_value, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(g_dashboard.lv_voltage_value, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(g_dashboard.lv_voltage_value, 78, 166);

        lv_obj_t * lv_current_label = lv_label_create(mode_box);
        lv_label_set_text(lv_current_label, "LV A:");
        lv_obj_set_style_text_color(lv_current_label, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lv_current_label, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(lv_current_label, 16, 188);
        g_dashboard.lv_current_value = lv_label_create(mode_box);
        lv_label_set_text(g_dashboard.lv_current_value, "0.0A");
        lv_obj_set_style_text_color(g_dashboard.lv_current_value, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(g_dashboard.lv_current_value, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(g_dashboard.lv_current_value, 78, 188);

        lv_obj_t * lv_power_label = lv_label_create(mode_box);
        lv_label_set_text(lv_power_label, "LV W:");
        lv_obj_set_style_text_color(lv_power_label, UI_TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lv_power_label, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(lv_power_label, 16, 210);
        g_dashboard.lv_power_value = lv_label_create(mode_box);
        lv_label_set_text(g_dashboard.lv_power_value, "0W");
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
            lv_snprintf(fan_label_text, sizeof(fan_label_text), "F%u", (unsigned)(fan + 1U));
            lv_label_set_text(fan_label, fan_label_text);
            lv_obj_set_style_text_color(fan_label, UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(fan_label, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(fan_label, 16, (lv_coord_t)(240 + fan * 22));
            g_dashboard.fan_value_labels[fan] = lv_label_create(mode_box);
            lv_label_set_text(g_dashboard.fan_value_labels[fan], "0% 0");
            lv_obj_set_style_text_color(g_dashboard.fan_value_labels[fan], UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(g_dashboard.fan_value_labels[fan], &lv_font_montserrat_16, 0);
            lv_obj_set_pos(g_dashboard.fan_value_labels[fan], 44, (lv_coord_t)(240 + fan * 22));
        }
    }

    g_dashboard.speed_box = create_panel(middle_panel, UI_CENTER_PANEL_X, UI_SPEED_BOX_Y, UI_CENTER_PANEL_WIDTH, UI_SPEED_BOX_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(g_dashboard.speed_box, 0, 0);
    lv_obj_set_style_pad_all(g_dashboard.speed_box, 0, 0);

    create_speed_digits(g_dashboard.speed_box);

    g_dashboard.speed_unit = lv_label_create(g_dashboard.speed_box);
    lv_label_set_text(g_dashboard.speed_unit, "km/h");
    lv_obj_set_style_text_color(g_dashboard.speed_unit, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_opa(g_dashboard.speed_unit, LV_OPA_60, 0);
    lv_obj_set_style_text_font(g_dashboard.speed_unit, &lv_font_montserrat_24, 0);
    lv_obj_align(g_dashboard.speed_unit, LV_ALIGN_CENTER, 0, 72);

    /* Large pedal bars flanking the speed digits: brake left (red), throttle
     * right (green), each with a live percentage under the bar. */
    {
        lv_obj_t * brake_bar_track = lv_obj_create(middle_panel);
        lv_obj_remove_style_all(brake_bar_track);
        lv_obj_set_pos(brake_bar_track, UI_PEDAL_BRAKE_X, UI_PEDAL_BAR_TOP_Y);
        lv_obj_set_size(brake_bar_track, UI_PEDAL_BAR_W, UI_PEDAL_BAR_H);
        lv_obj_set_style_radius(brake_bar_track, 0, 0);
        lv_obj_set_style_bg_color(brake_bar_track, UI_BG_COLOR, 0);
        lv_obj_set_style_bg_opa(brake_bar_track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(brake_bar_track, 1, 0);
        lv_obj_set_style_border_color(brake_bar_track, UI_BORDER_COLOR, 0);
        lv_obj_add_event_cb(brake_bar_track, pedal_bar_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

        lv_obj_t * brake_limit = lv_obj_create(middle_panel);
        lv_obj_remove_style_all(brake_limit);
        lv_obj_set_pos(brake_limit, UI_PEDAL_BRAKE_X, UI_PEDAL_BAR_TOP_Y - 8);
        lv_obj_set_size(brake_limit, UI_PEDAL_BAR_W, 5);
        lv_obj_set_style_bg_color(brake_limit, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_bg_opa(brake_limit, LV_OPA_COVER, 0);

        g_dashboard.brake_bar_fill = lv_obj_create(brake_bar_track);
        lv_obj_remove_style_all(g_dashboard.brake_bar_fill);
        lv_obj_set_pos(g_dashboard.brake_bar_fill, 0, UI_PEDAL_BAR_H - 1);
        lv_obj_set_size(g_dashboard.brake_bar_fill, UI_PEDAL_BAR_W, 1);
        lv_obj_set_style_bg_color(g_dashboard.brake_bar_fill, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_bg_opa(g_dashboard.brake_bar_fill, LV_OPA_COVER, 0);

        g_dashboard.brake_pct_label = lv_label_create(middle_panel);
        lv_obj_set_pos(g_dashboard.brake_pct_label, UI_PEDAL_BRAKE_X - 12, UI_PEDAL_PCT_LABEL_Y);
        lv_obj_set_width(g_dashboard.brake_pct_label, UI_PEDAL_BAR_W + 24);
        lv_obj_set_style_text_align(g_dashboard.brake_pct_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(g_dashboard.brake_pct_label, "0%");
        lv_obj_set_style_text_color(g_dashboard.brake_pct_label, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(g_dashboard.brake_pct_label, &lv_font_montserrat_18, 0);

        lv_obj_t * brake_name_label = lv_label_create(middle_panel);
        lv_obj_set_pos(brake_name_label, UI_PEDAL_BRAKE_X - 12, UI_PEDAL_NAME_LABEL_Y);
        lv_obj_set_width(brake_name_label, UI_PEDAL_BAR_W + 24);
        lv_obj_set_style_text_align(brake_name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(brake_name_label, "BRAKE");
        lv_obj_set_style_text_color(brake_name_label, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(brake_name_label, &lv_font_montserrat_16, 0);

        lv_obj_t * throttle_bar_track = lv_obj_create(middle_panel);
        lv_obj_remove_style_all(throttle_bar_track);
        lv_obj_set_pos(throttle_bar_track, UI_PEDAL_THROTTLE_X, UI_PEDAL_BAR_TOP_Y);
        lv_obj_set_size(throttle_bar_track, UI_PEDAL_BAR_W, UI_PEDAL_BAR_H);
        lv_obj_set_style_radius(throttle_bar_track, 0, 0);
        lv_obj_set_style_bg_color(throttle_bar_track, UI_BG_COLOR, 0);
        lv_obj_set_style_bg_opa(throttle_bar_track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(throttle_bar_track, 1, 0);
        lv_obj_set_style_border_color(throttle_bar_track, UI_BORDER_COLOR, 0);
        lv_obj_add_event_cb(throttle_bar_track, pedal_bar_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

        lv_obj_t * throttle_limit = lv_obj_create(middle_panel);
        lv_obj_remove_style_all(throttle_limit);
        lv_obj_set_pos(throttle_limit, UI_PEDAL_THROTTLE_X, UI_PEDAL_BAR_TOP_Y - 8);
        lv_obj_set_size(throttle_limit, UI_PEDAL_BAR_W, 5);
        lv_obj_set_style_bg_color(throttle_limit, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_bg_opa(throttle_limit, LV_OPA_COVER, 0);

        g_dashboard.throttle_bar_fill = lv_obj_create(throttle_bar_track);
        lv_obj_remove_style_all(g_dashboard.throttle_bar_fill);
        lv_obj_set_pos(g_dashboard.throttle_bar_fill, 0, UI_PEDAL_BAR_H - 1);
        lv_obj_set_size(g_dashboard.throttle_bar_fill, UI_PEDAL_BAR_W, 1);
        lv_obj_set_style_bg_color(g_dashboard.throttle_bar_fill, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_bg_opa(g_dashboard.throttle_bar_fill, LV_OPA_COVER, 0);

        g_dashboard.throttle_pct_label = lv_label_create(middle_panel);
        lv_obj_set_pos(g_dashboard.throttle_pct_label, UI_PEDAL_THROTTLE_X - 12, UI_PEDAL_PCT_LABEL_Y);
        lv_obj_set_width(g_dashboard.throttle_pct_label, UI_PEDAL_BAR_W + 24);
        lv_obj_set_style_text_align(g_dashboard.throttle_pct_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(g_dashboard.throttle_pct_label, "0%");
        lv_obj_set_style_text_color(g_dashboard.throttle_pct_label, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_text_font(g_dashboard.throttle_pct_label, &lv_font_montserrat_18, 0);

        lv_obj_t * throttle_name_label = lv_label_create(middle_panel);
        lv_obj_set_pos(throttle_name_label, UI_PEDAL_THROTTLE_X - 12, UI_PEDAL_NAME_LABEL_Y);
        lv_obj_set_width(throttle_name_label, UI_PEDAL_BAR_W + 24);
        lv_obj_set_style_text_align(throttle_name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(throttle_name_label, "THRTL");
        lv_obj_set_style_text_color(throttle_name_label, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(throttle_name_label, &lv_font_montserrat_16, 0);
    }

    lv_obj_t * vehicle_box = create_panel(middle_panel, 0, 0, UI_LEFT_PANEL_WIDTH, UI_MIDDLE_HEIGHT - 9, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(vehicle_box, 0, 0);

    g_dashboard.mode_tile = create_panel(vehicle_box, 52, 10, 76, 76,
                                         lv_palette_main(LV_PALETTE_RED), LV_OPA_COVER);
    lv_obj_set_style_border_width(g_dashboard.mode_tile, 2, 0);
    lv_obj_set_style_radius(g_dashboard.mode_tile, 0, 0);

    g_dashboard.mode_value = lv_label_create(g_dashboard.mode_tile);
    lv_obj_set_style_text_font(g_dashboard.mode_value, &lv_font_montserrat_32, 0);
    lv_obj_align(g_dashboard.mode_value, LV_ALIGN_CENTER, 0, 0);
    apply_drive_mode_ui();

    g_dashboard.slip_value = lv_label_create(vehicle_box);
    lv_label_set_text(g_dashboard.slip_value, "SLIP 0");
    lv_obj_set_style_text_color(g_dashboard.slip_value, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.slip_value, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(g_dashboard.slip_value, 55, 101);
    for(uint32_t index = 0U; index < UI_SLIP_BAR_COUNT; index++) {
        g_dashboard.slip_bars[index] =
            create_panel(vehicle_box, 12 + (lv_coord_t)(index * 24U), 132,
                         14, 6, UI_SEGMENT_OFF_COLOR, LV_OPA_COVER);
        lv_obj_set_style_border_width(g_dashboard.slip_bars[index], 0, 0);
    }
    update_slip_level_ui();

    g_dashboard.wheel_fl = lv_obj_create(vehicle_box);
    lv_obj_remove_style_all(g_dashboard.wheel_fl);
    lv_obj_set_pos(g_dashboard.wheel_fl, UI_TIRE_LEFT_X, UI_TIRE_FRONT_Y);
    lv_obj_set_size(g_dashboard.wheel_fl, UI_TIRE_WIDTH, UI_TIRE_HEIGHT);
    lv_obj_set_style_radius(g_dashboard.wheel_fl, 2, 0);
    lv_obj_set_style_bg_opa(g_dashboard.wheel_fl, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_dashboard.wheel_fl, UI_BG_COLOR, 0);
    lv_obj_set_style_border_width(g_dashboard.wheel_fl, 1, 0);
    lv_obj_set_style_border_color(g_dashboard.wheel_fl, UI_BORDER_COLOR, 0);

    g_dashboard.lightning_fl = lv_label_create(vehicle_box);
    lv_label_set_text(g_dashboard.lightning_fl, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(g_dashboard.lightning_fl, lv_color_hex(0xFFD400), 0);
    lv_obj_set_style_text_font(g_dashboard.lightning_fl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_transform_scale(g_dashboard.lightning_fl, UI_TIRE_LIGHTNING_SCALE, 0);
    lv_obj_align_to(g_dashboard.lightning_fl, g_dashboard.wheel_fl, LV_ALIGN_OUT_LEFT_MID, -4, UI_TIRE_LIGHTNING_Y);

    g_dashboard.wheel_fr = lv_obj_create(vehicle_box);
    lv_obj_remove_style_all(g_dashboard.wheel_fr);
    lv_obj_set_pos(g_dashboard.wheel_fr, UI_TIRE_RIGHT_X, UI_TIRE_FRONT_Y);
    lv_obj_set_size(g_dashboard.wheel_fr, UI_TIRE_WIDTH, UI_TIRE_HEIGHT);
    lv_obj_set_style_radius(g_dashboard.wheel_fr, 2, 0);
    lv_obj_set_style_bg_opa(g_dashboard.wheel_fr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_dashboard.wheel_fr, UI_BG_COLOR, 0);
    lv_obj_set_style_border_width(g_dashboard.wheel_fr, 1, 0);
    lv_obj_set_style_border_color(g_dashboard.wheel_fr, UI_BORDER_COLOR, 0);

    g_dashboard.lightning_fr = lv_label_create(vehicle_box);
    lv_label_set_text(g_dashboard.lightning_fr, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(g_dashboard.lightning_fr, lv_color_hex(0xFFD400), 0);
    lv_obj_set_style_text_font(g_dashboard.lightning_fr, &lv_font_montserrat_18, 0);
    lv_obj_set_style_transform_scale(g_dashboard.lightning_fr, UI_TIRE_LIGHTNING_SCALE, 0);
    lv_obj_align_to(g_dashboard.lightning_fr, g_dashboard.wheel_fr, LV_ALIGN_OUT_RIGHT_MID, 4, UI_TIRE_LIGHTNING_Y);

    g_dashboard.wheel_rl = lv_obj_create(vehicle_box);
    lv_obj_remove_style_all(g_dashboard.wheel_rl);
    lv_obj_set_pos(g_dashboard.wheel_rl, UI_TIRE_LEFT_X, UI_TIRE_REAR_Y);
    lv_obj_set_size(g_dashboard.wheel_rl, UI_TIRE_WIDTH, UI_TIRE_HEIGHT);
    lv_obj_set_style_radius(g_dashboard.wheel_rl, 2, 0);
    lv_obj_set_style_bg_opa(g_dashboard.wheel_rl, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_dashboard.wheel_rl, UI_BG_COLOR, 0);
    lv_obj_set_style_border_width(g_dashboard.wheel_rl, 1, 0);
    lv_obj_set_style_border_color(g_dashboard.wheel_rl, UI_BORDER_COLOR, 0);

    g_dashboard.lightning_rl = lv_label_create(vehicle_box);
    lv_label_set_text(g_dashboard.lightning_rl, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(g_dashboard.lightning_rl, lv_color_hex(0xFFD400), 0);
    lv_obj_set_style_text_font(g_dashboard.lightning_rl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_transform_scale(g_dashboard.lightning_rl, UI_TIRE_LIGHTNING_SCALE, 0);
    lv_obj_align_to(g_dashboard.lightning_rl, g_dashboard.wheel_rl, LV_ALIGN_OUT_LEFT_MID, -4, UI_TIRE_LIGHTNING_Y);

    g_dashboard.wheel_rr = lv_obj_create(vehicle_box);
    lv_obj_remove_style_all(g_dashboard.wheel_rr);
    lv_obj_set_pos(g_dashboard.wheel_rr, UI_TIRE_RIGHT_X, UI_TIRE_REAR_Y);
    lv_obj_set_size(g_dashboard.wheel_rr, UI_TIRE_WIDTH, UI_TIRE_HEIGHT);
    lv_obj_set_style_radius(g_dashboard.wheel_rr, 2, 0);
    lv_obj_set_style_bg_opa(g_dashboard.wheel_rr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_dashboard.wheel_rr, UI_BG_COLOR, 0);
    lv_obj_set_style_border_width(g_dashboard.wheel_rr, 1, 0);
    lv_obj_set_style_border_color(g_dashboard.wheel_rr, UI_BORDER_COLOR, 0);

    g_dashboard.lightning_rr = lv_label_create(vehicle_box);
    lv_label_set_text(g_dashboard.lightning_rr, LV_SYMBOL_CHARGE);
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
            g_dashboard.tire_max_labels[wheel] = lv_label_create(vehicle_box);
            lv_label_set_text(g_dashboard.tire_max_labels[wheel], "0°");
            lv_obj_set_style_text_color(g_dashboard.tire_max_labels[wheel], UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(g_dashboard.tire_max_labels[wheel], &lv_font_montserrat_16, 0);
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

    /* Night-mode toggle: clickable EYE icon under the tires. The overlay on
     * the top layer dims everything to fake the MCU's 60% backlight. */
    g_dashboard.night_icon = lv_obj_create(vehicle_box);
    lv_obj_remove_style_all(g_dashboard.night_icon);
    lv_obj_set_pos(g_dashboard.night_icon, UI_NIGHT_ICON_X, UI_NIGHT_ICON_Y);
    lv_obj_set_size(g_dashboard.night_icon, UI_NIGHT_ICON_SIZE, UI_NIGHT_ICON_SIZE);
    lv_obj_set_style_radius(g_dashboard.night_icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_dashboard.night_icon, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(g_dashboard.night_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dashboard.night_icon, 2, 0);
    lv_obj_set_style_border_color(g_dashboard.night_icon, UI_BORDER_COLOR, 0);
    lv_obj_add_flag(g_dashboard.night_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_dashboard.night_icon, night_icon_event_cb, LV_EVENT_ALL, NULL);

    g_dashboard.night_icon_label = lv_label_create(g_dashboard.night_icon);
    lv_obj_set_style_text_font(g_dashboard.night_icon_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(g_dashboard.night_icon_label, UI_TEXT_COLOR, 0);
    lv_obj_align(g_dashboard.night_icon_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(g_dashboard.night_icon_label, LV_SYMBOL_EYE_OPEN);

    g_dashboard.night_mask = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_dashboard.night_mask);
    lv_obj_set_pos(g_dashboard.night_mask, 0, 0);
    lv_obj_set_size(g_dashboard.night_mask, SIM_HOR_RES, SIM_VER_RES);
    lv_obj_set_style_bg_color(g_dashboard.night_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_dashboard.night_mask, UI_NIGHT_MASK_OPA, 0);
    lv_obj_add_flag(g_dashboard.night_mask, LV_OBJ_FLAG_HIDDEN);
    update_night_mode_ui();

    lv_obj_t * bottom_info = create_panel(screen, 0, UI_BOTTOM_Y, SIM_HOR_RES, UI_BOTTOM_HEIGHT, UI_BG_COLOR, LV_OPA_COVER);
    lv_obj_set_style_border_width(bottom_info, 0, 0);

    lv_obj_t * motor_fl_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fl_label, "LF T:");
    lv_obj_set_style_text_color(motor_fl_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fl_label, 14, 4);
    g_dashboard.motor_fl_torque = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fl_torque, "120");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_torque, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_torque, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_torque, 56, 4);
    g_dashboard.motor_fl_speed = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fl_speed, "850");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_speed, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_speed, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_speed, 56, 24);

    lv_obj_t * motor_fl_speed_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fl_speed_label, "LF N:");
    lv_obj_set_style_text_color(motor_fl_speed_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_speed_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fl_speed_label, 14, 24);

    lv_obj_t * motor_fl_power_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fl_power_label, "P:");
    lv_obj_set_style_text_color(motor_fl_power_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_power_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fl_power_label, 102, 4);
    g_dashboard.motor_fl_power_live = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fl_power_live, "10");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_power_live, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_power_live, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_power_live, 126, 4);

    lv_obj_t * motor_fl_peak_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fl_peak_label, "Pk:");
    lv_obj_set_style_text_color(motor_fl_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_peak_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fl_peak_label, 102, 24);
    g_dashboard.motor_fl_power_peak = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fl_power_peak, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_power_peak, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_power_peak, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_power_peak, 136, 24);

    lv_obj_t * motor_fl_temp_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fl_temp_label, "Tm:");
    lv_obj_set_style_text_color(motor_fl_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fl_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fl_temp_label, 14, 44);
    g_dashboard.motor_fl_temp = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fl_temp, "48");
    lv_obj_set_style_text_color(g_dashboard.motor_fl_temp, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fl_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fl_temp, 56, 44);

    lv_obj_t * motor_fr_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fr_label, "RF T:");
    lv_obj_set_style_text_color(motor_fr_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fr_label, 214, 4);
    g_dashboard.motor_fr_torque = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fr_torque, "118");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_torque, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_torque, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_torque, 256, 4);
    g_dashboard.motor_fr_speed = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fr_speed, "840");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_speed, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_speed, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_speed, 256, 24);

    lv_obj_t * motor_fr_speed_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fr_speed_label, "RF N:");
    lv_obj_set_style_text_color(motor_fr_speed_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_speed_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fr_speed_label, 214, 24);

    lv_obj_t * motor_fr_power_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fr_power_label, "P:");
    lv_obj_set_style_text_color(motor_fr_power_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_power_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fr_power_label, 302, 4);
    g_dashboard.motor_fr_power_live = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fr_power_live, "10");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_power_live, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_power_live, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_power_live, 326, 4);

    lv_obj_t * motor_fr_peak_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fr_peak_label, "Pk:");
    lv_obj_set_style_text_color(motor_fr_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_peak_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fr_peak_label, 302, 24);
    g_dashboard.motor_fr_power_peak = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fr_power_peak, "24");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_power_peak, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_power_peak, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_power_peak, 336, 24);

    lv_obj_t * motor_fr_temp_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_fr_temp_label, "Tm:");
    lv_obj_set_style_text_color(motor_fr_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_fr_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_fr_temp_label, 214, 44);
    g_dashboard.motor_fr_temp = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_fr_temp, "47");
    lv_obj_set_style_text_color(g_dashboard.motor_fr_temp, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_fr_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_fr_temp, 256, 44);

    lv_obj_t * motor_rl_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rl_label, "LR T:");
    lv_obj_set_style_text_color(motor_rl_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rl_label, 414, 4);
    g_dashboard.motor_rl_torque = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rl_torque, "116");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_torque, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_torque, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_torque, 456, 4);
    g_dashboard.motor_rl_speed = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rl_speed, "830");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_speed, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_speed, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_speed, 456, 24);

    lv_obj_t * motor_rl_speed_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rl_speed_label, "LR N:");
    lv_obj_set_style_text_color(motor_rl_speed_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_speed_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rl_speed_label, 414, 24);

    lv_obj_t * motor_rl_power_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rl_power_label, "P:");
    lv_obj_set_style_text_color(motor_rl_power_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_power_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rl_power_label, 502, 4);
    g_dashboard.motor_rl_power_live = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rl_power_live, "9");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_power_live, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_power_live, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_power_live, 526, 4);

    lv_obj_t * motor_rl_peak_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rl_peak_label, "Pk:");
    lv_obj_set_style_text_color(motor_rl_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_peak_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rl_peak_label, 502, 24);
    g_dashboard.motor_rl_power_peak = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rl_power_peak, "23");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_power_peak, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_power_peak, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_power_peak, 536, 24);

    lv_obj_t * motor_rl_temp_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rl_temp_label, "Tm:");
    lv_obj_set_style_text_color(motor_rl_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rl_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rl_temp_label, 414, 44);
    g_dashboard.motor_rl_temp = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rl_temp, "49");
    lv_obj_set_style_text_color(g_dashboard.motor_rl_temp, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rl_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rl_temp, 456, 44);

    lv_obj_t * motor_rr_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rr_label, "RR T:");
    lv_obj_set_style_text_color(motor_rr_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rr_label, 614, 4);
    g_dashboard.motor_rr_torque = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rr_torque, "114");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_torque, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_torque, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_torque, 656, 4);
    g_dashboard.motor_rr_speed = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rr_speed, "820");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_speed, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_speed, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_speed, 656, 24);

    lv_obj_t * motor_rr_speed_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rr_speed_label, "RR N:");
    lv_obj_set_style_text_color(motor_rr_speed_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_speed_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rr_speed_label, 614, 24);

    lv_obj_t * motor_rr_power_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rr_power_label, "P:");
    lv_obj_set_style_text_color(motor_rr_power_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_power_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rr_power_label, 702, 4);
    g_dashboard.motor_rr_power_live = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rr_power_live, "9");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_power_live, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_power_live, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_power_live, 726, 4);

    lv_obj_t * motor_rr_peak_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rr_peak_label, "Pk:");
    lv_obj_set_style_text_color(motor_rr_peak_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_peak_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rr_peak_label, 702, 24);
    g_dashboard.motor_rr_power_peak = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rr_power_peak, "23");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_power_peak, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_power_peak, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_power_peak, 736, 24);

    lv_obj_t * motor_rr_temp_label = lv_label_create(bottom_info);
    lv_label_set_text(motor_rr_temp_label, "Tm:");
    lv_obj_set_style_text_color(motor_rr_temp_label, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(motor_rr_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(motor_rr_temp_label, 614, 44);
    g_dashboard.motor_rr_temp = lv_label_create(bottom_info);
    lv_label_set_text(g_dashboard.motor_rr_temp, "50");
    lv_obj_set_style_text_color(g_dashboard.motor_rr_temp, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(g_dashboard.motor_rr_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_dashboard.motor_rr_temp, 656, 44);

    /* Per-wheel inverter temperature "Ti" (DBC BO_1287 Debug7) and diagnostic
     * number "E" (DBC BO_1283/1284). Column order matches the blocks above:
     * LF, RF, LR, RR; data arrays use LF/LR/RF/RR order (see defaults). */
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
            lv_label_set_text(inverter_caption, "Ti:");
            lv_obj_set_style_text_color(inverter_caption, UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(inverter_caption, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(inverter_caption, x + 102, 44);
            *inverter_labels[col] = lv_label_create(bottom_info);
            lv_label_set_text(*inverter_labels[col], "0");
            lv_obj_set_style_text_color(*inverter_labels[col], UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(*inverter_labels[col], &lv_font_montserrat_16, 0);
            lv_obj_set_pos(*inverter_labels[col], x + 140, 44);

            lv_obj_t * error_caption = lv_label_create(bottom_info);
            lv_label_set_text(error_caption, "E:");
            lv_obj_set_style_text_color(error_caption, UI_TEXT_COLOR, 0);
            lv_obj_set_style_text_font(error_caption, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(error_caption, x + 14, 64);
            *error_labels[col] = lv_label_create(bottom_info);
            lv_label_set_text(*error_labels[col], "OK");
            lv_obj_set_style_text_color(*error_labels[col], lv_palette_main(LV_PALETTE_GREY), 0);
            lv_obj_set_style_text_font(*error_labels[col], &lv_font_montserrat_16, 0);
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

    lv_obj_t * separator_value_top = lv_obj_create(screen);
    lv_obj_remove_style_all(separator_value_top);
    lv_obj_set_pos(separator_value_top, UI_CENTER_PANEL_X, UI_MIDDLE_Y + UI_SPEED_BOX_Y);
    lv_obj_set_size(separator_value_top, UI_CENTER_PANEL_WIDTH, 1);
    lv_obj_set_style_bg_color(separator_value_top, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(separator_value_top, LV_OPA_COVER, 0);

    lv_obj_t * separator_bottom = lv_obj_create(screen);
    lv_obj_remove_style_all(separator_bottom);
    lv_obj_set_pos(separator_bottom, 0, UI_BOTTOM_Y - 1);
    lv_obj_set_size(separator_bottom, SIM_HOR_RES, 1);
    lv_obj_set_style_bg_color(separator_bottom, UI_BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(separator_bottom, LV_OPA_COVER, 0);

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
        lv_obj_set_style_text_font(g_dashboard.odometer_label, &lv_font_montserrat_24, 0);
        lv_label_set_text(g_dashboard.odometer_label, "41.2 km");

        g_dashboard.alert_label = lv_label_create(screen);
        lv_obj_set_pos(g_dashboard.alert_label, 322, circle_y - 3);
        lv_obj_set_size(g_dashboard.alert_label, 258, circle_d + 6);
        lv_label_set_long_mode(g_dashboard.alert_label, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(g_dashboard.alert_label, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(g_dashboard.alert_label, &lv_font_montserrat_18, 0);
        lv_label_set_text(g_dashboard.alert_label, "");
        lv_obj_add_flag(g_dashboard.alert_label, LV_OBJ_FLAG_HIDDEN);
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

    g_dashboard.key_target = lv_button_create(screen);
    lv_obj_set_size(g_dashboard.key_target, 1, 1);
    lv_obj_set_style_opa(g_dashboard.key_target, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(g_dashboard.key_target, mode_key_event_cb, LV_EVENT_KEY, NULL);

    /* Initialize layout based on current mode */
    apply_drive_mode_ui();
    update_layout_by_mode();
    apply_main_dashboard_defaults();

    lv_scr_load(screen);
    lv_group_focus_obj(g_dashboard.key_target);
    lv_refr_now(NULL);
}

/* Format a signed deci-scaled value (e.g. cA -> A, mV -> V) with one decimal
 * and a unit suffix, keeping the minus sign out of the fractional part. */
static void format_deci_value(char * buf, size_t buf_size, int deci, const char * unit)
{
    int magnitude = deci < 0 ? -deci : deci;
    lv_snprintf(buf, buf_size, "%s%d.%01d%s",
                (deci < 0) ? "-" : "", magnitude / 10, magnitude % 10, unit);
}

static void apply_main_dashboard_defaults(void)
{
    char text_buf[16];
    int display_speed = speed;
    lv_obj_t * torque_labels[4] = {
        g_dashboard.motor_fl_torque, g_dashboard.motor_rl_torque,
        g_dashboard.motor_fr_torque, g_dashboard.motor_rr_torque
    };
    lv_obj_t * rpm_labels[4] = {
        g_dashboard.motor_fl_speed, g_dashboard.motor_rl_speed,
        g_dashboard.motor_fr_speed, g_dashboard.motor_rr_speed
    };
    lv_obj_t * power_live_labels[4] = {
        g_dashboard.motor_fl_power_live, g_dashboard.motor_rl_power_live,
        g_dashboard.motor_fr_power_live, g_dashboard.motor_rr_power_live
    };
    lv_obj_t * power_peak_labels[4] = {
        g_dashboard.motor_fl_power_peak, g_dashboard.motor_rl_power_peak,
        g_dashboard.motor_fr_power_peak, g_dashboard.motor_rr_power_peak
    };
    lv_obj_t * temp_labels[4] = {
        g_dashboard.motor_fl_temp, g_dashboard.motor_rl_temp,
        g_dashboard.motor_fr_temp, g_dashboard.motor_rr_temp
    };
    lv_obj_t * inverter_labels[4] = {
        g_dashboard.motor_fl_inverter, g_dashboard.motor_rl_inverter,
        g_dashboard.motor_fr_inverter, g_dashboard.motor_rr_inverter
    };
    lv_obj_t * error_labels[4] = {
        g_dashboard.motor_fl_error, g_dashboard.motor_rl_error,
        g_dashboard.motor_fr_error, g_dashboard.motor_rr_error
    };

    if(display_speed < 0) display_speed = 0;
    if(display_speed > 99) display_speed = 99;
    set_speed_digits(display_speed);

    lv_snprintf(text_buf, sizeof(text_buf), "%d%%", SOC);
    lv_label_set_text(g_dashboard.soc_value, text_buf);
    lv_obj_set_width(g_dashboard.battery_fill,
                     SOC == 0 ? 1 : (SOC * UI_BATTERY_FILL_MAX_W / 100));
    lv_snprintf(text_buf, sizeof(text_buf), "%d", Sum_Voltage);
    lv_label_set_text(g_dashboard.total_voltage_value, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%d", Sum_I);
    lv_label_set_text(g_dashboard.total_current_value, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%d", Top_Temperature);
    lv_label_set_text(g_dashboard.max_temp_value, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%dkW", Power_Live);
    lv_label_set_text(g_dashboard.power_live_value, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%dkW", Power_Peak);
    lv_label_set_text(g_dashboard.power_peak_value, text_buf);

    if(g_dashboard.throttle_bar_fill != NULL) {
        int height = g_aps_pct == 0 ? 1 : (g_aps_pct * UI_PEDAL_BAR_H / 100);
        lv_obj_set_pos(g_dashboard.throttle_bar_fill, 0, UI_PEDAL_BAR_H - height);
        lv_obj_set_size(g_dashboard.throttle_bar_fill, UI_PEDAL_BAR_W, height);
    }
    if(g_dashboard.brake_bar_fill != NULL) {
        int height = g_brake_pct == 0 ? 1 : (g_brake_pct * UI_PEDAL_BAR_H / 100);
        lv_obj_set_pos(g_dashboard.brake_bar_fill, 0, UI_PEDAL_BAR_H - height);
        lv_obj_set_size(g_dashboard.brake_bar_fill, UI_PEDAL_BAR_W, height);
    }
    if(g_dashboard.throttle_pct_label != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%d%%", g_aps_pct);
        lv_label_set_text(g_dashboard.throttle_pct_label, text_buf);
    }
    if(g_dashboard.brake_pct_label != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%d%%", g_brake_pct);
        lv_label_set_text(g_dashboard.brake_pct_label, text_buf);
    }

    /* Low-voltage bus: DBC BO_1440 PDM_LowVoltageBus (demo values) */
    if(g_dashboard.lv_voltage_value != NULL) {
        format_deci_value(text_buf, sizeof(text_buf), LV_Bus_Voltage_mV / 100, "V");
        lv_label_set_text(g_dashboard.lv_voltage_value, text_buf);
    }
    if(g_dashboard.lv_current_value != NULL) {
        format_deci_value(text_buf, sizeof(text_buf), LV_Bus_Current_cA, "A");
        lv_label_set_text(g_dashboard.lv_current_value, text_buf);
    }
    if(g_dashboard.lv_power_value != NULL) {
        lv_snprintf(text_buf, sizeof(text_buf), "%dW", LV_Bus_Power_dW / 10);
        lv_label_set_text(g_dashboard.lv_power_value, text_buf);
    }

    /* Fans: DBC BO_1442 FanController_Status (demo values) */
    for(uint32_t fan = 0U; fan < 3U; fan++) {
        if(g_dashboard.fan_value_labels[fan] == NULL) continue;
        if(fan < 2U) {
            lv_snprintf(text_buf, sizeof(text_buf), "%d%% %d",
                        Fan_PWM_Duty[fan], Fan_RPM[fan]);
        }
        else {
            /* Fan 3 has no independent duty signal in the DBC. */
            lv_snprintf(text_buf, sizeof(text_buf), "-- %d", Fan_RPM[fan]);
        }
        lv_label_set_text(g_dashboard.fan_value_labels[fan], text_buf);
    }

    format_lap_time(text_buf, sizeof(text_buf), current_lap_time);
    lv_label_set_text(g_dashboard.lap_current_value, text_buf);
    format_lap_time(text_buf, sizeof(text_buf), last_lap_time);
    lv_label_set_text(g_dashboard.lap_last_value, text_buf);
    format_lap_time(text_buf, sizeof(text_buf), best_lap_time);
    lv_label_set_text(g_dashboard.lap_best_value, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%02d", laps_current);
    lv_label_set_text(g_dashboard.laps_current_value, text_buf);
    lv_snprintf(text_buf, sizeof(text_buf), "%02d", laps_left);
    lv_label_set_text(g_dashboard.laps_left_value, text_buf);
    update_lap_delta_ui();

    for(uint32_t index = 0U; index < 4U; index++) {
        lv_snprintf(text_buf, sizeof(text_buf), "%d", torque_M[index]);
        lv_label_set_text(torque_labels[index], text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%d", RPM[index]);
        lv_label_set_text(rpm_labels[index], text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%d", Motor_Power_Live[index]);
        lv_label_set_text(power_live_labels[index], text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%d", Motor_Power_Peak[index]);
        lv_label_set_text(power_peak_labels[index], text_buf);
        lv_snprintf(text_buf, sizeof(text_buf), "%d", Motor_Temp[index]);
        lv_label_set_text(temp_labels[index], text_buf);
        if(inverter_labels[index] != NULL) {
            lv_snprintf(text_buf, sizeof(text_buf), "%d", Inverter_Temp[index]);
            lv_label_set_text(inverter_labels[index], text_buf);
            lv_obj_set_style_text_color(inverter_labels[index],
                                        temp_to_color(Inverter_Temp[index]), 0);
        }
        if(error_labels[index] != NULL) {
            if(Diag_Num[index] != 0U) {
                lv_snprintf(text_buf, sizeof(text_buf), "0x%04X",
                            (unsigned)(Diag_Num[index] & 0xFFFFU));
                lv_obj_set_style_text_color(error_labels[index],
                                            lv_palette_main(LV_PALETTE_RED), 0);
            }
            else {
                lv_snprintf(text_buf, sizeof(text_buf), "OK");
                lv_obj_set_style_text_color(error_labels[index],
                                            lv_palette_main(LV_PALETTE_GREY), 0);
            }
            lv_label_set_text(error_labels[index], text_buf);
        }
    }

    lv_snprintf(text_buf, sizeof(text_buf), "%d.%01d km",
                vehicle_distance_m / 1000, (vehicle_distance_m % 1000) / 100);
    lv_label_set_text(g_dashboard.odometer_label, text_buf);
    apply_drive_mode_ui();
    update_slip_level_ui();
    apply_vehicle_ui();
}


// ........................................................................................................
/**
 * @brief   LVGL task
 *
 * This task initializes LVGL and runs the main loop, periodically calling the LVGL task handler.
 * It is responsible for managing the LVGL state and rendering updates.
 *
 * @param   pvParameters   Task parameters (not used in this example)
 * @return  None
 */
void lvgl_task(void *pvParameters)
{
    lv_display_t * disp;
    LV_UNUSED(pvParameters);

    /*Initialize LVGL*/
    lv_init();

    /*Initialize the HAL (display, input devices, tick) for LVGL*/
    disp = sdl_hal_init(SIM_HOR_RES, SIM_VER_RES);
    if(disp == NULL) {
        printf("Simulator display init failed\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    /* Show main dashboard screen */
    create_main_dashboard_screen();
    lv_timer_create(tire_demo_timer_cb, 120, NULL);

    while (true){
        if(g_simulator_exit_requested) {
            break;
        }

        lv_timer_handler(); /* Handle LVGL tasks */
        SIM_LVGL_LOOP_DELAY();
    }

    printf("Simulator exiting by request\n");
    fflush(stdout);
    exit(EXIT_SUCCESS);
}

// ........................................................................................................
/**
 * @brief   Another task
 *
 * This task simulates some background work by periodically printing a message.
 *
 * @param   pvParameters   Task parameters (not used in this example)
 * @return  None
 */
void another_task(void *pvParameters)
{
    LV_UNUSED(pvParameters);
    while (true){
        if(g_simulator_exit_requested) {
            vTaskDelete(NULL);
        }

        /* Keep a very low-impact background task only to exercise the scheduler. */
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ........................................................................................................
/**
 * @brief   FreeRTOS main function
 *
 * This function sets up and starts the FreeRTOS tasks, including the LVGL task and another demo task.
 *
 * @param   None
 * @return  None
 */
int main(int argc, char **argv)
{
    int i;
    /* --night: start in night mode (45% simulated backlight), used for
     * screenshot capture where synthetic key injection is unreliable. */
    for(i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--night") == 0) {
            g_night_mode = 1U;
        }
    }

#if defined(__APPLE__)
    lvgl_task(NULL); /* 主线程直接运行, 退出时由 lvgl_task 内 exit() 结束进程 */
    return 0;
#else
    /* Create the LVGL task */
    if (xTaskCreate(lvgl_task, "LVGL Task", 4096, NULL, 1, NULL) != pdPASS) {
        printf("Error creating LVGL task\n");
        /* Error handling */
    }

    /* Create another task */
    if (xTaskCreate(another_task, "Another Task", 1024, NULL, 1, NULL) != pdPASS) {
        printf("Error creating another task\n");
        /* Error handling */
    }

    /* Start the scheduler */
    vTaskStartScheduler();
#endif
}

#endif
