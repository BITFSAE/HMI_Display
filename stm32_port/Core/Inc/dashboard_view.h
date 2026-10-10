/* USER CODE BEGIN Dashboard_View_H */
#ifndef DASHBOARD_VIEW_H
#define DASHBOARD_VIEW_H

/* Pure LVGL dashboard view, shared by the firmware (dashboard_ui.c) and the PC
 * simulator (src/freertos_main.c). No HAL/RTOS calls: every Dash_View_* except
 * Dash_View_HitTest must run in the LVGL thread. Setters cache their inputs
 * and skip LVGL work when nothing visible changes. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DASH_SCREEN_W 800
#define DASH_SCREEN_H 480

/* Drive modes, same numbering as DBC ModeFlag: 0=S 1=Q 2=C 3=E. */
typedef enum {
    DASH_MODE_S = 0,
    DASH_MODE_Q,
    DASH_MODE_C,
    DASH_MODE_E
} dash_mode_t;

typedef enum {
    DASH_ZONE_NONE = 0,
    DASH_ZONE_LAP,      /* top bar: start/stop lap or 75 m run timing */
    DASH_ZONE_ALERT,    /* alert banner: dismiss the oldest alert */
    DASH_ZONE_NIGHT     /* night button: toggle backlight dimming */
} dash_zone_t;

/* Per-wheel motor readout. Values keep the raw dashboard units. */
typedef struct {
    int32_t torque;        /* DBC ActualTorque raw (0.1 %Mn) */
    int32_t rpm;
    int32_t power;         /* dashboard_ui.c motor power estimate */
    int32_t power_peak;
    int32_t motor_temp;    /* degC */
    int32_t inverter_temp; /* degC */
    uint32_t diag;         /* AMK diagnostic number, 0 = OK */
    uint8_t enabled;       /* AMK_bEnable */
} dash_motor_t;

/* Thread-safe pure function: maps screen coordinates to a touch zone. */
dash_zone_t Dash_View_HitTest(int32_t x, int32_t y);

/* Builds and loads the dashboard screen. */
void Dash_View_Create(void);

/* Wheel index everywhere: 0=FL 1=RL 2=FR 3=RR (dashboard_data_t order). */
void Dash_View_SetSpeed(int32_t kmh);
void Dash_View_SetMode(dash_mode_t mode);
void Dash_View_SetSlip(int32_t level);
void Dash_View_SetPedals(int32_t throttle_pct, int32_t brake_pct);
void Dash_View_SetBattery(int32_t soc_pct, int32_t voltage_v, int32_t current_a);
void Dash_View_SetPower(int32_t live_kw, int32_t peak_kw);
void Dash_View_SetMaxTemp(int32_t temp_c);
void Dash_View_SetLowVoltage(int32_t voltage_mv, int32_t current_ca, int32_t power_dw);
/* Fan 3 has no duty signal in the DBC, so only two duties are passed. */
void Dash_View_SetFans(const int32_t rpm[3], const int32_t duty_pct[2]);
void Dash_View_SetMotor(uint32_t wheel, const dash_motor_t * motor);
/* Tread temperatures outside-to-inside, 0.01 degC. */
void Dash_View_SetTire(uint32_t wheel, const int32_t temps_centi[4]);
/* Times in 0.01 s. */
void Dash_View_SetLapTimes(int32_t current, int32_t last, int32_t best);
/* Q/C/E: lap count and laps left; S: run count and metres remaining. */
void Dash_View_SetLapCounter(int32_t count, int32_t remaining);
/* Lap delta in 0.01 s, negative = faster. */
void Dash_View_SetDelta(int32_t delta);
/* GPS signal level 0..4. */
void Dash_View_SetSignal(int32_t level);
/* Blinking lap-timing state dot next to the lap time. */
void Dash_View_SetTimingDot(uint8_t visible, uint32_t color_hex);
/* text: oldest pending alert (NULL/"" = none); count: queue length.
 * ready != 0 shows the green S-mode READY banner instead. */
void Dash_View_SetAlert(const char * text, uint32_t count, uint8_t ready);
void Dash_View_SetOdometer(int32_t tenths_km);
void Dash_View_SetNight(uint8_t night);

#ifdef __cplusplus
}
#endif

#endif
/* USER CODE END Dashboard_View_H */
