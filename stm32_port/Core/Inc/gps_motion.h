#ifndef GPS_MOTION_H
#define GPS_MOTION_H

#include <stdint.h>

/* 坐标为 X 向前、Y 向左；杆臂从质心指向主天线，单位 m。 */
#define GPS_MOTION_MASTER_X_M           (-0.512f)
#define GPS_MOTION_MASTER_Y_M             0.080f
#define GPS_MOTION_IMU_STALE_MS           100U
#define GPS_MOTION_RMC_STALE_MS           150U
#define GPS_MOTION_HPR_STALE_MS           300U
#define GPS_MOTION_HPR_GAP_MAX_MS         400U

#define GPS_MOTION_VALID                 (1U << 0)
#define GPS_MOTION_SPEED_VALID           (1U << 1)
#define GPS_MOTION_TRACK_VALID           (1U << 2)
#define GPS_MOTION_HEADING_VALID         (1U << 3)
#define GPS_MOTION_YAW_VALID             (1U << 4)
#define GPS_MOTION_YAW_ESTIMATED         (1U << 5)
#define GPS_MOTION_SATURATED             (1U << 6)

typedef struct {
    int16_t rate_cdeg_s;       /* 0.01 °/s，左转为正；没有帧内健康状态。 */
    uint8_t valid;
    uint32_t tick_ms;          /* CAN 接收时刻，32 位毫秒回绕。 */
} gps_motion_imu_t;

typedef struct {
    int16_t vx_cms;
    int16_t vy_cms;
    uint8_t flags;
    uint8_t sample_counter;
    uint16_t sample_age_ms;
} gps_motion_output_t;

/* 仅由 GPS 任务持有和修改，不含 HAL/RTOS 调用。UTC 为当日毫秒。 */
typedef struct {
    uint32_t rmc_epoch_ms, rmc_tick_ms;
    int32_t speed_centi_kmh;
    float track_deg;
    uint8_t rmc_seen, speed_valid, track_valid, epoch_valid, sample_counter;
    uint32_t hpr_epoch_ms, hpr_tick_ms;
    float heading_deg, estimated_rate_rad_s;
    uint8_t heading_seen, estimate_valid;
} gps_motion_t;

void GPS_Motion_Init(gps_motion_t *state);
void GPS_Motion_InvalidateRmc(gps_motion_t *state);
void GPS_Motion_OnRmc(gps_motion_t *state, uint32_t epoch_ms, uint8_t epoch_valid,
                     int32_t speed_centi_kmh, float track_deg, uint8_t track_valid,
                     uint32_t tick_ms);
void GPS_Motion_OnHeading(gps_motion_t *state, uint32_t epoch_ms, float heading_deg,
                         uint8_t valid, uint32_t tick_ms);
void GPS_Motion_Get(const gps_motion_t *state, const gps_motion_imu_t *imu,
                    uint32_t now, gps_motion_output_t *out);

#endif
