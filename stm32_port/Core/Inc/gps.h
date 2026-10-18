#ifndef __GPS_H
#define __GPS_H

#include <stdint.h>
#include "gps_speed.h"

#define GPS_DMA_BUF_SIZE  512U

#define GPS_SIG_LEVEL_NONE      0
#define GPS_SIG_LEVEL_WEAK      1
#define GPS_SIG_LEVEL_FAIR      2
#define GPS_SIG_LEVEL_GOOD      3
#define GPS_SIG_LEVEL_EXCELLENT 4

#define GPS_SNR_EXCELLENT  42
#define GPS_SNR_GOOD       35
#define GPS_SNR_FAIR       28
#define GPS_SNR_WEAK       20

/* GPHPR heading describes the ANT1-to-ANT2 baseline.  Set this offset to the
 * clockwise angle from that baseline to the vehicle forward direction. */
#define GPS_HEADING_INSTALL_OFFSET_DEG  0.0f

typedef struct {
	/* 定位有效：GGA 定位质量 > 0 且 GGA 与 RMC 位置都在新鲜度门限内。 */
	uint8_t  valid;
	float    latitude;
	float    longitude;
	float    altitude;
	float    track_angle;
	float    heading_angle;
	/* 速度无效时为 0；调用方必须先看 speed_valid，不能把 0 当作停车。 */
	float    speed_kmh;
	uint8_t  speed_valid;
	uint8_t  speed_reason;       /* gps_speed_reason_t */
	uint16_t speed_age_ms;       /* 最后一条可信 RMC 至今，饱和到 65535 */
	/* 每次在速度失效后重新接受数据时加一；计时模块比较它来识别断流并重建锚点。 */
	uint32_t continuity_seq;
	uint8_t  heading_valid;
	uint8_t  heading_quality;
	uint8_t  fix_quality;
	uint8_t  satellites;
	uint8_t  signal_level;
	int8_t   max_snr;
	int8_t   avg_snr;
	uint8_t  gsv_tracked_sats;
} GPS_Data_t;

/* 累计计数，上电清零，32 位回绕。CLI 取两次快照相减得到一个采样窗口内的增量。 */
typedef struct {
	uint32_t tick_ms;
	/* 串口与 DMA */
	uint32_t dma_bytes;
	uint32_t uart_err_pe;
	uint32_t uart_err_ne;
	uint32_t uart_err_fe;
	uint32_t uart_err_ore;
	uint32_t uart_err_dma;
	uint32_t rx_restarts;          /* 因错误或接收停止而重启 DMA 的次数 */
	uint32_t rx_restart_fail;
	uint32_t poll_gap_max_ms;      /* 轮询间隔最大值（累计） */
	uint32_t late_poll;            /* 轮询间隔 ≥ 44 ms 的次数：可能丢字节，不是精确丢帧数 */
	uint32_t ring_pending_max;     /* 一次轮询时环里待读字节数最大值，接近 512 即将覆盖 */
	/* 行与语句 */
	uint32_t checksum_fail;
	uint32_t line_overflow;
	uint32_t line_truncated;       /* 行未结束就遇到新的 '$' */
	uint32_t sentences;            /* 通过校验和的 NMEA */
	uint32_t n_rmc;
	uint32_t n_gga;
	uint32_t n_hpr;
	uint32_t n_gsv;
	uint32_t n_reply;              /* $command 回执 */
	uint32_t n_other;              /* 白名单之外的语句：非零说明 UNLOG/白名单没有生效 */
	/* RMC 处理结果 */
	uint32_t rmc_accepted;
	uint32_t rmc_no_fix;
	uint32_t rmc_bad_field;
	uint32_t rmc_dup_epoch;
	uint32_t rmc_back_epoch;
	uint32_t speed_invalid_events; /* 速度由有效变为无效的次数 */
	/* 最近值 */
	int32_t  raw_speed_centi;      /* 最近一条被接受的 RMC 速度，0.01 km/h */
	int32_t  shown_speed_kmh;      /* 最近提交给界面的值，-1 表示 "--" */
	uint32_t last_epoch_ms;        /* 最近一条被接受的 RMC 的 UTC 当日毫秒 */
	uint32_t rmc_period_ms;        /* 被接受的 RMC 间隔的滑动平均 */
	uint32_t fresh_limit_ms;       /* 当前新鲜度门限 */
} GPS_Stats_t;

/* 采样窗口内的极值，GPS_WindowReset 清零。 */
typedef struct {
	uint32_t start_tick_ms;
	uint32_t samples;              /* 窗口内被接受的 RMC 数 */
	uint32_t interval_min_ms;      /* 相邻被接受 RMC 的 MCU 间隔 */
	uint32_t interval_max_ms;
	uint32_t epoch_min_ms;         /* 相邻被接受 RMC 的 UTC 历元间隔 */
	uint32_t epoch_max_ms;
	int32_t  speed_min_centi;
	int32_t  speed_max_centi;
	uint32_t age_max_ms;           /* 轮询时观察到的速度数据龄期最大值 */
	uint32_t valid_ms;             /* 速度有效的累计时间 */
	uint32_t poll_gap_max_ms;
	uint32_t ring_pending_max;
} GPS_Window_t;

#define GPS_CAPTURE_MAX_RECORDS  420U
#define GPS_CAPTURE_NO_EPOCH     0xFFFFFFFFUL
/* verdict 取 gps_rmc_verdict_t；速度由有效变无效时额外记一条，verdict = 0x80 | 原因。 */
#define GPS_CAPTURE_EVENT_FLAG   0x80U

typedef struct {
	uint32_t tick_ms;
	uint32_t epoch_ms;
	int32_t  speed_centi;          /* 原始 RMC 速度；INT32_MIN 表示字段缺失 */
	int16_t  shown_kmh;            /* 提交给界面的值；-1 表示 "--" */
	uint8_t  verdict;
	uint8_t  valid;                /* 记录时刻速度是否有效 */
} GPS_CaptureRec_t;

typedef enum {
	GPS_CAPTURE_IDLE = 0,
	GPS_CAPTURE_RUNNING,
	GPS_CAPTURE_DONE
} GPS_CaptureState_t;

#define GPS_CFG_MAX_ITEMS        8U
#define GPS_CFG_REPLY_LEN       40U
#define GPS_RAW_LINES           16U
#define GPS_RAW_LINE_LEN        96U

typedef enum {
	GPS_CFG_PENDING = 0,
	GPS_CFG_OK,                    /* 收到回执 OK */
	GPS_CFG_REJECTED,              /* 收到回执但不是 OK */
	GPS_CFG_NOACK,                 /* 重试后仍无回执 */
	GPS_CFG_UNCONFIRMED            /* 不要求或无法确认，只按固定时间等待 */
} GPS_CfgState_t;

typedef struct {
	const char * cmd;
	uint8_t  state;
	uint8_t  attempts;
	char     reply[GPS_CFG_REPLY_LEN];
} GPS_CfgItem_t;

typedef struct {
	uint8_t  running;
	uint8_t  ack_seen;             /* 至少收到过一条回执 */
	uint8_t  ack_unsupported;      /* 连续无回执后放弃确认，改用固定等待 */
	uint8_t  count;
	GPS_CfgItem_t item[GPS_CFG_MAX_ITEMS];
	GPS_CfgItem_t request;         /* 最近一次 CLI 请求（gps rate / gps save）的结果 */
} GPS_CfgStatus_t;

extern uint8_t g_gps_dma_buf[GPS_DMA_BUF_SIZE];

void GPS_Init(void);
void GPS_ISR_Notify(void);
void GPS_GetData(GPS_Data_t * out);

/* 供 CAN 发送使用：以给定门限（ms）判断速度是否有效，0 表示用本地新鲜度门限。 */
void GPS_GetSpeedView(uint32_t max_age_ms, gps_speed_view_t * out);

/* 诊断接口，可从任意任务调用，返回一致的拷贝。 */
void GPS_GetStats(GPS_Stats_t * out);
void GPS_WindowReset(void);
void GPS_GetWindow(GPS_Window_t * out);
/* 最近的速度有效性切换，newest first；返回条数。 */
uint32_t GPS_GetSpeedEvents(gps_speed_event_t * out, uint32_t max_events);

uint8_t GPS_CaptureStart(uint32_t seconds);
void GPS_CaptureStop(void);
GPS_CaptureState_t GPS_CaptureGetState(void);
uint32_t GPS_CaptureCount(void);
uint8_t GPS_CaptureGet(uint32_t index, GPS_CaptureRec_t * out);

void GPS_GetConfigStatus(GPS_CfgStatus_t * out);

/* 接收机查询与可恢复的临时设置。命令由 GPS 任务发送，避免多个任务同时占用 USART3。
 * 同一时刻只接受一个请求，已有请求未完成时返回 0。 */
typedef enum {
	GPS_REQ_NONE = 0,
	GPS_REQ_QUERY,                 /* VERSION、MODE、CONFIG、UNILOGLIST，收集原始回复 */
	GPS_REQ_RATE_10HZ,             /* GPRMC 0.1，不保存，重启恢复 */
	GPS_REQ_RATE_20HZ,             /* GPRMC 0.05 */
	GPS_REQ_SMOOTH_ON,             /* CONFIG SMOOTH PSRVEL enable，多普勒速度平滑，不保存 */
	GPS_REQ_SMOOTH_OFF,            /* CONFIG SMOOTH PSRVEL disable（接收机默认） */
	GPS_REQ_SAVE                   /* SAVECONFIG */
} GPS_Request_t;

uint8_t GPS_Request(GPS_Request_t request);
GPS_Request_t GPS_RequestPending(void);
/* 最近一次请求的原始回复行；返回行数，行内容以 '\0' 结尾。 */
uint32_t GPS_GetRawLines(char lines[][GPS_RAW_LINE_LEN], uint32_t max_lines);
uint32_t GPS_RawLinesDropped(void);

/* HAL 回调转发：USART3 错误或接收被中止后重启循环 DMA 接收（中断上下文）。 */
void GPS_UartErrorCallback(uint32_t error_code);

#endif
