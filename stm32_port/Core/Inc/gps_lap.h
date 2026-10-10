/* USER CODE BEGIN gps_lap */
#ifndef __GPS_LAP_H
#define __GPS_LAP_H

#include "gps.h"

#define GPS_LAP_ENABLE  1
/* S-mode launch is detected when GNSS speed crosses this threshold. */
#define GPS_SPRINT_START_SPEED_KMH  0.5f

typedef enum {
	GPS_LAP_DIAG_INACTIVE = 0,
	GPS_LAP_DIAG_WAIT_ARM,
	GPS_LAP_DIAG_ARMED,
	GPS_LAP_DIAG_APPROACHING,
	GPS_LAP_DIAG_GATE_MISS,
	GPS_LAP_DIAG_REVERSE_PASS,
	GPS_LAP_DIAG_CROSSED
} GPS_LapDiagState_t;

typedef struct {
	GPS_LapDiagState_t state;
	float forward_m;
	float lateral_m;
	float gate_half_width_m;
	uint16_t sample_count;
	uint8_t fix_quality;
} GPS_LapDiagnostic_t;

#if GPS_LAP_ENABLE

void GPS_Lap_InitLock(void);
void GPS_Lap_SetStartLine(float lat, float lon, float track);
void GPS_Lap_SetFinishLine(float lat, float lon, float track);
uint8_t GPS_Lap_StartAtCurrent(const GPS_Data_t * data);
void GPS_Lap_Reset(void);
void GPS_Lap_SetAnalysisActive(uint8_t active);
uint8_t GPS_Lap_IsAnalysisActive(void);
void GPS_Lap_Tick(void);
/* S-mode straight-line acceleration timer. Arming enters READY; timing starts
 * at the interpolated speed-threshold crossing and ends at 75.0 m. */
void GPS_Sprint_Reset(void);
uint8_t GPS_Sprint_StartAtCurrent(const GPS_Data_t * data, float heading_deg);
void GPS_Sprint_Cancel(void);
uint8_t GPS_Sprint_IsActive(void);
void GPS_Sprint_Tick(void);
void GPS_SprintProcess(const GPS_Data_t * data);
void GPS_Lap_GetDiagnostic(GPS_LapDiagnostic_t * out);
const char * GPS_Lap_GetDeltaStr(char * buf, uint32_t size);
int32_t GPS_Lap_GetDeltaHundredths(void);
int32_t GPS_Lap_GetCurrentLapHundredths(void);
int32_t GPS_Lap_GetLastLapHundredths(void);
int32_t GPS_Lap_GetBestLapHundredths(void);
int32_t GPS_Lap_GetCurrentLapNum(void);

#else

#define GPS_Lap_SetStartLine(lat,lon,t)  ((void)0)
#define GPS_Lap_SetFinishLine(lat,lon,t) ((void)0)
#define GPS_Lap_StartAtCurrent(data)     0
#define GPS_Lap_Reset()                  ((void)0)
#define GPS_Lap_SetAnalysisActive(a)     ((void)(a))
#define GPS_Lap_IsAnalysisActive()       0
#define GPS_Lap_Tick()                   ((void)0)
#define GPS_Sprint_Reset()                ((void)0)
#define GPS_Sprint_StartAtCurrent(d,h)    0
#define GPS_Sprint_Cancel()               ((void)0)
#define GPS_Sprint_IsActive()             0
#define GPS_Sprint_Tick()                 ((void)0)
#define GPS_SprintProcess(d)              ((void)(d))
#define GPS_Lap_GetDiagnostic(out)        ((void)(out))
#define GPS_Lap_GetDeltaStr(b,s)         ("0.00s")
#define GPS_Lap_GetDeltaHundredths()     0
#define GPS_Lap_GetCurrentLapHundredths() 0
#define GPS_Lap_GetLastLapHundredths()    0
#define GPS_Lap_GetBestLapHundredths()    0
#define GPS_Lap_GetCurrentLapNum()        0

#endif

void GPS_LapProcess(const GPS_Data_t * data);

#endif

/* USER CODE END gps_lap */
