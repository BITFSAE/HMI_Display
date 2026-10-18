#include "gps_speed.h"

#include <string.h>

#define GPS_DAY_MS  86400000UL

static uint32_t clamp_u32(uint32_t value, uint32_t low, uint32_t high)
{
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

void GPS_Speed_Init(gps_speed_state_t * st)
{
    (void)memset(st, 0, sizeof(*st));
    st->period_ms = GPS_RMC_NOMINAL_PERIOD_MS;
}

uint32_t GPS_Speed_FreshLimit(const gps_speed_state_t * st)
{
    return clamp_u32(st->period_ms * GPS_FRESH_PERIODS, GPS_FRESH_MIN_MS, GPS_FRESH_MAX_MS);
}

/* 间隔按 1/8 权重并入周期估计。超过当前门限的间隔先当作断流：连续三次都超过才认为接收机
 * 的输出率真的降低了（如配置没生效退回 1 Hz），之后每个间隔最多按 4 倍当前周期计入，
 * 门限十来条内跟上；偶发的断流不会抬高门限。超过门限上限的间隔一律不参与估计。 */
static void period_update(gps_speed_state_t * st, uint32_t interval, uint32_t limit)
{
    uint32_t cap;

    if((interval < 10U) || (interval >= GPS_FRESH_MAX_MS)) return;
    if(interval > limit) {
        if(st->slow_streak < 255U) st->slow_streak++;
        if(st->slow_streak < GPS_SLOW_STREAK_ADAPT) return;
    }
    else {
        st->slow_streak = 0U;
    }
    cap = st->period_ms * 4U;
    if(interval > cap) interval = cap;
    st->period_ms = clamp_u32(((st->period_ms * 7U) + interval + 4U) / 8U, 20U, 1000U);
}

gps_rmc_verdict_t GPS_Speed_OnRmc(gps_speed_state_t * st, const gps_rmc_sample_t * rmc,
                                  uint32_t now, uint8_t * position_refreshed)
{
    uint8_t epoch_checked = 0U;
    uint32_t epoch_delta = 0U;
    uint32_t interval = 0U;
    uint8_t was_valid;
    uint32_t limit;

    if(position_refreshed != NULL) *position_refreshed = 0U;

    if(rmc->fix_ok == 0U) {
        st->speed_invalid = 1U;
        st->pos_invalid = 1U;
        st->last_reject = (uint8_t)GPS_RMC_NO_FIX;
        return GPS_RMC_NO_FIX;
    }

    if((rmc->epoch_present != 0U) && (rmc->epoch_fine != 0U)) {
        uint32_t epoch = rmc->epoch_ms % GPS_DAY_MS;

        if((st->have_epoch != 0U) && ((now - st->epoch_tick) <= GPS_EPOCH_BASELINE_MS)) {
            /* 模 24 小时取前进量：跨午夜（23:59:59.95 → 00:00:00.00）仍是 +50 ms。 */
            epoch_delta = (epoch + GPS_DAY_MS - st->epoch_ms) % GPS_DAY_MS;
            if(epoch_delta == 0U) {
                st->last_reject = (uint8_t)GPS_RMC_DUP_EPOCH;
                return GPS_RMC_DUP_EPOCH;
            }
            if(epoch_delta > (GPS_DAY_MS / 2U)) {
                st->last_reject = (uint8_t)GPS_RMC_BACK_EPOCH;
                return GPS_RMC_BACK_EPOCH;
            }
            epoch_checked = 1U;
        }
        st->have_epoch = 1U;
        st->epoch_ms = epoch;
        st->epoch_tick = now;
    }
    else {
        /* 没有可比的小数秒历元：不做重复/倒退检查，并丢弃旧基准。 */
        st->have_epoch = 0U;
    }

    if(rmc->position_present != 0U) {
        st->pos_tick = now;
        st->has_pos = 1U;
        st->pos_invalid = 0U;
        if(position_refreshed != NULL) *position_refreshed = 1U;
    }

    if(rmc->speed_present == 0U) {
        st->last_reject = (uint8_t)GPS_RMC_BAD_FIELD;
        return GPS_RMC_BAD_FIELD;
    }

    /* 失效后（包括上电首条）重新接受数据：计时模块据此丢弃旧锚点。 */
    limit = GPS_Speed_FreshLimit(st);
    was_valid = (uint8_t)((st->has_fresh != 0U) && (st->speed_invalid == 0U) &&
                          ((now - st->fresh_tick) <= limit));
    if(was_valid == 0U) st->continuity_seq++;
    if(st->has_fresh != 0U) {
        interval = now - st->fresh_tick;
        period_update(st, interval, limit);
    }
    st->last_interval_ms = (st->has_fresh != 0U) ? interval : 0U;
    st->last_epoch_delta_ms = (epoch_checked != 0U) ? epoch_delta : 0U;
    st->speed_centi = rmc->speed_centi;
    st->fresh_tick = now;
    st->has_fresh = 1U;
    st->speed_invalid = 0U;
    st->accepted++;
    return GPS_RMC_ACCEPTED;
}

void GPS_Speed_GetView(const gps_speed_state_t * st, uint32_t now, uint32_t max_age_ms,
                       gps_speed_view_t * out)
{
    uint32_t age;

    out->limit_ms = (max_age_ms != 0U) ? max_age_ms : GPS_Speed_FreshLimit(st);
    out->speed_centi = 0;
    out->valid = 0U;
    if(st->speed_invalid != 0U) {
        out->reason = (uint8_t)GPS_SPEED_REASON_NO_FIX;
        out->age_ms = (st->has_fresh != 0U) ? (now - st->fresh_tick) : 0U;
        return;
    }
    if(st->has_fresh == 0U) {
        out->reason = (uint8_t)GPS_SPEED_REASON_NO_DATA;
        out->age_ms = 0U;
        return;
    }
    age = now - st->fresh_tick;
    out->age_ms = age;
    if(age > out->limit_ms) {
        out->reason = (uint8_t)GPS_SPEED_REASON_STALE;
        return;
    }
    out->valid = 1U;
    out->reason = (uint8_t)GPS_SPEED_REASON_OK;
    out->speed_centi = st->speed_centi;
}

uint8_t GPS_Speed_PositionFresh(const gps_speed_state_t * st, uint32_t now)
{
    if((st->has_pos == 0U) || (st->pos_invalid != 0U)) return 0U;
    return (uint8_t)((now - st->pos_tick) <= GPS_Speed_FreshLimit(st));
}

uint8_t GPS_Speed_Poll(gps_speed_state_t * st, uint32_t now)
{
    gps_speed_view_t view;
    gps_speed_event_t * event;

    GPS_Speed_GetView(st, now, 0U, &view);
    if(view.valid == st->prev_valid) return 0U;
    st->prev_valid = view.valid;
    event = &st->events[st->event_head];
    event->tick = now;
    event->valid = view.valid;
    event->reason = view.reason;
    event->age_ms = (view.age_ms > 65535U) ? 65535U : (uint16_t)view.age_ms;
    st->event_head = (uint8_t)((st->event_head + 1U) % GPS_SPEED_EVENT_COUNT);
    st->event_total++;
    return 1U;
}

const char * GPS_Speed_ReasonName(uint8_t reason)
{
    switch(reason) {
    case GPS_SPEED_REASON_OK:      return "ok";
    case GPS_SPEED_REASON_NO_DATA: return "no_data";
    case GPS_SPEED_REASON_STALE:   return "stale";
    case GPS_SPEED_REASON_NO_FIX:  return "no_fix";
    default:                       return "?";
    }
}

const char * GPS_Speed_VerdictName(uint8_t verdict)
{
    switch(verdict) {
    case GPS_RMC_ACCEPTED:   return "accepted";
    case GPS_RMC_NO_FIX:     return "no_fix";
    case GPS_RMC_BAD_FIELD:  return "bad_field";
    case GPS_RMC_DUP_EPOCH:  return "dup_epoch";
    case GPS_RMC_BACK_EPOCH: return "back_epoch";
    default:                 return "?";
    }
}
