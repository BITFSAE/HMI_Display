#include "diag_cli.h"
#include "main.h"
#include "usart.h"
#include "gps.h"
#include "gps_speed.h"
#include "firmware_diag.h"
#include "speed_trace.h"
#include "odometer_flash.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include <string.h>

/* 固件版本由 build_fw.mk 生成的 fw_version.h 提供；CubeIDE 构建没有这个头，显示 unknown。 */
#if defined(__has_include)
#if __has_include("fw_version.h")
#include "fw_version.h"
#endif
#endif
#ifndef FW_GIT_VERSION
#define FW_GIT_VERSION "unknown"
#endif

#define CLI_RX_RING          128U   /* 2 的幂 */
#define CLI_TX_RING         1024U   /* 2 的幂 */
#define CLI_LINE_MAX          80U
#define CLI_OUT_LINE         256U   /* 一行输出的上限，含换行 */
#define CLI_TOKENS             4U
#define CLI_STACK_WORDS      768U
#define CLI_REPORT_MAX_S      60U
#define CLI_CAPTURE_MAX_S     20U
#define CLI_REQUEST_TIMEOUT_MS 8000U
#define ARRAY_COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))
#define CLI_RX_STALL_RETRY_MS 1000U
#define CLI_RX_ERROR_STREAK     64U

extern volatile uint32_t g_can_rx_drop_count;
extern volatile uint32_t g_can_rx_invalid_count;
extern volatile uint32_t g_can_rx_queue_peak;
extern volatile uint32_t g_can_bus_off_count;
extern uint32_t SystemCoreClock;

/* ------------------------------------------------------------------ 串口收发 */

static uint8_t g_rx_ring[CLI_RX_RING];
static volatile uint32_t g_rx_head;
static volatile uint32_t g_rx_tail;
static uint8_t g_rx_byte;
static volatile uint32_t g_rx_drop;
static volatile uint32_t g_rx_errors;
static volatile uint32_t g_rx_err_streak;
static volatile uint8_t g_rx_stalled;
static volatile uint32_t g_rx_stall_tick;

static uint8_t g_tx_ring[CLI_TX_RING];
static volatile uint32_t g_tx_head;
static volatile uint32_t g_tx_tail;
static volatile uint32_t g_tx_chunk;
static volatile uint8_t g_tx_busy;
static volatile uint32_t g_tx_drop_bytes;
static volatile uint32_t g_tx_hw_errors;

static TaskHandle_t g_cli_task;
static StaticTask_t g_cli_task_control;
static StackType_t g_cli_task_stack[CLI_STACK_WORDS];

static void cli_hw_rx_arm(void)
{
	(void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
}

static HAL_StatusTypeDef cli_hw_tx(const uint8_t * data, uint16_t length)
{
	return HAL_UART_Transmit_IT(&huart2, (uint8_t *)data, length);
}

/* 调用者必须处在临界区或中断里。 */
static void tx_start_locked(void)
{
	uint32_t tail = g_tx_tail;
	uint32_t head = g_tx_head;
	uint32_t count;

	if(head == tail) {
		g_tx_busy = 0U;
		return;
	}
	count = (head > tail) ? (head - tail) : (CLI_TX_RING - tail);
	if(cli_hw_tx(&g_tx_ring[tail], (uint16_t)count) == HAL_OK) {
		g_tx_chunk = count;
		g_tx_busy = 1U;
	}
	else {
		g_tx_busy = 0U;
		g_tx_hw_errors++;
	}
}

void Diag_CLI_UartTxDone(void)
{
	g_tx_tail = (g_tx_tail + g_tx_chunk) & (CLI_TX_RING - 1U);
	g_tx_chunk = 0U;
	tx_start_locked();
}

void Diag_CLI_UartRxCplt(void)
{
	uint32_t next = (g_rx_head + 1U) & (CLI_RX_RING - 1U);

	g_rx_err_streak = 0U;
	if(next == g_rx_tail) {
		g_rx_drop++;
	}
	else {
		g_rx_ring[g_rx_head] = g_rx_byte;
		g_rx_head = next;
	}
	cli_hw_rx_arm();
}

/* RX 引脚悬空或线路干扰会持续触发帧/噪声错误。连续错误过多时先停止接收，任务过一会儿
 * 再重新打开，避免错误中断长期占用 CPU。 */
void Diag_CLI_UartError(uint32_t error_code)
{
	(void)error_code;
	g_rx_errors++;
	g_rx_err_streak++;
	if(g_rx_err_streak > CLI_RX_ERROR_STREAK) {
		if(g_rx_stalled == 0U) {
			(void)HAL_UART_AbortReceive(&huart2);
			g_rx_stalled = 1U;
			g_rx_stall_tick = HAL_GetTick();
		}
		return;
	}
	cli_hw_rx_arm();
}

static uint32_t tx_free(void)
{
	return CLI_TX_RING - 1U - ((g_tx_head - g_tx_tail) & (CLI_TX_RING - 1U));
}

/* 空间不足时整段丢弃并计数；用于回显和提示符，命令输出先检查空间再生成。 */
static void tx_put(const char * data, uint32_t length)
{
	uint32_t head = g_tx_head;

	if(tx_free() < length) {
		g_tx_drop_bytes += length;
		return;
	}
	for(uint32_t i = 0U; i < length; i++) {
		g_tx_ring[(head + i) & (CLI_TX_RING - 1U)] = (uint8_t)data[i];
	}
	g_tx_head = (head + length) & (CLI_TX_RING - 1U);
}

static void tx_puts(const char * text)
{
	tx_put(text, (uint32_t)strlen(text));
}

static void tx_kick(void)
{
	taskENTER_CRITICAL();
	if(g_tx_busy == 0U) tx_start_locked();
	taskEXIT_CRITICAL();
}

/* ------------------------------------------------------------------ 行构造（不用 printf） */

typedef struct {
	char * p;
	uint32_t cap;
	uint32_t len;
} lb_t;

static void lb_init(lb_t * b, char * buf, uint32_t cap)
{
	b->p = buf;
	b->cap = cap;
	b->len = 0U;
	buf[0] = '\0';
}

static void lb_c(lb_t * b, char c)
{
	if((b->len + 1U) < b->cap) {
		b->p[b->len++] = c;
		b->p[b->len] = '\0';
	}
}

static void lb_s(lb_t * b, const char * s)
{
	if(s == NULL) s = "(null)";
	while(*s != '\0') lb_c(b, *s++);
}

static void lb_u(lb_t * b, uint32_t v)
{
	char tmp[11];
	uint32_t n = 0U;

	do {
		tmp[n++] = (char)('0' + (v % 10U));
		v /= 10U;
	} while(v != 0U);
	while(n > 0U) lb_c(b, tmp[--n]);
}

static void lb_i(lb_t * b, int32_t v)
{
	if(v < 0) {
		lb_c(b, '-');
		lb_u(b, (uint32_t)(-(int64_t)v));
	}
	else {
		lb_u(b, (uint32_t)v);
	}
}

/* 定点数：value 已按 10^decimals 放大，如 (7234, 2) → "72.34"。 */
static void lb_fix(lb_t * b, int32_t value, uint32_t decimals)
{
	uint32_t scale = 1U;
	uint32_t magnitude;
	uint32_t fraction;

	for(uint32_t i = 0U; i < decimals; i++) scale *= 10U;
	if(value < 0) {
		lb_c(b, '-');
		magnitude = (uint32_t)(-(int64_t)value);
	}
	else {
		magnitude = (uint32_t)value;
	}
	lb_u(b, magnitude / scale);
	if(decimals == 0U) return;
	lb_c(b, '.');
	fraction = magnitude % scale;
	for(uint32_t div = scale / 10U; div > 0U; div /= 10U) {
		lb_c(b, (char)('0' + ((fraction / div) % 10U)));
	}
}

static void lb_key(lb_t * b, const char * key)
{
	if(b->len > 0U) lb_c(b, ' ');
	lb_s(b, key);
	lb_c(b, '=');
}

static void lb_ku(lb_t * b, const char * key, uint32_t v)   { lb_key(b, key); lb_u(b, v); }
static void lb_ks(lb_t * b, const char * key, const char * v) { lb_key(b, key); lb_s(b, v); }
static void lb_kf(lb_t * b, const char * key, int32_t v, uint32_t decimals)
{
	lb_key(b, key);
	lb_fix(b, v, decimals);
}
/* 窗口增量/累计：a/b。 */
static void lb_kdt(lb_t * b, const char * key, uint32_t window, uint32_t total)
{
	lb_key(b, key);
	lb_u(b, window);
	lb_c(b, '/');
	lb_u(b, total);
}
static void lb_kq(lb_t * b, const char * key, const char * v)
{
	lb_key(b, key);
	lb_c(b, '"');
	lb_s(b, v);
	lb_c(b, '"');
}

/* ------------------------------------------------------------------ 命令与任务 */

typedef uint32_t (*cli_line_fn)(lb_t * b, uint32_t sub);   /* 返回此条目之后还有几行 */

typedef enum {
	JOB_IDLE = 0,
	JOB_SAMPLE,                 /* report：等采样窗口结束 */
	JOB_CAPTURE,                /* gps capture：等采集结束 */
	JOB_REQUEST,                /* gps config/rate/save：等 GPS 任务执行完 */
	JOB_PRINT                   /* 逐行输出，发送空间不足时下一轮继续 */
} job_kind_t;

typedef struct {
	job_kind_t kind;
	uint32_t deadline_ms;
	const cli_line_fn * fns;
	uint32_t count;
	uint32_t entry;
	uint32_t sub;
	const cli_line_fn * after_fns;       /* 等待结束后要打印的表 */
	uint32_t after_count;
	uint8_t freeze_report;
	uint8_t freeze_config;
} cli_job_t;

/* 采样窗口结束时冻结的数据，保证报告各行互相一致。 */
typedef struct {
	uint32_t start_ms;
	uint32_t end_ms;
	GPS_Stats_t s0;
	GPS_Stats_t s1;
	GPS_Window_t win;
	gps_speed_view_t view;
	gps_speed_event_t events[8];
	uint32_t event_count;
	speed_trace_t trace;
	GPS_CfgStatus_t cfg;
	char raw[GPS_RAW_LINES][GPS_RAW_LINE_LEN];
	uint32_t raw_count;
	uint32_t raw_dropped;
	uint32_t capture_seconds;
} cli_frozen_t;

static cli_frozen_t g_fz;
static cli_job_t g_job;
static char g_line[CLI_LINE_MAX];
static uint32_t g_line_len;
static uint8_t g_last_was_cr;
static uint32_t g_cli_commands;
static uint32_t g_cli_rejected;
static uint32_t g_cli_fallback_lines;

static void cli_prompt(void)
{
	tx_puts("> ");
}

static void cli_freeze_config(void)
{
	GPS_GetConfigStatus(&g_fz.cfg);
	g_fz.raw_count = GPS_GetRawLines(g_fz.raw, GPS_RAW_LINES);
	g_fz.raw_dropped = GPS_RawLinesDropped();
}

static void cli_freeze_report(uint32_t now)
{
	g_fz.end_ms = now;
	GPS_GetStats(&g_fz.s1);
	GPS_GetWindow(&g_fz.win);
	GPS_GetSpeedView(0U, &g_fz.view);
	g_fz.event_count = GPS_GetSpeedEvents(g_fz.events, 8U);
	SpeedTrace_Get(&g_fz.trace);
	cli_freeze_config();
}

/* ---- 行生成：每个函数产生一个条目的第 sub 行，返回之后还剩几行 ---- */

static uint32_t d32(uint32_t now_v, uint32_t then_v)
{
	return now_v - then_v;
}

static uint32_t ln_begin(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "HMI-DIAG BEGIN");
	lb_ku(b, "fmt", DIAG_REPORT_FORMAT);
	lb_s(b, "  (a/b = window/since-boot)");
	return 0U;
}

static uint32_t ln_build(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "build");
	lb_ks(b, "fw", FW_GIT_VERSION);
	lb_ks(b, "built", __DATE__ " " __TIME__);
	lb_ku(b, "uptime_ms", HAL_GetTick());
	return 0U;
}

static uint32_t ln_window(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "window");
	lb_ku(b, "start_ms", g_fz.start_ms);
	lb_ku(b, "end_ms", g_fz.end_ms);
	lb_ku(b, "length_ms", d32(g_fz.end_ms, g_fz.start_ms));
	return 0U;
}

static uint32_t window_ms(void)
{
	uint32_t length = d32(g_fz.end_ms, g_fz.start_ms);
	return (length == 0U) ? 1U : length;
}

static uint32_t ln_rate(lb_t * b, uint32_t sub)
{
	uint32_t accepted = d32(g_fz.s1.rmc_accepted, g_fz.s0.rmc_accepted);

	(void)sub;
	lb_s(b, "gps.rmc");
	lb_ku(b, "rmc_total", d32(g_fz.s1.n_rmc, g_fz.s0.n_rmc));
	lb_ku(b, "accepted", accepted);
	lb_kf(b, "rate_hz", (int32_t)((accepted * 10000ULL) / window_ms()), 1);
	lb_ku(b, "interval_ms_min", g_fz.win.interval_min_ms);
	lb_ku(b, "interval_ms_avg", (accepted != 0U) ? (window_ms() / accepted) : 0U);
	lb_ku(b, "interval_ms_max", g_fz.win.interval_max_ms);
	lb_ku(b, "utc_step_ms_min", g_fz.win.epoch_min_ms);
	lb_ku(b, "utc_step_ms_max", g_fz.win.epoch_max_ms);
	return 0U;
}

static uint32_t ln_reject(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "gps.rmc.rejected");
	lb_ku(b, "no_fix", d32(g_fz.s1.rmc_no_fix, g_fz.s0.rmc_no_fix));
	lb_ku(b, "bad_field", d32(g_fz.s1.rmc_bad_field, g_fz.s0.rmc_bad_field));
	lb_ku(b, "dup_epoch", d32(g_fz.s1.rmc_dup_epoch, g_fz.s0.rmc_dup_epoch));
	lb_ku(b, "back_epoch", d32(g_fz.s1.rmc_back_epoch, g_fz.s0.rmc_back_epoch));
	lb_ku(b, "checksum_fail", d32(g_fz.s1.checksum_fail, g_fz.s0.checksum_fail));
	return 0U;
}

static uint32_t ln_nmea(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "gps.nmea");
	lb_ku(b, "bytes", d32(g_fz.s1.dma_bytes, g_fz.s0.dma_bytes));
	lb_ku(b, "sentences", d32(g_fz.s1.sentences, g_fz.s0.sentences));
	lb_ku(b, "gga", d32(g_fz.s1.n_gga, g_fz.s0.n_gga));
	lb_ku(b, "hpr", d32(g_fz.s1.n_hpr, g_fz.s0.n_hpr));
	lb_ku(b, "gsv", d32(g_fz.s1.n_gsv, g_fz.s0.n_gsv));
	lb_ku(b, "reply", d32(g_fz.s1.n_reply, g_fz.s0.n_reply));
	lb_ku(b, "other", d32(g_fz.s1.n_other, g_fz.s0.n_other));
	lb_ku(b, "line_overflow", d32(g_fz.s1.line_overflow, g_fz.s0.line_overflow));
	lb_ku(b, "line_truncated", d32(g_fz.s1.line_truncated, g_fz.s0.line_truncated));
	return 0U;
}

static uint32_t ln_speed(lb_t * b, uint32_t sub)
{
	uint32_t valid_pct = (uint32_t)(((uint64_t)g_fz.win.valid_ms * 1000ULL) / window_ms());

	(void)sub;
	lb_s(b, "gps.speed");
	lb_ku(b, "valid_now", g_fz.view.valid);
	lb_ks(b, "reason", GPS_Speed_ReasonName(g_fz.view.reason));
	lb_ku(b, "age_ms", g_fz.view.age_ms);
	lb_ku(b, "age_max_ms", g_fz.win.age_max_ms);
	lb_ku(b, "limit_ms", g_fz.s1.fresh_limit_ms);
	lb_ku(b, "period_ema_ms", g_fz.s1.rmc_period_ms);
	lb_kf(b, "valid_pct", (int32_t)valid_pct, 1);
	lb_kdt(b, "invalid_events", d32(g_fz.s1.speed_invalid_events, g_fz.s0.speed_invalid_events),
	       g_fz.s1.speed_invalid_events);
	return 0U;
}

static uint32_t ln_speed_value(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "gps.speed.value");
	lb_kf(b, "raw_kmh", g_fz.s1.raw_speed_centi, 2);
	lb_key(b, "shown_kmh");
	if(g_fz.s1.shown_speed_kmh < 0) lb_s(b, "--"); else lb_i(b, g_fz.s1.shown_speed_kmh);
	lb_kf(b, "min_kmh", g_fz.win.speed_min_centi, 2);
	lb_kf(b, "max_kmh", g_fz.win.speed_max_centi, 2);
	lb_ku(b, "last_utc_ms", g_fz.s1.last_epoch_ms);
	return 0U;
}

static uint32_t ln_link(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "gps.link");
	lb_kdt(b, "poll_gap_max_ms", g_fz.win.poll_gap_max_ms, g_fz.s1.poll_gap_max_ms);
	lb_kdt(b, "late_poll", d32(g_fz.s1.late_poll, g_fz.s0.late_poll), g_fz.s1.late_poll);
	lb_kdt(b, "ring_pending_max", g_fz.win.ring_pending_max, g_fz.s1.ring_pending_max);
	lb_s(b, " (late_poll = possible overrun risk, not an exact lost-frame count)");
	return 0U;
}

static uint32_t ln_uart(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "gps.uart");
	lb_kdt(b, "pe", d32(g_fz.s1.uart_err_pe, g_fz.s0.uart_err_pe), g_fz.s1.uart_err_pe);
	lb_kdt(b, "ne", d32(g_fz.s1.uart_err_ne, g_fz.s0.uart_err_ne), g_fz.s1.uart_err_ne);
	lb_kdt(b, "fe", d32(g_fz.s1.uart_err_fe, g_fz.s0.uart_err_fe), g_fz.s1.uart_err_fe);
	lb_kdt(b, "ore", d32(g_fz.s1.uart_err_ore, g_fz.s0.uart_err_ore), g_fz.s1.uart_err_ore);
	lb_kdt(b, "dma", d32(g_fz.s1.uart_err_dma, g_fz.s0.uart_err_dma), g_fz.s1.uart_err_dma);
	lb_kdt(b, "rx_restarts", d32(g_fz.s1.rx_restarts, g_fz.s0.rx_restarts), g_fz.s1.rx_restarts);
	lb_kdt(b, "restart_fail", d32(g_fz.s1.rx_restart_fail, g_fz.s0.rx_restart_fail),
	       g_fz.s1.rx_restart_fail);
	return 0U;
}

static const char * cfg_state_name(uint8_t state)
{
	switch(state) {
	case GPS_CFG_OK:          return "ok";
	case GPS_CFG_REJECTED:    return "rejected";
	case GPS_CFG_NOACK:       return "no_ack";
	case GPS_CFG_UNCONFIRMED: return "unconfirmed";
	default:                  return "pending";
	}
}

static uint32_t ln_cfg(lb_t * b, uint32_t sub)
{
	if(sub == 0U) {
		lb_s(b, "gps.cfg");
		lb_ku(b, "ack_seen", g_fz.cfg.ack_seen);
		lb_ku(b, "ack_unsupported", g_fz.cfg.ack_unsupported);
		lb_ku(b, "running", g_fz.cfg.running);
		lb_ku(b, "commands", g_fz.cfg.count);
		return (g_fz.cfg.count > 0U) ? g_fz.cfg.count : 0U;
	}
	{
		const GPS_CfgItem_t * item = &g_fz.cfg.item[sub - 1U];

		lb_s(b, "gps.cfg.cmd");
		lb_ku(b, "i", sub - 1U);
		lb_kq(b, "cmd", item->cmd);
		lb_ks(b, "state", cfg_state_name(item->state));
		lb_ku(b, "attempts", item->attempts);
		lb_kq(b, "reply", item->reply);
		return g_fz.cfg.count - sub;
	}
}

static uint32_t ln_cfg_request(lb_t * b, uint32_t sub)
{
	const GPS_CfgItem_t * item = &g_fz.cfg.request;

	(void)sub;
	if(item->cmd == NULL) return 0U;
	lb_s(b, "gps.cfg.request");
	lb_kq(b, "cmd", item->cmd);
	lb_ks(b, "state", cfg_state_name(item->state));
	lb_ku(b, "attempts", item->attempts);
	lb_kq(b, "reply", item->reply);
	return 0U;
}

static uint32_t ln_raw(lb_t * b, uint32_t sub)
{
	if(g_fz.raw_count == 0U) return 0U;
	lb_s(b, "gps.raw");
	lb_ku(b, "i", sub);
	lb_c(b, ' ');
	lb_s(b, g_fz.raw[sub]);
	return g_fz.raw_count - 1U - sub;
}

static uint32_t ln_events(lb_t * b, uint32_t sub)
{
	const gps_speed_event_t * event;

	if(g_fz.event_count == 0U) {
		lb_s(b, "gps.events none");
		return 0U;
	}
	event = &g_fz.events[sub];
	lb_s(b, "gps.event");
	lb_ku(b, "t_ms", event->tick);
	lb_ks(b, "to", (event->valid != 0U) ? "valid" : "invalid");
	lb_ks(b, "reason", GPS_Speed_ReasonName(event->reason));
	lb_ku(b, "age_ms", event->age_ms);
	return g_fz.event_count - 1U - sub;
}

static void lb_stage(lb_t * b, const char * name, const speed_trace_stage_t * stage)
{
	lb_s(b, name);
	lb_ku(b, "n", stage->count);
	lb_ku(b, "avg_ms", (stage->count != 0U) ? (stage->sum_ms / stage->count) : 0U);
	lb_ku(b, "max_ms", stage->max_ms);
	lb_ku(b, "last_ms", stage->last_ms);
}

static uint32_t cycles_to_centi_ms(uint32_t cycles)
{
	uint32_t per_ms = SystemCoreClock / 1000U;
	return (per_ms == 0U) ? 0U : (uint32_t)(((uint64_t)cycles * 100ULL) / per_ms);
}

static uint32_t ln_latency(lb_t * b, uint32_t sub)
{
	if(sub == 0U) {
		lb_stage(b, "latency.submit_to_apply", &g_fz.trace.submit_to_apply);
		return 2U;
	}
	if(sub == 1U) {
		lb_stage(b, "latency.apply_to_frame_done", &g_fz.trace.apply_to_frame);
		lb_ku(b, "superseded", g_fz.trace.superseded);
		return 1U;
	}
	lb_s(b, "latency.display");
	lb_kf(b, "loop_max_ms", (int32_t)cycles_to_centi_ms(g_firmware_diag.display_loop_max_cycles), 2);
	lb_kf(b, "flush_max_ms", (int32_t)cycles_to_centi_ms(g_firmware_diag.flush_max_cycles), 2);
	lb_s(b, " (frame_done = last LVGL flush returned; panel response is not measured)");
	return 0U;
}

static uint32_t ln_sys(lb_t * b, uint32_t sub)
{
	const firmware_diag_t * d = (const firmware_diag_t *)&g_firmware_diag;
	/* 共 FW_DIAG_TASK_COUNT + 3 行：汇总、每个任务一行、IO 计数、CLI 自身计数。 */
	const uint32_t last = FW_DIAG_TASK_COUNT + 2U;

	if(sub == 0U) {
		lb_s(b, "sys");
		lb_ku(b, "uptime_ms", HAL_GetTick());
		lb_ku(b, "heap_free", d->heap_free_bytes);
		lb_ku(b, "heap_free_min", d->heap_free_min_bytes);
		lb_ku(b, "lvgl_free", d->lvgl_free_bytes);
		lb_ku(b, "lvgl_largest_free", d->lvgl_largest_free_bytes);
		lb_ku(b, "lvgl_used_peak", d->lvgl_used_peak_bytes);
		lb_ku(b, "stack_warn_mask", d->stack_warning_mask);
		lb_ku(b, "fault_code", d->fault_code);
		return last;
	}
	if(sub <= FW_DIAG_TASK_COUNT) {
		const firmware_task_diag_t * task = &d->tasks[sub - 1U];

		lb_s(b, "sys.task");
		lb_ks(b, "name", (task->name != NULL) ? task->name : "-");
		lb_ku(b, "active", task->active);
		lb_ku(b, "stack_size", task->stack_size_bytes);
		lb_ku(b, "stack_free_min", task->stack_free_min_bytes);
		return last - sub;
	}
	if(sub == (FW_DIAG_TASK_COUNT + 1U)) {
		lb_s(b, "sys.io");
		lb_ku(b, "can_rx_drop", g_can_rx_drop_count);
		lb_ku(b, "can_rx_invalid", g_can_rx_invalid_count);
		lb_ku(b, "can_rx_queue_peak", g_can_rx_queue_peak);
		lb_ku(b, "can_bus_off", g_can_bus_off_count);
		lb_ku(b, "odo_save_fail", g_odometer_flash_save_fail_count);
		lb_ku(b, "odo_erase", g_odometer_flash_erase_count);
		return 1U;
	}
	lb_s(b, "sys.cli");
	lb_ku(b, "rx_drop", g_rx_drop);
	lb_ku(b, "rx_err", g_rx_errors);
	lb_ku(b, "tx_drop_bytes", g_tx_drop_bytes);
	lb_ku(b, "tx_hw_err", g_tx_hw_errors);
	lb_ku(b, "commands", g_cli_commands);
	lb_ku(b, "rejected", g_cli_rejected);
	lb_ku(b, "fallback_lines", g_cli_fallback_lines);
	return 0U;
}

static uint32_t ln_end(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "HMI-DIAG END");
	return 0U;
}

static const cli_line_fn k_report_fns[] = {
	ln_begin, ln_build, ln_window, ln_rate, ln_reject, ln_nmea, ln_speed, ln_speed_value,
	ln_link, ln_uart, ln_cfg, ln_cfg_request, ln_events, ln_latency, ln_sys, ln_end
};

/* ---- gps status：当前瞬时状态，不需要采样窗口 ---- */

static uint32_t ln_status(lb_t * b, uint32_t sub)
{
	GPS_Data_t d;
	gps_speed_view_t can_view;

	(void)sub;
	GPS_GetData(&d);
	GPS_GetSpeedView(500U, &can_view);
	lb_s(b, "gps.status");
	lb_ku(b, "speed_valid", d.speed_valid);
	lb_ks(b, "reason", GPS_Speed_ReasonName(d.speed_reason));
	lb_ku(b, "age_ms", d.speed_age_ms);
	lb_kf(b, "speed_kmh", (int32_t)(d.speed_kmh * 100.0f + (d.speed_kmh >= 0.0f ? 0.5f : -0.5f)), 2);
	lb_ku(b, "can_speed_valid", can_view.valid);
	lb_ku(b, "position_valid", d.valid);
	lb_ku(b, "fix_quality", d.fix_quality);
	lb_ku(b, "sats", d.satellites);
	lb_ku(b, "signal_level", d.signal_level);
	lb_ku(b, "heading_valid", d.heading_valid);
	lb_ku(b, "heading_quality", d.heading_quality);
	return 0U;
}

static uint32_t ln_status_cfg(lb_t * b, uint32_t sub)
{
	GPS_Stats_t s;

	(void)sub;
	GPS_GetStats(&s);
	lb_s(b, "gps.status.link");
	lb_ku(b, "uptime_ms", s.tick_ms);
	lb_ku(b, "limit_ms", s.fresh_limit_ms);
	lb_ku(b, "period_ema_ms", s.rmc_period_ms);
	lb_ku(b, "rmc_accepted", s.rmc_accepted);
	lb_ku(b, "rx_restarts", s.rx_restarts);
	lb_ku(b, "late_poll", s.late_poll);
	lb_ku(b, "other_sentences", s.n_other);
	return 0U;
}

static const cli_line_fn k_status_fns[] = { ln_status, ln_status_cfg };

static uint32_t ln_sys_cmd(lb_t * b, uint32_t sub)
{
	if(sub == 0U) {
		lb_s(b, "HMI-DIAG SYS");
		lb_ku(b, "fmt", DIAG_REPORT_FORMAT);
		lb_ks(b, "fw", FW_GIT_VERSION);
		return 1U;
	}
	return ln_sys(b, sub - 1U);
}

static const cli_line_fn k_sys_fns[] = { ln_sys_cmd };

static uint32_t ln_version(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "hmi-display");
	lb_ks(b, "fw", FW_GIT_VERSION);
	lb_ks(b, "built", __DATE__ " " __TIME__);
	lb_ku(b, "report_fmt", DIAG_REPORT_FORMAT);
	return 0U;
}

static const cli_line_fn k_version_fns[] = { ln_version };

static const char * const k_help_lines[] = {
	"help                 this list",
	"version              firmware version",
	"report [N]           sample N s (1..60, default 10), print full report",
	"gps status           speed/position/heading state now",
	"gps config           query receiver (VERSION/MODE/CONFIG/UNILOGLIST), print boot config acks",
	"gps capture [N]      record N s (1..20, default 15) of RMC samples, CSV",
	"gps rate 10|20       set RMC rate until reboot (not saved)",
	"gps smooth on|off    Doppler speed smoothing (CONFIG SMOOTH PSRVEL), not saved",
	"gps save             SAVECONFIG, persist receiver settings",
	"sys status           tasks, memory, CAN, storage, display timing",
	"stop                 abort the running command (Ctrl-C too)",
};

static uint32_t ln_help(lb_t * b, uint32_t sub)
{
	lb_s(b, k_help_lines[sub]);
	return (sizeof(k_help_lines) / sizeof(k_help_lines[0])) - 1U - sub;
}

static const cli_line_fn k_help_fns[] = { ln_help };

/* ---- gps capture：CSV，一行一条 RMC ---- */

#define CAPTURE_HEADER_LINES  2U

static uint32_t ln_capture(lb_t * b, uint32_t sub)
{
	uint32_t count = GPS_CaptureCount();
	uint32_t total = CAPTURE_HEADER_LINES + count + 1U;

	if(sub == 0U) {
		lb_s(b, "HMI-DIAG GPS-CAPTURE BEGIN");
		lb_ku(b, "fmt", DIAG_REPORT_FORMAT);
		lb_ks(b, "fw", FW_GIT_VERSION);
		lb_ku(b, "seconds", g_fz.capture_seconds);
		lb_ku(b, "records", count);
		lb_ks(b, "state", (GPS_CaptureGetState() == GPS_CAPTURE_DONE) ? "done" : "stopped");
		return total - 1U;
	}
	if(sub == 1U) {
		lb_s(b, "idx,tick_ms,utc_ms,raw_kmh,shown_kmh,verdict,valid");
		return total - 2U;
	}
	if(sub < (CAPTURE_HEADER_LINES + count)) {
		GPS_CaptureRec_t rec;

		if(GPS_CaptureGet(sub - CAPTURE_HEADER_LINES, &rec) != 0U) {
			lb_u(b, sub - CAPTURE_HEADER_LINES);
			lb_c(b, ',');
			lb_u(b, rec.tick_ms);
			lb_c(b, ',');
			if(rec.epoch_ms == GPS_CAPTURE_NO_EPOCH) lb_c(b, '-'); else lb_u(b, rec.epoch_ms);
			lb_c(b, ',');
			if(rec.speed_centi == INT32_MIN) lb_c(b, '-'); else lb_fix(b, rec.speed_centi, 2);
			lb_c(b, ',');
			if(rec.shown_kmh < 0) lb_s(b, "--"); else lb_i(b, rec.shown_kmh);
			lb_c(b, ',');
			if((rec.verdict & GPS_CAPTURE_EVENT_FLAG) != 0U) {
				lb_s(b, "invalid:");
				lb_s(b, GPS_Speed_ReasonName((uint8_t)(rec.verdict & ~GPS_CAPTURE_EVENT_FLAG)));
			}
			else {
				lb_s(b, GPS_Speed_VerdictName(rec.verdict));
			}
			lb_c(b, ',');
			lb_u(b, rec.valid);
		}
		return total - 1U - sub;
	}
	lb_s(b, "HMI-DIAG GPS-CAPTURE END");
	return 0U;
}

static const cli_line_fn k_capture_fns[] = { ln_capture };

static uint32_t ln_config_head(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "HMI-DIAG GPS-CONFIG BEGIN");
	lb_ku(b, "fmt", DIAG_REPORT_FORMAT);
	lb_ks(b, "fw", FW_GIT_VERSION);
	return 0U;
}

static uint32_t ln_config_raw_head(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "gps.raw");
	lb_ku(b, "lines", g_fz.raw_count);
	lb_ku(b, "dropped", g_fz.raw_dropped);
	return 0U;
}

static uint32_t ln_config_end(lb_t * b, uint32_t sub)
{
	(void)sub;
	lb_s(b, "HMI-DIAG GPS-CONFIG END");
	return 0U;
}

static const cli_line_fn k_config_fns[] = {
	ln_config_head, ln_cfg, ln_cfg_request, ln_config_raw_head, ln_raw, ln_config_end
};

static uint32_t ln_request_result(lb_t * b, uint32_t sub)
{
	const GPS_CfgItem_t * item = &g_fz.cfg.request;

	(void)sub;
	lb_s(b, "gps.request");
	lb_kq(b, "cmd", item->cmd);
	lb_ks(b, "state", cfg_state_name(item->state));
	lb_ku(b, "attempts", item->attempts);
	lb_kq(b, "reply", item->reply);
	return 0U;
}

static const cli_line_fn k_request_fns[] = { ln_request_result };

/* ---- 作业调度 ---- */

static void job_print(const cli_line_fn * fns, uint32_t count)
{
	g_job.kind = JOB_PRINT;
	g_job.fns = fns;
	g_job.count = count;
	g_job.entry = 0U;
	g_job.sub = 0U;
}

static void job_wait(job_kind_t kind, uint32_t deadline_ms, const cli_line_fn * after,
                     uint32_t after_count)
{
	g_job.kind = kind;
	g_job.deadline_ms = deadline_ms;
	g_job.after_fns = after;
	g_job.after_count = after_count;
}

static void job_end(void)
{
	g_job.kind = JOB_IDLE;
	tx_puts("\r\n");
	cli_prompt();
}

static uint8_t job_emit_one(void)
{
	char text[CLI_OUT_LINE];
	lb_t b;
	uint32_t remaining;

	while(g_job.entry < g_job.count) {
		lb_init(&b, text, sizeof(text) - 2U);
		remaining = g_job.fns[g_job.entry](&b, g_job.sub);
		if(remaining != 0U) {
			g_job.sub++;
		}
		else {
			g_job.entry++;
			g_job.sub = 0U;
		}
		if(b.len == 0U) continue;     /* 此条目本次无内容 */
		b.cap = sizeof(text);         /* 构造时给换行留了 2 字节 */
		lb_c(&b, '\r');
		lb_c(&b, '\n');
		tx_put(text, b.len);
		return 1U;
	}
	return 0U;
}

static void job_service(uint32_t now)
{
	switch(g_job.kind) {
	case JOB_SAMPLE:
		if((int32_t)(now - g_job.deadline_ms) >= 0) {
			cli_freeze_report(now);
			job_print(g_job.after_fns, g_job.after_count);
		}
		break;
	case JOB_CAPTURE:
		if((GPS_CaptureGetState() != GPS_CAPTURE_RUNNING) ||
		   ((int32_t)(now - g_job.deadline_ms) >= 0)) {
			job_print(g_job.after_fns, g_job.after_count);
		}
		break;
	case JOB_REQUEST:
		if((GPS_RequestPending() == GPS_REQ_NONE) ||
		   ((int32_t)(now - g_job.deadline_ms) >= 0)) {
			cli_freeze_config();
			job_print(g_job.after_fns, g_job.after_count);
		}
		break;
	case JOB_PRINT:
		/* 发送环有空间才生成下一行：CLI 任务在这里等，不丢报告内容。 */
		while(tx_free() >= (CLI_OUT_LINE + 4U)) {
			if(job_emit_one() == 0U) {
				job_end();
				break;
			}
		}
		break;
	default:
		break;
	}
}

static uint8_t parse_uint(const char * text, uint32_t * value)
{
	uint32_t v = 0U;

	if((text == NULL) || (text[0] == '\0')) return 0U;
	for(; *text != '\0'; text++) {
		if((*text < '0') || (*text > '9')) return 0U;
		v = (v * 10U) + (uint32_t)(*text - '0');
		if(v > 100000U) return 0U;
	}
	*value = v;
	return 1U;
}

static void cli_error(const char * message)
{
	tx_puts("error: ");
	tx_puts(message);
	tx_puts("\r\n");
	g_cli_rejected++;
	cli_prompt();
}

static void cli_stop(void)
{
	if(g_job.kind == JOB_CAPTURE) GPS_CaptureStop();
	g_job.kind = JOB_IDLE;
	tx_puts("\r\nstopped\r\n");
	cli_prompt();
}

static uint8_t is_cmd(const char * a, const char * b)
{
	return (uint8_t)(strcmp(a, b) == 0);
}

static void cmd_request(GPS_Request_t request, const cli_line_fn * fns, uint32_t count, uint32_t now)
{
	if(GPS_Request(request) == 0U) {
		cli_error("gps task busy, try again");
		return;
	}
	job_wait(JOB_REQUEST, now + CLI_REQUEST_TIMEOUT_MS, fns, count);
	tx_puts("sent to receiver, waiting...\r\n");
}

/* CLI 的命令词。首个词不在表里的行不是命令。 */
static const char * const k_command_words[] = { "help", "version", "report", "gps", "sys", "stop" };

static uint8_t first_word_is_command(const char * line)
{
	uint32_t length = 0U;

	while((*line == ' ') || (*line == '\t')) line++;
	while((line[length] != '\0') && (line[length] != ' ') && (line[length] != '\t')) length++;
	for(uint32_t i = 0U; i < ARRAY_COUNT(k_command_words); i++) {
		if((strlen(k_command_words[i]) == length) && (strncmp(line, k_command_words[i], length) == 0)) {
			return 1U;
		}
	}
	return 0U;
}

static uint8_t (*g_line_fallback)(const char * line);

void Diag_CLI_SetLineFallback(uint8_t (*handler)(const char * line))
{
	g_line_fallback = handler;
}

static void cli_execute(char * line, uint32_t now)
{
	char * argv[CLI_TOKENS];
	uint32_t argc = 0U;
	uint32_t seconds;
	char * p = line;

	g_cli_commands++;
	/* 同一个串口上还有圈速差输入（"+1.23"）：首个词不是命令的非空行先交给它，命令执行中也照常处理。 */
	if(first_word_is_command(line) == 0U) {
		for(p = line; (*p == ' ') || (*p == '\t'); p++) { }
		if(*p == '\0') {
			cli_prompt();
			return;
		}
		if((g_line_fallback != NULL) && (g_line_fallback(line) != 0U)) {
			g_cli_fallback_lines++;
			cli_prompt();
			return;
		}
		cli_error("unknown command, type help");
		return;
	}
	p = line;
	while(*p != '\0') {
		while(*p == ' ' || *p == '\t') *p++ = '\0';
		if(*p == '\0') break;
		if(argc >= CLI_TOKENS) {
			cli_error("too many arguments");
			return;
		}
		argv[argc++] = p;
		while(*p != '\0' && *p != ' ' && *p != '\t') p++;
	}
	if(argc == 0U) {
		cli_prompt();
		return;
	}
	if(is_cmd(argv[0], "stop")) {
		cli_stop();
		return;
	}
	if(g_job.kind != JOB_IDLE) {
		cli_error("busy, type stop");
		return;
	}

	if(is_cmd(argv[0], "help")) {
		job_print(k_help_fns, ARRAY_COUNT(k_help_fns));
	}
	else if(is_cmd(argv[0], "version")) {
		job_print(k_version_fns, ARRAY_COUNT(k_version_fns));
	}
	else if(is_cmd(argv[0], "report")) {
		seconds = 10U;
		if(argc > 2U) { cli_error("usage: report [seconds]"); return; }
		if((argc == 2U) && (parse_uint(argv[1], &seconds) == 0U)) {
			cli_error("seconds must be a number"); return;
		}
		if((seconds < 1U) || (seconds > CLI_REPORT_MAX_S)) {
			cli_error("seconds must be 1..60"); return;
		}
		GPS_GetStats(&g_fz.s0);
		GPS_WindowReset();
		SpeedTrace_Reset();
		g_fz.start_ms = g_fz.s0.tick_ms;
		job_wait(JOB_SAMPLE, now + (seconds * 1000U), k_report_fns, ARRAY_COUNT(k_report_fns));
		tx_puts("sampling ");
		{
			char digits[12];
			lb_t b;
			lb_init(&b, digits, sizeof(digits));
			lb_u(&b, seconds);
			tx_put(digits, b.len);
		}
		tx_puts(" s, type stop to abort\r\n");
	}
	else if(is_cmd(argv[0], "sys")) {
		if((argc != 2U) || !is_cmd(argv[1], "status")) { cli_error("usage: sys status"); return; }
		job_print(k_sys_fns, ARRAY_COUNT(k_sys_fns));
	}
	else if(is_cmd(argv[0], "gps")) {
		if(argc < 2U) { cli_error("usage: gps status|config|capture|rate|smooth|save"); return; }
		if(is_cmd(argv[1], "status")) {
			job_print(k_status_fns, ARRAY_COUNT(k_status_fns));
		}
		else if(is_cmd(argv[1], "config")) {
			cmd_request(GPS_REQ_QUERY, k_config_fns, ARRAY_COUNT(k_config_fns), now);
		}
		else if(is_cmd(argv[1], "save")) {
			cmd_request(GPS_REQ_SAVE, k_request_fns, ARRAY_COUNT(k_request_fns), now);
		}
		else if(is_cmd(argv[1], "rate")) {
			if(argc != 3U) { cli_error("usage: gps rate 10|20"); return; }
			if(is_cmd(argv[2], "10")) {
				cmd_request(GPS_REQ_RATE_10HZ, k_request_fns, ARRAY_COUNT(k_request_fns), now);
			}
			else if(is_cmd(argv[2], "20")) {
				cmd_request(GPS_REQ_RATE_20HZ, k_request_fns, ARRAY_COUNT(k_request_fns), now);
			}
			else {
				cli_error("rate must be 10 or 20");
			}
		}
		else if(is_cmd(argv[1], "smooth")) {
			if(argc != 3U) { cli_error("usage: gps smooth on|off"); return; }
			if(is_cmd(argv[2], "on")) {
				cmd_request(GPS_REQ_SMOOTH_ON, k_request_fns, ARRAY_COUNT(k_request_fns), now);
			}
			else if(is_cmd(argv[2], "off")) {
				cmd_request(GPS_REQ_SMOOTH_OFF, k_request_fns, ARRAY_COUNT(k_request_fns), now);
			}
			else {
				cli_error("smooth must be on or off");
			}
		}
		else if(is_cmd(argv[1], "capture")) {
			seconds = 15U;
			if(argc > 3U) { cli_error("usage: gps capture [seconds]"); return; }
			if((argc == 3U) && (parse_uint(argv[2], &seconds) == 0U)) {
				cli_error("seconds must be a number"); return;
			}
			if((seconds < 1U) || (seconds > CLI_CAPTURE_MAX_S)) {
				cli_error("seconds must be 1..20"); return;
			}
			if(GPS_CaptureStart(seconds) == 0U) { cli_error("capture failed"); return; }
			g_fz.capture_seconds = seconds;
			job_wait(JOB_CAPTURE, now + (seconds * 1000U) + 1000U, k_capture_fns,
			         ARRAY_COUNT(k_capture_fns));
			tx_puts("capturing, type stop to abort\r\n");
		}
		else {
			cli_error("unknown gps command");
		}
	}
	else {
		cli_error("unknown command, type help");
	}
}

/* ---- 输入：行编辑 ---- */

static void rx_service(uint32_t now)
{
	while(g_rx_tail != g_rx_head) {
		uint8_t c = g_rx_ring[g_rx_tail];

		g_rx_tail = (g_rx_tail + 1U) & (CLI_RX_RING - 1U);
		if(c == 0x03U) {                       /* Ctrl-C */
			g_line_len = 0U;
			cli_stop();
			g_last_was_cr = 0U;
			continue;
		}
		if(c == '\r' || c == '\n') {
			if((c == '\n') && (g_last_was_cr != 0U)) {   /* CRLF 只算一次 */
				g_last_was_cr = 0U;
				continue;
			}
			g_last_was_cr = (c == '\r') ? 1U : 0U;
			tx_puts("\r\n");
			g_line[g_line_len] = '\0';
			g_line_len = 0U;
			cli_execute(g_line, now);
			continue;
		}
		g_last_was_cr = 0U;
		if((c == 0x08U) || (c == 0x7FU)) {      /* 退格 */
			if(g_line_len > 0U) {
				g_line_len--;
				tx_puts("\b \b");
			}
			continue;
		}
		if((c >= 0x20U) && (c < 0x7FU)) {
			if(g_line_len < (CLI_LINE_MAX - 1U)) {
				g_line[g_line_len++] = (char)c;
				tx_put((const char *)&c, 1U);
			}
			else {
				tx_puts("\a");                 /* 行太长，忽略多余字符 */
			}
		}
	}
}

void Diag_CLI_Step(uint32_t now)
{
	if((g_rx_stalled != 0U) && ((now - g_rx_stall_tick) >= CLI_RX_STALL_RETRY_MS)) {
		g_rx_stalled = 0U;
		g_rx_err_streak = 0U;
		cli_hw_rx_arm();
	}
	rx_service(now);
	job_service(now);
	tx_kick();
}

static void Diag_CLI_TaskFunc(void * argument)
{
	(void)argument;
	for(;;) {
		Diag_CLI_Step(HAL_GetTick());
		vTaskDelay(pdMS_TO_TICKS(10U));
	}
}

void Diag_CLI_Init(void)
{
	g_cli_task = xTaskCreateStatic(Diag_CLI_TaskFunc, "CLI", CLI_STACK_WORDS, NULL,
	                               (UBaseType_t)osPriorityLow,
	                               g_cli_task_stack, &g_cli_task_control);
	Firmware_Diag_RegisterTask(FW_TASK_CLI, g_cli_task, "CLI", sizeof(g_cli_task_stack));
	cli_hw_rx_arm();
}
