/**
 * @file    freertos_main.c
 * @brief   PC simulator: renders the firmware dashboard view
 *          (stm32_port/Core/Src/dashboard_view.c) with animated demo data.
 * @license MIT License
 *
 * Keys: Space cycle mode, A/X/8/E select AC/AX/Skidpad/Endurance, 0-7 slip level, R lap timing,
 * N night mode, F push demo alert, Y READY banner, [ ] / G GPS signal,
 * Esc quits. Mouse: top bar = lap timing, top-left logo = replay the splash animation
 * (any click skips it while it plays), mode tile = cycle driving mode,
 * alert banner = dismiss, eye button = night mode (same zones as the touch panel).
 */

#include "lvgl/lvgl.h"

#include "FreeRTOS.h"
#include "task.h"

#if LV_USE_OS == LV_OS_FREERTOS || defined(__APPLE__)

#include "hal/hal.h"
#include "../stm32_port/Core/Inc/dashboard_view.h"
#include "../stm32_port/Core/Inc/splash_view.h"
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

#define SIM_DEMO_PERIOD_MS 100
#define SIM_NIGHT_MASK_OPA ((lv_opa_t)140) /* 140/255 = 55% dimming, like 45% backlight */
#define SIM_ALERT_MAX 6U

static volatile bool g_simulator_exit_requested = false;
static uint8_t g_night_mode = 0U;
static lv_obj_t * g_night_mask;
static int g_mode = DASH_MODE_DEFAULT;
static int g_slip = 3;
static int g_signal = 4;
static bool g_timing_active = false;
static bool g_ready = false;
static uint32_t g_tick;
static int32_t g_lap_time, g_last_lap, g_best_lap, g_lap_count;
static int32_t g_odometer_tenths = 412;
static int32_t g_power_peak;
static int32_t g_motor_peak[4];
static int32_t g_current_peak;
static const char * g_alerts[SIM_ALERT_MAX];
static uint32_t g_alert_count;

static void refresh_alert(void)
{
    Dash_View_SetAlert(g_alert_count ? g_alerts[0] : NULL, g_alert_count, g_ready);
}

static void push_demo_alert(void)
{
    static const char * const demo[] = {"gps not fixed", "can timeout", "inverter overtemp", "motor fault"};
    static uint32_t next;

    if(g_alert_count < SIM_ALERT_MAX) g_alerts[g_alert_count++] = demo[next++ % 4U];
    refresh_alert();
}

static void pop_alert(void)
{
    if(g_alert_count == 0U) return;
    memmove(&g_alerts[0], &g_alerts[1], (g_alert_count - 1U) * sizeof(g_alerts[0]));
    g_alert_count--;
    refresh_alert();
}

static void set_night(uint8_t night)
{
    g_night_mode = night;
    Dash_View_SetNight(night);
    if(night) lv_obj_remove_flag(g_night_mask, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_night_mask, LV_OBJ_FLAG_HIDDEN);
}

static void reset_lap(void)
{
    g_lap_time = g_last_lap = g_best_lap = g_lap_count = 0;
    Dash_View_SetLapTimes(0, 0, 0);
    Dash_View_SetLapCounter(0, g_mode == DASH_MODE_ACCELERATION ? 75 : 12);
    Dash_View_SetDelta(0);
}

static void set_mode(int mode)
{
    g_mode = mode;
    g_timing_active = false;
    Dash_View_SetTimingDot(0U, 0U);
    Dash_View_SetMode((dash_mode_t)mode);
    reset_lap();
}

/* Triangle wave 0..amplitude..0 with the given period in demo ticks. */
static int32_t triangle(uint32_t tick, uint32_t period, int32_t amplitude)
{
    uint32_t phase = tick % period;
    uint32_t half = period / 2U;
    return (int32_t)((phase < half ? phase : period - phase) * (uint32_t)amplitude / half);
}

static void demo_timer_cb(lv_timer_t * timer)
{
    static const int8_t tread_offset[4][4] = {{-3, -1, 1, 3}, {-2, 0, 2, 4}, {-4, -2, 0, 2}, {-1, 1, 3, 5}};
    int32_t speed, throttle, brake, regen, fan_rpm[3], fan_duty[2];
    int32_t soc, current, power;

    LV_UNUSED(timer);
    g_tick++;

    /* 20 s accelerate/brake cycle. */
    speed = triangle(g_tick, 200U, 118);
    throttle = (g_tick % 200U) < 100U ? 40 + triangle(g_tick, 50U, 60) : 0;
    brake = (g_tick % 200U) >= 100U ? triangle(g_tick, 100U, 90) : 0;
    regen = (g_tick % 200U) >= 100U ? triangle(g_tick, 100U, 100) : 0;
    Dash_View_SetSpeed(speed);
    Dash_View_SetPedals(throttle, brake, regen);

    soc = 100 - (int32_t)((g_tick / 50U) % 100U);
    current = throttle * 3;
    power = (392 * current) / 1000;
    if(power > g_power_peak) g_power_peak = power;
    Dash_View_SetBattery(soc, 392, current);
    Dash_View_SetPower(power, g_power_peak);
    if(current > g_current_peak) g_current_peak = current;
    Dash_View_SetCurrentPeak(g_current_peak);
    /* BMS SOP: 放电上限随温升降额，回收上限固定 8.0 kW / 8.0 A。 */
    Dash_View_SetSop(800 - triangle(g_tick, 600U, 300), 80,
                     1800 - triangle(g_tick, 600U, 600), 80);
    Dash_View_SetMaxTemp(55 + triangle(g_tick, 600U, 35));
    Dash_View_SetLowVoltage(13820, -1240 - throttle * 4, 1712 + throttle * 5);

    fan_duty[0] = fan_duty[1] = 40 + throttle * 3 / 5;
    fan_rpm[0] = 1200 + fan_duty[0] * 24;
    fan_rpm[1] = fan_rpm[0] - 60;
    fan_rpm[2] = fan_rpm[0] - 180;
    Dash_View_SetFans(fan_rpm, fan_duty);

    for(uint32_t wheel = 0U; wheel < 4U; wheel++) {
        dash_motor_t motor = {
            .torque = throttle * 21 / 100 - brake / 8,
            .rpm = speed * 100,
            .motor_temp = 48 + (int32_t)wheel + triangle(g_tick, 600U, 45),
            .inverter_temp = 44 + (int32_t)wheel * 2 + triangle(g_tick, 400U, 20),
            .diag = (wheel == 1U && (g_tick % 300U) > 240U) ? 0x0231U : 0U,
            .enabled = 1U,
        };
        int32_t tread[4];
        motor.power_dkw = (motor.torque * motor.rpm) / 955;
        if(motor.power_dkw > g_motor_peak[wheel]) g_motor_peak[wheel] = motor.power_dkw;
        motor.power_peak_dkw = g_motor_peak[wheel];
        Dash_View_SetMotor(wheel, &motor);
        for(uint32_t seg = 0U; seg < 4U; seg++) {
            tread[seg] = (30 + triangle(g_tick, 360U, 70) + tread_offset[wheel][seg]) * 100;
        }
        Dash_View_SetTire(wheel, tread);
    }

    g_odometer_tenths += (speed > 0 && (g_tick % 30U) == 0U) ? 1 : 0;
    Dash_View_SetOdometer(g_odometer_tenths);

    if(g_timing_active) {
        g_lap_time += SIM_DEMO_PERIOD_MS / 10;
        if(g_lap_time >= 6000 + (int32_t)(g_lap_count % 3) * 150) {
            g_last_lap = g_lap_time;
            if((g_best_lap == 0) || (g_last_lap < g_best_lap)) g_best_lap = g_last_lap;
            g_lap_time = 0;
            g_lap_count++;
        }
        Dash_View_SetLapTimes(g_lap_time, g_last_lap, g_best_lap);
        Dash_View_SetLapCounter(g_lap_count,
                                g_mode == DASH_MODE_ACCELERATION ? 75 - triangle(g_tick, 150U, 75)
                                                      : 12 - g_lap_count % 12);
        Dash_View_SetDelta(triangle(g_tick, 120U, 300) - 150);
        Dash_View_SetTimingDot((g_tick / 5U) % 2U, 0x20D050);
    }
}

static void splash_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    Splash_View_Step(lv_tick_get());
}

static void screen_press_cb(lv_event_t * e)
{
    lv_point_t point;

    LV_UNUSED(e);
    if(Splash_View_IsActive()) {      /* 开屏中任意点击只用来跳过动画 */
        Splash_View_Skip();
        return;
    }
    lv_indev_get_point(lv_indev_active(), &point);
    switch(Dash_View_HitTest(point.x, point.y)) {
        case DASH_ZONE_LOGO:
            Splash_View_Start(SPLASH_REPLAY);
            break;
        case DASH_ZONE_LAP:
            g_timing_active = !g_timing_active;
            if(!g_timing_active) Dash_View_SetTimingDot(0U, 0U);
            break;
        case DASH_ZONE_MODE:
            set_mode(g_mode == DASH_MODE_ENDURANCE ? DASH_MODE_DEFAULT : g_mode + 1);
            break;
        case DASH_ZONE_ALERT:
            pop_alert();
            break;
        case DASH_ZONE_NIGHT:
            set_night(g_night_mode ? 0U : 1U);
            break;
        default:
            break;
    }
}

static void key_event_cb(lv_event_t * e)
{
    uint32_t key = lv_event_get_key(e);

    if(key >= 'a' && key <= 'z') key -= 'a' - 'A';
    switch(key) {
        case LV_KEY_ESC: g_simulator_exit_requested = true; break;
        case ' ': set_mode(g_mode == DASH_MODE_ENDURANCE ? DASH_MODE_DEFAULT : g_mode + 1); break;
        case 'A': set_mode(DASH_MODE_ACCELERATION); break;
        case 'X': set_mode(DASH_MODE_AUTOCROSS); break;
        case '8': set_mode(DASH_MODE_SKIDPAD); break;
        case 'E': set_mode(DASH_MODE_ENDURANCE); break;
        case 'D': set_mode(DASH_MODE_DEFAULT); break;
        case 'R':
            g_timing_active = !g_timing_active;
            if(!g_timing_active) Dash_View_SetTimingDot(0U, 0U);
            break;
        case 'N': set_night(g_night_mode ? 0U : 1U); break;
        case 'F': push_demo_alert(); break;
        case 'Y': g_ready = !g_ready; refresh_alert(); break;
        case '[': if(g_signal > 0) Dash_View_SetSignal(--g_signal); break;
        case ']': if(g_signal < 4) Dash_View_SetSignal(++g_signal); break;
        case 'G': g_signal = (g_signal + 1) % 5; Dash_View_SetSignal(g_signal); break;
        default:
            if(key >= '0' && key <= '7') {
                g_slip = (int)(key - '0');
                Dash_View_SetSlip(g_slip);
            }
            break;
    }
}

static void create_simulator_ui(void)
{
    lv_obj_t * key_target;

    Dash_View_Create();
    lv_obj_add_event_cb(lv_screen_active(), screen_press_cb, LV_EVENT_PRESSED, NULL);

    /* Night backlight stand-in: a translucent black layer over everything. */
    g_night_mask = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_night_mask);
    lv_obj_set_size(g_night_mask, DASH_SCREEN_W, DASH_SCREEN_H);
    lv_obj_set_style_bg_color(g_night_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_night_mask, SIM_NIGHT_MASK_OPA, 0);
    lv_obj_remove_flag(g_night_mask, LV_OBJ_FLAG_CLICKABLE);

    /* Invisible focus target that receives SDL keyboard events. */
    key_target = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(key_target);
    lv_obj_set_size(key_target, 1, 1);
    lv_obj_add_event_cb(key_target, key_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), key_target);
    lv_group_focus_obj(key_target);

    Dash_View_SetSlip(g_slip);
    Dash_View_SetSignal(g_signal);
    set_mode(DASH_MODE_DEFAULT);
    set_night(g_night_mode);
    demo_timer_cb(NULL);
    lv_timer_create(demo_timer_cb, SIM_DEMO_PERIOD_MS, NULL);
    Splash_View_Start(SPLASH_BOOT);
    lv_timer_create(splash_timer_cb, 16, NULL);
}

void vApplicationMallocFailedHook(void)
{
    printf("Malloc failed! Available heap: %ld bytes\n", xPortGetFreeHeapSize());
    fflush(stdout);
    exit(EXIT_FAILURE);
}

void vApplicationIdleHook(void) {}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("Stack overflow in task %s\n", pcTaskName);
    LV_UNUSED(xTask);
    fflush(stdout);
    exit(EXIT_FAILURE);
}

void vApplicationTickHook(void) {}

void lvgl_task(void *pvParameters)
{
    LV_UNUSED(pvParameters);

    lv_init();
    if(sdl_hal_init(DASH_SCREEN_W, DASH_SCREEN_H) == NULL) {
        printf("Simulator display init failed\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    create_simulator_ui();

    while(!g_simulator_exit_requested) {
        lv_timer_handler();
        SIM_LVGL_LOOP_DELAY();
    }

    printf("Simulator exiting by request\n");
    fflush(stdout);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    /* --night: start in night mode, used for screenshot capture where
     * synthetic key injection is unreliable. */
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--night") == 0) {
            g_night_mode = 1U;
        }
    }

#if defined(__APPLE__)
    lvgl_task(NULL); /* 主线程直接运行, 退出时由 lvgl_task 内 exit() 结束进程 */
    return 0;
#else
    if(xTaskCreate(lvgl_task, "LVGL Task", 4096, NULL, 1, NULL) != pdPASS) {
        printf("Error creating LVGL task\n");
        return 1;
    }
    vTaskStartScheduler();
    return 0;
#endif
}

#endif
