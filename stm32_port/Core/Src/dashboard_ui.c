/* USER CODE BEGIN dashboard_ui */
/* Dashboard controller: thread-safe data intake (CAN/GPS/touch tasks and
 * ISRs submit into pending buffers), lap/sprint timing control, alert queue
 * and backlight. Drawing lives in dashboard_view.c, which only runs from
 * Dashboard_UI_Process() in the dashboard task. */
#include "dashboard_ui.h"
#include "dashboard_view.h"

#include "main.h"

#include "can.h"
#include "gps.h"
#include "gps_lap.h"
#include "speed_trace.h"

#include <string.h>

static volatile uint8_t g_dashboard_initialized;

typedef enum {
    DRIVE_MODE_S = 0,
    DRIVE_MODE_Q,
    DRIVE_MODE_C,
    DRIVE_MODE_E
} drive_mode_t;

static volatile drive_mode_t g_drive_mode = DRIVE_MODE_S;
/* 当前界面显示的车速；DASH_VIEW_INVALID 表示显示 "--"（没有可信速度）。 */
static int32_t g_speed = DASH_VIEW_INVALID;
static volatile int32_t g_mode_index = 0;
static int32_t g_current_lap_time = 0;
static int32_t g_last_lap_time = 0;
static int32_t g_best_lap_time = 0;
static int32_t g_lap_delta = 0;
static int32_t g_laps_current = 0;
static int32_t g_laps_left = 75;
/* 整车放电功率峰值(kW)：上电后的最大值，不清零。g_power_peak_seen 为 0 时尚未收到
 * 有效电压和电流，界面显示 "--"。 */
static int32_t g_power_peak = 0;
static uint8_t g_power_peak_seen = 0U;
/* 放电电流峰值(0.1 A)：上电后出现过的最大放电电流，与 g_power_peak 一样不清零。
 * g_current_peak_seen 为 0 时尚未收到有效电流，界面显示 "--"。 */
static int32_t g_current_peak_da = 0;
static uint8_t g_current_peak_seen = 0U;
/* 单电机轴功率峰值(0.1 kW)：上电后的最大值，与 g_power_peak 一样不清零。 */
static int32_t g_motor_power_peak[4];
/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR; segment order is outside-to-inside.
 * Tire temperatures are stored in 0.01 degC to preserve the CAN precision. */
static int32_t g_tire_temp[4][4];
static volatile int32_t g_pending_tire_temp[4][4];
/* valid_mask starts at 0: every CAN-fed field shows "--" until its first frame. */
static dashboard_data_t g_dashboard_data;
static volatile dashboard_data_t g_pending_dashboard_data;
static volatile uint32_t g_dashboard_dirty_mask = 0U;
/* g_pending_speed < 0 表示 "--"；提交时刻与数值同在一个关中断段内写入，供 speed_trace 计时。 */
static volatile int32_t g_pending_speed = -1;
static volatile uint32_t g_pending_speed_tick = 0U;
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
#define DASHBOARD_MODE_TOUCH_DEBOUNCE_MS 200U
#define LAP_START_MIN_SPEED_KMH     5.0f
#define DELTA_TEXT_LIMIT_HUNDREDTHS 3000
#define SPEED_DISPLAY_MAX_KMH       299

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
#define DASH_DIRTY_SOP      (1UL << 15)
#define DASH_DIRTY_MOTORS   (DASH_DIRTY_MOTOR_0 | DASH_DIRTY_MOTOR_1 | \
                             DASH_DIRTY_MOTOR_2 | DASH_DIRTY_MOTOR_3)
#define DASH_DIRTY_TIRES    (DASH_DIRTY_TIRE_0 | DASH_DIRTY_TIRE_1 | \
                             DASH_DIRTY_TIRE_2 | DASH_DIRTY_TIRE_3)
#define DASH_DIRTY_FAST     (DASH_DIRTY_MODE | DASH_DIRTY_PEDALS | DASH_DIRTY_MOTORS | \
                             DASH_DIRTY_SLIP | DASH_DIRTY_TIRES | DASH_DIRTY_FANS)
#define DASH_DIRTY_SLOW     (DASH_DIRTY_BATTERY | DASH_DIRTY_ODOMETER | \
                             DASH_DIRTY_LOWVOLTAGE | DASH_DIRTY_SOP)
#define DASH_DIRTY_ALL      (DASH_DIRTY_FAST | DASH_DIRTY_SLOW)
/* Which DASH_VALID_* bits feed each dirty group. The three temperature bits
 * feed both the battery group (MAX T) and the motor columns. */
#define DASH_VALID_BATTERY_GROUP (DASH_VALID_SOC | DASH_VALID_HV_V | DASH_VALID_HV_I | \
                                  DASH_VALID_MOTOR_TEMP | DASH_VALID_INV_TEMP | \
                                  DASH_VALID_IGBT_TEMP)
#define DASH_VALID_PEDAL_GROUP   (DASH_VALID_APS | DASH_VALID_BRAKE | DASH_VALID_REGEN)
/* Wheel order 0=LF 1=LR 2=RF 3=RR: front wheels take 0x504 diagnostics,
 * rear wheels 0x503. */
#define DASH_VALID_MOTOR_GROUP(wheel) \
    (DASH_VALID_TORQUE | DASH_VALID_RPM | DASH_VALID_MOTOR_TEMP | DASH_VALID_INV_TEMP | \
     DASH_VALID_AMK_EN | ((((wheel) & 1U) != 0U) ? DASH_VALID_DIAG_RL_RR : DASH_VALID_DIAG_FL_FR))
static char g_alert_queue[DASHBOARD_ALERT_MAX][DASHBOARD_ALERT_TEXT_MAX];
static uint8_t g_alert_count = 0U;
static char g_pending_alert_queue[DASHBOARD_ALERT_MAX][DASHBOARD_ALERT_TEXT_MAX];
static volatile uint8_t g_pending_alert_count = 0U;
static volatile uint8_t g_pending_alert_pop_count = 0U;
static volatile uint8_t g_pending_lap_toggle = 0U;
static volatile uint8_t g_pending_mode_cycle = 0U;
static volatile uint8_t g_pending_alert_clear = 0U;
static volatile uint8_t g_pending_mode_lock_alert = 0U;
static volatile uint8_t g_pending_sprint_ready = 0U;
static uint8_t g_sprint_ready_dirty = 0U;
static uint8_t g_sprint_ready_visible = 0U;
static uint32_t g_mode_lock_alert_last_tick = 0U;
/* Night mode: 0 = day (100% backlight), 1 = night (45% backlight). */
static volatile uint8_t g_pending_night_toggle = 0U;
static uint8_t g_night_mode = 0U;

/* Backlight dimming: PF9 is reconfigured from plain GPIO output to TIM14_CH1
 * PWM (AF9). 84 MHz / 84 / 1000 = 1 kHz; night compare = 45% duty. */
#define BACKLIGHT_PWM_PERIOD 1000U
#define BACKLIGHT_NIGHT_COMPARE 450U

/* Boot-stage LED breadcrumbs implemented in main.c. Stage 3 = this init is
 * running (RED on), stage 4 = view created (RED+GREEN). */
void LED_Diag_SetBootStage(uint8_t stage);

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
    /* A NULL Instance (before init or after fallback) would write CCR1 at
     * 0x28 (flash alias) and hardfault; skip it, the panel is fully lit. */
    if(s_backlight_tim.Instance == NULL) return;
    __HAL_TIM_SET_COMPARE(&s_backlight_tim, TIM_CHANNEL_1, compare);
}

static void update_night_mode_ui(void)
{
    Dash_View_SetNight(g_night_mode);
    dashboard_backlight_set(g_night_mode);
}

static void update_alert_ui(void)
{
    Dash_View_SetAlert(g_alert_queue[0], g_alert_count, g_sprint_ready_visible);
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

static void update_lap_delta_ui(void)
{
    Dash_View_SetDelta(g_lap_delta);
}

static void dashboard_reset_lap_ui(void)
{
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

    Dash_View_SetLapTimes(0, 0, 0);
    Dash_View_SetLapCounter(g_laps_current, g_laps_left);
    update_lap_delta_ui();
}

static uint32_t lap_diagnostic_color(GPS_LapDiagState_t state)
{
    switch(state) {
        case GPS_LAP_DIAG_ARMED:
            return 0x20D050;
        case GPS_LAP_DIAG_APPROACHING:
            return 0xFFD020;
        case GPS_LAP_DIAG_GATE_MISS:
            return 0xFF20D0;
        case GPS_LAP_DIAG_REVERSE_PASS:
            return 0x2080FF;
        case GPS_LAP_DIAG_CROSSED:
            return 0x20F0F0;
        case GPS_LAP_DIAG_WAIT_ARM:
        default:
            return 0xFF1A1A;
    }
}

static void update_lap_analysis_indicator(uint32_t now)
{
    GPS_LapDiagnostic_t diagnostic;

    if(g_dashboard_initialized == 0U) {
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
            Dash_View_SetTimingDot(0U, 0U);
            g_lap_analysis_indicator_visible = 0U;
        }
        return;
    }

    GPS_Lap_GetDiagnostic(&diagnostic);
    if((g_lap_analysis_blink_tick == 0U) || ((now - g_lap_analysis_blink_tick) >= 500U)) {
        g_lap_analysis_blink_tick = now;
        g_lap_analysis_blink_visible = (g_lap_analysis_blink_visible == 0U) ? 1U : 0U;
    }

    if((g_lap_analysis_indicator_state == diagnostic.state) &&
       (g_lap_analysis_indicator_visible == g_lap_analysis_blink_visible)) {
        return;
    }
    g_lap_analysis_indicator_state = diagnostic.state;
    g_lap_analysis_indicator_visible = g_lap_analysis_blink_visible;
    Dash_View_SetTimingDot(g_lap_analysis_indicator_visible,
                           lap_diagnostic_color(diagnostic.state));
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
    /* 速度数据过期时 speed_kmh 为 0，但那不是停车：S 模式靠"车速 < 0.5 km/h"准备冲刺，
     * 没有双天线航向时圈速也靠速度判断，两者都必须先有可信速度。 */
    if((gps_data.speed_valid == 0U) &&
       ((g_drive_mode == DRIVE_MODE_S) || (gps_data.heading_valid == 0U))) {
        Dashboard_UI_PushAlert("gps speed invalid");
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

static void apply_drive_mode_ui(void)
{
    Dash_View_SetMode((dash_mode_t)g_drive_mode);
}

static void sync_mode_from_index(void)
{
    switch(g_mode_index) {
        case 1:
            g_drive_mode = DRIVE_MODE_Q;
            break;
        case 2:
            g_drive_mode = DRIVE_MODE_C;
            break;
        case 3:
            g_drive_mode = DRIVE_MODE_E;
            break;
        case 0:
        default:
            g_drive_mode = DRIVE_MODE_S;
            break;
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

/* force=1 用于触摸轮转和方向盘 0x310 用户请求：目标可能正好等于已经显示的
 * 旧档位，但待应用模式不同，此时仍必须改写 pending 模式。CAN 0x509 回报
 * 走 force=0，重复的旧模式不会撤销刚收到的请求。 */
static void dashboard_submit_drive_mode(int32_t mode_index, uint8_t force)
{
    uint32_t primask;

    if((mode_index < 0) || (mode_index > 3)) return;
    if((g_dashboard_data.mode_index == mode_index) &&
       ((force == 0U) || ((g_dashboard_dirty_mask & DASH_DIRTY_MODE) == 0U) ||
        (g_pending_dashboard_data.mode_index == mode_index))) return;
    if(dashboard_timing_mode_locked() != 0U) {
        dashboard_request_mode_lock_alert();
        return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if((g_dashboard_data.mode_index == mode_index) &&
       ((force == 0U) || ((g_dashboard_dirty_mask & DASH_DIRTY_MODE) == 0U) ||
        (g_pending_dashboard_data.mode_index == mode_index))) {
        if(primask == 0U) __enable_irq();
        return;
    }
    g_pending_dashboard_data.mode_index = mode_index;
    g_dashboard_dirty_mask |= DASH_DIRTY_MODE;
    if(primask == 0U) __enable_irq();
}

/* 触摸轮转要基于最新的目标模式，而不是尚未应用的显示值；
 * 否则方向盘 0x310 刚提交、界面还没刷新时，点击会覆盖掉这次请求。 */
static int32_t dashboard_effective_mode_index(void)
{
    uint32_t primask = __get_PRIMASK();
    int32_t mode_index;

    __disable_irq();
    mode_index = ((g_dashboard_dirty_mask & DASH_DIRTY_MODE) != 0U) ?
                 g_pending_dashboard_data.mode_index : g_mode_index;
    if(primask == 0U) {
        __enable_irq();
    }
    return mode_index;
}

static void dashboard_cycle_drive_mode(void)
{
    int32_t next_mode;

    if(dashboard_timing_mode_locked() != 0U) {
        dashboard_request_mode_lock_alert();
        return;
    }

    next_mode = (dashboard_effective_mode_index() + 1) & 3;
    /* 只在请求已进入 CAN 发送队列后改界面；队列满时保留当前模式并提示。 */
    if(CAN_SendDriveModeRequest(next_mode) != 0U) {
        dashboard_submit_drive_mode(next_mode, 1U);
    }
    else {
        dashboard_push_alert_local("MODE TX QUEUE FULL");
    }
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
    apply_drive_mode_ui();
    dashboard_reset_lap_ui();
    update_lap_analysis_indicator(HAL_GetTick());
}

/* Publishes the pending validity of one dirty group to the UI-owned copy.
 * Caller holds the IRQ lock. */
static void dashboard_copy_valid(uint32_t bits)
{
    g_dashboard_data.valid_mask = (g_dashboard_data.valid_mask & ~bits) |
                                  (g_pending_dashboard_data.valid_mask & bits);
}

static void apply_motor_ui(uint32_t index)
{
    dash_motor_t motor;
    const uint32_t valid = g_dashboard_data.valid_mask;
    int32_t power = 0;

    motor.invalid = 0U;
    if((valid & DASH_VALID_TORQUE) == 0U) motor.invalid |= DASH_MOTOR_NO_TORQUE;
    if((valid & DASH_VALID_RPM) == 0U) motor.invalid |= DASH_MOTOR_NO_RPM;
    if((valid & DASH_VALID_MOTOR_TEMP) == 0U) motor.invalid |= DASH_MOTOR_NO_MOTOR_TEMP;
    if((valid & DASH_VALID_INV_TEMP) == 0U) motor.invalid |= DASH_MOTOR_NO_INV_TEMP;
    if((valid & DASH_VALID_AMK_EN) == 0U) motor.invalid |= DASH_MOTOR_NO_ENABLED;
    if((valid & (((index & 1U) != 0U) ? DASH_VALID_DIAG_RL_RR : DASH_VALID_DIAG_FL_FR)) == 0U) {
        motor.invalid |= DASH_MOTOR_NO_DIAG;
    }

    if((motor.invalid & (DASH_MOTOR_NO_TORQUE | DASH_MOTOR_NO_RPM)) == 0U) {
        /* P[0.1 kW] = T[Nm] x n[rpm] x 10 / 9549; /955 keeps int32 within the
         * DBC ranges (|T| <= 32767, |n| <= 32767) at 0.01 % error. */
        power = (g_dashboard_data.torque[index] * g_dashboard_data.rpm[index]) / 955;
        if(power > g_motor_power_peak[index]) {
            g_motor_power_peak[index] = power;
        }
    }
    motor.torque = g_dashboard_data.torque[index];          /* DBC BO_1282 Debug2 */
    motor.rpm = g_dashboard_data.rpm[index];                /* DBC BO_1285 Debug5 */
    motor.power_dkw = power;
    motor.power_peak_dkw = g_motor_power_peak[index];
    motor.motor_temp = g_dashboard_data.motor_temp[index];  /* DBC BO_1286 Debug6 */
    motor.inverter_temp = g_dashboard_data.inverter_temp[index]; /* DBC BO_1287 Debug7 */
    motor.diag = g_dashboard_data.diag_num[index];          /* DBC BO_1283/1284 */
    motor.enabled = g_dashboard_data.motor_enable[index];   /* DBC BO_1289 Debug9 */
    Dash_View_SetMotor(index, &motor);
}

static void dashboard_apply_data(uint32_t dirty_mask)
{
    const uint32_t valid = g_dashboard_data.valid_mask;

    if((dirty_mask & DASH_DIRTY_BATTERY) != 0U) {
        /* DBC BO_1200: SOC, BatteryVoltage (V), BatteryCurrent (A).
         * BMS current is charge +, discharge -, so P NOW (discharge power)
         * is -V x I; charging and regen show 0. Units: dV x dA = 0.01 W. */
        int32_t power_live = DASH_VIEW_INVALID;
        if((valid & (DASH_VALID_HV_V | DASH_VALID_HV_I)) == (DASH_VALID_HV_V | DASH_VALID_HV_I)) {
            int64_t centiwatt = -((int64_t)g_dashboard_data.sum_voltage_dv *
                                  (int64_t)g_dashboard_data.sum_current_da);
            power_live = (centiwatt > 0) ? (int32_t)((centiwatt + 50000) / 100000) : 0;
            g_power_peak_seen = 1U;
            if(power_live > g_power_peak) g_power_peak = power_live;
        }
        Dash_View_SetBattery(((valid & DASH_VALID_SOC) != 0U) ? g_dashboard_data.soc : DASH_VIEW_INVALID,
                             ((valid & DASH_VALID_HV_V) != 0U) ? g_dashboard_data.sum_voltage : DASH_VIEW_INVALID,
                             ((valid & DASH_VALID_HV_I) != 0U) ? g_dashboard_data.sum_current : DASH_VIEW_INVALID);
        Dash_View_SetPower(power_live, (g_power_peak_seen != 0U) ? g_power_peak : DASH_VIEW_INVALID);
        /* 放电为负：取 -I，充电和回收不计入。显示按整安培截断，与 HV 行一致。 */
        if((valid & DASH_VALID_HV_I) != 0U) {
            const int32_t discharge_da = -g_dashboard_data.sum_current_da;
            g_current_peak_seen = 1U;
            if(discharge_da > g_current_peak_da) g_current_peak_da = discharge_da;
        }
        Dash_View_SetCurrentPeak((g_current_peak_seen != 0U) ? (g_current_peak_da / 10) :
                                 DASH_VIEW_INVALID);
        /* can.c: max over the fresh motor/inverter/IGBT temperatures. */
        Dash_View_SetMaxTemp(((valid & (DASH_VALID_MOTOR_TEMP | DASH_VALID_INV_TEMP |
                                        DASH_VALID_IGBT_TEMP)) != 0U) ?
                             g_dashboard_data.max_temperature : DASH_VIEW_INVALID);
    }

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
        Dash_View_SetSlip(g_dashboard_data.slip_level);
    }
    if((dirty_mask & DASH_DIRTY_ALL) == DASH_DIRTY_ALL) {
        Dash_View_SetSpeed(g_speed);
    }
    if((dirty_mask & DASH_DIRTY_PEDALS) != 0U) {
        Dash_View_SetPedals(((valid & DASH_VALID_APS) != 0U) ? g_dashboard_data.aps_open_pct : DASH_VIEW_INVALID,
                            ((valid & DASH_VALID_BRAKE) != 0U) ? g_dashboard_data.brake_pct : DASH_VIEW_INVALID,
                            ((valid & DASH_VALID_REGEN) != 0U) ? g_dashboard_data.regen_pct : DASH_VIEW_INVALID);
    }
    if((dirty_mask & DASH_DIRTY_LOWVOLTAGE) != 0U) {
        /* DBC BO_1440 PDM_LowVoltageBus: mV/cA/dW raw engineering units */
        if((valid & DASH_VALID_LV_BUS) != 0U) {
            Dash_View_SetLowVoltage(g_dashboard_data.lv_bus_voltage_mV,
                                    g_dashboard_data.lv_bus_current_cA,
                                    g_dashboard_data.lv_bus_power_dW);
        }
        else {
            Dash_View_SetLowVoltage(DASH_VIEW_INVALID, 0, 0);
        }
    }
    if((dirty_mask & DASH_DIRTY_SOP) != 0U) {
        /* DBC BO_1184/BO_1187: 功率与电流是两帧，各自判断新鲜度。 */
        const uint8_t power_ok = ((valid & DASH_VALID_SOP_POWER) != 0U) ? 1U : 0U;
        const uint8_t current_ok = ((valid & DASH_VALID_SOP_CURRENT) != 0U) ? 1U : 0U;
        Dash_View_SetSop(power_ok ? g_dashboard_data.sop_discharge_power_dkw : DASH_VIEW_INVALID,
                         power_ok ? g_dashboard_data.sop_charge_power_dkw : DASH_VIEW_INVALID,
                         current_ok ? g_dashboard_data.sop_discharge_current_da : DASH_VIEW_INVALID,
                         current_ok ? g_dashboard_data.sop_charge_current_da : DASH_VIEW_INVALID);
    }
    if((dirty_mask & DASH_DIRTY_FANS) != 0U) {
        /* DBC BO_1442: three RPM values, two measured duty cycles. */
        if((valid & DASH_VALID_FAN) != 0U) {
            Dash_View_SetFans(g_dashboard_data.fan_rpm, g_dashboard_data.fan_pwm_duty);
        }
        else {
            static const int32_t no_rpm[3] = {DASH_VIEW_INVALID, DASH_VIEW_INVALID, DASH_VIEW_INVALID};
            static const int32_t no_duty[2] = {DASH_VIEW_INVALID, DASH_VIEW_INVALID};
            Dash_View_SetFans(no_rpm, no_duty);
        }
    }
    for(uint32_t i = 0U; i < 4U; i++) {
        if((dirty_mask & (DASH_DIRTY_MOTOR_0 << i)) != 0U) apply_motor_ui(i);
        if((dirty_mask & (DASH_DIRTY_TIRE_0 << i)) != 0U) Dash_View_SetTire(i, g_tire_temp[i]);
    }
    if((dirty_mask & DASH_DIRTY_ODOMETER) != 0U) {
        Dash_View_SetOdometer(g_dashboard_data.odometer_tenths);
    }
}

void Dashboard_UI_SubmitData(const dashboard_data_t * data)
{
    uint32_t index;
    uint32_t fan;
    uint32_t dirty_mask = 0U;
    uint32_t valid_changed;
    uint32_t primask;

    if(data == NULL) {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    valid_changed = g_pending_dashboard_data.valid_mask ^ data->valid_mask;
    if((g_pending_dashboard_data.soc != data->soc) ||
       (g_pending_dashboard_data.sum_voltage_dv != data->sum_voltage_dv) ||
       (g_pending_dashboard_data.sum_current_da != data->sum_current_da) ||
       (g_pending_dashboard_data.max_temperature != data->max_temperature) ||
       ((valid_changed & DASH_VALID_BATTERY_GROUP) != 0U)) {
        dirty_mask |= DASH_DIRTY_BATTERY;
    }
    if((g_pending_dashboard_data.aps_open_pct != data->aps_open_pct) ||
       (g_pending_dashboard_data.brake_pct != data->brake_pct) ||
       (g_pending_dashboard_data.regen_pct != data->regen_pct) ||
       ((valid_changed & DASH_VALID_PEDAL_GROUP) != 0U)) {
        dirty_mask |= DASH_DIRTY_PEDALS;
    }
    if((g_pending_dashboard_data.lv_bus_voltage_mV != data->lv_bus_voltage_mV) ||
       (g_pending_dashboard_data.lv_bus_current_cA != data->lv_bus_current_cA) ||
       (g_pending_dashboard_data.lv_bus_power_dW != data->lv_bus_power_dW) ||
       ((valid_changed & DASH_VALID_LV_BUS) != 0U)) {
        dirty_mask |= DASH_DIRTY_LOWVOLTAGE;
    }
    if((valid_changed & DASH_VALID_FAN) != 0U) {
        dirty_mask |= DASH_DIRTY_FANS;
    }
    if((g_pending_dashboard_data.sop_discharge_power_dkw != data->sop_discharge_power_dkw) ||
       (g_pending_dashboard_data.sop_charge_power_dkw != data->sop_charge_power_dkw) ||
       (g_pending_dashboard_data.sop_discharge_current_da != data->sop_discharge_current_da) ||
       (g_pending_dashboard_data.sop_charge_current_da != data->sop_charge_current_da) ||
       ((valid_changed & (DASH_VALID_SOP_POWER | DASH_VALID_SOP_CURRENT)) != 0U)) {
        dirty_mask |= DASH_DIRTY_SOP;
    }
    for(fan = 0U; fan < 3U; fan++) {
        if((g_pending_dashboard_data.fan_rpm[fan] != data->fan_rpm[fan]) ||
           ((fan < 2U) && (g_pending_dashboard_data.fan_pwm_duty[fan] != data->fan_pwm_duty[fan]))) {
            dirty_mask |= DASH_DIRTY_FANS;
        }
    }
    for(index = 0U; index < 4U; index++) {
        if((g_pending_dashboard_data.torque[index] != data->torque[index]) ||
           (g_pending_dashboard_data.motor_enable[index] != data->motor_enable[index]) ||
           (g_pending_dashboard_data.rpm[index] != data->rpm[index]) ||
           (g_pending_dashboard_data.motor_temp[index] != data->motor_temp[index]) ||
           (g_pending_dashboard_data.inverter_temp[index] != data->inverter_temp[index]) ||
           (g_pending_dashboard_data.diag_num[index] != data->diag_num[index]) ||
           ((valid_changed & DASH_VALID_MOTOR_GROUP(index)) != 0U)) {
            dirty_mask |= (DASH_DIRTY_MOTOR_0 << index);
        }
    }

    if(dirty_mask == 0U) {
        __set_PRIMASK(primask);
        return;
    }

    g_pending_dashboard_data.valid_mask = data->valid_mask;
    g_pending_dashboard_data.soc = data->soc;
    g_pending_dashboard_data.sum_voltage = data->sum_voltage;
    g_pending_dashboard_data.sum_current = data->sum_current;
    g_pending_dashboard_data.sum_voltage_dv = data->sum_voltage_dv;
    g_pending_dashboard_data.sum_current_da = data->sum_current_da;
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
    g_pending_dashboard_data.sop_discharge_power_dkw = data->sop_discharge_power_dkw;
    g_pending_dashboard_data.sop_charge_power_dkw = data->sop_charge_power_dkw;
    g_pending_dashboard_data.sop_discharge_current_da = data->sop_discharge_current_da;
    g_pending_dashboard_data.sop_charge_current_da = data->sop_charge_current_da;
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
    g_pending_dashboard_data.regen_pct = data->regen_pct;

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

static void dashboard_submit_speed(int32_t speed)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t now = HAL_GetTick();
    __disable_irq();
    if((g_speed_dirty != 0U) && (g_pending_speed == speed)) {
        __set_PRIMASK(primask);
        return;
    }
    if(g_speed_dirty != 0U) SpeedTrace_Superseded();
    g_pending_speed = speed;
    g_pending_speed_tick = now;
    g_speed_dirty = 1U;
    __set_PRIMASK(primask);
}

void Dashboard_UI_SubmitSpeed(int32_t speed)
{
    if(speed < 0) speed = 0;
    if(speed > 300) speed = 300;
    dashboard_submit_speed(speed);
}

void Dashboard_UI_SubmitSpeedInvalid(void)
{
    dashboard_submit_speed(-1);
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
    dashboard_submit_drive_mode(mode_index, 0U);
}

void Dashboard_UI_RequestDriveMode(int32_t mode_index)
{
    dashboard_submit_drive_mode(mode_index, 1U);
}

void Dashboard_UI_RequestDriveModeCycle(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_pending_mode_cycle = 1U;
    if(primask == 0U) {
        __enable_irq();
    }
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
       (g_pending_alert_clear == 0U) && (g_pending_mode_cycle == 0U) &&
       (g_pending_mode_lock_alert == 0U)) {
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

    if(g_pending_mode_cycle != 0U) {
        primask = __get_PRIMASK();
        __disable_irq();
        g_pending_mode_cycle = 0U;
        if(primask == 0U) {
            __enable_irq();
        }
        dashboard_cycle_drive_mode();
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
        Dash_View_SetSignal(pending_signal_level);
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
            Dash_View_SetLapCounter(g_laps_current, remaining_m);
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
            g_dashboard_data.sum_voltage_dv = g_pending_dashboard_data.sum_voltage_dv;
            g_dashboard_data.sum_current_da = g_pending_dashboard_data.sum_current_da;
            g_dashboard_data.max_temperature = g_pending_dashboard_data.max_temperature;
            dashboard_copy_valid(DASH_VALID_BATTERY_GROUP);
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
            g_dashboard_data.regen_pct = g_pending_dashboard_data.regen_pct;
            dashboard_copy_valid(DASH_VALID_PEDAL_GROUP);
        }
        if((dashboard_mask & DASH_DIRTY_LOWVOLTAGE) != 0U) {
            g_dashboard_data.lv_bus_voltage_mV = g_pending_dashboard_data.lv_bus_voltage_mV;
            g_dashboard_data.lv_bus_current_cA = g_pending_dashboard_data.lv_bus_current_cA;
            g_dashboard_data.lv_bus_power_dW = g_pending_dashboard_data.lv_bus_power_dW;
            dashboard_copy_valid(DASH_VALID_LV_BUS);
        }
        if((dashboard_mask & DASH_DIRTY_SOP) != 0U) {
            g_dashboard_data.sop_discharge_power_dkw = g_pending_dashboard_data.sop_discharge_power_dkw;
            g_dashboard_data.sop_charge_power_dkw = g_pending_dashboard_data.sop_charge_power_dkw;
            g_dashboard_data.sop_discharge_current_da = g_pending_dashboard_data.sop_discharge_current_da;
            g_dashboard_data.sop_charge_current_da = g_pending_dashboard_data.sop_charge_current_da;
            dashboard_copy_valid(DASH_VALID_SOP_POWER | DASH_VALID_SOP_CURRENT);
        }
        if((dashboard_mask & DASH_DIRTY_FANS) != 0U) {
            for(uint32_t fan = 0U; fan < 3U; fan++) {
                g_dashboard_data.fan_rpm[fan] = g_pending_dashboard_data.fan_rpm[fan];
            }
            for(uint32_t fan = 0U; fan < 2U; fan++) {
                g_dashboard_data.fan_pwm_duty[fan] = g_pending_dashboard_data.fan_pwm_duty[fan];
            }
            dashboard_copy_valid(DASH_VALID_FAN);
        }
        for(index = 0; index < 4U; index++) {
            if((dashboard_mask & (DASH_DIRTY_MOTOR_0 << index)) != 0U) {
                g_dashboard_data.torque[index] = g_pending_dashboard_data.torque[index];
                g_dashboard_data.motor_enable[index] = g_pending_dashboard_data.motor_enable[index];
                g_dashboard_data.rpm[index] = g_pending_dashboard_data.rpm[index];
                g_dashboard_data.motor_temp[index] = g_pending_dashboard_data.motor_temp[index];
                g_dashboard_data.inverter_temp[index] = g_pending_dashboard_data.inverter_temp[index];
                g_dashboard_data.diag_num[index] = g_pending_dashboard_data.diag_num[index];
                dashboard_copy_valid(DASH_VALID_MOTOR_GROUP(index));
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

    /* Speed is applied on every pass, not throttled: the GNSS delivers 20 Hz and
     * Dash_View_SetSpeed() redraws only when the shown text actually changes.
     * A negative pending value means "no trusted speed" and is shown as "--". */
    if(g_speed_dirty != 0U) {
        uint32_t submit_tick;
        int32_t shown;

        primask = __get_PRIMASK();
        __disable_irq();
        pending_speed = g_pending_speed;
        submit_tick = g_pending_speed_tick;
        g_speed_dirty = 0U;
        if(primask == 0U) {
            __enable_irq();
        }

        if(pending_speed < 0) {
            shown = DASH_VIEW_INVALID;
        }
        else {
            if(pending_speed > SPEED_DISPLAY_MAX_KMH) pending_speed = SPEED_DISPLAY_MAX_KMH;
            shown = pending_speed;
        }
        g_dashboard_data.speed = (shown == DASH_VIEW_INVALID) ? -1 : shown;
        if(shown != g_speed) {
            g_speed = shown;
            Dash_View_SetSpeed(shown);
            SpeedTrace_Applied(submit_tick, HAL_GetTick());
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
        primask = __get_PRIMASK();
        __disable_irq();
        g_lap_times_dirty = 0U;
        g_current_lap_time = g_pending_lap_current;
        g_last_lap_time = g_pending_lap_last;
        g_best_lap_time = g_pending_lap_best;
        g_laps_current = g_pending_lap_count;
        __set_PRIMASK(primask);

        Dash_View_SetLapTimes(g_current_lap_time, g_last_lap_time, g_best_lap_time);
        Dash_View_SetLapCounter(g_laps_current, g_laps_left);
    }
}

void Dashboard_UI_Init(void)
{
    g_dashboard_initialized = 0U;

    LED_Diag_SetBootStage(3);
    Dash_View_Create();
    LED_Diag_SetBootStage(4);

    apply_drive_mode_ui();
    dashboard_apply_data(DASH_DIRTY_ALL);
    dashboard_reset_lap_ui();
    Dash_View_SetSignal(g_dashboard_data.signal_level);
    update_alert_ui();
    dashboard_backlight_init();
    update_night_mode_ui();
    g_dashboard_initialized = 1U;
    LED_Diag_SetBootStage(5);   /* both LEDs off: UI init fully complete */
}

void Dashboard_UI_SubmitTouchState(uint16_t x, uint16_t y, uint8_t pressed)
{
    static uint8_t last_pressed = 0U;
    static uint32_t last_lap_toggle_tick = 0U;
    static uint32_t last_mode_cycle_tick = 0U;
    static uint32_t last_alert_touch_tick = 0U;
    static uint32_t last_night_toggle_tick = 0U;
    uint32_t now = HAL_GetTick();

    if((pressed != 0U) && (last_pressed == 0U)) {
        switch(Dash_View_HitTest(x, y)) {
            case DASH_ZONE_LAP:
                if((now - last_lap_toggle_tick) >= 200U) {
                    last_lap_toggle_tick = now;
                    g_pending_lap_toggle = 1U;
                }
                break;
            case DASH_ZONE_MODE:
                if((now - last_mode_cycle_tick) >= DASHBOARD_MODE_TOUCH_DEBOUNCE_MS) {
                    last_mode_cycle_tick = now;
                    Dashboard_UI_RequestDriveModeCycle();
                }
                break;
            case DASH_ZONE_ALERT:
                if((now - last_alert_touch_tick) >= 200U) {
                    last_alert_touch_tick = now;
                    if(g_pending_alert_pop_count < 255U) {
                        g_pending_alert_pop_count++;
                    }
                }
                break;
            case DASH_ZONE_NIGHT:
                if((now - last_night_toggle_tick) >= 200U) {
                    last_night_toggle_tick = now;
                    g_pending_night_toggle = 1U;
                }
                break;
            default:
                break;
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
