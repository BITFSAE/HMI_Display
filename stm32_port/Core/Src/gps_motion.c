#include "gps_motion.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define MOTION_RAD_PER_DEG 0.01745329251994329577f
#define MOTION_DAY_MS 86400000L

static int32_t motion_epoch_delta(uint32_t newer, uint32_t older)
{
    int32_t delta = (int32_t)newer - (int32_t)older;
    if(delta > MOTION_DAY_MS / 2) delta -= MOTION_DAY_MS;
    if(delta < -MOTION_DAY_MS / 2) delta += MOTION_DAY_MS;
    return delta;
}

static float motion_wrap_angle(float angle)
{
    angle = fmodf(angle + 180.0f, 360.0f);
    if(angle < 0.0f) angle += 360.0f;
    return angle - 180.0f;
}

void GPS_Motion_Init(gps_motion_t *state)
{
    memset(state, 0, sizeof(*state));
}

void GPS_Motion_InvalidateRmc(gps_motion_t *state)
{
    state->speed_valid = 0U;
    state->track_valid = 0U;
}

void GPS_Motion_OnRmc(gps_motion_t *state, uint32_t epoch_ms, uint8_t epoch_valid,
                     int32_t speed_centi_kmh, float track_deg, uint8_t track_valid,
                     uint32_t tick_ms)
{
    state->rmc_seen = 1U;
    state->rmc_epoch_ms = epoch_ms;
    state->epoch_valid = (uint8_t)(epoch_valid && epoch_ms < (uint32_t)MOTION_DAY_MS);
    state->rmc_tick_ms = tick_ms;
    state->speed_centi_kmh = speed_centi_kmh;
    state->speed_valid = (uint8_t)(speed_centi_kmh >= 0 && speed_centi_kmh <= 30000);
    state->track_deg = track_deg;
    state->track_valid = (uint8_t)(track_valid && isfinite(track_deg) &&
                                  track_deg >= 0.0f && track_deg <= 360.0f);
    state->sample_counter++;
}

void GPS_Motion_OnHeading(gps_motion_t *state, uint32_t epoch_ms, float heading_deg,
                         uint8_t valid, uint32_t tick_ms)
{
    if(!valid || epoch_ms >= (uint32_t)MOTION_DAY_MS || !isfinite(heading_deg)) {
        state->heading_seen = 0U;
        state->estimate_valid = 0U;
        return;
    }
    if(state->heading_seen) {
        int32_t dt = motion_epoch_delta(epoch_ms, state->hpr_epoch_ms);
        uint32_t arrival_gap = tick_ms - state->hpr_tick_ms;
        /* 重复/乱序不续期；长时间断流后允许以新历元重新建立基准。 */
        if(dt <= 0 && arrival_gap <= GPS_MOTION_HPR_GAP_MAX_MS) return;
        state->estimate_valid = 0U;
        if(dt > 0 && dt <= (int32_t)GPS_MOTION_HPR_GAP_MAX_MS &&
           arrival_gap <= GPS_MOTION_HPR_GAP_MAX_MS) {
            float rate = -motion_wrap_angle(heading_deg - state->heading_deg) *
                         (1000.0f / (float)dt) * MOTION_RAD_PER_DEG;
            /* 估算量程与 int16、0.01 °/s 的 ECU 输入一致；突跳不作为角速度。 */
            if(fabsf(rate) <= 327.68f * MOTION_RAD_PER_DEG) {
                state->estimated_rate_rad_s = rate;
                state->estimate_valid = 1U;
            }
        }
    } else {
        state->estimate_valid = 0U;
    }
    state->heading_seen = 1U;
    state->heading_deg = motion_wrap_angle(heading_deg);
    state->hpr_epoch_ms = epoch_ms;
    state->hpr_tick_ms = tick_ms;
}

void GPS_Motion_Get(const gps_motion_t *state, const gps_motion_imu_t *imu,
                    uint32_t now, gps_motion_output_t *out)
{
    uint32_t age = state->rmc_seen ? now - state->rmc_tick_ms : UINT32_MAX;
    uint8_t hpr_fresh = (uint8_t)(state->heading_seen &&
        now - state->hpr_tick_ms <= GPS_MOTION_HPR_STALE_MS);
    float rate = 0.0f;
    int32_t epoch_delta = 0;
    memset(out, 0, sizeof(*out));
    out->vx_cms = INT16_MIN;
    out->vy_cms = INT16_MIN;
    out->sample_counter = state->sample_counter;
    out->sample_age_ms = age > UINT16_MAX ? UINT16_MAX : (uint16_t)age;
    if(state->speed_valid && age <= GPS_MOTION_RMC_STALE_MS)
        out->flags |= GPS_MOTION_SPEED_VALID;
    if(state->track_valid && age <= GPS_MOTION_RMC_STALE_MS)
        out->flags |= GPS_MOTION_TRACK_VALID;
    if(imu != NULL && imu->valid && now - imu->tick_ms <= GPS_MOTION_IMU_STALE_MS) {
        rate = (float)imu->rate_cdeg_s * (MOTION_RAD_PER_DEG / 100.0f);
        out->flags |= GPS_MOTION_YAW_VALID;
    } else if(hpr_fresh && state->estimate_valid) {
        rate = state->estimated_rate_rad_s;
        out->flags |= GPS_MOTION_YAW_VALID | GPS_MOTION_YAW_ESTIMATED;
    }
    if(hpr_fresh && state->rmc_seen && state->epoch_valid) {
        epoch_delta = motion_epoch_delta(state->rmc_epoch_ms, state->hpr_epoch_ms);
        if(epoch_delta >= -(int32_t)GPS_MOTION_HPR_STALE_MS &&
           epoch_delta <= (int32_t)GPS_MOTION_HPR_STALE_MS)
            out->flags |= GPS_MOTION_HEADING_VALID;
    }
    const uint8_t required = GPS_MOTION_SPEED_VALID | GPS_MOTION_TRACK_VALID |
                             GPS_MOTION_HEADING_VALID | GPS_MOTION_YAW_VALID;
    if((out->flags & required) != required) return;

    /* 航向按 GNSS UTC 外推到 RMC 历元；IMU 只有接收时间，仍有传感器延迟误差。 */
    float heading = state->heading_deg * MOTION_RAD_PER_DEG -
                    rate * ((float)epoch_delta / 1000.0f);
    float angle = state->track_deg * MOTION_RAD_PER_DEG - heading;
    float speed = (float)state->speed_centi_kmh / 360.0f;
    float vx = speed * cosf(angle) + rate * GPS_MOTION_MASTER_Y_M;
    float vy = -speed * sinf(angle) - rate * GPS_MOTION_MASTER_X_M;
    if(!isfinite(vx) || !isfinite(vy) || fabsf(vx) > 327.67f || fabsf(vy) > 327.67f) {
        out->flags |= GPS_MOTION_SATURATED;
        return;
    }
    out->vx_cms = (int16_t)lroundf(vx * 100.0f);
    out->vy_cms = (int16_t)lroundf(vy * 100.0f);
    out->flags |= GPS_MOTION_VALID;
}
