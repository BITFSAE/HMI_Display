#include "brake_pedal.h"

#include <string.h>

/* 0..4095 映射到 -2048..2047，处理单圈编码器跨 4096/0 回绕。
 * 释放位为 0 count，因此负值按未踩下处理。 */
static int32_t signed_count(uint16_t count)
{
  return (count >= 2048U) ? ((int32_t)count - 4096) : (int32_t)count;
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
  if(state == NULL) return;
  state->signed_count = (int16_t)signed_count(count);
  state->encoder_tick = now;
  state->has_encoder = 1U;
}

int32_t BrakePedal_GetRegenPct(const brake_pedal_t *state, uint32_t now)
{
  int32_t relative_mdeg;
  int32_t active_mdeg;
  uint32_t pct;

  if((state == NULL) || (state->has_encoder == 0U) ||
     ((now - state->encoder_tick) > BRAKE_PEDAL_ENCODER_TIMEOUT_MS)) {
    return -1;
  }

  /* 1 count = 360000 / 4096 mdeg；释放位 0 count 已由传感器标定。 */
  relative_mdeg = (int32_t)state->signed_count * 360000 / 4096;
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
