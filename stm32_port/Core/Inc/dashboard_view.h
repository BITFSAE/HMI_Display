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

/* 与 ECU ModeFlag 一致：-1=默认，0=AC，1=AX，2=8，3=EN。 */
typedef enum {
    DASH_MODE_DEFAULT = -1,
    DASH_MODE_ACCELERATION = 0,
    DASH_MODE_AUTOCROSS,
    DASH_MODE_SKIDPAD,
    DASH_MODE_ENDURANCE
} dash_mode_t;

typedef enum {
    DASH_ZONE_NONE = 0,
    DASH_ZONE_LAP,      /* top bar: start/stop lap or 75 m run timing */
    DASH_ZONE_MODE,     /* mode tile: cycle DF -> AC -> AX -> 8 -> EN -> DF */
    DASH_ZONE_ALERT,    /* alert banner: dismiss the oldest alert */
    DASH_ZONE_NIGHT,    /* night button: toggle backlight dimming */
    DASH_ZONE_LOGO      /* top-left logo: replay the splash animation */
} dash_zone_t;

/* Pass as a value to the setters below to show "--" for that field. */
#define DASH_VIEW_INVALID INT32_MIN

/* dash_motor_t.invalid bits: field has no valid data and is shown as "--". */
#define DASH_MOTOR_NO_TORQUE      (1U << 0)
#define DASH_MOTOR_NO_RPM         (1U << 1)
#define DASH_MOTOR_NO_MOTOR_TEMP  (1U << 2)
#define DASH_MOTOR_NO_INV_TEMP    (1U << 3)
#define DASH_MOTOR_NO_DIAG        (1U << 4)
#define DASH_MOTOR_NO_ENABLED     (1U << 5)

/* Per-wheel motor readout. Values keep the raw dashboard units. */
typedef struct {
    int32_t torque;        /* Nm (DBC ActualTorque, raw 1 Nm; DBC unit text is wrong) */
    int32_t rpm;
    int32_t power_dkw;     /* 0.1 kW, torque x rpm / 9549 */
    int32_t power_peak_dkw;  /* 0.1 kW, highest power since power-on (never reset) */
    int32_t motor_temp;    /* degC */
    int32_t inverter_temp; /* degC */
    uint32_t diag;         /* AMK diagnostic number, 0 = OK */
    uint8_t enabled;       /* AMK_bEnable */
    uint8_t invalid;       /* DASH_MOTOR_NO_* bits, 0 = everything valid */
} dash_motor_t;

/* Thread-safe pure function: maps screen coordinates to a touch zone. */
dash_zone_t Dash_View_HitTest(int32_t x, int32_t y);

/* Builds and loads the dashboard screen. */
void Dash_View_Create(void);

/* Wheel index everywhere: 0=FL 1=RL 2=FR 3=RR (dashboard_data_t order). */
/* kmh == DASH_VIEW_INVALID shows "--" (no trusted speed); a real 0 km/h shows "0". */
void Dash_View_SetSpeed(int32_t kmh);
void Dash_View_SetMode(dash_mode_t mode);
void Dash_View_SetSlip(int32_t level);
/* The pedal, battery, power, max-temp, low-voltage and fan setters accept
 * DASH_VIEW_INVALID per argument and then show "--" (pedal bars empty). */
/* 三条独立开度条：油门、机械制动（油压）、能量回收（踏板转角）。 */
void Dash_View_SetPedals(int32_t throttle_pct, int32_t mechanical_brake_pct,
                         int32_t regen_pct);
void Dash_View_SetBattery(int32_t soc_pct_x10, int32_t voltage_v, int32_t current_a);
/* peak_kw: highest discharge power since power-on; DASH_VIEW_INVALID until
 * the first valid sample. */
void Dash_View_SetPower(int32_t live_kw, int32_t peak_kw);
/* Highest discharge current seen since power-on, whole amperes (right side
 * of the PK row). */
void Dash_View_SetCurrentPeak(int32_t peak_a);
/* BMS SOP limits: power in 0.1 kW, current in 0.1 A. Power (0x4A0) and
 * current (0x4A3) are separate frames, so each argument may be
 * DASH_VIEW_INVALID on its own. */
void Dash_View_SetSop(int32_t discharge_dkw, int32_t charge_dkw,
                      int32_t discharge_da, int32_t charge_da);
void Dash_View_SetMaxTemp(int32_t temp_c);
/* voltage_mv == DASH_VIEW_INVALID blanks all three values. */
void Dash_View_SetLowVoltage(int32_t voltage_mv, int32_t current_ca, int32_t power_dw);
/* Fan 3 has no duty signal in the DBC, so only two duties are passed. */
void Dash_View_SetFans(const int32_t rpm[3], const int32_t duty_pct[2]);
void Dash_View_SetMotor(uint32_t wheel, const dash_motor_t * motor);
/* Tread temperatures outside-to-inside, 0.01 degC. */
void Dash_View_SetTire(uint32_t wheel, const int32_t temps_centi[4]);
/* Times in 0.01 s. */
void Dash_View_SetLapTimes(int32_t current, int32_t last, int32_t best);
/* AX/8/EN: lap count and laps left; AC: run count and metres remaining. */
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
