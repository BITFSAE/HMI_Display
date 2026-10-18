/* USER CODE BEGIN gps_lap */
#include "gps_lap.h"
#include "main.h"
#include "gps.h"
#include "dashboard_ui.h"
#include <math.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
static StaticSemaphore_t g_lap_mutex_control;
static SemaphoreHandle_t g_lap_mutex;

/* Geometry and timer state are shared by GPS and the dashboard. Lock at API
 * boundaries; the mutex preserves IRQ latency and supports priority inheritance. */
void GPS_Lap_InitLock(void)
{
    if(g_lap_mutex == NULL) g_lap_mutex = xSemaphoreCreateRecursiveMutexStatic(&g_lap_mutex_control);
    configASSERT(g_lap_mutex != NULL);
}

static void GPS_Lap_SetStartLine_Impl(float lat, float lon, float track);
static void GPS_Lap_SetFinishLine_Impl(float lat, float lon, float track);
static uint8_t GPS_Lap_StartAtCurrent_Impl(const GPS_Data_t *data);
static void GPS_Lap_Reset_Impl(void);
static void GPS_Lap_SetAnalysisActive_Impl(uint8_t active);
static uint8_t GPS_Lap_IsAnalysisActive_Impl(void);
static void GPS_Lap_Tick_Impl(void);
static void GPS_Sprint_Reset_Impl(void);
static uint8_t GPS_Sprint_StartAtCurrent_Impl(const GPS_Data_t *data, float heading_deg);
static void GPS_Sprint_Cancel_Impl(void);
static uint8_t GPS_Sprint_IsActive_Impl(void);
static void GPS_Sprint_Tick_Impl(void);
static void GPS_SprintProcess_Impl(const GPS_Data_t *data);
static void GPS_Lap_GetDiagnostic_Impl(GPS_LapDiagnostic_t *out);
static void GPS_LapProcess_Impl(const GPS_Data_t *data);

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("Os")
#endif

#if GPS_LAP_ENABLE

#define GPS_LAP_EARTH_RADIUS_M       6371000.0f
#define GPS_LAP_GATE_HALF_RTK_FIXED_M 2.5f
#define GPS_LAP_GATE_HALF_RTK_FLOAT_M 3.0f
#define GPS_LAP_GATE_HALF_SINGLE_M     4.0f
#define GPS_LAP_LINE_ARM_DISTANCE_M  10.0f
#define GPS_LAP_APPROACH_DISTANCE_M  30.0f
#define GPS_LAP_DIAG_HOLD_MS         2000U
#define GPS_LAP_TRACK_SAMPLE_MAX     512U
#define GPS_LAP_SAMPLE_MIN_DIST_M    1.0f
#define GPS_LAP_SAMPLE_MAX_PERIOD_MS 500U
#define GPS_LAP_UI_UPDATE_PERIOD_MS  200U
#define GPS_LAP_REF_BACKTRACK_SEGMENTS 4U
#define GPS_LAP_REF_LOOKAHEAD_SEGMENTS 32U
#define GPS_LAP_REF_MAX_DISTANCE_DM  300.0f
#define GPS_LAP_MAX_LINE_DISTANCE_M  20000.0f
#define GPS_LAP_SAMPLE_MIN_DM        (-32768.0f)
#define GPS_LAP_SAMPLE_MAX_DM        32767.0f
#define GPS_LAP_ENABLE_GEOMETRY      1U
#define GPS_LAP_ENABLE_LIVE_DELTA    1U
#define GPS_SPRINT_DISTANCE_M         75.0f
#define GPS_SPRINT_STEP_MARGIN_M       8.0f
#define GPS_SPRINT_STEP_FACTOR         3.0f

typedef struct {
	int16_t x_dm;
	int16_t y_dm;
	uint16_t time_cs;
} GPS_LapSample_t;

typedef struct {
	float lat;
	float lon;
	float track;
	float forward_e;
	float forward_n;
	float lateral_e;
	float lateral_n;
	float longitude_scale;
} GPS_LapLine_t;

typedef struct {
	float forward_m;
	float lateral_m;
} GPS_LapLinePos_t;

static GPS_LapLine_t g_start_line;
static GPS_LapLine_t g_finish_line;
static uint8_t       g_start_set;
static uint8_t       g_finish_set;

static int32_t  g_lap_current_ms;
static int32_t  g_lap_last_ms;
static int32_t  g_lap_best_ms;
static int32_t  g_lap_delta_ms;
static int32_t  g_lap_count;
static uint32_t g_lap_start_tick;
static uint32_t g_lap_prev_tick;
static uint8_t  g_lap_active;
static uint8_t  g_lap_analysis_enabled;
static uint8_t  g_lap_finish_armed;
static uint8_t  g_lap_prev_valid;
static GPS_LapLinePos_t g_lap_prev_pos;
static GPS_LapSample_t g_lap_current_samples[GPS_LAP_TRACK_SAMPLE_MAX];
static GPS_LapSample_t g_lap_ref_samples[GPS_LAP_TRACK_SAMPLE_MAX];
static uint16_t g_lap_current_sample_count;
static uint16_t g_lap_ref_sample_count;
static uint8_t  g_lap_ref_valid;
static uint16_t g_lap_ref_match_index;
static uint8_t  g_lap_ref_match_valid;
static int32_t  g_lap_ref_match_time_ms;
static GPS_LapLinePos_t g_lap_last_sample_pos;
static uint32_t g_lap_last_sample_tick;
static uint32_t g_lap_ui_last_tick;
static GPS_LapDiagnostic_t g_lap_diag;
static uint32_t g_lap_diag_hold_until;
static GPS_LapLine_t g_sprint_start_line;
static volatile uint8_t g_sprint_active;
static volatile uint8_t g_sprint_started;
static float g_sprint_progress_m;
static float g_sprint_prev_forward_m;
static GPS_LapLinePos_t g_sprint_prev_pos;
static uint32_t g_sprint_prev_tick;
static float g_sprint_ready_prev_speed_kmh;
static float g_sprint_ready_prev_latitude;
static float g_sprint_ready_prev_longitude;
static uint32_t g_sprint_ready_prev_tick;
static uint8_t g_sprint_ready_prev_valid;
/* 数据连续性：GPS_Data_t.continuity_seq 变化说明速度/位置曾失效又恢复。中断期间的旧位置不得
 * 再用于过线判定或插值，恢复后的第一个样本只重建锚点，并标记受影响的计时结果。 */
static uint32_t g_lap_seen_seq;
static uint8_t  g_lap_gap_flag;
static uint32_t g_sprint_seen_seq;
static uint8_t  g_sprint_gap_flag;

static float deg_to_radf32(float deg)
{
	return deg * 0.01745329251994329577f;
}

static float absf32(float v)
{
	return v < 0.0f ? -v : v;
}

static uint8_t finite_f32(float v)
{
	return (uint8_t)((v == v) && (v <= 3.4e38f) && (v >= -3.4e38f));
}

static uint8_t coord_valid(float lat, float lon)
{
	if((finite_f32(lat) == 0U) || (finite_f32(lon) == 0U)) return 0U;
	if((lat < -90.0f) || (lat > 90.0f)) return 0U;
	if((lon < -180.0f) || (lon > 180.0f)) return 0U;
	if((absf32(lat) < 0.000001f) && (absf32(lon) < 0.000001f)) return 0U;
	return 1U;
}

static uint8_t line_pos_valid(const GPS_LapLinePos_t * pos)
{
	if(pos == NULL) return 0U;
	if((finite_f32(pos->forward_m) == 0U) || (finite_f32(pos->lateral_m) == 0U)) return 0U;
	if(absf32(pos->forward_m) > GPS_LAP_MAX_LINE_DISTANCE_M) return 0U;
	if(absf32(pos->lateral_m) > GPS_LAP_MAX_LINE_DISTANCE_M) return 0U;
	return 1U;
}

static void submit_lap_times_limited(uint32_t now, uint8_t force)
{
	if((force == 0U) && ((now - g_lap_ui_last_tick) < GPS_LAP_UI_UPDATE_PERIOD_MS)) return;
	g_lap_ui_last_tick = now;
	Dashboard_UI_SubmitLapTimes(g_lap_current_ms / 10,
								g_lap_last_ms / 10,
								g_lap_best_ms / 10,
								g_lap_count);
}

static void update_line_vectors(GPS_LapLine_t * line)
{
	float heading;
	if((finite_f32(line->track) == 0U) || (line->track < 0.0f) || (line->track >= 360.0f)) {
		line->track = 0.0f;
	}
	heading = deg_to_radf32((float)line->track);
	line->forward_e = sinf(heading);
	line->forward_n = cosf(heading);
	line->lateral_e = line->forward_n;
	line->lateral_n = -line->forward_e;
	line->longitude_scale = cosf(deg_to_radf32(line->lat));
}

static GPS_LapLinePos_t project_to_line(const GPS_LapLine_t * line,
										float lat, float lon)
{
	GPS_LapLinePos_t pos;
	float north_m = deg_to_radf32(lat - line->lat) * GPS_LAP_EARTH_RADIUS_M;
	float east_m = deg_to_radf32(lon - line->lon) * GPS_LAP_EARTH_RADIUS_M *
					  line->longitude_scale;

	pos.forward_m = east_m * line->forward_e + north_m * line->forward_n;
	pos.lateral_m = east_m * line->lateral_e + north_m * line->lateral_n;
	return pos;
}

#if GPS_LAP_ENABLE_GEOMETRY
static uint8_t finish_crossing(const GPS_LapLinePos_t * prev,
								 const GPS_LapLinePos_t * curr,
								 float * ratio_out,
								 float * lateral_out)
{
	float denom;
	float ratio;
	float lateral;

	if((prev->forward_m >= 0.0f) || (curr->forward_m < 0.0f)) return 0U;
	denom = curr->forward_m - prev->forward_m;
	if(denom <= 0.001f) return 0U;

	ratio = -prev->forward_m / denom;
	if((ratio < 0.0f) || (ratio > 1.0f)) return 0U;
	lateral = prev->lateral_m + ((curr->lateral_m - prev->lateral_m) * ratio);
	*ratio_out = ratio;
	*lateral_out = lateral;
	return 1U;
}

static uint8_t reverse_crossing(const GPS_LapLinePos_t * prev,
								  const GPS_LapLinePos_t * curr,
								  float * ratio_out,
								  float * lateral_out)
{
	float denom;
	float ratio;

	if((prev->forward_m <= 0.0f) || (curr->forward_m > 0.0f)) return 0U;
	denom = prev->forward_m - curr->forward_m;
	if(denom <= 0.001f) return 0U;
	ratio = prev->forward_m / denom;
	if((ratio < 0.0f) || (ratio > 1.0f)) return 0U;
	*ratio_out = ratio;
	*lateral_out = prev->lateral_m + ((curr->lateral_m - prev->lateral_m) * ratio);
	return 1U;
}
#endif

static float gate_half_width_for_fix(uint8_t fix_quality)
{
	if(fix_quality == 4U) return GPS_LAP_GATE_HALF_RTK_FIXED_M;
	if((fix_quality == 2U) || (fix_quality == 5U)) return GPS_LAP_GATE_HALF_RTK_FLOAT_M;
	return GPS_LAP_GATE_HALF_SINGLE_M;
}

static uint8_t diag_hold_active(uint32_t now)
{
	return (uint8_t)(((int32_t)(g_lap_diag_hold_until - now)) > 0);
}

static void update_diagnostic(GPS_LapDiagState_t state,
							  const GPS_LapLinePos_t * pos,
							  const GPS_Data_t * data)
{
	if(pos != NULL) {
		g_lap_diag.forward_m = pos->forward_m;
		g_lap_diag.lateral_m = pos->lateral_m;
	}
	if(data != NULL) {
		g_lap_diag.fix_quality = data->fix_quality;
		g_lap_diag.gate_half_width_m = gate_half_width_for_fix(data->fix_quality);
	}
	g_lap_diag.sample_count = g_lap_current_sample_count;
	g_lap_diag.state = state;
}

static int16_t clamp_dm_f32(float meter)
{
	float dm = meter * 10.0f;
	if(finite_f32(dm) == 0U) return 0;
	if(dm > GPS_LAP_SAMPLE_MAX_DM) return 32767;
	if(dm < GPS_LAP_SAMPLE_MIN_DM) return -32768;
	return (int16_t)(dm + (dm >= 0.0f ? 0.5f : -0.5f));
}

static uint16_t clamp_time_cs(int32_t ms)
{
	int32_t cs = ms / 10;
	if(cs < 0) return 0U;
	if(cs > 65535) return 65535U;
	return (uint16_t)cs;
}

static GPS_LapSample_t make_sample(const GPS_LapLinePos_t * pos, int32_t time_ms)
{
	GPS_LapSample_t sample;
	sample.x_dm = clamp_dm_f32(pos->forward_m);
	sample.y_dm = clamp_dm_f32(pos->lateral_m);
	sample.time_cs = clamp_time_cs(time_ms);
	return sample;
}

static void clear_current_samples(void)
{
	g_lap_current_sample_count = 0U;
	g_lap_last_sample_tick = 0U;
}

static void compact_current_samples(void)
{
	uint16_t source;
	uint16_t target = 0U;
	uint16_t last_index;
	uint16_t last_copied = 0U;

	if(g_lap_current_sample_count < GPS_LAP_TRACK_SAMPLE_MAX) return;
	last_index = g_lap_current_sample_count - 1U;
	for(source = 0U; source < g_lap_current_sample_count; source += 2U) {
		g_lap_current_samples[target++] = g_lap_current_samples[source];
		last_copied = source;
	}
	if(last_copied != last_index) {
		g_lap_current_samples[target++] = g_lap_current_samples[last_index];
	}
	g_lap_current_sample_count = target;
}

static void add_current_sample(const GPS_LapLinePos_t * pos,
							   uint32_t tick,
							   int32_t time_ms,
							   uint8_t force)
{
	float dx;
	float dy;

	if(g_lap_current_sample_count >= GPS_LAP_TRACK_SAMPLE_MAX) {
		compact_current_samples();
	}
	if(g_lap_current_sample_count >= GPS_LAP_TRACK_SAMPLE_MAX) return;
	if((force == 0U) && (g_lap_current_sample_count > 0U)) {
		dx = pos->forward_m - g_lap_last_sample_pos.forward_m;
		dy = pos->lateral_m - g_lap_last_sample_pos.lateral_m;
		if(((dx * dx + dy * dy) < (GPS_LAP_SAMPLE_MIN_DIST_M * GPS_LAP_SAMPLE_MIN_DIST_M)) &&
		   ((tick - g_lap_last_sample_tick) < GPS_LAP_SAMPLE_MAX_PERIOD_MS)) {
			return;
		}
	}

	g_lap_current_samples[g_lap_current_sample_count] = make_sample(pos, time_ms);
	g_lap_current_sample_count++;
	g_lap_last_sample_pos = *pos;
	g_lap_last_sample_tick = tick;
}

#if GPS_LAP_ENABLE_GEOMETRY
static void archive_current_samples(void)
{
	if(g_lap_current_sample_count < 2U) {
		g_lap_ref_sample_count = 0U;
		g_lap_ref_valid = 0U;
		return;
	}
	memcpy(g_lap_ref_samples,
	       g_lap_current_samples,
	       (size_t)g_lap_current_sample_count * sizeof(g_lap_current_samples[0]));
	g_lap_ref_sample_count = g_lap_current_sample_count;
	g_lap_ref_valid = 1U;
}
#endif

#if GPS_LAP_ENABLE_LIVE_DELTA
static uint8_t interpolate_ref_time(const GPS_LapLinePos_t * pos, int32_t * time_ms_out)
{
	float px = pos->forward_m * 10.0f;
	float py = pos->lateral_m * 10.0f;
	float best_dist2 = 3.4e38f;
	float best_time_cs = 0.0f;
	uint16_t best_index = 1U;
	uint16_t start_index = 1U;
	uint16_t end_index;
	int32_t match_time_ms;

	if((g_lap_ref_valid == 0U) || (g_lap_ref_sample_count < 2U) || (time_ms_out == NULL)) {
		return 0U;
	}
	end_index = g_lap_ref_sample_count;
	if(g_lap_ref_match_valid != 0U) {
		start_index = (g_lap_ref_match_index > GPS_LAP_REF_BACKTRACK_SEGMENTS) ?
					  (g_lap_ref_match_index - GPS_LAP_REF_BACKTRACK_SEGMENTS) : 1U;
		end_index = g_lap_ref_match_index + GPS_LAP_REF_LOOKAHEAD_SEGMENTS + 1U;
		if(end_index > g_lap_ref_sample_count) end_index = g_lap_ref_sample_count;
	}

	for(uint16_t i = start_index; i < end_index; i++) {
		float ax = (float)g_lap_ref_samples[i - 1U].x_dm;
		float ay = (float)g_lap_ref_samples[i - 1U].y_dm;
		float bx = (float)g_lap_ref_samples[i].x_dm;
		float by = (float)g_lap_ref_samples[i].y_dm;
		float vx = bx - ax;
		float vy = by - ay;
		float seg_len2 = (vx * vx) + (vy * vy);
		float t = 0.0f;
		float qx;
		float qy;
		float dx;
		float dy;
		float dist2;
		if(seg_len2 > 0.001f) {
			t = (((px - ax) * vx) + ((py - ay) * vy)) / seg_len2;
			if(t < 0.0f) t = 0.0f;
			if(t > 1.0f) t = 1.0f;
		}
		qx = ax + (vx * t);
		qy = ay + (vy * t);
		dx = px - qx;
		dy = py - qy;
		dist2 = (dx * dx) + (dy * dy);
		if(dist2 < best_dist2) {
			best_dist2 = dist2;
			best_index = i;
			best_time_cs = (float)g_lap_ref_samples[i - 1U].time_cs +
						   (((float)g_lap_ref_samples[i].time_cs -
							 (float)g_lap_ref_samples[i - 1U].time_cs) * t);
		}
	}
	if(best_dist2 > (GPS_LAP_REF_MAX_DISTANCE_DM * GPS_LAP_REF_MAX_DISTANCE_DM)) {
		return 0U;
	}

	match_time_ms = (int32_t)((best_time_cs * 10.0f) + 0.5f);
	if((g_lap_ref_match_valid != 0U) && (match_time_ms < g_lap_ref_match_time_ms)) {
		*time_ms_out = g_lap_ref_match_time_ms;
		return 1U;
	}
	g_lap_ref_match_index = best_index;
	g_lap_ref_match_time_ms = match_time_ms;
	g_lap_ref_match_valid = 1U;
	*time_ms_out = match_time_ms;
	return 1U;
}

static void update_live_delta(const GPS_LapLinePos_t * pos, int32_t current_ms)
{
	int32_t ref_ms;
	if(interpolate_ref_time(pos, &ref_ms) == 0U) return;
	g_lap_delta_ms = current_ms - ref_ms;
	Dashboard_UI_SubmitLapDelta(g_lap_delta_ms / 10);
}
#endif

static void GPS_Lap_SetStartLine_Impl(float lat, float lon, float track)
{
	if(coord_valid(lat, lon) == 0U) {
		g_start_set = 0U;
		return;
	}
	g_start_line.lat = lat;
	g_start_line.lon = lon;
	g_start_line.track = track;
	update_line_vectors(&g_start_line);
	g_start_set = 1U;
}

static void GPS_Lap_SetFinishLine_Impl(float lat, float lon, float track)
{
	if(coord_valid(lat, lon) == 0U) {
		g_finish_set = 0U;
		return;
	}
	g_finish_line.lat = lat;
	g_finish_line.lon = lon;
	g_finish_line.track = track;
	update_line_vectors(&g_finish_line);
	g_finish_set = 1U;
}

static uint8_t GPS_Lap_StartAtCurrent_Impl(const GPS_Data_t * data)
{
	if((data == NULL) || (data->valid == 0U) || !g_finish_set) return 0U;
	if(coord_valid(data->latitude, data->longitude) == 0U) return 0U;
	g_lap_active = 1U;
	g_lap_seen_seq = data->continuity_seq;
	g_lap_gap_flag = 0U;
	g_lap_start_tick = HAL_GetTick();
	g_lap_prev_tick = g_lap_start_tick;
	g_lap_current_ms = 0;
	g_lap_finish_armed = 0U;
	g_lap_prev_pos = project_to_line(&g_finish_line, data->latitude, data->longitude);
	if(line_pos_valid(&g_lap_prev_pos) == 0U) {
		g_lap_active = 0U;
		g_lap_prev_valid = 0U;
		return 0U;
	}
	g_lap_prev_valid = 1U;
	clear_current_samples();
	add_current_sample(&g_lap_prev_pos, g_lap_start_tick, 0, 1U);
	g_lap_diag_hold_until = 0U;
	update_diagnostic(GPS_LAP_DIAG_WAIT_ARM, &g_lap_prev_pos, data);
	submit_lap_times_limited(g_lap_start_tick, 1U);
	return 1U;
}

static void GPS_Lap_Reset_Impl(void)
{
	g_lap_current_ms = 0;
	g_lap_last_ms = 0;
	g_lap_best_ms = 0;
	g_lap_delta_ms = 0;
	g_lap_count = 0;
	g_lap_active = 0U;
	g_lap_start_tick = 0;
	g_lap_prev_tick = 0;
	g_lap_finish_armed = 0U;
	g_lap_prev_valid = 0U;
	g_lap_gap_flag = 0U;
	g_sprint_gap_flag = 0U;
	clear_current_samples();
	g_lap_ref_sample_count = 0U;
	g_lap_ref_valid = 0U;
	g_lap_ref_match_index = 1U;
	g_lap_ref_match_valid = 0U;
	g_lap_ref_match_time_ms = 0;
	g_lap_ui_last_tick = 0U;
	g_lap_diag_hold_until = 0U;
	(void)memset(&g_lap_diag, 0, sizeof(g_lap_diag));
	g_lap_diag.state = GPS_LAP_DIAG_INACTIVE;
	g_sprint_active = 0U;
	g_sprint_started = 0U;
	g_sprint_progress_m = 0.0f;
	g_sprint_prev_forward_m = 0.0f;
	(void)memset(&g_sprint_prev_pos, 0, sizeof(g_sprint_prev_pos));
	g_sprint_prev_tick = 0U;
	g_sprint_ready_prev_speed_kmh = 0.0f;
	g_sprint_ready_prev_latitude = 0.0f;
	g_sprint_ready_prev_longitude = 0.0f;
	g_sprint_ready_prev_tick = 0U;
	g_sprint_ready_prev_valid = 0U;
	Dashboard_UI_SetSprintReady(0U);
}

static void GPS_Lap_SetAnalysisActive_Impl(uint8_t active)
{
	g_lap_analysis_enabled = active != 0U ? 1U : 0U;
	if(g_lap_analysis_enabled != 0U) {
		g_sprint_active = 0U;
		g_sprint_started = 0U;
		g_sprint_ready_prev_valid = 0U;
		Dashboard_UI_SetSprintReady(0U);
	}
	if(g_lap_analysis_enabled == 0U) {
		g_lap_active = 0U;
		g_lap_start_tick = 0U;
		g_lap_prev_tick = 0U;
		g_lap_finish_armed = 0U;
		g_lap_prev_valid = 0U;
		clear_current_samples();
		g_lap_ref_match_index = 1U;
		g_lap_ref_match_valid = 0U;
		g_lap_ref_match_time_ms = 0;
		g_lap_diag_hold_until = 0U;
		g_lap_diag.state = GPS_LAP_DIAG_INACTIVE;
	}
}

static uint8_t GPS_Lap_IsAnalysisActive_Impl(void)
{
	return g_lap_analysis_enabled;
}

static void GPS_Lap_Tick_Impl(void)
{
	uint32_t now;

	if((g_lap_analysis_enabled == 0U) || (g_lap_active == 0U)) return;

	now = HAL_GetTick();
	g_lap_current_ms = (int32_t)(now - g_lap_start_tick);
	submit_lap_times_limited(now, 0U);
}

static void GPS_Sprint_Reset_Impl(void)
{
	GPS_Lap_Reset_Impl();
	Dashboard_UI_SubmitSprintRemaining((int32_t)GPS_SPRINT_DISTANCE_M);
}

static uint8_t GPS_Sprint_StartAtCurrent_Impl(const GPS_Data_t * data, float heading_deg)
{
	GPS_LapLinePos_t start_pos;
	uint32_t now;

	if((data == NULL) || (data->valid == 0U)) return 0U;
	if(coord_valid(data->latitude, data->longitude) == 0U) return 0U;
	/* 速度失效时 speed_kmh 是 0，但不代表停着：没有可信速度不能进入准备状态。 */
	if((data->speed_valid == 0U) || (finite_f32(data->speed_kmh) == 0U) ||
	   (data->speed_kmh >= GPS_SPRINT_START_SPEED_KMH)) return 0U;
	if((finite_f32(heading_deg) == 0U) || (heading_deg < 0.0f) ||
	   (heading_deg >= 360.0f)) return 0U;

	g_lap_active = 0U;
	g_lap_analysis_enabled = 0U;
	g_sprint_start_line.lat = data->latitude;
	g_sprint_start_line.lon = data->longitude;
	g_sprint_start_line.track = heading_deg;
	update_line_vectors(&g_sprint_start_line);
	start_pos = project_to_line(&g_sprint_start_line, data->latitude, data->longitude);
	if(line_pos_valid(&start_pos) == 0U) return 0U;

	now = HAL_GetTick();
	g_sprint_active = 1U;
	g_sprint_started = 0U;
	g_sprint_seen_seq = data->continuity_seq;
	g_sprint_gap_flag = 0U;
	g_sprint_progress_m = 0.0f;
	g_sprint_prev_forward_m = start_pos.forward_m;
	g_sprint_prev_pos = start_pos;
	g_sprint_prev_tick = now;
	g_sprint_ready_prev_speed_kmh = data->speed_kmh;
	g_sprint_ready_prev_latitude = data->latitude;
	g_sprint_ready_prev_longitude = data->longitude;
	g_sprint_ready_prev_tick = now;
	g_sprint_ready_prev_valid = 1U;
	g_lap_start_tick = 0U;
	g_lap_prev_tick = 0U;
	g_lap_current_ms = 0;
	g_lap_delta_ms = 0;
	g_lap_ui_last_tick = 0U;
	g_lap_diag_hold_until = 0U;
	update_diagnostic(GPS_LAP_DIAG_ARMED, &start_pos, data);
	Dashboard_UI_SubmitLapTimes(0,
								g_lap_last_ms / 10,
								g_lap_best_ms / 10,
								g_lap_count);
	Dashboard_UI_SubmitLapDelta(0);
	Dashboard_UI_SubmitSprintRemaining((int32_t)GPS_SPRINT_DISTANCE_M);
	Dashboard_UI_SetSprintReady(1U);
	return 1U;
}

static void GPS_Sprint_Cancel_Impl(void)
{
	if(g_sprint_active == 0U) return;
	g_sprint_active = 0U;
	g_sprint_started = 0U;
	g_sprint_gap_flag = 0U;
	g_lap_current_ms = 0;
	g_lap_start_tick = 0U;
	g_sprint_progress_m = 0.0f;
	g_sprint_prev_forward_m = 0.0f;
	(void)memset(&g_sprint_prev_pos, 0, sizeof(g_sprint_prev_pos));
	g_sprint_prev_tick = 0U;
	g_sprint_ready_prev_valid = 0U;
	g_lap_diag.state = GPS_LAP_DIAG_INACTIVE;
	Dashboard_UI_SubmitLapTimes(0,
								g_lap_last_ms / 10,
								g_lap_best_ms / 10,
								g_lap_count);
	Dashboard_UI_SubmitSprintRemaining((int32_t)GPS_SPRINT_DISTANCE_M);
	Dashboard_UI_SetSprintReady(0U);
}

static uint8_t GPS_Sprint_IsActive_Impl(void)
{
	return g_sprint_active;
}

static void GPS_Sprint_Tick_Impl(void)
{
	uint32_t now;
	if((g_sprint_active == 0U) || (g_sprint_started == 0U)) return;
	now = HAL_GetTick();
	g_lap_current_ms = (int32_t)(now - g_lap_start_tick);
	submit_lap_times_limited(now, 0U);
}

static void GPS_SprintProcess_Impl(const GPS_Data_t * data)
{
	GPS_LapLinePos_t curr_pos;
	GPS_LapLinePos_t finish_pos;
	GPS_LapLinePos_t launch_pos;
	float curr_forward;
	float step_dx;
	float step_dy;
	float step_m;
	float expected_step_m;
	float max_step_m;
	float ratio;
	float denominator;
	float launch_latitude;
	float launch_longitude;
	uint32_t now;
	uint32_t launch_tick;
	uint32_t finish_tick;
	int32_t reference_ms;
	int32_t remaining_m;
	uint8_t gap;

	if(g_sprint_active == 0U) return;
	if((data == NULL) || (data->valid == 0U)) return;
	if(coord_valid(data->latitude, data->longitude) == 0U) return;
	if((data->speed_valid == 0U) || (finite_f32(data->speed_kmh) == 0U) ||
	   (data->speed_kmh < 0.0f)) return;

	now = HAL_GetTick();
	gap = (uint8_t)(data->continuity_seq != g_sprint_seen_seq);
	g_sprint_seen_seq = data->continuity_seq;
	if(g_sprint_started == 0U) {
		if(gap != 0U) {
			if(data->speed_kmh > GPS_SPRINT_START_SPEED_KMH) {
				/* 起步发生在数据中断期间：起步位置和时刻无法确定，这次冲刺作废。 */
				GPS_Sprint_Cancel_Impl();
				Dashboard_UI_PushAlert("gps lost, run void");
				return;
			}
			g_sprint_ready_prev_valid = 0U;   /* 丢弃中断前的样本，只重建锚点 */
		}
		if((g_sprint_ready_prev_valid == 0U) ||
		   (data->speed_kmh <= GPS_SPRINT_START_SPEED_KMH)) {
			g_sprint_ready_prev_speed_kmh = data->speed_kmh;
			g_sprint_ready_prev_latitude = data->latitude;
			g_sprint_ready_prev_longitude = data->longitude;
			g_sprint_ready_prev_tick = now;
			g_sprint_ready_prev_valid = 1U;
			curr_pos = project_to_line(&g_sprint_start_line,
								   data->latitude,
								   data->longitude);
			if(line_pos_valid(&curr_pos) != 0U) {
				update_diagnostic(GPS_LAP_DIAG_ARMED, &curr_pos, data);
			}
			return;
		}

		/* Interpolate both the launch position and tick at the first crossing of
		 * the movement threshold. This removes almost all of the GNSS sample
		 * period from the measured 75 m time. */
		denominator = data->speed_kmh - g_sprint_ready_prev_speed_kmh;
		ratio = (denominator > 0.001f) ?
				((GPS_SPRINT_START_SPEED_KMH - g_sprint_ready_prev_speed_kmh) /
				 denominator) : 1.0f;
		if(ratio < 0.0f) ratio = 0.0f;
		if(ratio > 1.0f) ratio = 1.0f;
		launch_latitude = g_sprint_ready_prev_latitude +
						  ((data->latitude - g_sprint_ready_prev_latitude) * ratio);
		launch_longitude = g_sprint_ready_prev_longitude +
						   ((data->longitude - g_sprint_ready_prev_longitude) * ratio);
		launch_tick = g_sprint_ready_prev_tick +
					  (uint32_t)(((float)(now - g_sprint_ready_prev_tick) * ratio) + 0.5f);
		if(coord_valid(launch_latitude, launch_longitude) == 0U) return;

		g_sprint_start_line.lat = launch_latitude;
		g_sprint_start_line.lon = launch_longitude;
		update_line_vectors(&g_sprint_start_line);
		launch_pos = project_to_line(&g_sprint_start_line, launch_latitude, launch_longitude);
		curr_pos = project_to_line(&g_sprint_start_line, data->latitude, data->longitude);
		if((line_pos_valid(&launch_pos) == 0U) || (line_pos_valid(&curr_pos) == 0U)) return;

		g_sprint_ready_prev_valid = 0U;
		g_sprint_progress_m = 0.0f;
		g_sprint_prev_forward_m = launch_pos.forward_m;
		g_sprint_prev_pos = launch_pos;
		g_sprint_prev_tick = launch_tick;
		g_lap_start_tick = launch_tick;
		g_lap_prev_tick = launch_tick;
		g_lap_current_ms = (int32_t)(now - launch_tick);
		/* Publish RUNNING only after every timestamp and position anchor is valid. */
		g_sprint_started = 1U;
		Dashboard_UI_SetSprintReady(0U);
	}
	else {
		curr_pos = project_to_line(&g_sprint_start_line, data->latitude, data->longitude);
		if(line_pos_valid(&curr_pos) == 0U) return;
		if(gap != 0U) {
			g_sprint_gap_flag = 1U;
			if(curr_pos.forward_m >= GPS_SPRINT_DISTANCE_M) {
				/* 终点在中断期间越过：不能用中断前的旧位置插值出完成时刻。 */
				GPS_Sprint_Cancel_Impl();
				Dashboard_UI_PushAlert("gps lost, run void");
				return;
			}
			/* 以恢复后的第一个样本重建锚点，位移合理性检查不跨中断。 */
			g_sprint_prev_forward_m = curr_pos.forward_m;
			g_sprint_prev_pos = curr_pos;
			g_sprint_prev_tick = now;
		}
	}
	step_dx = curr_pos.forward_m - g_sprint_prev_pos.forward_m;
	step_dy = curr_pos.lateral_m - g_sprint_prev_pos.lateral_m;
	step_m = sqrtf((step_dx * step_dx) + (step_dy * step_dy));
	expected_step_m = data->speed_kmh * (float)(now - g_sprint_prev_tick) / 3600.0f;
	if(expected_step_m < 0.0f) expected_step_m = 0.0f;
	max_step_m = GPS_SPRINT_STEP_MARGIN_M +
				 (expected_step_m * GPS_SPRINT_STEP_FACTOR);
	/* Reject an isolated valid-status position jump instead of allowing it to
	 * complete the 75 m run. The accepted anchor is retained for recovery. */
	if((finite_f32(step_m) == 0U) || (step_m > max_step_m)) return;
	curr_forward = curr_pos.forward_m;
	if(curr_forward > g_sprint_progress_m) g_sprint_progress_m = curr_forward;
	if(g_sprint_progress_m < 0.0f) g_sprint_progress_m = 0.0f;
	if(g_sprint_progress_m > GPS_SPRINT_DISTANCE_M) {
		g_sprint_progress_m = GPS_SPRINT_DISTANCE_M;
	}
	remaining_m = (int32_t)(GPS_SPRINT_DISTANCE_M - g_sprint_progress_m + 0.999f);
	if(remaining_m < 0) remaining_m = 0;
	if(remaining_m > (int32_t)GPS_SPRINT_DISTANCE_M) {
		remaining_m = (int32_t)GPS_SPRINT_DISTANCE_M;
	}
	Dashboard_UI_SubmitSprintRemaining(remaining_m);

	if((g_sprint_prev_forward_m < GPS_SPRINT_DISTANCE_M) &&
	   (curr_forward >= GPS_SPRINT_DISTANCE_M)) {
		denominator = curr_forward - g_sprint_prev_forward_m;
		ratio = (denominator > 0.001f) ?
				((GPS_SPRINT_DISTANCE_M - g_sprint_prev_forward_m) / denominator) : 1.0f;
		if(ratio < 0.0f) ratio = 0.0f;
		if(ratio > 1.0f) ratio = 1.0f;
		finish_tick = g_sprint_prev_tick +
					  (uint32_t)(((float)(now - g_sprint_prev_tick) * ratio) + 0.5f);
		g_lap_current_ms = (int32_t)(finish_tick - g_lap_start_tick);
		reference_ms = g_lap_best_ms;
		g_lap_delta_ms = (reference_ms > 0) ? (g_lap_current_ms - reference_ms) : 0;
		g_lap_last_ms = g_lap_current_ms;
		if((g_lap_best_ms == 0) || (g_lap_current_ms < g_lap_best_ms)) {
			g_lap_best_ms = g_lap_current_ms;
		}
		g_lap_count++;
		g_sprint_active = 0U;
		g_sprint_started = 0U;
		g_sprint_progress_m = GPS_SPRINT_DISTANCE_M;
		finish_pos = curr_pos;
		finish_pos.forward_m = GPS_SPRINT_DISTANCE_M;
		update_diagnostic(GPS_LAP_DIAG_CROSSED, &finish_pos, data);
		Dashboard_UI_SubmitLapTimes(g_lap_current_ms / 10,
									g_lap_last_ms / 10,
									g_lap_best_ms / 10,
									g_lap_count);
		Dashboard_UI_SubmitLapDelta(g_lap_delta_ms / 10);
		Dashboard_UI_SubmitSprintRemaining(0);
		/* 途中有过数据中断：起点和终点插值都用的是中断前后的有效样本，但中间路径未被观测。 */
		Dashboard_UI_PushAlert((g_sprint_gap_flag != 0U) ? "75m done (gps gap)" : "75m run complete");
		g_sprint_gap_flag = 0U;
		return;
	}

	g_lap_current_ms = (int32_t)(now - g_lap_start_tick);
	update_diagnostic(GPS_LAP_DIAG_ARMED, &curr_pos, data);
	submit_lap_times_limited(now, 0U);
	g_sprint_prev_forward_m = curr_forward;
	g_sprint_prev_pos = curr_pos;
	g_sprint_prev_tick = now;
}

static void GPS_Lap_GetDiagnostic_Impl(GPS_LapDiagnostic_t * out)
{
	uint32_t primask;
	if(out == NULL) return;
	primask = __get_PRIMASK();
	__disable_irq();
	*out = g_lap_diag;
	if(primask == 0U) __enable_irq();
}

static void GPS_LapProcess_Impl(const GPS_Data_t * data)
{
#if GPS_LAP_ENABLE_GEOMETRY
	GPS_LapLinePos_t curr_pos;
	GPS_LapLinePos_t cross_pos;
	float ratio;
	float cross_lateral = 0.0f;
	float gate_half_width;
	uint32_t cross_tick;
	int32_t reference_lap_ms;
	uint8_t new_best_lap;
	uint8_t lap_crossed = 0U;
#endif
	uint32_t now;

	if(!g_start_set || !g_finish_set) return;
	if(g_lap_analysis_enabled == 0U) return;
	if(data == NULL) return;
	if(data->valid == 0U || coord_valid(data->latitude, data->longitude) == 0U) {
		/* 计时中定位失效或坐标非法：丢弃旧锚点，恢复后不跨这段插值，这一圈标记为不可信。 */
		if(g_lap_active != 0U) {
			g_lap_prev_valid = 0U;
			g_lap_gap_flag = 1U;
		}
		return;
	}

	if(!g_lap_active) return;

	now = HAL_GetTick();
	if(data->continuity_seq != g_lap_seen_seq) {
		/* 数据曾中断：旧锚点作废，本样本只重建锚点；这一圈的用时标记为不可信。 */
		g_lap_seen_seq = data->continuity_seq;
		g_lap_prev_valid = 0U;
		g_lap_gap_flag = 1U;
	}
#if GPS_LAP_ENABLE_GEOMETRY == 0U
	g_lap_current_ms = (int32_t)(now - g_lap_start_tick);
	submit_lap_times_limited(now, 0U);
	return;
#endif
#if GPS_LAP_ENABLE_GEOMETRY
	curr_pos = project_to_line(&g_finish_line, data->latitude, data->longitude);
	if(line_pos_valid(&curr_pos) == 0U) {
		g_lap_prev_valid = 0U;
		return;
	}
	g_lap_current_ms = (int32_t)(now - g_lap_start_tick);
	gate_half_width = gate_half_width_for_fix(data->fix_quality);
	update_diagnostic(g_lap_diag.state, &curr_pos, data);

	if(absf32(curr_pos.forward_m) >= GPS_LAP_LINE_ARM_DISTANCE_M) {
		g_lap_finish_armed = 1U;
	}

	if((g_lap_finish_armed != 0U) && (g_lap_prev_valid != 0U)) {
		if(finish_crossing(&g_lap_prev_pos, &curr_pos, &ratio, &cross_lateral) != 0U) {
			cross_pos.forward_m = 0.0f;
			cross_pos.lateral_m = cross_lateral;
			if(absf32(cross_lateral) <= gate_half_width) {
				lap_crossed = 1U;
			} else {
				g_lap_diag_hold_until = now + GPS_LAP_DIAG_HOLD_MS;
				update_diagnostic(GPS_LAP_DIAG_GATE_MISS, &cross_pos, data);
			}
		} else if(reverse_crossing(&g_lap_prev_pos, &curr_pos, &ratio, &cross_lateral) != 0U) {
			cross_pos.forward_m = 0.0f;
			cross_pos.lateral_m = cross_lateral;
			if(absf32(cross_lateral) <= gate_half_width) {
				g_lap_diag_hold_until = now + GPS_LAP_DIAG_HOLD_MS;
				update_diagnostic(GPS_LAP_DIAG_REVERSE_PASS, &cross_pos, data);
			}
		}
	}

	if(lap_crossed != 0U) {
		cross_tick = g_lap_prev_tick +
					 (uint32_t)(((float)(now - g_lap_prev_tick) * ratio) + 0.5f);
		cross_pos.forward_m = g_lap_prev_pos.forward_m +
							  ((curr_pos.forward_m - g_lap_prev_pos.forward_m) * ratio);
		cross_pos.lateral_m = g_lap_prev_pos.lateral_m +
							 ((curr_pos.lateral_m - g_lap_prev_pos.lateral_m) * ratio);
		g_lap_current_ms = (int32_t)(cross_tick - g_lap_start_tick);
		add_current_sample(&cross_pos, cross_tick, g_lap_current_ms, 1U);

		reference_lap_ms = g_lap_best_ms;
		/* 本圈内有过数据中断：过线可能发生在中断期间而被漏掉，这一圈的用时不可信，
		 * 只显示为上一圈，不参与最快圈、参考轨迹和圈差。 */
		new_best_lap = (uint8_t)((g_lap_gap_flag == 0U) &&
								 ((reference_lap_ms == 0) ||
								  (g_lap_current_ms < reference_lap_ms)));
		g_lap_delta_ms = ((reference_lap_ms > 0) && (g_lap_gap_flag == 0U)) ?
						 (g_lap_current_ms - reference_lap_ms) : 0;
		g_lap_last_ms = g_lap_current_ms;
		if(new_best_lap != 0U) {
			g_lap_best_ms = g_lap_current_ms;
			archive_current_samples();
		}
		if(g_lap_gap_flag != 0U) {
			g_lap_gap_flag = 0U;
			Dashboard_UI_PushAlert("lap time uncertain");
		}

		g_lap_count++;
		clear_current_samples();
		g_lap_ref_match_index = 1U;
		g_lap_ref_match_valid = g_lap_ref_valid;
		g_lap_ref_match_time_ms = 0;
		g_lap_start_tick = cross_tick;
		g_lap_current_ms = (int32_t)(now - g_lap_start_tick);
		g_lap_finish_armed = 0U;
		add_current_sample(&cross_pos, cross_tick, 0, 1U);

		Dashboard_UI_SubmitLapTimes(g_lap_current_ms / 10,
									g_lap_last_ms / 10,
									g_lap_best_ms / 10,
									g_lap_count);
		Dashboard_UI_SubmitLapDelta(g_lap_delta_ms / 10);
		g_lap_diag_hold_until = now + GPS_LAP_DIAG_HOLD_MS;
		update_diagnostic(GPS_LAP_DIAG_CROSSED, &cross_pos, data);
	} else {
		add_current_sample(&curr_pos, now, g_lap_current_ms, 0U);
		update_live_delta(&curr_pos, g_lap_current_ms);
		submit_lap_times_limited(now, 0U);
		if(diag_hold_active(now) == 0U) {
			GPS_LapDiagState_t state = GPS_LAP_DIAG_WAIT_ARM;
			if(g_lap_finish_armed != 0U) {
				state = ((curr_pos.forward_m < 0.0f) &&
						 (absf32(curr_pos.forward_m) <= GPS_LAP_APPROACH_DISTANCE_M) &&
						 (absf32(curr_pos.lateral_m) <= (gate_half_width * 2.0f))) ?
						GPS_LAP_DIAG_APPROACHING : GPS_LAP_DIAG_ARMED;
			}
			update_diagnostic(state, &curr_pos, data);
		}
	}

	g_lap_prev_pos = curr_pos;
	g_lap_prev_tick = now;
	g_lap_prev_valid = 1U;
#endif
}

const char * GPS_Lap_GetDeltaStr(char * buf, uint32_t size)
{
	int32_t abs_delta = g_lap_delta_ms < 0 ? -g_lap_delta_ms : g_lap_delta_ms;
	int32_t secs = abs_delta / 1000;
	int32_t cs = (abs_delta % 1000) / 10;
	uint32_t pos = 0U;
	char tmp[10];
	uint32_t n = 0U;

	if((buf == NULL) || (size == 0U)) return "";
	if(pos + 1U < size) buf[pos++] = g_lap_delta_ms >= 0 ? '+' : '-';
	if(secs == 0) {
		if(pos + 1U < size) buf[pos++] = '0';
	} else {
		while((secs > 0) && (n < sizeof(tmp))) {
			tmp[n++] = (char)('0' + (secs % 10));
			secs /= 10;
		}
		while(n > 0U) {
			if(pos + 1U < size) buf[pos++] = tmp[--n];
			else break;
		}
	}
	if(pos + 1U < size) buf[pos++] = '.';
	if(pos + 1U < size) buf[pos++] = (char)('0' + (cs / 10));
	if(pos + 1U < size) buf[pos++] = (char)('0' + (cs % 10));
	if(pos + 1U < size) buf[pos++] = 's';
	buf[pos < size ? pos : (size - 1U)] = '\0';
	return buf;
}

int32_t GPS_Lap_GetDeltaHundredths(void)  { return g_lap_delta_ms / 10; }
int32_t GPS_Lap_GetCurrentLapHundredths(void) { return g_lap_current_ms / 10; }
int32_t GPS_Lap_GetLastLapHundredths(void)    { return g_lap_last_ms / 10; }
int32_t GPS_Lap_GetBestLapHundredths(void)    { return g_lap_best_ms / 10; }
int32_t GPS_Lap_GetCurrentLapNum(void)         { return g_lap_count; }

#else

static void GPS_LapProcess_Impl(const GPS_Data_t * data)
{
	(void)data;
}

#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif


void GPS_Lap_SetStartLine(float lat, float lon, float track)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Lap_SetStartLine_Impl(lat, lon, track);
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

void GPS_Lap_SetFinishLine(float lat, float lon, float track)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Lap_SetFinishLine_Impl(lat, lon, track);
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

uint8_t GPS_Lap_StartAtCurrent(const GPS_Data_t *data)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    uint8_t result = GPS_Lap_StartAtCurrent_Impl(data);
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
    return result;
}

void GPS_Lap_Reset(void)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Lap_Reset_Impl();
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

void GPS_Lap_SetAnalysisActive(uint8_t active)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Lap_SetAnalysisActive_Impl(active);
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

uint8_t GPS_Lap_IsAnalysisActive(void)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    uint8_t result = GPS_Lap_IsAnalysisActive_Impl();
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
    return result;
}

void GPS_Lap_Tick(void)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Lap_Tick_Impl();
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

void GPS_Sprint_Reset(void)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Sprint_Reset_Impl();
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

uint8_t GPS_Sprint_StartAtCurrent(const GPS_Data_t *data, float heading_deg)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    uint8_t result = GPS_Sprint_StartAtCurrent_Impl(data, heading_deg);
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
    return result;
}

void GPS_Sprint_Cancel(void)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Sprint_Cancel_Impl();
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

uint8_t GPS_Sprint_IsActive(void)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    uint8_t result = GPS_Sprint_IsActive_Impl();
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
    return result;
}

void GPS_Sprint_Tick(void)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Sprint_Tick_Impl();
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

void GPS_SprintProcess(const GPS_Data_t *data)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_SprintProcess_Impl(data);
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

void GPS_Lap_GetDiagnostic(GPS_LapDiagnostic_t *out)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_Lap_GetDiagnostic_Impl(out);
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

void GPS_LapProcess(const GPS_Data_t *data)
{
    configASSERT(g_lap_mutex != NULL);
    BaseType_t locked = xSemaphoreTakeRecursive(g_lap_mutex, portMAX_DELAY);
    configASSERT(locked == pdTRUE);
    GPS_LapProcess_Impl(data);
    (void)xSemaphoreGiveRecursive(g_lap_mutex);
}

/* USER CODE END gps_lap */
