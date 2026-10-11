#ifndef SOP_H
#define SOP_H

/* BMS SOP V2 接收端解码（CANB，小端）。布局和校验以 vehicle-interfaces 的
 * docs/CANB接口.md 第 4 章为准。车手屏只接收 BMS 发的 0x4A0/0x4A3，
 * 不接收 ECU 的 0x4A4 确认。纯函数，无 HAL/RTOS 依赖。 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOP_ID_POWER    0x4A0U   /* BMS_SOPLimits，DLC 8，放电高压接通后 10 ms */
#define SOP_ID_CURRENT  0x4A3U   /* BMS_SOPStatus，DLC 8，放电高压接通时 500 ms */

typedef struct {
    uint16_t discharge_dkw;   /* 放电功率上限，0.1 kW */
    uint16_t charge_dkw;      /* 回收功率上限，0.1 kW */
    uint8_t limits_valid;     /* LimitsValid：0 时两个功率均为 0，不应当作限值显示 */
} sop_power_t;

typedef struct {
    uint16_t discharge_da;    /* BMS 当前允许放电电流，0.1 A */
    uint16_t charge_da;       /* BMS 当前允许回充电流，0.1 A */
    uint8_t inputs_valid;     /* InputHealth bit7：全部 SOP 输入有效 */
} sop_current_t;

/* CRC-8/SAE-J1850（多项式 0x1D，初值 0xFF，结果异或 0xFF）；
 * 校验字符串 "123456789" 的结果为 0x4B。 */
uint8_t Sop_Crc8(const uint8_t * data, uint32_t length);

/* 返回 1 表示帧合法并已填入 out。拒绝：DLC≠8、版本≠2、保留位非零、CRC 错误。
 * 计数只用于 ECU 的前进判定，车手屏不使用。 */
uint8_t Sop_DecodePower(const uint8_t * data, uint32_t dlc, sop_power_t * out);

/* 返回 1 表示帧合法并已填入 out。拒绝：DLC≠8、版本≠2。该帧没有 CRC。 */
uint8_t Sop_DecodeCurrent(const uint8_t * data, uint32_t dlc, sop_current_t * out);

#ifdef __cplusplus
}
#endif

#endif
