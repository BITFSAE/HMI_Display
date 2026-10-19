#include "brake_pedal.h"

#include <string.h>

static int32_t signed_count(uint16_t count)
{
  return (count >= 2048U) ? ((int32_t)count - 4096) : (int32_t)count;
}

static uint16_t oldest_tick_in_window(const brake_pedal_t *state, uint32_t now,
                                      int32_t *minimum, int32_t *maximum,
                                      uint32_t *sample_count)
{
  uint32_t oldest_tick = now;
  uint32_t oldest_age = 0U;
  int32_t min_value = 0;
  int32_t max_value = 0;
  uint32_t count = 0U;

  for(uint32_t i = 0U; i < state->length; i++) {
    uint32_t index = (state->head + i) % BRAKE_PEDAL_REST_RING_SIZE;
    uint32_t age = now - state->tick[index];
    if(age > BRAKE_PEDAL_REST_WINDOW_MS) continue;
    if(count == 0U) {
      min_value = state->count[index];
      max_value = state->count[index];
    }
    else {
      if(state->count[index] < min_value) min_value = state->count[index];
      if(state->count[index] > max_value) max_value = state->count[index];
    }
    if(age >= oldest_age) {
      oldest_age = age;
      oldest_tick = state->tick[index];
    }
    count++;
  }

  *minimum = min_value;
  *maximum = max_value;
  *sample_count = count;
  return oldest_tick;
}

static int32_t window_median(const brake_pedal_t *state, uint32_t now)
{
  int16_t values[BRAKE_PEDAL_REST_RING_SIZE];
  uint32_t count = 0U;

  for(uint32_t i = 0U; i < state->length; i++) {
    uint32_t index = (state->head + i) % BRAKE_PEDAL_REST_RING_SIZE;
    if((now - state->tick[index]) > BRAKE_PEDAL_REST_WINDOW_MS) continue;
    values[count++] = state->count[index];
  }
  if(count == 0U) return 0;

  /* 窗口最多 1.5 s（约 150~151 个样本），插入排序只在校准零点时执行。 */
  for(uint32_t i = 1U; i < count; i++) {
    int16_t value = values[i];
    uint32_t j = i;
    while((j > 0U) && (values[j - 1U] > value)) {
      values[j] = values[j - 1U];
      j--;
    }
    values[j] = value;
  }
  if((count & 1U) != 0U) return values[count / 2U];
  return ((int32_t)values[count / 2U - 1U] +
          (int32_t)values[count / 2U]) / 2;
}

static uint8_t oil_is_resting(const brake_pedal_t *state, uint32_t now)
{
  int32_t delta;

  if((state->has_oil == 0U) ||
     ((now - state->oil_tick) > BRAKE_PEDAL_OIL_TIMEOUT_MS)) {
    return 0U;
  }
  delta = (int32_t)state->oil_raw - (int32_t)BRAKE_PEDAL_OIL_REST_RAW;
  if(delta < 0) delta = -delta;
  return (delta <= (int32_t)BRAKE_PEDAL_OIL_REST_TOLERANCE_RAW) ? 1U : 0U;
}

static void update_rest(brake_pedal_t *state, uint32_t now)
{
  int32_t minimum;
  int32_t maximum;
  uint32_t samples;
  uint32_t oldest = oldest_tick_in_window(state, now, &minimum, &maximum, &samples);
  uint8_t stable = 0U;

  if((samples != 0U) &&
     ((now - oldest) >= BRAKE_PEDAL_REST_WINDOW_MS) &&
     ((uint32_t)(maximum - minimum) <= BRAKE_PEDAL_REST_MAX_P2P_COUNTS) &&
     (oil_is_resting(state, now) != 0U)) {
    stable = 1U;
  }

  if(stable == 0U) {
    state->stable_active = 0U;
    return;
  }

  if(state->stable_active == 0U) {
    state->stable_active = 1U;
    state->stable_since = oldest;
  }

  if(state->has_rest == 0U) {
    state->rest_signed_count = window_median(state, now);
    state->has_rest = 1U;
  }
  else if(maximum < state->rest_signed_count) {
    state->rest_signed_count = window_median(state, now);
  }
  else if((minimum > state->rest_signed_count) &&
          ((now - state->stable_since) >= BRAKE_PEDAL_REST_RAISE_MS)) {
    state->rest_signed_count = window_median(state, now);
  }
}

void BrakePedal_Init(brake_pedal_t *state)
{
  if(state == NULL) return;
  (void)memset(state, 0, sizeof(*state));
}

uint8_t BrakePedal_DecodePosition(const uint8_t *data, uint8_t dlc,
                                  uint16_t *count)
{
  uint32_t raw;

  if((data == NULL) || (count == NULL) || (dlc != 7U)) return 0U;
  if((data[0] != dlc) || (data[1] != 0x01U) || (data[2] != 0x01U)) return 0U;
  raw = (uint32_t)data[3] | ((uint32_t)data[4] << 8) |
        ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 24);
  if(raw >= 4096U) return 0U;
  *count = (uint16_t)raw;
  return 1U;
}

void BrakePedal_OnOil(brake_pedal_t *state, uint16_t oil_raw, uint32_t now)
{
  if(state == NULL) return;
  state->oil_raw = oil_raw;
  state->oil_tick = now;
  state->has_oil = 1U;
}

void BrakePedal_OnEncoder(brake_pedal_t *state, uint16_t count, uint32_t now)
{
  uint32_t index;

  if(state == NULL) return;
  if(state->length < BRAKE_PEDAL_REST_RING_SIZE) {
    index = (state->head + state->length) % BRAKE_PEDAL_REST_RING_SIZE;
    state->length++;
  }
  else {
    index = state->head;
    state->head = (state->head + 1U) % BRAKE_PEDAL_REST_RING_SIZE;
  }

  state->signed_count = (int16_t)signed_count(count);
  state->count[index] = state->signed_count;
  state->tick[index] = now;
  state->encoder_tick = now;
  state->has_encoder = 1U;
  update_rest(state, now);
}

int32_t BrakePedal_GetRegenPct(const brake_pedal_t *state, uint32_t now)
{
  int32_t relative_mdeg;
  int32_t active_mdeg;
  uint32_t pct;

  if((state == NULL) || (state->has_encoder == 0U) || (state->has_rest == 0U) ||
     ((now - state->encoder_tick) > BRAKE_PEDAL_ENCODER_TIMEOUT_MS)) {
    return -1;
  }

  /* 1 count = 360000 / 4096 mdeg；余数为正时舍去，与界面的整数截断一致。 */
  relative_mdeg = ((int32_t)state->signed_count - state->rest_signed_count) *
                  360000 / 4096;
  if(relative_mdeg <= BRAKE_PEDAL_REGEN_DEADBAND_MDEG) return 0;

  active_mdeg = relative_mdeg - BRAKE_PEDAL_REGEN_DEADBAND_MDEG;
  pct = (uint32_t)active_mdeg * 100U /
        (BRAKE_PEDAL_REGEN_FULL_SCALE_MDEG - BRAKE_PEDAL_REGEN_DEADBAND_MDEG);
  return (pct > 100U) ? 100 : (int32_t)pct;
}

int32_t BrakePedal_GetMechanicalPct(const brake_pedal_t *state, uint32_t now)
{
  uint32_t pct;

  if((state == NULL) || (state->has_oil == 0U) ||
     ((now - state->oil_tick) > BRAKE_PEDAL_OIL_TIMEOUT_MS)) {
    return -1;
  }
  if(state->oil_raw <= BRAKE_OIL_ZERO_RAW) return 0;

  pct = ((uint32_t)state->oil_raw - BRAKE_OIL_ZERO_RAW) * BRAKE_OIL_FULL_PCT /
        (BRAKE_OIL_FULL_RAW - BRAKE_OIL_ZERO_RAW);
  return (pct > 100U) ? 100 : (int32_t)pct;
}

uint8_t BrakePedal_GetRestCount(const brake_pedal_t *state,
                                int32_t *signed_count_out)
{
  if((state == NULL) || (state->has_rest == 0U) || (signed_count_out == NULL)) {
    return 0U;
  }
  *signed_count_out = state->rest_signed_count;
  return 1U;
}
