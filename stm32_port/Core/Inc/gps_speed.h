#ifndef GPS_SPEED_H
#define GPS_SPEED_H

/* RMC 速度与位置的有效性判断。屏幕、CAN、计时和 CLI 共用这一份状态，不再各自清零、
 * 各自保持。本模块不含 HAL/RTOS 调用，不加锁：由 GPS 任务修改，其他任务取走拷贝
 * （gps.c 在临界区内拷贝）。时间单位均为 ms，来自 32 位 MCU 计时，按无符号差值处理回绕。 */

#include <stdint.h>

/* 接收机配置的 RMC 周期（GPRMC 0.05，20 Hz）。门限按实测周期的 3 倍收敛，见 GPS_Speed_FreshLimit。 */
#define GPS_RMC_NOMINAL_PERIOD_MS    50U
#define GPS_FRESH_PERIODS             3U
#define GPS_FRESH_MIN_MS            150U   /* 20 Hz × 3 */
#define GPS_FRESH_MAX_MS           1500U   /* 低速率输出时的上限，防止门限无限放大 */
/* 超过此间隔没有新历元时，不再拿旧历元做重复/倒退比较（断流后重新建立基准）。 */
#define GPS_EPOCH_BASELINE_MS      1000U
#define GPS_SPEED_EVENT_COUNT        16U
#define GPS_SLOW_STREAK_ADAPT         3U   /* 连续几次间隔超过门限才调整门限 */

typedef enum {
    GPS_SPEED_REASON_OK = 0,
    GPS_SPEED_REASON_NO_DATA,    /* 上电后还没有收到过可信 RMC */
    GPS_SPEED_REASON_STALE,      /* 最后一条可信 RMC 超过新鲜度门限 */
    GPS_SPEED_REASON_NO_FIX      /* 接收机报告 RMC 状态 V 或模式指示 N */
} gps_speed_reason_t;

typedef enum {
    GPS_RMC_ACCEPTED = 0,
    GPS_RMC_NO_FIX,              /* 状态 V / 模式 N：立即使速度失效 */
    GPS_RMC_BAD_FIELD,           /* 状态 A 但速度为空或不是数字：不当作 0，也不续期 */
    GPS_RMC_DUP_EPOCH,           /* UTC 历元与上一条相同：重复输出，不续期 */
    GPS_RMC_BACK_EPOCH           /* UTC 历元倒退：乱序或模块重启，不续期 */
} gps_rmc_verdict_t;

/* 一条已通过校验和的 RMC 解出的字段。 */
typedef struct {
    uint8_t  fix_ok;             /* 状态 A 且模式指示不是 N */
    uint8_t  speed_present;      /* 速度字段是合法数字 */
    int32_t  speed_centi;        /* 0.01 km/h，已限幅到 0..30000 */
    uint8_t  position_present;   /* 经纬度字段合法 */
    uint8_t  epoch_present;      /* UTC 时间字段合法 */
    uint8_t  epoch_fine;         /* 时间字段带小数秒；只有整秒时无法区分同秒内的多条，跳过重复检查 */
    uint32_t epoch_ms;           /* UTC 当日毫秒 */
} gps_rmc_sample_t;

typedef struct {
    uint32_t tick;
    uint8_t  valid;
    uint8_t  reason;
    uint16_t age_ms;             /* 切换瞬间的数据龄期，饱和到 65535 */
} gps_speed_event_t;

typedef struct {
    int32_t  speed_centi;
    uint32_t fresh_tick;         /* 最后一条被接受的 RMC 的 MCU 时刻 */
    uint8_t  has_fresh;
    uint8_t  speed_invalid;      /* 最后一条 RMC 报告无定位，速度立即失效 */
    uint32_t pos_tick;
    uint8_t  has_pos;
    uint8_t  pos_invalid;
    uint8_t  have_epoch;
    uint32_t epoch_ms;
    uint32_t epoch_tick;
    uint32_t period_ms;          /* 被接受的 RMC 间隔的滑动平均 */
    uint8_t  slow_streak;        /* 连续超过门限的间隔数 */
    uint32_t continuity_seq;     /* 每次在失效后重新接受数据时加一，计时模块据此重建锚点 */
    uint32_t accepted;
    uint32_t last_interval_ms;   /* 与上一条被接受的 RMC 的 MCU 间隔，首条为 0 */
    uint32_t last_epoch_delta_ms;/* 与上一条被接受的 RMC 的 UTC 间隔，无基准时为 0 */
    uint8_t  last_reject;        /* 最近一次被拒绝的原因，gps_rmc_verdict_t */
    uint8_t  prev_valid;
    uint8_t  event_head;
    uint32_t event_total;
    gps_speed_event_t events[GPS_SPEED_EVENT_COUNT];
} gps_speed_state_t;

typedef struct {
    uint8_t  valid;
    uint8_t  reason;             /* gps_speed_reason_t，valid 为 1 时是 OK */
    int32_t  speed_centi;        /* 无效时为 0，调用方不得把它当作真实的 0 km/h */
    uint32_t age_ms;
    uint32_t limit_ms;
} gps_speed_view_t;

void GPS_Speed_Init(gps_speed_state_t * st);
/* 处理一条 RMC。position_refreshed（可为 NULL）返回位置是否被续期。 */
gps_rmc_verdict_t GPS_Speed_OnRmc(gps_speed_state_t * st, const gps_rmc_sample_t * rmc,
                                  uint32_t now, uint8_t * position_refreshed);
/* 新鲜度门限：3 个实测输出周期，夹在 [GPS_FRESH_MIN_MS, GPS_FRESH_MAX_MS]。 */
uint32_t GPS_Speed_FreshLimit(const gps_speed_state_t * st);
/* max_age_ms 为 0 时用 GPS_Speed_FreshLimit；CAN 另有独立门限，见 gps.c。 */
void GPS_Speed_GetView(const gps_speed_state_t * st, uint32_t now, uint32_t max_age_ms,
                       gps_speed_view_t * out);
uint8_t GPS_Speed_PositionFresh(const gps_speed_state_t * st, uint32_t now);
/* 周期性调用（GPS 任务每 10 ms）。有效性相对上次调用变化时记事件并返回 1。 */
uint8_t GPS_Speed_Poll(gps_speed_state_t * st, uint32_t now);
const char * GPS_Speed_ReasonName(uint8_t reason);
const char * GPS_Speed_VerdictName(uint8_t verdict);

#endif
