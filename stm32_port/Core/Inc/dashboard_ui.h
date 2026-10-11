#ifndef DASHBOARD_UI_H
#define DASHBOARD_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* dashboard_data_t.valid_mask: a bit is set while its CAN source is fresh and
 * flagged valid. All bits start cleared (shown as "--") and are cleared again
 * when the source frame times out or reports itself invalid. */
#define DASH_VALID_SOC         (1UL << 0)   /* 0x4B0 byte5 bit2 */
#define DASH_VALID_HV_V        (1UL << 1)   /* 0x4B0 byte5 bit0 */
#define DASH_VALID_HV_I        (1UL << 2)   /* 0x4B0 byte5 bit1 */
#define DASH_VALID_APS         (1UL << 3)   /* 0x305 */
#define DASH_VALID_BRAKE       (1UL << 4)   /* 0x305 油压推算（临时） */
#define DASH_VALID_TORQUE      (1UL << 5)   /* 0x502 */
#define DASH_VALID_RPM         (1UL << 6)   /* 0x505 */
#define DASH_VALID_MOTOR_TEMP  (1UL << 7)   /* 0x506 */
#define DASH_VALID_INV_TEMP    (1UL << 8)   /* 0x507 */
#define DASH_VALID_IGBT_TEMP   (1UL << 9)   /* 0x508 */
#define DASH_VALID_AMK_EN      (1UL << 10)  /* 0x509 */
#define DASH_VALID_DIAG_RL_RR  (1UL << 11)  /* 0x503: Diagnostic_number_1/2 */
#define DASH_VALID_DIAG_FL_FR  (1UL << 12)  /* 0x504: Diagnostic_number_3/4 */
#define DASH_VALID_LV_BUS      (1UL << 13)  /* 0x5A0, not the INA226-offline placeholder */
#define DASH_VALID_FAN         (1UL << 14)  /* 0x5A2 */
#define DASH_VALID_SOP_POWER   (1UL << 15)  /* 0x4A0, LimitsValid=1 且 CRC/版本通过 */
#define DASH_VALID_SOP_CURRENT (1UL << 16)  /* 0x4A3, 版本通过且 InputHealth bit7=1 */

typedef struct {
	/* Display speed in integer km/h. CAN GPS_Speed uses 0.1 km/h raw units. */
	int32_t speed;
	/* DBC BO_1200 BatterySOC (%) */
	int32_t soc;
	/* DBC BO_1289 Debug9: ModeFlag, bit4|4@1- [-8,7] */
	int32_t mode_index;
	/* DBC BO_1282 Debug2: RL/RR/FL/FR_ActualTorque (1,0) Nm */
	/* Wheel order: 0=LF(DBC FL), 1=LR(DBC RL), 2=RF(DBC FR), 3=RR(DBC RR) */
	int32_t torque[4];
	/* DBC BO_1289 Debug9: FR/FL/RR/RL_AMK_bEnable at bit20/21/22/23 */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	uint8_t motor_enable[4];
	/* DBC BO_1285 Debug5: RL/RR/FL/FR_ActualVelocity (1,0) */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	int32_t rpm[4];
	/* DBC BO_1200 BatteryVoltage (V, 0.1V/raw /10) */
	int32_t sum_voltage;
	/* DBC BO_1200 BatteryCurrent (A, 0.1A/raw /10, signed; charge +, discharge -) */
	int32_t sum_current;
	/* Same two signals in raw 0.1 V / 0.1 A units, used for the power estimate. */
	int32_t sum_voltage_dv;
	int32_t sum_current_da;
	/* 由can.c计算: max(电机/逆变器/IGBT温度, 四轮) degC, 无温度源时为0 */
	int32_t max_temperature;
	/* DBC BO_1286 Debug6: RL/RR/FL/FR_Motor_temperature (0.1,0) degC */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	int32_t motor_temp[4];
	/* DBC BO_773 DataLogger: APS_OpenPct (0.1,0) % */
	int32_t aps_open_pct;
	/* DBC BO_773 DataLogger: SteeringWheelAngle (0.1,0) deg */
	int32_t steering_angle;
	/* DBC BO_773 DataLogger: OilPressure_Kpa (0.001,0) Kpa */
	int32_t oil_pressure;
	/* DBC BO_1288 Debug8: RL/RR/FL/FR_IGBT_temperature (0.1,0) degC */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	int32_t igbt_temp[4];
	/* DBC BO_1287 Debug7: RL/RR/FL/FR_Inverter_temperature (0.1,0) degC */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	int32_t inverter_temp[4];
	/* DBC BO_1283 Debug3 / BO_1284 Debug4: Diagnostic_number_1~4 = AMK1~4 = RL/RR/FL/FR */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR (remapped like torque/rpm) */
	uint32_t diag_num[4];
	/* 非DBC: 信号强度等级 */
	int32_t signal_level;
	/* 非DBC: 报警激活标志 */
	uint8_t alert_active;
	/* 非DBC: 里程(0.1km) */
	int32_t odometer_tenths;
	/* 刹车开度 %，0..100。临时由 0x305 油压推算：2.1 kPa→0%，4.9 kPa→90%（见 can.c） */
	int32_t brake_pct;
	/* Vehicle_CanB.dbc BO_1792: SlipLevel 0..7 */
	int32_t slip_level;
	/* DBC BO_1440 PDM_LowVoltageBus 0x5A0: BusVoltage (0.001V, Motorola int16) */
	int32_t lv_bus_voltage_mV;
	/* DBC BO_1440 PDM_LowVoltageBus 0x5A0: BusCurrent (0.01A, Motorola int16) */
	int32_t lv_bus_current_cA;
	/* DBC BO_1440 PDM_LowVoltageBus 0x5A0: BusPower (0.1W, Motorola uint16) */
	int32_t lv_bus_power_dW;
	/* DBC BO_1441 PDM_LowVoltageBattery 0x5A1: BatteryVoltage (0.001V) */
	int32_t lv_batt_voltage_mV;
	/* DBC BO_1441 PDM_LowVoltageBattery 0x5A1: BatteryCurrent (0.01A) */
	int32_t lv_batt_current_cA;
	/* DBC BO_1441 PDM_LowVoltageBattery 0x5A1: BatteryPower (0.1W) */
	int32_t lv_batt_power_dW;
	/* DBC BO_1442 FanController_Status 0x5A2: Fan1..3_RPM (1rpm) */
	int32_t fan_rpm[3];
	/* DBC BO_1442 FanController_Status 0x5A2: Fan_PWM1/2_Duty (1%), 实测占空比;
	 * DBC 只定义两路实测占空比, Fan3 无独立信号 */
	int32_t fan_pwm_duty[2];
	/* DBC BO_1184 BMS_SOPLimits 0x4A0: DischargePowerLimit/ChargePowerLimit (0.1 kW) */
	int32_t sop_discharge_power_dkw;
	int32_t sop_charge_power_dkw;
	/* DBC BO_1187 BMS_SOPStatus 0x4A3: DischargeCurrentLimit/ChargeCurrentLimit (0.1 A) */
	int32_t sop_discharge_current_da;
	int32_t sop_charge_current_da;
	/* DASH_VALID_* bits; fields whose bit is clear are not meaningful. */
	uint32_t valid_mask;
} dashboard_data_t;

void Dashboard_UI_Init(void);
void Dashboard_UI_Process(void);
/* Compatibility hook: the dashboard is ready immediately after initialization. */
uint8_t Dashboard_UI_IsStartupComplete(void);
void Dashboard_UI_SubmitData(const dashboard_data_t * data);
/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR; temperature order: outside-to-inside. */
void Dashboard_UI_SubmitTireTemperatures(uint32_t wheel_index,
                                         const int32_t temperatures[4]);
/* Same wheel/segment order, with each value expressed in 0.01 degC. */
void Dashboard_UI_SubmitTireTemperaturesCenti(uint32_t wheel_index,
                                              const int32_t temperatures_centi[4]);
void Dashboard_UI_SubmitSpeed(int32_t speed);
void Dashboard_UI_SubmitOdometer(int32_t odometer_tenths);
void Dashboard_UI_SubmitDriveMode(int32_t mode_index);
/* Non-zero while a sprint/lap timing session is active: drive-mode changes
 * are rejected by the UI and must not be relayed to the ECU either. */
uint8_t Dashboard_UI_IsTimingModeLocked(void);
void Dashboard_UI_SubmitSlipLevel(int32_t slip_level);
void Dashboard_UI_RequestLapToggle(void);
void Dashboard_UI_RequestAlertClear(void);
void Dashboard_UI_SubmitLapDelta(int32_t delta_hundredths);
void Dashboard_UI_SubmitLapTimes(int32_t current_hundredths,
								 int32_t last_hundredths,
								 int32_t best_hundredths,
								 int32_t lap_count);
/* Remaining distance shown in S-mode, in whole metres (0..75). */
void Dashboard_UI_SubmitSprintRemaining(int32_t remaining_m);
/* Show or hide the green S-mode READY prompt in the alert area. */
void Dashboard_UI_SetSprintReady(uint8_t ready);
void Dashboard_UI_SubmitSignalLevel(int32_t level);
void Dashboard_UI_SubmitTouchState(uint16_t x, uint16_t y, uint8_t pressed);
void Dashboard_UI_PushAlert(const char * text);
const dashboard_data_t * Dashboard_UI_GetCurrentData(void);

#ifdef __cplusplus
}
#endif

#endif
