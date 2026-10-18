/* USER CODE BEGIN gps */
#include "main.h"
#include "gps.h"
#include "gps_speed.h"
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
/* ISR 重启了 DMA 接收：环从下标 0 重新写起，任务侧据此丢弃旧读位置。 */
static volatile uint8_t g_gps_dma_restarted;
static uint32_t g_isr_restart_tick;
static uint32_t g_isr_restart_count;
/* 速度与位置的有效性，屏幕、CAN、计时和 CLI 共用，只在 GPS 任务里修改。 */
static gps_speed_state_t g_gps_speed_st;
static GPS_Stats_t g_stats;
static GPS_Window_t g_win;
static uint32_t    g_service_tick;
static int32_t     g_gps_track_tenths;
static uint8_t     g_ui_speed_shown;     /* 界面当前显示的是数字(1)还是 "--"(0) */
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
static uint32_t    g_gga_tick;
static uint32_t    g_hpr_last_tick;
static uint32_t    g_gps_can_last_tick;
static uint8_t     g_gps_can_slow_phase;
static uint8_t     g_gps_can_lap_count;

static float       g_odometer_prev_latitude;
static float       g_odometer_prev_longitude;
static float       g_odometer_fraction_m;
static uint32_t    g_odometer_prev_tick;
static uint32_t    g_odometer_total_m;
static uint32_t    g_odometer_checkpoint_m;
static int32_t     g_odometer_ui_tenths;
static uint8_t     g_odometer_anchor_valid;
static uint8_t     g_odometer_initialized;
static uint32_t    g_odometer_seen_seq;

#define GPS_GSV_EXPIRE_MS             12000U
#define GPS_HPR_EXPIRE_MS               1500U
/* GGA 按 1 s 输出，门限 3.5 个周期；超时后按"无定位"处理，不再沿用最后一条的定位质量。 */
#define GPS_GGA_EXPIRE_MS               3500U
#define GPS_CAN_SPEED_PERIOD_MS            50U
/* CAN 车速的失效门限，沿用原值；改为与本地新鲜度门限一致需先在 vehicle-interfaces 评审。 */
#define GPS_CAN_SPEED_STALE_MS             500U
/* 0x301/0x067/0x068 go out every GPS_CAN_SPEED_PERIOD_MS tick. Odometer, GPS
 * quality (0x7A0) and lap timing (0x7A1) change slowly and the ground station
 * records them, so they use a multiple of the tick. A change of the lap count
 * sends both at once so a finished lap is reported without delay. */
#define GPS_CAN_STATUS_TICKS               10U   /* 0x7A0: 500 ms */
#define GPS_CAN_LAP_TICKS                   4U   /* 0x7A1: 200 ms */
#define GPS_CAN_SLOW_CYCLE_TICKS           20U   /* common multiple of both */
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

/* 接收机配置确认：每条命令等回执，超时重发，仍无回执则标记 NOACK 继续。 */
#define GPS_CFG_ACK_TIMEOUT_MS            400U
#define GPS_CFG_RETRIES                     2U
#define GPS_CFG_ACK_NONE                    0U
#define GPS_CFG_ACK_OK                      1U
#define GPS_CFG_ACK_REJECT                  2U

static GPS_CfgStatus_t g_cfg;
static const char * volatile g_cfg_pending_cmd;
static volatile uint8_t g_cfg_ack;
static char g_cfg_reply[GPS_CFG_REPLY_LEN];
static uint8_t g_cfg_noack_streak;
static volatile GPS_Request_t g_gps_request;

/* 查询回复的原始行：'#' 日志、'<' 续行、$command 回执和白名单之外的语句。 */
static char g_raw_lines[GPS_RAW_LINES][GPS_RAW_LINE_LEN];
static uint32_t g_raw_count;
static uint32_t g_raw_dropped;
static uint32_t g_raw_other_nmea;
static volatile uint8_t g_raw_active;

static GPS_CaptureRec_t g_capture[GPS_CAPTURE_MAX_RECORDS];
static uint32_t g_capture_count;
static uint32_t g_capture_end_tick;
static volatile GPS_CaptureState_t g_capture_state;

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
	gps_speed_view_t speed_view;
	GPS_Data_t gps_snapshot;
	GPS_LapDiagnostic_t lap_diagnostic;
	CAN_GPSTelemetry_t telemetry;

	if((g_gps_can_last_tick != 0U) &&
	   ((now - g_gps_can_last_tick) < GPS_CAN_SPEED_PERIOD_MS)) {
		return;
	}
	g_gps_can_last_tick = now;
	/* 速度有效性只在这里读取，不修改：无效时 CAN 报 0，同时 0x7A0 的 RMC_VALID 位清零，
	 * 接收方据此区分"真实的 0 km/h"和"没有可信速度"。 */
	taskENTER_CRITICAL();
	GPS_Speed_GetView(&g_gps_speed_st, now, GPS_CAN_SPEED_STALE_MS, &speed_view);
	taskEXIT_CRITICAL();
	speed_kmh_centi = speed_view.speed_centi;
	rmc_valid = speed_view.valid;
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

/* RMC 速度字段（节）→ 0.01 km/h。字段为空、含非数字字符、为负或位数溢出时返回 0，
 * 调用方必须据此拒绝这条速度，不能把它当作真实的 0 km/h。 */
static uint8_t nmea_parse_speed_kmh_centi(const char * buf, int32_t idx, int32_t * out_centi)
{
	char fld[32];
	const char * p = nmea_get_field(buf, idx, fld, sizeof(fld));
	int32_t int_part = 0;
	int32_t frac_part = 0;
	int32_t frac_digits = 0;
	uint8_t digits = 0U;
	const char * s = fld;
	int32_t knot_milli;
	int32_t centi;

	if((p == NULL) || (fld[0] == '\0')) return 0U;

	while(*s >= '0' && *s <= '9') {
		if(nmea_append_digit(&int_part, *s, INT32_MAX / 1000) == 0U) return 0U;
		digits = 1U;
		s++;
	}
	if(*s == '.') {
		s++;
		while(*s >= '0' && *s <= '9') {
			if(frac_digits < 3) {
				frac_part = frac_part * 10 + (int32_t)(*s - '0');
				frac_digits++;
			}
			digits = 1U;
			s++;
		}
	}
	if((digits == 0U) || (*s != '\0')) return 0U;
	while(frac_digits < 3) {
		frac_part *= 10;
		frac_digits++;
	}

	/* RMC speed is knots. Convert 0.001 kn to 0.01 km/h with rounding. */
	if(frac_part > INT32_MAX - int_part * 1000) return 0U;
	knot_milli = (int_part * 1000) + frac_part;
	centi = (int32_t)(((int64_t)knot_milli * 1852 + 5000) / 10000);
	if(centi > 30000) centi = 30000;
	*out_centi = centi;
	return 1U;
}

/* hhmmss.ss → UTC 当日毫秒。fine 表示字段带小数秒（至少一位）。 */
static uint8_t nmea_parse_utc_ms(const char * buf, int32_t idx, uint32_t * out_ms, uint8_t * fine)
{
	char fld[24];
	const char * p = nmea_get_field(buf, idx, fld, sizeof(fld));
	const char * s = fld;
	uint32_t hh;
	uint32_t mm;
	uint32_t ss;
	uint32_t ms = 0U;
	uint32_t scale = 100U;

	*fine = 0U;
	if((p == NULL) || (fld[0] == '\0')) return 0U;
	for(uint32_t i = 0U; i < 6U; i++) {
		if((s[i] < '0') || (s[i] > '9')) return 0U;
	}
	hh = (uint32_t)((s[0] - '0') * 10 + (s[1] - '0'));
	mm = (uint32_t)((s[2] - '0') * 10 + (s[3] - '0'));
	ss = (uint32_t)((s[4] - '0') * 10 + (s[5] - '0'));
	if((hh > 23U) || (mm > 59U) || (ss > 60U)) return 0U;
	s += 6;
	if(*s == '.') {
		s++;
		while((*s >= '0') && (*s <= '9')) {
			if(scale > 0U) {
				ms += (uint32_t)(*s - '0') * scale;
				scale /= 10U;
			}
			*fine = 1U;
			s++;
		}
	}
	if(*s != '\0') return 0U;
	*out_ms = (((hh * 60U) + mm) * 60U + ss) * 1000U + ms;
	return 1U;
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

typedef enum {
	GPS_KIND_OTHER = 0,
	GPS_KIND_RMC,
	GPS_KIND_GGA,
	GPS_KIND_HPR,
	GPS_KIND_GSV,
	GPS_KIND_REPLY
} gps_sentence_kind_t;

/* 按地址字段末三个字符判断语句类型（$GNRMC、$GPRMC、$BDRMC 都是 RMC）。不能在整行里搜
 * "RMC"：接收机对 GPRMC 命令的回执 "$command,GPRMC 0.05,response: OK" 也含这三个字符，
 * 误当作 RMC 解析会把状态读成 V 并使车速失效。 */
static gps_sentence_kind_t gps_sentence_kind(const char * s)
{
	if(strncmp(s, "$command,", 9U) == 0) return GPS_KIND_REPLY;
	if((s[1] != '\0') && (s[2] != '\0') && (s[3] != '\0') && (s[4] != '\0') &&
	   (s[5] != '\0') && (s[6] == ',')) {
		const char * type = &s[3];
		if((type[0] == 'R') && (type[1] == 'M') && (type[2] == 'C')) return GPS_KIND_RMC;
		if((type[0] == 'G') && (type[1] == 'G') && (type[2] == 'A')) return GPS_KIND_GGA;
		if((type[0] == 'H') && (type[1] == 'P') && (type[2] == 'R')) return GPS_KIND_HPR;
		if((type[0] == 'G') && (type[1] == 'S') && (type[2] == 'V')) return GPS_KIND_GSV;
	}
	return GPS_KIND_OTHER;
}

/* ---- 诊断：原始回复行、采集、窗口 ---- */

static void gps_raw_store(const char * line)
{
	uint32_t n = 0U;

	if(g_raw_count >= GPS_RAW_LINES) {
		g_raw_dropped++;
		return;
	}
	while((n < (GPS_RAW_LINE_LEN - 1U)) && (line[n] != '\0') &&
	      (line[n] != '\r') && (line[n] != '\n')) {
		/* 噪声字节不能原样送到终端。 */
		g_raw_lines[g_raw_count][n] = ((line[n] >= 0x20) && (line[n] < 0x7F)) ? line[n] : '.';
		n++;
	}
	g_raw_lines[g_raw_count][n] = '\0';
	g_raw_count++;
}

/* 查询期间保存 '#' 日志、'<' 续行和 $command 回执。 */
static void gps_raw_capture(const char * line)
{
	if(g_raw_active == 0U) return;
	gps_raw_store(line);
}

/* 白名单之外的 NMEA 语句：说明 UNLOG 或白名单没有生效。只留前几条做证据。 */
static void gps_raw_capture_other(const char * line)
{
	if(g_raw_active == 0U) return;
	if(g_raw_other_nmea >= 4U) return;
	g_raw_other_nmea++;
	gps_raw_store(line);
}

static void gps_capture_add(uint32_t now, uint8_t verdict, uint32_t epoch_ms,
                            int32_t speed_centi, int16_t shown_kmh, uint8_t valid)
{
	GPS_CaptureRec_t * rec;

	if(g_capture_state != GPS_CAPTURE_RUNNING) return;
	if(g_capture_count >= GPS_CAPTURE_MAX_RECORDS) {
		g_capture_state = GPS_CAPTURE_DONE;
		return;
	}
	rec = &g_capture[g_capture_count++];
	rec->tick_ms = now;
	rec->epoch_ms = epoch_ms;
	rec->speed_centi = speed_centi;
	rec->shown_kmh = shown_kmh;
	rec->verdict = verdict;
	rec->valid = valid;
}

static void gps_capture_service(uint32_t now)
{
	if((g_capture_state == GPS_CAPTURE_RUNNING) &&
	   ((int32_t)(now - g_capture_end_tick) >= 0)) {
		g_capture_state = GPS_CAPTURE_DONE;
	}
}

static void gps_window_clear(uint32_t now)
{
	(void)memset(&g_win, 0, sizeof(g_win));
	g_win.start_tick_ms = now;
	g_win.interval_min_ms = UINT32_MAX;
	g_win.epoch_min_ms = UINT32_MAX;
	g_win.speed_min_centi = INT32_MAX;
	g_win.speed_max_centi = INT32_MIN;
}

static void gps_window_note_accept(int32_t speed_centi)
{
	g_win.samples++;
	if(g_gps_speed_st.last_interval_ms != 0U) {
		if(g_gps_speed_st.last_interval_ms < g_win.interval_min_ms) g_win.interval_min_ms = g_gps_speed_st.last_interval_ms;
		if(g_gps_speed_st.last_interval_ms > g_win.interval_max_ms) g_win.interval_max_ms = g_gps_speed_st.last_interval_ms;
	}
	if(g_gps_speed_st.last_epoch_delta_ms != 0U) {
		if(g_gps_speed_st.last_epoch_delta_ms < g_win.epoch_min_ms) g_win.epoch_min_ms = g_gps_speed_st.last_epoch_delta_ms;
		if(g_gps_speed_st.last_epoch_delta_ms > g_win.epoch_max_ms) g_win.epoch_max_ms = g_gps_speed_st.last_epoch_delta_ms;
	}
	if(speed_centi < g_win.speed_min_centi) g_win.speed_min_centi = speed_centi;
	if(speed_centi > g_win.speed_max_centi) g_win.speed_max_centi = speed_centi;
}

/* 记录有效性切换，并在速度失效时通知界面显示 "--"。既在每个周期检查里调用，也在每条 RMC
 * 解析后调用：一次轮询读到 "A、A、V" 连发时，也不会漏掉最后的失效。
 * 有效性不在这里被"保持"或清零，界面、CAN 和计时都直接读 g_gps_speed_st。 */
static void gps_speed_sync(uint32_t now, gps_speed_view_t * view)
{
	uint8_t changed;

	taskENTER_CRITICAL();
	changed = GPS_Speed_Poll(&g_gps_speed_st, now);
	GPS_Speed_GetView(&g_gps_speed_st, now, 0U, view);
	taskEXIT_CRITICAL();

	if((changed != 0U) && (view->valid == 0U)) {
		g_stats.speed_invalid_events++;
		gps_capture_add(now, (uint8_t)(GPS_CAPTURE_EVENT_FLAG | view->reason),
		                GPS_CAPTURE_NO_EPOCH, INT32_MIN, -1, 0U);
	}
	if((view->valid == 0U) && (g_ui_speed_shown != 0U)) {
		g_ui_speed_shown = 0U;
		g_stats.shown_speed_kmh = -1;
		Dashboard_UI_SubmitSpeedInvalid();
	}
}

/* GPS 任务每 10 ms 一次：累计有效时间、记录最长无数据时间，再同步有效性。 */
static void gps_speed_service(uint32_t now)
{
	gps_speed_view_t view;
	uint32_t dt = (g_service_tick != 0U) ? (now - g_service_tick) : 0U;

	g_service_tick = now;
	if(dt > 1000U) dt = 0U;
	if(g_gps_speed_st.prev_valid != 0U) g_win.valid_ms += dt;

	gps_speed_sync(now, &view);
	if((g_gps_speed_st.has_fresh != 0U) && (view.age_ms > g_win.age_max_ms)) {
		g_win.age_max_ms = view.age_ms;
	}
}

/* 经纬度字段都非空、半球字符合法且在范围内才算位置可用。 */
static uint8_t nmea_parse_position(const char * buf, float * lat, float * lon)
{
	char fld[24];
	char hemi[4];

	if((nmea_get_field(buf, 3, fld, sizeof(fld)) == NULL) || (fld[0] == '\0')) return 0U;
	(void)nmea_get_field(buf, 4, hemi, sizeof(hemi));
	if((hemi[0] != 'N') && (hemi[0] != 'S')) return 0U;
	if((nmea_get_field(buf, 5, fld, sizeof(fld)) == NULL) || (fld[0] == '\0')) return 0U;
	(void)nmea_get_field(buf, 6, hemi, sizeof(hemi));
	if((hemi[0] != 'E') && (hemi[0] != 'W')) return 0U;
	*lat = nmea_parse_coord_deg(buf, 3, 4);
	*lon = nmea_parse_coord_deg(buf, 5, 6);
	return (uint8_t)((fabsf(*lat) <= 90.0f) && (fabsf(*lon) <= 180.0f));
}

static void parse_GNRMC(const char * sentence)
{
	char stat[4];
	char mode[4];
	char fld[16];
	gps_rmc_sample_t rmc;
	gps_rmc_verdict_t verdict;
	uint8_t position_refreshed = 0U;
	uint8_t fine = 0U;
	uint8_t track_present;
	int32_t speed_centi = 0;
	int32_t track_tenths = 0;
	uint32_t epoch_ms = 0U;
	float latitude = 0.0f;
	float longitude = 0.0f;
	uint32_t now = HAL_GetTick();

	(void)memset(&rmc, 0, sizeof(rmc));
	(void)nmea_get_field(sentence, 2, stat, sizeof(stat));
	(void)nmea_get_field(sentence, 12, mode, sizeof(mode));
	/* 状态 A 且模式指示不是 N（NMEA 2.3+：N = 数据无效）才算有定位。 */
	rmc.fix_ok = (uint8_t)((stat[0] == 'A') && (mode[0] != 'N'));
	if(rmc.fix_ok != 0U) {
		rmc.speed_present = nmea_parse_speed_kmh_centi(sentence, 7, &speed_centi);
		rmc.speed_centi = speed_centi;
		rmc.epoch_present = nmea_parse_utc_ms(sentence, 1, &epoch_ms, &fine);
		rmc.epoch_fine = fine;
		rmc.epoch_ms = epoch_ms;
		rmc.position_present = nmea_parse_position(sentence, &latitude, &longitude);
		track_present = (uint8_t)((nmea_get_field(sentence, 8, fld, sizeof(fld)) != NULL) &&
		                          (fld[0] != '\0'));
		if(track_present != 0U) track_tenths = nmea_parse_track_int(sentence, 8);
	}
	else {
		track_present = 0U;
	}

	taskENTER_CRITICAL();
	verdict = GPS_Speed_OnRmc(&g_gps_speed_st, &rmc, now, &position_refreshed);
	if(position_refreshed != 0U) {
		g_gps_data.latitude = latitude;
		g_gps_data.longitude = longitude;
		if(track_present != 0U) g_gps_track_tenths = track_tenths;
	}
	taskEXIT_CRITICAL();

	{
		gps_speed_view_t view;
		gps_speed_sync(now, &view);   /* 状态 V 立即通知界面，不等下一个 10 ms 周期 */
	}

	switch(verdict) {
	case GPS_RMC_NO_FIX:     g_stats.rmc_no_fix++;     break;
	case GPS_RMC_BAD_FIELD:  g_stats.rmc_bad_field++;  break;
	case GPS_RMC_DUP_EPOCH:  g_stats.rmc_dup_epoch++;  break;
	case GPS_RMC_BACK_EPOCH: g_stats.rmc_back_epoch++; break;
	default:                 g_stats.rmc_accepted++;   break;
	}

	if(verdict != GPS_RMC_ACCEPTED) {
		if(verdict == GPS_RMC_NO_FIX) g_odometer_anchor_valid = 0U;
		gps_capture_add(now, (uint8_t)verdict,
		                (rmc.epoch_present != 0U) ? rmc.epoch_ms : GPS_CAPTURE_NO_EPOCH,
		                (rmc.speed_present != 0U) ? speed_centi : INT32_MIN,
		                (int16_t)g_stats.shown_speed_kmh, g_gps_speed_st.prev_valid);
		return;
	}

	{
		int32_t speed_kmh = (speed_centi + 50) / 100;
		GPS_Data_t lap_data;

		if(speed_kmh < 0) speed_kmh = 0;
		if(speed_kmh > 300) speed_kmh = 300;
		g_stats.raw_speed_centi = speed_centi;
		g_stats.shown_speed_kmh = speed_kmh;
		g_stats.last_epoch_ms = (rmc.epoch_present != 0U) ? rmc.epoch_ms : GPS_CAPTURE_NO_EPOCH;
		gps_window_note_accept(speed_centi);
		gps_capture_add(now, (uint8_t)verdict,
		                (rmc.epoch_present != 0U) ? rmc.epoch_ms : GPS_CAPTURE_NO_EPOCH,
		                speed_centi, (int16_t)speed_kmh, 1U);

		g_ui_speed_shown = 1U;
		Dashboard_UI_SubmitSpeed(speed_kmh);

		/* 速度失效后重新接受数据：里程锚点作废，圈速/冲刺在各自模块里比较 continuity_seq。 */
		if(g_gps_speed_st.continuity_seq != g_odometer_seen_seq) {
			g_odometer_seen_seq = g_gps_speed_st.continuity_seq;
			g_odometer_anchor_valid = 0U;
		}
		GPS_GetData(&lap_data);
		GPS_LapProcess(&lap_data);
		GPS_SprintProcess(&lap_data);
		if(rmc.position_present != 0U) {
			gps_odometer_process(latitude, longitude, speed_centi, now);
		}
		else {
			g_odometer_anchor_valid = 0U;
		}
	}
}

/* 定位质量按 GGA 更新；超时后用 fix_qual = 0 走同一条路径，不再沿用最后一条。 */
static void gga_apply(int32_t fix_qual, int32_t sats, uint32_t now)
{
	uint8_t fallback_signal = gps_signal_level_from_sats(sats);
	uint8_t submit_fallback = 0U;
	uint8_t submit_level = GPS_SIG_LEVEL_NONE;

	taskENTER_CRITICAL();
	g_gps_data.fix_quality = (uint8_t)fix_qual;
	g_gps_data.satellites  = (uint8_t)sats;
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

static void parse_GNGGA(const char * sentence)
{
	int32_t fix_qual = nmea_field_atoi(sentence, 6);
	int32_t sats     = nmea_field_atoi(sentence, 7);
	int32_t altitude_milli = nmea_field_atoi_milli(sentence, 9);
	uint32_t now = HAL_GetTick();

	taskENTER_CRITICAL();
	g_gps_data.altitude = (float)altitude_milli / 1000.0f;
	taskEXIT_CRITICAL();
	g_gga_tick = (now != 0U) ? now : 1U;
	gga_apply(fix_qual, sats, now);
}

static void gps_expire_gga(uint32_t now)
{
	if(g_gga_tick == 0U) return;
	if((now - g_gga_tick) <= GPS_GGA_EXPIRE_MS) return;
	g_gga_tick = 0U;
	gga_apply(0, 0, now);
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

static uint8_t gps_ci_equal(char a, char b)
{
	if((a >= 'a') && (a <= 'z')) a = (char)(a - 'a' + 'A');
	if((b >= 'a') && (b <= 'z')) b = (char)(b - 'a' + 'A');
	return (uint8_t)(a == b);
}

/* 接收机对命令的回执："$command,<命令原文>,response: OK*CS"。手册（V1 CH）没有记载这一格式，
 * 这里按已知的 UM98x 行为识别，格式不符时命令按 NOACK 处理，不会卡住启动流程。
 * 只比较命令的第一个词（忽略大小写），防止上一条命令的迟到回执被算到当前命令上。 */
static void gps_handle_reply(const char * s)
{
	const char * echo = s + 9;
	const char * tag = strstr(echo, ",response:");
	const char * pending = g_cfg_pending_cmd;
	const char * text;
	uint32_t i;
	uint32_t n = 0U;

	if((tag == NULL) || (pending == NULL)) return;
	for(i = 0U; ; i++) {
		char c = pending[i];
		char e = echo[i];
		if((c == '\0') || (c == ' ')) {
			if((e != ',') && (e != ' ')) return;
			break;
		}
		if(gps_ci_equal(c, e) == 0U) return;
	}
	text = tag + 10;
	while(*text == ' ') text++;
	while((n < (GPS_CFG_REPLY_LEN - 1U)) && (text[n] != '\0') && (text[n] != '*') &&
	      (text[n] != '\r') && (text[n] != '\n')) {
		g_cfg_reply[n] = text[n];
		n++;
	}
	g_cfg_reply[n] = '\0';
	g_cfg_ack = (uint8_t)((gps_ci_equal(text[0], 'O') != 0U) && (gps_ci_equal(text[1], 'K') != 0U) &&
	                      ((text[2] == '*') || (text[2] == '\0') || (text[2] == '\r') ||
	                       (text[2] == '\n'))
	                      ? GPS_CFG_ACK_OK : GPS_CFG_ACK_REJECT);
}

static void process_sentence(const char * buf)
{
	gps_sentence_kind_t kind;

	if(buf[0] != '$') {
		gps_raw_capture(buf);
		return;
	}
	if(!nmea_checksum(buf)) {
		/* 回执若缺校验和也照收，别让一条命令因此白等超时。 */
		if((strncmp(buf, "$command,", 9U) == 0) && (strchr(buf, '*') == NULL)) {
			g_stats.n_reply++;
			gps_handle_reply(buf);
			gps_raw_capture(buf);
		}
		else {
			g_stats.checksum_fail++;
		}
		return;
	}

	g_stats.sentences++;
	kind = gps_sentence_kind(buf);
	switch(kind) {
	case GPS_KIND_RMC:   g_stats.n_rmc++; parse_GNRMC(buf); break;
	case GPS_KIND_GGA:   g_stats.n_gga++; parse_GNGGA(buf); break;
	case GPS_KIND_HPR:   g_stats.n_hpr++; parse_GNHPR(buf); break;
	case GPS_KIND_GSV:   g_stats.n_gsv++; parse_GNGSV(buf); break;
	case GPS_KIND_REPLY:
		g_stats.n_reply++;
		gps_handle_reply(buf);
		gps_raw_capture(buf);
		break;
	default:
		g_stats.n_other++;
		gps_raw_capture_other(buf);
		break;
	}
}

static void GPS_ProcessByte(uint8_t byte)
{
	char ch = (char)byte;

	if(ch == '$') {
		if((g_gps_line_active != 0U) && (g_gps_line_len > 0U)) g_stats.line_truncated++;
		g_gps_line_active = 1U;
		g_gps_line_len = 0U;
	}
	else if(((ch == '#') || (ch == '<')) && (g_gps_line_active == 0U)) {
		/* 接收机的 '#' 日志和 '<' 续行，只在查询时被保存，平时丢弃。 */
		g_gps_line_active = 1U;
		g_gps_line_len = 0U;
	}

	if(g_gps_line_active == 0U) return;

	if(g_gps_line_len >= (uint16_t)(sizeof(g_gps_line_buf) - 1U)) {
		if(g_gps_line_buf[0] == '$') {
			g_stats.line_overflow++;
		}
		else {
			/* 过长的 '#' 日志（如 VERSION）：保留已收到的部分。 */
			g_gps_line_buf[g_gps_line_len] = '\0';
			process_sentence(g_gps_line_buf);
		}
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

/* 重启 USART3 的循环 DMA 接收。HAL 在 DMA 模式下遇到任何串口错误（帧、噪声、校验、溢出）
 * 都会中止接收且不自动恢复，不重启则 GPS 数据永久中断。中断和任务两处都会调用，
 * HAL_UART_Receive_DMA 只在 RxState 为 READY 时才会真正启动。 */
static void gps_dma_restart(void)
{
	if(huart3.RxState != HAL_UART_STATE_READY) return;
	if(HAL_UART_Receive_DMA(&huart3, g_gps_dma_buf, GPS_DMA_BUF_SIZE) == HAL_OK) {
		g_stats.rx_restarts++;
		g_gps_dma_restarted = 1U;
	}
	else {
		g_stats.rx_restart_fail++;
	}
}

void GPS_UartErrorCallback(uint32_t error_code)
{
	uint32_t now = HAL_GetTick();

	if((error_code & HAL_UART_ERROR_PE) != 0U) g_stats.uart_err_pe++;
	if((error_code & HAL_UART_ERROR_NE) != 0U) g_stats.uart_err_ne++;
	if((error_code & HAL_UART_ERROR_FE) != 0U) g_stats.uart_err_fe++;
	if((error_code & HAL_UART_ERROR_ORE) != 0U) g_stats.uart_err_ore++;
	if((error_code & HAL_UART_ERROR_DMA) != 0U) g_stats.uart_err_dma++;
	/* 线路持续出错时错误中断会以字节速率反复触发：100 ms 内最多在中断里重启 8 次，其余留给
	 * 任务每 10 ms 的轮询补救，错误风暴不会占满 CPU。 */
	if((now - g_isr_restart_tick) >= 100U) {
		g_isr_restart_tick = now;
		g_isr_restart_count = 0U;
	}
	if(g_isr_restart_count < 8U) {
		g_isr_restart_count++;
		gps_dma_restart();
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
        if(gap > g_win.poll_gap_max_ms) g_win.poll_gap_max_ms = gap;
        /* 512 bytes at 115200 baud/8N1 can wrap in about 44 ms. This is a
         * possible-overrun indicator, not an exact lost-byte count. */
        if(gap >= 44U) {
            g_gps_dma_late_poll_count++;
            g_gps_line_active = 0U;
            g_gps_line_len = 0U;
        }
    }
    g_gps_poll_tick = now;

	uint32_t ndtr;
	uint8_t restarted;

	/* 接收被中止（错误回调还没来得及重启，或回调丢失）时在这里补救。 */
	taskENTER_CRITICAL();
	gps_dma_restart();
	restarted = g_gps_dma_restarted;
	g_gps_dma_restarted = 0U;
	ndtr = __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
	taskEXIT_CRITICAL();
	if(ndtr > GPS_DMA_BUF_SIZE) {
		return;
	}

	uint32_t write_pos = (GPS_DMA_BUF_SIZE - ndtr) % GPS_DMA_BUF_SIZE;
	if(restarted != 0U) {
		/* 新的 DMA 传输从下标 0 开始写；半行数据作废。 */
		g_gps_dma_last_pos = 0U;
		g_gps_line_active = 0U;
		g_gps_line_len = 0U;
	}
	uint32_t pos = g_gps_dma_last_pos;
	uint32_t guard = 0U;
	uint32_t pending = (write_pos + GPS_DMA_BUF_SIZE - pos) % GPS_DMA_BUF_SIZE;

	g_stats.dma_bytes += pending;
	if(pending > g_stats.ring_pending_max) g_stats.ring_pending_max = pending;
	if(pending > g_win.ring_pending_max) g_win.ring_pending_max = pending;

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

/* GPS 任务的一次服务：读 DMA、解析、有效性检查、CAN 发送节拍。主循环和所有等待都走这里，
 * 配置命令期间 512 字节 DMA 环同样不会无人读取。 */
static void gps_service_once(void)
{
	uint32_t now;

	GPS_SentenceProcess();
	now = HAL_GetTick();
	gps_expire_gga(now);
	gps_speed_service(now);
	gps_capture_service(now);
	invalidate_expired_gsv();
	gps_can_speed_tick(now);
}

/* Receiver configuration must not leave the 512-byte DMA ring undrained. */
static void GPS_WaitAndDrain(uint32_t duration_ms)
{
    uint32_t start = HAL_GetTick();
    do {
        gps_service_once();
        vTaskDelay(pdMS_TO_TICKS(10U));
    } while((HAL_GetTick() - start) < duration_ms);
}

static uint8_t gps_wait_ack(uint32_t timeout_ms)
{
	uint32_t start = HAL_GetTick();

	do {
		gps_service_once();
		if(g_cfg_ack != GPS_CFG_ACK_NONE) return g_cfg_ack;
		vTaskDelay(pdMS_TO_TICKS(10U));
	} while((HAL_GetTick() - start) < timeout_ms);
	return g_cfg_ack;
}

/* 发送一条命令并确认。needs_ack 为 0（UNLOG 的回执可能随输出一起被关掉），或此前连续两条都
 * 没有回执（接收机不按此格式回复）时，只按 settle_ms 固定等待，记为 UNCONFIRMED。
 * 其余命令超时重发至多 GPS_CFG_RETRIES 次；回执非 OK 记 REJECTED，不重发。 */
static void gps_cfg_send(GPS_CfgItem_t * item, const char * cmd, uint8_t needs_ack,
                         uint32_t settle_ms)
{
	uint32_t attempts = ((needs_ack != 0U) && (g_cfg.ack_unsupported == 0U)) ?
	                    (1U + GPS_CFG_RETRIES) : 1U;
	uint8_t ack;

	item->cmd = cmd;
	item->state = (uint8_t)GPS_CFG_PENDING;
	item->attempts = 0U;
	item->reply[0] = '\0';
	for(uint32_t attempt = 0U; attempt < attempts; attempt++) {
		g_cfg_ack = GPS_CFG_ACK_NONE;
		g_cfg_reply[0] = '\0';
		g_cfg_pending_cmd = cmd;
		GPS_SendCmd(cmd);
		item->attempts++;
		if((needs_ack == 0U) || (g_cfg.ack_unsupported != 0U)) {
			g_cfg_pending_cmd = NULL;
			item->state = (uint8_t)GPS_CFG_UNCONFIRMED;
			GPS_WaitAndDrain(settle_ms);
			return;
		}
		ack = gps_wait_ack(GPS_CFG_ACK_TIMEOUT_MS);
		g_cfg_pending_cmd = NULL;
		if(ack != GPS_CFG_ACK_NONE) {
			(void)strncpy(item->reply, g_cfg_reply, GPS_CFG_REPLY_LEN - 1U);
			item->reply[GPS_CFG_REPLY_LEN - 1U] = '\0';
			g_cfg.ack_seen = 1U;
			g_cfg_noack_streak = 0U;
			item->state = (uint8_t)((ack == GPS_CFG_ACK_OK) ? GPS_CFG_OK : GPS_CFG_REJECTED);
			GPS_WaitAndDrain(settle_ms);
			return;
		}
	}
	item->state = (uint8_t)GPS_CFG_NOACK;
	if(g_cfg.ack_seen == 0U) {
		g_cfg_noack_streak++;
		if(g_cfg_noack_streak >= 2U) g_cfg.ack_unsupported = 1U;
	}
	GPS_WaitAndDrain(settle_ms);
}

typedef struct {
	const char * cmd;
	uint8_t needs_ack;
	uint16_t settle_ms;
} gps_cfg_cmd_t;

/* 启动配置。顺序和等待时间沿用原先的固定序列：
 *  - UNLOG 先清掉出厂默认流（1 Hz 的 GGA/RMC/GSA/GSV/GST，全星座），否则 GSV 突发会冲垮
 *    512 字节 DMA 环、RMC 被破坏。无参数 UNLOG 停止当前端口全部输出（协议手册 §8.1），
 *    不带端口名，所以不论 UM982 的哪个 COM 口接到 USART3 都有效。
 *  - MODE ROVER AUTOMOTIVE：车载动态模式，水平最大 100 m/s，垂直动态低于默认的 UAV 模式。
 *  - CONFIG HEADING FIXLENGTH / LENGTH 111 10：固定基线 111 cm，容许误差 ±10 cm
 *    （两参数单位均为 cm，参数 2 是容许误差而不是方差）。
 *  - 白名单只留解析用的四类语句：RMC 20 Hz 驱动车速/CAN/圈速，GGA 1 Hz 定位质量与卫星数，
 *    HPR 5 Hz 双天线航向，GSV 每 5 s 信号条。请求统一带 GP 前缀（手册 §7.1，GBGSV/GLGSV
 *    为非法输入），返回语句的说话者由接收机决定，解析只按报文名匹配。总负载约 2.2 kB/s，
 *    约为 115200 baud 链路的 1/5。
 * 不再每次上电 SAVECONFIG：每次开机都会重发这些命令，保存只是白写一次 UM982 非易失存储；
 * 需要持久保存时用 CLI 的 "gps save"。 */
static const gps_cfg_cmd_t k_cfg_cmds[GPS_CFG_MAX_ITEMS] = {
	{ "UNLOG",                        0U, 300U },
	{ "MODE ROVER AUTOMOTIVE",        1U, 500U },
	{ "CONFIG HEADING FIXLENGTH",     1U,   0U },
	{ "CONFIG HEADING LENGTH 111 10", 1U, 100U },
	{ "GPRMC 0.05",                   1U, 100U },
	{ "GPGGA 1",                      1U, 100U },
	{ "GPHPR 0.2",                    1U, 100U },
	{ "GPGSV 5",                      1U, 200U },
};

static void gps_cfg_run_boot(void)
{
	g_cfg.running = 1U;
	for(uint32_t i = 0U; i < GPS_CFG_MAX_ITEMS; i++) {
		g_cfg.item[i].cmd = k_cfg_cmds[i].cmd;
	}
	for(uint32_t i = 0U; i < GPS_CFG_MAX_ITEMS; i++) {
		gps_cfg_send(&g_cfg.item[i], k_cfg_cmds[i].cmd, k_cfg_cmds[i].needs_ack,
		             k_cfg_cmds[i].settle_ms);
		g_cfg.count = (uint8_t)(i + 1U);
	}
	g_cfg.running = 0U;
}

/* CLI 请求在 GPS 任务里执行，避免多个任务同时写 USART3。执行期间照常读 DMA、发 CAN。 */
static void gps_request_service(void)
{
	GPS_Request_t request = g_gps_request;

	if(request == GPS_REQ_NONE) return;
	switch(request) {
	case GPS_REQ_QUERY:
		g_raw_count = 0U;
		g_raw_dropped = 0U;
		g_raw_other_nmea = 0U;
		g_raw_active = 1U;
		GPS_SendCmd("VERSION");
		GPS_WaitAndDrain(500U);
		GPS_SendCmd("MODE");
		GPS_WaitAndDrain(500U);
		GPS_SendCmd("CONFIG");
		GPS_WaitAndDrain(700U);
		GPS_SendCmd("UNILOGLIST");
		GPS_WaitAndDrain(900U);
		g_raw_active = 0U;
		break;
	case GPS_REQ_RATE_10HZ:
		gps_cfg_send(&g_cfg.request, "GPRMC 0.1", 1U, 100U);
		break;
	case GPS_REQ_RATE_20HZ:
		gps_cfg_send(&g_cfg.request, "GPRMC 0.05", 1U, 100U);
		break;
	case GPS_REQ_SMOOTH_ON:
		gps_cfg_send(&g_cfg.request, "CONFIG SMOOTH PSRVEL enable", 1U, 100U);
		break;
	case GPS_REQ_SMOOTH_OFF:
		gps_cfg_send(&g_cfg.request, "CONFIG SMOOTH PSRVEL disable", 1U, 100U);
		break;
	case GPS_REQ_SAVE:
		gps_cfg_send(&g_cfg.request, "SAVECONFIG", 1U, 500U);
		break;
	default:
		break;
	}
	g_gps_request = GPS_REQ_NONE;
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
	while((g_stats.sentences == 0U) && ((HAL_GetTick() - boot_start) < 5000U)) {
		gps_service_once();
		vTaskDelay(pdMS_TO_TICKS(20));
	}

	gps_cfg_run_boot();
	last_wake_tick = xTaskGetTickCount();

	for(;;) {
		/* Ten-millisecond polling drains the circular DMA and provides a stable
		 * time base for the independent 20 Hz CAN speed publisher. */
		vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(10));

		gps_service_once();
		if(g_gps_request != GPS_REQ_NONE) {
			gps_request_service();
			last_wake_tick = xTaskGetTickCount();
		}
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
	(void)memset(&g_stats, 0, sizeof(g_stats));
	(void)memset(&g_cfg, 0, sizeof(g_cfg));
	GPS_Speed_Init(&g_gps_speed_st);
	gps_window_clear(HAL_GetTick());
	g_stats.shown_speed_kmh = -1;
	g_stats.last_epoch_ms = GPS_CAPTURE_NO_EPOCH;
	g_ui_speed_shown = 0U;
	g_service_tick = 0U;
	g_gps_dma_restarted = 0U;
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
	g_gga_tick = 0U;
	g_hpr_last_tick = 0U;
	g_gps_can_last_tick = 0U;
	g_gps_can_slow_phase = 0U;
	g_gps_can_lap_count = 0U;
	g_odometer_initialized = 0U;
	g_odometer_seen_seq = 0U;
	g_gps_request = GPS_REQ_NONE;
	g_capture_state = GPS_CAPTURE_IDLE;
	/* 界面在首条可信 RMC 之前显示 "--"（初始化时 Dash_View_SetSpeed(DASH_VIEW_INVALID)）；
	 * CAN 在此期间按"速度无效"发送 0。 */
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
	gps_speed_view_t view;
	uint8_t position_fresh;
	uint32_t now = HAL_GetTick();

	if(out == NULL) return;
	taskENTER_CRITICAL();
	*out = g_gps_data;
	GPS_Speed_GetView(&g_gps_speed_st, now, 0U, &view);
	position_fresh = GPS_Speed_PositionFresh(&g_gps_speed_st, now);
	out->continuity_seq = g_gps_speed_st.continuity_seq;
	out->track_angle = (float)g_gps_track_tenths / 10.0f;
	if((out->heading_valid != 0U) &&
	   ((g_hpr_last_tick == 0U) ||
	    ((now - g_hpr_last_tick) > GPS_HPR_EXPIRE_MS))) {
		out->heading_valid = 0U;
	}
	taskEXIT_CRITICAL();

	out->speed_valid = view.valid;
	out->speed_reason = view.reason;
	out->speed_age_ms = (view.age_ms > 65535U) ? 65535U : (uint16_t)view.age_ms;
	out->speed_kmh = (view.valid != 0U) ? ((float)view.speed_centi / 100.0f) : 0.0f;
	if(position_fresh == 0U) out->valid = 0U;
}

void GPS_GetSpeedView(uint32_t max_age_ms, gps_speed_view_t * out)
{
	uint32_t now = HAL_GetTick();

	taskENTER_CRITICAL();
	GPS_Speed_GetView(&g_gps_speed_st, now, max_age_ms, out);
	taskEXIT_CRITICAL();
}

void GPS_GetStats(GPS_Stats_t * out)
{
	taskENTER_CRITICAL();
	*out = g_stats;
	out->tick_ms = HAL_GetTick();
	out->poll_gap_max_ms = g_gps_poll_gap_max_ms;
	out->late_poll = g_gps_dma_late_poll_count;
	out->rmc_period_ms = g_gps_speed_st.period_ms;
	out->fresh_limit_ms = GPS_Speed_FreshLimit(&g_gps_speed_st);
	taskEXIT_CRITICAL();
}

void GPS_WindowReset(void)
{
	taskENTER_CRITICAL();
	gps_window_clear(HAL_GetTick());
	taskEXIT_CRITICAL();
}

void GPS_GetWindow(GPS_Window_t * out)
{
	taskENTER_CRITICAL();
	*out = g_win;
	taskEXIT_CRITICAL();
	if(out->interval_min_ms == UINT32_MAX) out->interval_min_ms = 0U;
	if(out->epoch_min_ms == UINT32_MAX) out->epoch_min_ms = 0U;
	if(out->speed_min_centi == INT32_MAX) out->speed_min_centi = 0;
	if(out->speed_max_centi == INT32_MIN) out->speed_max_centi = 0;
}

uint32_t GPS_GetSpeedEvents(gps_speed_event_t * out, uint32_t max_events)
{
	uint32_t count;

	taskENTER_CRITICAL();
	count = (g_gps_speed_st.event_total < GPS_SPEED_EVENT_COUNT) ? g_gps_speed_st.event_total : GPS_SPEED_EVENT_COUNT;
	if(count > max_events) count = max_events;
	for(uint32_t i = 0U; i < count; i++) {
		uint32_t index = ((uint32_t)g_gps_speed_st.event_head + GPS_SPEED_EVENT_COUNT - 1U - i) %
		                 GPS_SPEED_EVENT_COUNT;
		out[i] = g_gps_speed_st.events[index];
	}
	taskEXIT_CRITICAL();
	return count;
}

uint8_t GPS_CaptureStart(uint32_t seconds)
{
	if((seconds == 0U) || (seconds > 21U)) return 0U;
	taskENTER_CRITICAL();
	g_capture_count = 0U;
	g_capture_end_tick = HAL_GetTick() + (seconds * 1000U);
	g_capture_state = GPS_CAPTURE_RUNNING;
	taskEXIT_CRITICAL();
	return 1U;
}

void GPS_CaptureStop(void)
{
	g_capture_state = GPS_CAPTURE_IDLE;
}

GPS_CaptureState_t GPS_CaptureGetState(void)
{
	return g_capture_state;
}

uint32_t GPS_CaptureCount(void)
{
	return g_capture_count;
}

uint8_t GPS_CaptureGet(uint32_t index, GPS_CaptureRec_t * out)
{
	if((index >= g_capture_count) || (out == NULL)) return 0U;
	*out = g_capture[index];
	return 1U;
}

void GPS_GetConfigStatus(GPS_CfgStatus_t * out)
{
	taskENTER_CRITICAL();
	*out = g_cfg;
	taskEXIT_CRITICAL();
}

uint8_t GPS_Request(GPS_Request_t request)
{
	uint8_t accepted = 0U;

	if(request == GPS_REQ_NONE) return 0U;
	taskENTER_CRITICAL();
	if(g_gps_request == GPS_REQ_NONE) {
		g_gps_request = request;
		accepted = 1U;
	}
	taskEXIT_CRITICAL();
	return accepted;
}

GPS_Request_t GPS_RequestPending(void)
{
	return g_gps_request;
}

uint32_t GPS_GetRawLines(char lines[][GPS_RAW_LINE_LEN], uint32_t max_lines)
{
	uint32_t count = (g_raw_count < max_lines) ? g_raw_count : max_lines;

	for(uint32_t i = 0U; i < count; i++) {
		(void)memcpy(lines[i], g_raw_lines[i], GPS_RAW_LINE_LEN);
	}
	return count;
}

uint32_t GPS_RawLinesDropped(void)
{
	return g_raw_dropped;
}

/* USER CODE END gps */
