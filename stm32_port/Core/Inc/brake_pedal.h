#ifndef BRAKE_PEDAL_H
#define BRAKE_PEDAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BRT25-C0M4096-RT1 单圈绝对值的换算。
 * 传感器已在踏板释放位清零，本模块只按原始计数换算，不做在线零点跟踪；
 * 不依赖 HAL/RTOS，便于在主机上验证纯算法。 */

/* 编码器 5 ms 周期；150 ms 可容忍连续丢帧，同时不把停发当作松开。 */
#define BRAKE_PEDAL_ENCODER_TIMEOUT_MS       150U
#define BRAKE_PEDAL_OIL_TIMEOUT_MS           1000U

/* 油压原始值 0.001 kPa/LSB。 */
#define BRAKE_OIL_ZERO_RAW                   2100U
#define BRAKE_OIL_FULL_RAW                   4900U
#define BRAKE_OIL_FULL_PCT                   90U

/* 能量回收满量程与死区，单位 0.001 度。传感器释放位为 0 count。 */
#define BRAKE_PEDAL_REGEN_DEADBAND_MDEG       300
#define BRAKE_PEDAL_REGEN_FULL_SCALE_MDEG     20000

typedef struct {
  uint32_t encoder_tick;
  uint32_t oil_tick;
  uint16_t oil_raw;
  int16_t signed_count;
  uint8_t has_encoder;
  uint8_t has_oil;
} brake_pedal_t;

void BrakePedal_Init(brake_pedal_t *state);

/* 位置上报帧：DLC=7，byte0=7、byte1=0x01、byte2=0x01，byte3..6 小端计数。
 * DLC=4 的命令应答和其他 FUNC 不算错误，仅返回 0 让调用方忽略。 */
uint8_t BrakePedal_DecodePosition(const uint8_t *data, uint8_t dlc,
                                  uint16_t *count);

void BrakePedal_OnEncoder(brake_pedal_t *state, uint16_t count, uint32_t now);
void BrakePedal_OnOil(brake_pedal_t *state, uint16_t oil_raw, uint32_t now);

/* 返回 0..100；编码器超时或尚未收到位置帧时返回 -1。 */
int32_t BrakePedal_GetRegenPct(const brake_pedal_t *state, uint32_t now);
/* 返回 0..100；油压超过 1 s 未更新时返回 -1。 */
int32_t BrakePedal_GetMechanicalPct(const brake_pedal_t *state, uint32_t now);

#ifdef __cplusplus
}
#endif

#endif
