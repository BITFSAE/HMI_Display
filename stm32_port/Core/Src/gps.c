/* USER CODE BEGIN gps */
#include "main.h"
#include "gps.h"
#include "gps_lap.h"
#include "can.h"
#include "dashboard_ui.h"
#include "odometer_flash.h"
#include "FreeRTOS.h"
#include "task.h"
#include "firmware_diag.h"
#include "cmsis_os2.h"
#include <math.h>
#include <string.h>

extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart3_rx;

uint8_t g_gps_dma_buf[GPS_DMA_BUF_SIZE];

static TaskHandle_t g_gps_task_handle = NULL;
static StaticTask_t g_gps_task_control;
static StackType_t g_gps_task_stack[3072U];
volatile uint32_t g_gps_poll_gap_max_ms;
volatile uint32_t g_gps_dma_late_poll_count;
static uint32_t g_gps_poll_tick;
static GPS_Data_t  g_gps_data;
static uint32_t    g_gps_dma_last_pos;
static uint32_t    g_gps_sentence_count;
static uint32_t    g_gps_rmc_count;
static int32_t     g_gps_speed_kmh_int;
static int32_t     g_gps_speed_kmh_centi;
static int32_t     g_gps_track_tenths;
static char        g_gps_line_buf[160];
static uint16_t    g_gps_line_len;
static uint8_t     g_gps_line_active;

static int32_t     g_gsv_snr_sum;
static int32_t     g_gsv_snr_count;
static int32_t     g_gsv_max_snr;
static int32_t     g_gsv_window_snr_sum;
static int32_t     g_gsv_window_snr_count;
static int32_t     g_gsv_window_max_snr;
static uint32_t    g_gsv_last_tick;
static uint32_t    g_gsv_signal_ui_last_tick;
static uint8_t     g_gga_signal_level;
static uint32_t    g_hpr_last_tick;
static uint32_t    g_gps_rmc_last_tick;
static uint32_t    g_gps_can_last_tick;
static uint8_t     g_gps_can_slow_phase;
static uint8_t     g_gps_can_lap_count;
static uint8_t     g_gps_rmc_valid;
static uint32_t    g_gps_speed_ui_alive_tick;

static float       g_odometer_prev_latitude;
static float       g_odometer_prev_longitude;
static float       g_odometer_fraction_m;
static uint32_t    g_odometer_prev_tick;
static uint32_t    g_odometer_total_m;
static uint32_t    g_odometer_checkpoint_m;
static int32_t     g_odometer_ui_tenths;
static uint8_t     g_odometer_anchor_valid;
static uint8_t     g_odometer_initialized;

#define GPS_GSV_EXPIRE_MS             12000U
#define GPS_HPR_EXPIRE_MS               1500U
#define GPS_CAN_SPEED_PERIOD_MS            50U
#define GPS_CAN_SPEED_STALE_MS             500U
/* 0x301/0x067/0x068 go out every GPS_CAN_SPEED_PERIOD_MS tick. Odometer, GPS
 * quality (0x069) and lap timing (0x06A) change slowly and the ground station
 * records them, so they use a multiple of the tick. A change of the lap count
 * sends both at once so a finished lap is reported without delay. */
#define GPS_CAN_STATUS_TICKS               10U   /* 0x069: 500 ms */
#define GPS_CAN_LAP_TICKS                   4U   /* 0x06A: 200 ms */
#define GPS_CAN_SLOW_CYCLE_TICKS           20U   /* common multiple of both */
/* How long the dashboard keeps showing the last valid speed after RMC stops
 * arriving (or arrives with V status). Bridges brief receiver dropouts without
 * freezing a stale value for long. CAN telemetry still reports zero/invalid
 * after the 500 ms stale window. */
#define GPS_SPEED_UI_HOLD_MS              3000U
#define GPS_CAN_STATUS_POSITION_VALID      (1U << 0)
#define GPS_CAN_STATUS_HEADING_VALID       (1U << 1)
#define GPS_CAN_STATUS_LAP_ACTIVE          (1U << 2)
#define GPS_CAN_STATUS_RMC_VALID           (1U << 3)
#define GPS_ODOMETER_MIN_SPEED_CENTI      100
#define GPS_ODOMETER_MAX_GAP_MS          2000U
#define GPS_ODOMETER_MAX_STEP_M          50.0f
#define GPS_ODOMETER_SAVE_INTERVAL_M     100U
#define GPS_EARTH_RADIUS_M          6371000.0f
#define GPS_DEG_TO_RAD             0.01745329251994329577f

static void gps_odometer_init(void)
{
	uint32_t initial_m;
	if(g_odometer_initialized != 0U) return;
	if(Odometer_Flash_GetInitial(&initial_m) == 0U) return;
	g_odometer_total_m = initial_m;
	g_odometer_checkpoint_m = g_odometer_total_m;
	g_odometer_fraction_m = 0.0f;
	g_odometer_anchor_valid = 0U;
	g_odometer_ui_tenths = (int32_t)(g_odometer_total_m / 100U);
	g_odometer_initialized = 1U;
	Dashboard_UI_SubmitOdometer(g_odometer_ui_tenths);
}

static void gps_odometer_process(float latitude, float longitude,
	                              int32_t speed_kmh_centi, uint32_t now)
{
	uint32_t elapsed_ms;
	float north_m;
	float east_m;
	float distance_m;
	float expected_m;
	float max_plausible_m;
	uint32_t whole_m;
	int32_t ui_tenths;

	if(g_odometer_initialized == 0U) return;
	if((latitude < -90.0f) || (latitude > 90.0f) ||
	   (longitude < -180.0f) || (longitude > 180.0f) ||
	   ((latitude == 0.0f) && (longitude == 0.0f))) {
		g_odometer_anchor_valid = 0U;
		return;
	}
	if(g_odometer_anchor_valid == 0U) {
		g_odometer_prev_latitude = latitude;
		g_odometer_prev_longitude = longitude;
		g_odometer_prev_tick = now;
		g_odometer_anchor_valid = 1U;
		return;
	}

	elapsed_ms = now - g_odometer_prev_tick;
	north_m = (latitude - g_odometer_prev_latitude) * GPS_DEG_TO_RAD *
	          GPS_EARTH_RADIUS_M;
	east_m = (longitude - g_odometer_prev_longitude) * GPS_DEG_TO_RAD *
	         GPS_EARTH_RADIUS_M *
	         cosf(((latitude + g_odometer_prev_latitude) * 0.5f) * GPS_DEG_TO_RAD);
	distance_m = sqrtf((north_m * north_m) + (east_m * east_m));
	g_odometer_prev_latitude = latitude;
	g_odometer_prev_longitude = longitude;
	g_odometer_prev_tick = now;

	if((elapsed_ms == 0U) || (elapsed_ms > GPS_ODOMETER_MAX_GAP_MS) ||
	   (speed_kmh_centi < GPS_ODOMETER_MIN_SPEED_CENTI)) {
		return;
	}
	expected_m = ((float)speed_kmh_centi / 360000.0f) * (float)elapsed_ms;
	max_plausible_m = (expected_m * 3.0f) + 3.0f;
	if((distance_m < 0.05f) || (distance_m > GPS_ODOMETER_MAX_STEP_M) ||
	   (distance_m > max_plausible_m)) {
		return;
	}

	g_odometer_fraction_m += distance_m;
	whole_m = (uint32_t)g_odometer_fraction_m;
	if(whole_m == 0U) return;
	g_odometer_fraction_m -= (float)whole_m;
	if((UINT32_MAX - g_odometer_total_m) < whole_m) {
		g_odometer_total_m = UINT32_MAX;
	}
	else {
		g_odometer_total_m += whole_m;
	}
	if((g_odometer_total_m - g_odometer_checkpoint_m) >=
	   GPS_ODOMETER_SAVE_INTERVAL_M) {
		g_odometer_checkpoint_m = g_odometer_total_m;
		(void)Odometer_Flash_Save(g_odometer_total_m);
	}

	ui_tenths = (int32_t)(g_odometer_total_m / 100U);
	if(ui_tenths != g_odometer_ui_tenths) {
		g_odometer_ui_tenths = ui_tenths;
		Dashboard_UI_SubmitOdometer(ui_tenths);
	}
}

static int32_t gps_scale_float_s32(float value, float scale,
                                   int32_t minimum, int32_t maximum)
{
	float scaled;

	if(value != value) return 0;
	scaled = value * scale;
	if(scaled <= (float)minimum) return minimum;
	if(scaled >= (float)maximum) return maximum;
	return (int32_t)(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
}

static uint16_t gps_angle_to_cdeg(float angle)
{
	int32_t scaled;

	if(!isfinite(angle) || angle < 0.0f || angle > 360.0f) return 0U;
	if(angle == 360.0f) angle = 0.0f;
	scaled = (int32_t)((angle * 100.0f) + 0.5f);
	if(scaled >= 36000) scaled = 0;
	return (uint16_t)scaled;
}

static uint16_t gps_clamp_u16(int32_t value)
{
	if(value <= 0) return 0U;
	if(value >= 65535) return 65535U;
	return (uint16_t)value;
}

static int16_t gps_clamp_i16(int32_t value)
{
	if(value <= -32768) return -32768;
	if(value >= 32767) return 32767;
	return (int16_t)value;
}

static uint8_t gps_clamp_u8(int32_t value)
{
	if(value <= 0) return 0U;
	if(value >= 255) return 255U;
	return (uint8_t)value;
}

static void gps_can_speed_tick(uint32_t now)
{
	int32_t speed_kmh_centi;
	uint8_t rmc_valid;
	uint32_t frames;
	GPS_Data_t gps_snapshot;
	GPS_LapDiagnostic_t lap_diagnostic;
	CAN_GPSTelemetry_t telemetry;

	if((g_gps_rmc_last_tick == 0U) ||
	   ((now - g_gps_rmc_last_tick) > GPS_CAN_SPEED_STALE_MS)) {
		taskENTER_CRITICAL();
		if((g_gps_speed_kmh_centi != 0) || (g_gps_speed_kmh_int != 0)) {
			g_gps_speed_kmh_centi = 0;
			g_gps_speed_kmh_int = 0;
		}
		g_gps_rmc_valid = 0U;
		taskEXIT_CRITICAL();
	}

	/* UI hold: keep the last displayed speed through short RMC outages and
	 * clear it only once no valid sentence has arrived for the hold window.
	 * g_gps_speed_ui_alive_tick == 0 keeps the startup placeholder intact. */
	if((g_gps_speed_ui_alive_tick != 0U) &&
	   ((now - g_gps_speed_ui_alive_tick) > GPS_SPEED_UI_HOLD_MS)) {
		g_gps_speed_ui_alive_tick = 0U;
		Dashboard_UI_SubmitSpeed(0);
	}

	if((g_gps_can_last_tick != 0U) &&
	   ((now - g_gps_can_last_tick) < GPS_CAN_SPEED_PERIOD_MS)) {
		return;
	}
	g_gps_can_last_tick = now;
	taskENTER_CRITICAL();
	speed_kmh_centi = g_gps_speed_kmh_centi;
	rmc_valid = g_gps_rmc_valid;
	taskEXIT_CRITICAL();
	GPS_GetData(&gps_snapshot);
	(void)memset(&lap_diagnostic, 0, sizeof(lap_diagnostic));
	GPS_Lap_GetDiagnostic(&lap_diagnostic);

	telemetry.latitude_e7 = gps_scale_float_s32(gps_snapshot.latitude, 10000000.0f,
	                                           -900000000, 900000000);
	telemetry.longitude_e7 = gps_scale_float_s32(gps_snapshot.longitude, 10000000.0f,
	                                            -1800000000, 1800000000);
	telemetry.ground_track_cdeg = gps_angle_to_cdeg(gps_snapshot.track_angle);
	telemetry.heading_cdeg = gps_angle_to_cdeg(gps_snapshot.heading_angle);
	telemetry.altitude_dm = gps_clamp_i16(
		gps_scale_float_s32(gps_snapshot.altitude, 10.0f, -32768, 32767));
	telemetry.status_flags = 0U;
	if(gps_snapshot.valid != 0U) {
		telemetry.status_flags |= GPS_CAN_STATUS_POSITION_VALID;
	}
	if(gps_snapshot.heading_valid != 0U) {
		telemetry.status_flags |= GPS_CAN_STATUS_HEADING_VALID;
	}
	if((GPS_Lap_IsAnalysisActive() != 0U) || (GPS_Sprint_IsActive() != 0U)) {
		telemetry.status_flags |= GPS_CAN_STATUS_LAP_ACTIVE;
	}
	if(rmc_valid != 0U) {
		telemetry.status_flags |= GPS_CAN_STATUS_RMC_VALID;
	}
	telemetry.lap_diag_state = (uint8_t)lap_diagnostic.state;
	telemetry.heading_quality = gps_snapshot.heading_quality;
	telemetry.odometer_tenths_km = g_odometer_total_m / 100U;
	telemetry.fix_quality = gps_snapshot.fix_quality;
	telemetry.satellites = gps_snapshot.satellites;
	telemetry.signal_level = gps_snapshot.signal_level;
	telemetry.max_snr = gps_snapshot.max_snr;
	telemetry.avg_snr = gps_snapshot.avg_snr;
	telemetry.gsv_tracked_sats = gps_snapshot.gsv_tracked_sats;
	telemetry.lap_count = gps_clamp_u8(GPS_Lap_GetCurrentLapNum());
	telemetry.lap_current_cs = gps_clamp_u16(GPS_Lap_GetCurrentLapHundredths());
	telemetry.lap_last_cs = gps_clamp_u16(GPS_Lap_GetLastLapHundredths());
	telemetry.lap_best_cs = gps_clamp_u16(GPS_Lap_GetBestLapHundredths());
	telemetry.lap_delta_cs = gps_clamp_i16(GPS_Lap_GetDeltaHundredths());

	frames = CAN_GPS_TX_POSITION | CAN_GPS_TX_MOTION;
	if((g_gps_can_slow_phase % GPS_CAN_STATUS_TICKS) == 0U) frames |= CAN_GPS_TX_STATUS;
	if((g_gps_can_slow_phase % GPS_CAN_LAP_TICKS) == 0U) frames |= CAN_GPS_TX_LAP;
	if(telemetry.lap_count != g_gps_can_lap_count) {
		g_gps_can_lap_count = telemetry.lap_count;
		frames |= CAN_GPS_TX_STATUS | CAN_GPS_TX_LAP;
	}
	g_gps_can_slow_phase = (uint8_t)((g_gps_can_slow_phase + 1U) % GPS_CAN_SLOW_CYCLE_TICKS);

	CAN_SendGPSSpeed((speed_kmh_centi + 5) / 10);
	CAN_SendGPSTelemetry(&telemetry, frames);
}

static uint8_t nmea_checksum(const char * sentence)
{
	uint8_t c = 0;
	const char * p = sentence;
	if(*p == '$') p++;
	while(*p != '*' && *p != '\0' && *p != '\r' && *p != '\n') {
		c ^= (uint8_t)*p;
		p++;
	}
	if(*p != '*') return 0;
	p++;
	uint8_t hi;
	uint8_t lo;
	char ch = *p++;
	if(ch >= '0' && ch <= '9') hi = (uint8_t)(ch - '0');
	else if(ch >= 'A' && ch <= 'F') hi = (uint8_t)(ch - 'A' + 10);
	else if(ch >= 'a' && ch <= 'f') hi = (uint8_t)(ch - 'a' + 10);
	else return 0;
	ch = *p;
	if(ch >= '0' && ch <= '9') lo = (uint8_t)(ch - '0');
	else if(ch >= 'A' && ch <= 'F') lo = (uint8_t)(ch - 'A' + 10);
	else if(ch >= 'a' && ch <= 'f') lo = (uint8_t)(ch - 'a' + 10);
	else return 0;
	return ((c == (uint8_t)((hi << 4) | lo)) ? 1U : 0U);
}

static const char * nmea_get_field(const char * buf, int32_t idx,
                                    char * out, uint32_t out_size)
{
	int32_t cur = 0;
	const char * p = buf;
	const char * start = NULL;
	uint32_t len = 0;

	while(*p != '\0' && *p != '*' && *p != '\r' && *p != '\n') {
		if(cur == idx) {
			start = p;
			while(*p != ',' && *p != '*' && *p != '\0' &&
			      *p != '\r' && *p != '\n') {
				len++;
				p++;
			}
			break;
		}
		if(*p == ',') cur++;
		p++;
	}

	if(start == NULL) {
		if(out != NULL && out_size > 0) out[0] = '\0';
		return NULL;
	}
	if(len == 0) {
		if(out != NULL && out_size > 0) out[0] = '\0';
		return start;
	}
	if(out != NULL && out_size > 0U) {
		if(len >= out_size) len = out_size - 1;
		for(uint32_t i = 0; i < len; i++) out[i] = start[i];
		out[len] = '\0';
	}
	return start;
}

/* Reject oversized numeric fields before signed accumulation can overflow. */
static uint8_t nmea_append_digit(int32_t *value, char digit, int32_t limit)
{
    int32_t next = (int32_t)(digit - '0');
    if(*value > (limit - next) / 10) return 0U;
    *value = (*value * 10) + next;
    return 1U;
}

static int32_t nmea_field_atoi(const char * buf, int32_t idx)
{
	char fld[16];
	const char * p = nmea_get_field(buf, idx, fld, sizeof(fld));
	if(p == NULL || fld[0] == '\0') return 0;
	int32_t val = 0;
	int32_t sign = 1;
	const char * s = fld;
	if(*s == '-') { sign = -1; s++; }
	while(*s >= '0' && *s <= '9') {
		if(nmea_append_digit(&val, *s, INT32_MAX) == 0U) return 0;
		s++;
	}
	return val * sign;
}

static int32_t nmea_field_atoi_tenths(const char * buf, int32_t idx, int32_t * out_frac)
{
	char fld[32];
	const char * p = nmea_get_field(buf, idx, fld, sizeof(fld));
	if(out_frac == NULL) return 0;
	if(p == NULL || fld[0] == '\0') { *out_frac = 0; return 0; }

	int32_t int_part = 0;
	int32_t frac_part = 0;
	int32_t sign = 1;
	const char * s = fld;

	if(*s == '-') { sign = -1; s++; }
	else if(*s == '+') { s++; }

	while(*s >= '0' && *s <= '9') {
		if(nmea_append_digit(&int_part, *s, INT32_MAX / 10) == 0U) {
			*out_frac = 0;
			return 0;
		}
		s++;
	}
	if(*s == '.') {
		s++;
		if(*s >= '0' && *s <= '9') {
			frac_part = (int32_t)(*s - '0');
			s++;
		}
	}
	*out_frac = sign * frac_part;
	return sign * int_part;
}

static int32_t nmea_field_atoi_milli(const char * buf, int32_t idx)
{
	char fld[32];
	const char * p = nmea_get_field(buf, idx, fld, sizeof(fld));
	int32_t int_part = 0;
	int32_t frac_part = 0;
	int32_t frac_digits = 0;
	int32_t sign = 1;
	const char * s = fld;

	if((p == NULL) || (fld[0] == '\0')) return 0;

	if(*s == '-') { sign = -1; s++; }
	else if(*s == '+') { s++; }

	while(*s >= '0' && *s <= '9') {
		if(nmea_append_digit(&int_part, *s, INT32_MAX / 1000) == 0U) return 0;
		s++;
	}
	if(*s == '.') {
		s++;
		while(*s >= '0' && *s <= '9' && frac_digits < 3) {
			frac_part = frac_part * 10 + (int32_t)(*s - '0');
			frac_digits++;
			s++;
		}
	}
	while(frac_digits < 3) {
		frac_part *= 10;
		frac_digits++;
	}

	if(frac_part > INT32_MAX - int_part * 1000) return 0;
	return sign * ((int_part * 1000) + frac_part);
}

static int32_t nmea_parse_speed_kmh_centi(const char * buf, int32_t idx)
{
	char fld[32];
	const char * p = nmea_get_field(buf, idx, fld, sizeof(fld));
	int32_t int_part = 0;
	int32_t frac_part = 0;
	int32_t frac_digits = 0;
	int32_t sign = 1;
	const char * s = fld;

	if((p == NULL) || (fld[0] == '\0')) return 0;

	if(*s == '-') { sign = -1; s++; }
	else if(*s == '+') { s++; }

	while(*s >= '0' && *s <= '9') {
		if(nmea_append_digit(&int_part, *s, INT32_MAX / 1000) == 0U) return 0;
		s++;
	}
	if(*s == '.') {
		s++;
		while(*s >= '0' && *s <= '9' && frac_digits < 3) {
			frac_part = frac_part * 10 + (int32_t)(*s - '0');
			frac_digits++;
			s++;
		}
	}
	while(frac_digits < 3) {
		frac_part *= 10;
		frac_digits++;
	}

	/* RMC speed is knots. Convert 0.001 kn to 0.01 km/h with rounding. */
	if(frac_part > INT32_MAX - int_part * 1000) return 0;
	int32_t knot_milli = sign * ((int_part * 1000) + frac_part);
	return (int32_t)(((int64_t)knot_milli * 1852 + 5000) / 10000);
}

static uint8_t gps_signal_level_from_sats(int32_t sats)
{
	if(sats >= 16) return GPS_SIG_LEVEL_EXCELLENT;
	if(sats >= 12) return GPS_SIG_LEVEL_GOOD;
	if(sats >= 8) return GPS_SIG_LEVEL_FAIR;
	if(sats >= 4) return GPS_SIG_LEVEL_WEAK;
	return GPS_SIG_LEVEL_NONE;
}

static int32_t nmea_parse_track_int(const char * buf, int32_t idx)
{
	int32_t frac;
	int32_t int_part = nmea_field_atoi_tenths(buf, idx, &frac);
	if(int_part < 0 || int_part > 360) return 0;
	if(frac < 0) frac = -frac;
	return int_part * 10 + (frac % 10);
}

static float nmea_parse_float(const char * buf, int32_t idx)
{
	char fld[24];
	const char * p = nmea_get_field(buf, idx, fld, sizeof(fld));
	const char * s = fld;
	float value = 0.0f;
	float frac_scale = 0.1f;
	float sign = 1.0f;

	if((p == NULL) || (fld[0] == '\0')) return 0.0f;
	if(*s == '-') { sign = -1.0f; s++; }
	else if(*s == '+') { s++; }
	while(*s >= '0' && *s <= '9') {
		value = (value * 10.0f) + (float)(*s - '0');
		s++;
	}
	if(*s == '.') {
		s++;
		while(*s >= '0' && *s <= '9') {
			value += (float)(*s - '0') * frac_scale;
			frac_scale *= 0.1f;
			s++;
		}
	}
	return value * sign;
}

static float nmea_parse_coord_deg(const char * buf, int32_t idx, int32_t hemi_idx)
{
	char fld[24];
	char hemi[4];
	const char * p = nmea_get_field(buf, idx, fld, sizeof(fld));
	const char * s = fld;
	int32_t raw_int = 0;
	int32_t degrees;
	float minutes;
	float fraction = 0.0f;
	float scale = 0.1f;
	float coord;

	if((p == NULL) || (fld[0] == '\0')) return 0.0f;
	while(*s >= '0' && *s <= '9') {
		if(nmea_append_digit(&raw_int, *s, 18059) == 0U) return 0.0f;
		s++;
	}
	if(*s == '.') {
		s++;
		while(*s >= '0' && *s <= '9') {
			fraction += (float)(*s - '0') * scale;
			scale *= 0.1f;
			s++;
		}
	}
	nmea_get_field(buf, hemi_idx, hemi, sizeof(hemi));
	degrees = raw_int / 100;
	minutes = (float)(raw_int % 100) + fraction;
	coord = (float)degrees + (minutes / 60.0f);
	if((hemi[0] == 'S') || (hemi[0] == 'W')) coord = -coord;
	return coord;
}

static void assign_signal_level_snr(int8_t avg_snr, uint8_t * out_level)
{
	if(avg_snr >= GPS_SNR_EXCELLENT) {
		*out_level = GPS_SIG_LEVEL_EXCELLENT;
	} else if(avg_snr >= GPS_SNR_GOOD) {
		*out_level = GPS_SIG_LEVEL_GOOD;
	} else if(avg_snr >= GPS_SNR_FAIR) {
		*out_level = GPS_SIG_LEVEL_FAIR;
	} else if(avg_snr >= GPS_SNR_WEAK) {
		*out_level = GPS_SIG_LEVEL_WEAK;
	} else {
		*out_level = GPS_SIG_LEVEL_NONE;
	}
}

static int is_sentence_type(const char * sentence, const char * type)
{
	const char * p = sentence;
	while(*p != '\0') {
		if(p[0] == type[0] && p[1] == type[1] && p[2] == type[2]) return 1;
		p++;
	}
	return 0;
}

static void parse_GNGSV(const char * sentence)
{
	int32_t total_msgs = nmea_field_atoi(sentence, 1);
	int32_t msg_num   = nmea_field_atoi(sentence, 2);
	if(total_msgs <= 0 || msg_num <= 0) {
		g_gsv_snr_sum = 0;
		g_gsv_snr_count = 0;
		g_gsv_max_snr = 0;
		return;
	}
	if(msg_num == 1) {
		g_gsv_snr_sum = 0;
		g_gsv_snr_count = 0;
		g_gsv_max_snr = 0;
	}

	int32_t n_fields = 0;
	{
		const char * p = sentence;
		while(*p != '\0' && *p != '*' && *p != '\r' && *p != '\n') {
			if(*p == ',') n_fields++;
			p++;
		}
	}

	for(int32_t i = 4; i + 3 <= n_fields; i += 4) {
		int32_t snr = nmea_field_atoi(sentence, i + 3);
		if(snr > 0 && snr <= 99) {
			g_gsv_snr_sum += snr;
			g_gsv_snr_count++;
			if(snr > g_gsv_max_snr) g_gsv_max_snr = snr;
			g_gsv_window_snr_sum += snr;
			g_gsv_window_snr_count++;
			if(snr > g_gsv_window_max_snr) g_gsv_window_max_snr = snr;
		}
	}

	if((msg_num >= total_msgs) && (g_gsv_snr_count > 0)) {
		uint32_t now = HAL_GetTick();
		g_gsv_snr_sum = 0;
		g_gsv_snr_count = 0;
		g_gsv_max_snr = 0;

		if(((g_gsv_signal_ui_last_tick == 0U) || ((now - g_gsv_signal_ui_last_tick) >= 1000U)) &&
		   (g_gsv_window_snr_count > 0)) {
			uint8_t sig_level;
			int8_t avg = (int8_t)(g_gsv_window_snr_sum / g_gsv_window_snr_count);
			assign_signal_level_snr(avg, &sig_level);

			taskENTER_CRITICAL();
			g_gps_data.avg_snr = avg;
			g_gps_data.max_snr = (int8_t)g_gsv_window_max_snr;
			g_gps_data.gsv_tracked_sats = (uint8_t)g_gsv_window_snr_count;
			g_gps_data.signal_level = sig_level;
			taskEXIT_CRITICAL();

			g_gsv_last_tick = now;
			g_gsv_signal_ui_last_tick = now;
			g_gsv_window_snr_sum = 0;
			g_gsv_window_snr_count = 0;
			g_gsv_window_max_snr = 0;

			Dashboard_UI_SubmitSignalLevel((int32_t)sig_level);
		}
	}
}

static void parse_GNRMC(const char * sentence)
{
	char stat[4];
	nmea_get_field(sentence, 2, stat, sizeof(stat));
	if(stat[0] != 'A') {
		g_odometer_anchor_valid = 0U;
		g_gps_rmc_last_tick = HAL_GetTick();
		taskENTER_CRITICAL();
		/* CAN telemetry reports zero immediately on an invalid fix; the
		 * display keeps its last speed until the UI hold window expires. */
		g_gps_speed_kmh_int = 0;
		g_gps_speed_kmh_centi = 0;
		g_gps_rmc_valid = 0U;
		taskEXIT_CRITICAL();
		return;
	}

	int32_t speed_kmh_centi = nmea_parse_speed_kmh_centi(sentence, 7);
	if(speed_kmh_centi < 0) speed_kmh_centi = 0;
	if(speed_kmh_centi > 30000) speed_kmh_centi = 30000;

	int32_t speed_kmh = (speed_kmh_centi + 50) / 100;
	if(speed_kmh < 0) speed_kmh = 0;
	if(speed_kmh > 300) speed_kmh = 300;
	int32_t track_tenths = nmea_parse_track_int(sentence, 8);
	float latitude = nmea_parse_coord_deg(sentence, 3, 4);
	float longitude = nmea_parse_coord_deg(sentence, 5, 6);
	uint32_t now = HAL_GetTick();
	g_gps_rmc_last_tick = now;

	taskENTER_CRITICAL();
	/* Keep the RMC fast path lightweight; real GPS can deliver this at 20Hz. */
	g_gps_speed_kmh_centi = speed_kmh_centi;
	g_gps_track_tenths = track_tenths;
	g_gps_speed_kmh_int = speed_kmh;
	g_gps_data.latitude = latitude;
	g_gps_data.longitude = longitude;
	g_gps_rmc_valid = 1U;
	g_gps_rmc_count++;
	taskEXIT_CRITICAL();

	Dashboard_UI_SubmitSpeed(speed_kmh);
	g_gps_speed_ui_alive_tick = now;
	{
		GPS_Data_t lap_data;
		GPS_GetData(&lap_data);
		GPS_LapProcess(&lap_data);
		GPS_SprintProcess(&lap_data);
	}
	gps_odometer_process(latitude, longitude, speed_kmh_centi, now);
}

static void parse_GNGGA(const char * sentence)
{
	int32_t fix_qual = nmea_field_atoi(sentence, 6);
	int32_t sats     = nmea_field_atoi(sentence, 7);
	int32_t altitude_milli = nmea_field_atoi_milli(sentence, 9);
	uint32_t now = HAL_GetTick();
	uint8_t fallback_signal = gps_signal_level_from_sats(sats);
	uint8_t submit_fallback = 0U;
	uint8_t submit_level = GPS_SIG_LEVEL_NONE;

	taskENTER_CRITICAL();
	g_gps_data.fix_quality = (uint8_t)fix_qual;
	g_gps_data.satellites  = (uint8_t)sats;
	g_gps_data.altitude = (float)altitude_milli / 1000.0f;
	g_gps_data.valid     = (fix_qual > 0) ? 1U : 0U;
	if(fix_qual > 0) {
		if((fallback_signal > g_gps_data.signal_level) ||
		   ((g_gsv_last_tick == 0U) || ((now - g_gsv_last_tick) > GPS_GSV_EXPIRE_MS))) {
			g_gps_data.signal_level = fallback_signal;
			submit_fallback = (fallback_signal != g_gga_signal_level) ? 1U : 0U;
			g_gga_signal_level = fallback_signal;
			submit_level = fallback_signal;
		}
	}
	else {
		g_gps_data.signal_level = GPS_SIG_LEVEL_NONE;
		submit_fallback = (g_gga_signal_level != GPS_SIG_LEVEL_NONE) ? 1U : 0U;
		g_gga_signal_level = GPS_SIG_LEVEL_NONE;
		submit_level = GPS_SIG_LEVEL_NONE;
	}
	taskEXIT_CRITICAL();

	if(submit_fallback != 0U) {
		Dashboard_UI_SubmitSignalLevel((int32_t)submit_level);
	}
}

static void parse_GNHPR(const char * sentence)
{
	float heading = nmea_parse_float(sentence, 2);
	int32_t quality = nmea_field_atoi(sentence, 5);
	uint8_t valid = ((quality == 4) || (quality == 5)) ? 1U : 0U;

	if(!isfinite(heading) || heading < 0.0f || heading > 360.0f) {
		heading = 0.0f;
		valid = 0U;
	}
	if(heading == 360.0f) heading = 0.0f;

	taskENTER_CRITICAL();
	g_gps_data.heading_angle = heading;
	g_gps_data.heading_quality = (uint8_t)quality;
	g_gps_data.heading_valid = valid;
	g_hpr_last_tick = HAL_GetTick();
	taskEXIT_CRITICAL();
}

static void invalidate_expired_gsv(void)
{
	if(g_gsv_last_tick == 0) return;
	uint32_t elapsed = HAL_GetTick() - g_gsv_last_tick;
	if(elapsed > GPS_GSV_EXPIRE_MS) {
		if(g_gps_data.valid != 0U) {
			g_gsv_last_tick = 0;
			return;
		}
		taskENTER_CRITICAL();
		g_gps_data.avg_snr = 0;
		g_gps_data.max_snr = 0;
		g_gps_data.gsv_tracked_sats = 0;
		g_gps_data.signal_level = GPS_SIG_LEVEL_NONE;
		taskEXIT_CRITICAL();
		g_gsv_last_tick = 0;
		Dashboard_UI_SubmitSignalLevel(GPS_SIG_LEVEL_NONE);
	}
}

static void process_sentence(const char * buf)
{
	if(buf[0] != '$') return;
	if(!nmea_checksum(buf)) return;

	g_gps_sentence_count++;

	if(is_sentence_type(buf, "RMC")) {
		parse_GNRMC(buf);
	} else if(is_sentence_type(buf, "GGA")) {
		parse_GNGGA(buf);
	} else if(is_sentence_type(buf, "HPR")) {
		parse_GNHPR(buf);
	} else if(is_sentence_type(buf, "GSV")) {
		parse_GNGSV(buf);
	}
}

static void GPS_ProcessByte(uint8_t byte)
{
	char ch = (char)byte;

	if(ch == '$') {
		g_gps_line_active = 1U;
		g_gps_line_len = 0U;
	}

	if(g_gps_line_active == 0U) return;

	if(g_gps_line_len >= (uint16_t)(sizeof(g_gps_line_buf) - 1U)) {
		g_gps_line_active = 0U;
		g_gps_line_len = 0U;
		return;
	}

	g_gps_line_buf[g_gps_line_len++] = ch;

	if(ch == '\r' || ch == '\n') {
		g_gps_line_buf[g_gps_line_len] = '\0';
		process_sentence(g_gps_line_buf);
		g_gps_line_active = 0U;
		g_gps_line_len = 0U;
	}
}

static void GPS_SentenceProcess(void)
{
    /* Odometer_Flash_Init() ran before the scheduler; the starting total is
     * published to the UI from here, once the dashboard is up. */
    gps_odometer_init();
    uint32_t now = HAL_GetTick();
    if(g_gps_poll_tick != 0U) {
        uint32_t gap = now - g_gps_poll_tick;
        if(gap > g_gps_poll_gap_max_ms) g_gps_poll_gap_max_ms = gap;
        /* 512 bytes at 115200 baud/8N1 can wrap in about 44 ms. This is a
         * possible-overrun indicator, not an exact lost-byte count. */
        if(gap >= 44U) {
            g_gps_dma_late_poll_count++;
            g_gps_line_active = 0U;
            g_gps_line_len = 0U;
        }
    }
    g_gps_poll_tick = now;
	uint32_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
	if(ndtr > GPS_DMA_BUF_SIZE) {
		return;
	}

	uint32_t write_pos = (GPS_DMA_BUF_SIZE - ndtr) % GPS_DMA_BUF_SIZE;
	uint32_t pos = g_gps_dma_last_pos;
	uint32_t guard = 0U;

	while(pos != write_pos && guard < GPS_DMA_BUF_SIZE) {
		GPS_ProcessByte(g_gps_dma_buf[pos]);
		pos++;
		if(pos >= GPS_DMA_BUF_SIZE) pos = 0U;
		guard++;
	}

	g_gps_dma_last_pos = write_pos;
}

static void GPS_SendCmd(const char * cmd)
{
	char buf[128];
	int32_t len = 0;
	while(len < 124 && cmd[len] != '\0') len++;
	for(int32_t i = 0; i < len; i++) buf[i] = cmd[i];
	buf[len] = '\r';
	buf[len + 1] = '\n';
	HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)(len + 2), 1000);
}

/* Receiver configuration must not leave the 512-byte DMA ring undrained. */
static void GPS_WaitAndDrain(uint32_t duration_ms)
{
    uint32_t start = HAL_GetTick();
    do {
        GPS_SentenceProcess();
        gps_can_speed_tick(HAL_GetTick());
        vTaskDelay(pdMS_TO_TICKS(10U));
    } while((HAL_GetTick() - start) < duration_ms);
}

static void GPS_TaskFunc(void * argument)
{
	TickType_t last_wake_tick;
	uint32_t boot_start;

	(void)argument;

    while(Dashboard_UI_IsStartupComplete() == 0U) vTaskDelay(pdMS_TO_TICKS(10U));

	/* Wait for the first valid NMEA sentence: proves the UM982 has finished
	 * booting and the 115200 link is alive.  Config commands sent during the
	 * module's own boot are silently dropped. */
	boot_start = HAL_GetTick();
	while((g_gps_sentence_count == 0U) && ((HAL_GetTick() - boot_start) < 5000U)) {
		GPS_SentenceProcess();
		gps_can_speed_tick(HAL_GetTick());
		vTaskDelay(pdMS_TO_TICKS(20));
	}

	GPS_SendCmd("UNLOG COM2");
	GPS_WaitAndDrain(200U);
	/* Vehicle dynamics profile: up to 100 m/s horizontal speed and lower
	 * vertical dynamics than the UM982 default UAV profile. */
	GPS_SendCmd("MODE ROVER AUTOMOTIVE");
	GPS_WaitAndDrain(500U);
	GPS_SendCmd("CONFIG HEADING FIXLENGTH");
	/* Fixed baseline: 111 cm with +/-10 cm tolerance (both parameters are in
	 * centimetres per the NebulasIV interface protocol; parameter 2 is the
	 * acceptable error range, not a variance in m^2). */
	GPS_SendCmd("CONFIG HEADING LENGTH 111 10");
	GPS_WaitAndDrain(100U);
	GPS_SendCmd("GPGGA 1");
	GPS_WaitAndDrain(100U);
	GPS_SendCmd("GPRMC 0.05");
	GPS_WaitAndDrain(100U);
	GPS_SendCmd("GPHPR 0.2");
	GPS_WaitAndDrain(100U);
	GPS_SendCmd("GPGSV 5");
	GPS_WaitAndDrain(200U);
	GPS_SendCmd("SAVECONFIG");
	GPS_WaitAndDrain(500U);
	last_wake_tick = xTaskGetTickCount();

	for(;;) {
		/* Ten-millisecond polling drains the circular DMA and provides a stable
		 * time base for the independent 20 Hz CAN speed publisher. */
		vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(10));

		GPS_SentenceProcess();
		invalidate_expired_gsv();
		gps_can_speed_tick(HAL_GetTick());
	}
}

void GPS_Init(void)
{
	uint32_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
	if(ndtr <= GPS_DMA_BUF_SIZE) {
		g_gps_dma_last_pos = (GPS_DMA_BUF_SIZE - ndtr) % GPS_DMA_BUF_SIZE;
	} else {
		g_gps_dma_last_pos = 0U;
	}
	g_gps_sentence_count = 0;
	g_gps_rmc_count = 0;
	g_gps_speed_kmh_int = 0;
	g_gps_speed_kmh_centi = 0;
	g_gps_track_tenths = 0;
	g_gps_data.heading_angle = 0.0f;
	g_gps_data.heading_valid = 0U;
	g_gps_data.heading_quality = 0U;
	g_gps_line_len = 0U;
	g_gps_line_active = 0U;
	g_gsv_window_snr_sum = 0;
	g_gsv_window_snr_count = 0;
	g_gsv_window_max_snr = 0;
	g_gsv_last_tick = 0;
	g_gsv_signal_ui_last_tick = 0U;
	g_gga_signal_level = GPS_SIG_LEVEL_NONE;
	g_hpr_last_tick = 0U;
	g_gps_rmc_last_tick = 0U;
	g_gps_can_last_tick = 0U;
	g_gps_can_slow_phase = 0U;
	g_gps_can_lap_count = 0U;
	g_gps_rmc_valid = 0U;
	g_gps_speed_ui_alive_tick = 0U;
	g_odometer_initialized = 0U;
	/* Preserve the dashboard's startup placeholder until the first valid RMC is
	 * received. The CAN publisher still uses the internal zero speed. */
    GPS_Lap_InitLock();
    g_gps_task_handle = xTaskCreateStatic(GPS_TaskFunc, "GPS_Task", 3072U, NULL,
                                         (UBaseType_t)osPriorityNormal + 1U,
                                         g_gps_task_stack, &g_gps_task_control);
    Firmware_Diag_RegisterTask(FW_TASK_GPS, g_gps_task_handle, "GPS", sizeof(g_gps_task_stack));
}

void GPS_ISR_Notify(void)
{
	/* Retained for the generated IRQ hook.  GPS RX is consumed by periodic
	 * circular-DMA polling, so no FreeRTOS API is called from USART3 IRQ. */
}

void GPS_GetData(GPS_Data_t * out)
{
	if(out == NULL) return;
	taskENTER_CRITICAL();
	*out = g_gps_data;
	out->speed_kmh = (float)g_gps_speed_kmh_centi / 100.0f;
	out->track_angle = (float)g_gps_track_tenths / 10.0f;
	if((out->heading_valid != 0U) &&
	   ((g_hpr_last_tick == 0U) ||
	    ((HAL_GetTick() - g_hpr_last_tick) > GPS_HPR_EXPIRE_MS))) {
		out->heading_valid = 0U;
	}
	taskEXIT_CRITICAL();
}

/* USER CODE END gps */
