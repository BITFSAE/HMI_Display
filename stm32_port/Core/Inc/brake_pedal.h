#ifndef BRAKE_PEDAL_H
#define BRAKE_PEDAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BRT25-C0M4096-RT1 单圈绝对值的换算与静止零点跟踪。
 * 本模块不依赖 HAL/RTOS，便于在主机上验证纯算法。 */

/* 编码器周期约 10 ms；150 ms 可容忍连续丢帧，同时不把停发当作松开。 */
#define BRAKE_PEDAL_ENCODER_TIMEOUT_MS       150U
#define BRAKE_PEDAL_OIL_TIMEOUT_MS           1000U
#define BRAKE_PEDAL_REST_WINDOW_MS           1500U
/* 比当前零点更高的静止位可能是脚搭在踏板上，须保持更久才采用。 */
#define BRAKE_PEDAL_REST_RAISE_MS            5000U
#define BRAKE_PEDAL_REST_MAX_P2P_COUNTS      2U
#define BRAKE_PEDAL_REST_RING_SIZE           192U

/* 油压原始值 0.001 kPa/LSB。静止值约 2.135 kPa；快速松开时最低约 2.126 kPa。 */
#define BRAKE_PEDAL_OIL_REST_RAW             2135U
#define BRAKE_PEDAL_OIL_REST_TOLERANCE_RAW   10U
#define BRAKE_OIL_ZERO_RAW                   2100U
#define BRAKE_OIL_FULL_RAW                   4900U
#define BRAKE_OIL_FULL_PCT                   90U

/* 能量回收满量程与死区，单位 0.001 度。 */
#define BRAKE_PEDAL_REGEN_DEADBAND_MDEG       300
#define BRAKE_PEDAL_REGEN_FULL_SCALE_MDEG     7000

typedef struct {
  int16_t count[BRAKE_PEDAL_REST_RING_SIZE];
  uint32_t tick[BRAKE_PEDAL_REST_RING_SIZE];
  uint16_t head;
  uint16_t length;
  uint32_t encoder_tick;
  uint32_t oil_tick;
  uint32_t stable_since;
  int32_t rest_signed_count;
  uint16_t oil_raw;
  int16_t signed_count;
  uint8_t has_encoder;
  uint8_t has_oil;
  uint8_t has_rest;
  uint8_t stable_active;
} brake_pedal_t;

void BrakePedal_Init(brake_pedal_t *state);

/* 位置上报帧：DLC=7，byte0=7、byte1=0x01、byte2=0x01，byte3..6 小端计数。
 * DLC=4 的命令应答和其他 FUNC 不算错误，仅返回 0 让调用方忽略。 */
uint8_t BrakePedal_DecodePosition(const uint8_t *data, uint8_t dlc,
                                  uint16_t *count);

void BrakePedal_OnEncoder(brake_pedal_t *state, uint16_t count, uint32_t now);
void BrakePedal_OnOil(brake_pedal_t *state, uint16_t oil_raw, uint32_t now);

/* 返回 0..100；编码器超时或尚无静止零点时返回 -1。 */
int32_t BrakePedal_GetRegenPct(const brake_pedal_t *state, uint32_t now);
/* 返回 0..100；油压超过 1 s 未更新时返回 -1。 */
int32_t BrakePedal_GetMechanicalPct(const brake_pedal_t *state, uint32_t now);

/* 供诊断与测试读取；尚未找到静止位时返回 0，不写出 *signed_count。 */
uint8_t BrakePedal_GetRestCount(const brake_pedal_t *state,
                                int32_t *signed_count);

#ifdef __cplusplus
}
#endif

#endif
