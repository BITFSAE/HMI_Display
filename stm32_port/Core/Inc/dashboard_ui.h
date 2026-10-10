#ifndef DASHBOARD_UI_H
#define DASHBOARD_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	/* Display speed in integer km/h. CAN GPS_Speed uses 0.1 km/h raw units. */
	int32_t speed;
	/* DBC BO_1200 BatterySOC (%) */
	int32_t soc;
	/* DBC BO_1289 Debug9: ModeFlag, bit4|4@1- [-8,7] */
	int32_t mode_index;
	/* DBC BO_1282 Debug2: FR/FL/RR/RL_ActualTorque (1,0) Nm (DBC unit text "0.1%Mn" is wrong) */
	/* Wheel order: 0=LF(DBC FL), 1=LR(DBC RL), 2=RF(DBC FR), 3=RR(DBC RR) */
	int32_t torque[4];
	/* DBC BO_1289 Debug9: FR/FL/RR/RL_AMK_bEnable, bit20~23 each 1bit */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	uint8_t motor_enable[4];
	/* DBC BO_1285 Debug5: FR/FL/RR/RL_ActualVelocity (1,0) */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	int32_t rpm[4];
	/* DBC BO_1200 BatteryVoltage (V, 0.1V/raw /10) */
	int32_t sum_voltage;
	/* DBC BO_1200 BatteryCurrent (A, 0.1A/raw /10, signed) */
	int32_t sum_current;
	/* 由can.c计算: max(电机/逆变器/IGBT温度, 四轮) degC, 无温度源时为0 */
	int32_t max_temperature;
	/* DBC BO_1286 Debug6: FR/FL/RR/RL_Motor_temperature (0.1,0) degC */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	int32_t motor_temp[4];
	/* DBC BO_773 DataLogger: APS_OpenPct (0.1,0) % */
	int32_t aps_open_pct;
	/* DBC BO_773 DataLogger: SteeringWheelAngle (0.1,0) deg */
	int32_t steering_angle;
	/* DBC BO_773 DataLogger: OilPressure_Kpa (0.001,0) Kpa */
	int32_t oil_pressure;
	/* DBC BO_1288 Debug8: FR/FL/RR/RL_IGBT_temperature (0.1,0) degC */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	int32_t igbt_temp[4];
	/* DBC BO_1287 Debug7: FR/FL/RR/RL_Inverter_temperature (0.1,0) degC */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	int32_t inverter_temp[4];
	/* DBC BO_1283 Debug3 / BO_1284 Debug4: Diagnostic_number_1~4 */
	/* Wheel order: 0=LF, 1=LR, 2=RF, 3=RR */
	uint32_t diag_num[4];
	/* DBC BO_97 IMU_Accel: IMU_AccelX/Y/Z (0.00048828125,0) g */
	int32_t imu_accel[3];
	/* DBC BO_98 IMU_Gyro: IMU_GyroX/Y/Z (0.0610352,0) deg/s */
	int32_t imu_gyro[3];
	/* DBC BO_99 IMU_Roll: IMU_Angle_Roll (0.005493,0) deg */
	int32_t imu_roll;
	/* DBC BO_100 IMU_Pitch: IMU_Angle_Pitch (0.005493,0) deg */
	int32_t imu_pitch;
	/* DBC BO_101 IMU_Yaw: IMU_Angle_Yaw (0.005493,0) deg */
	int32_t imu_yaw;
	/* DBC BO_102 IMU_Magnetic: IMU_MagX/Y/Z (1,0) */
	int32_t imu_mag[3];
	/* 非DBC: 信号强度等级 */
	int32_t signal_level;
	/* 非DBC: 报警激活标志 */
	uint8_t alert_active;
	/* 非DBC: 里程(0.1km) */
	int32_t odometer_tenths;
	/* DBC BO_774 BrakePedal_0x306 (网关转发CANC 0x270 IBS_Info): BrakePedalPct (%) */
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
