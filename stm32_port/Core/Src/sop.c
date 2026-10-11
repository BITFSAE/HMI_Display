#include "sop.h"

#define SOP_VERSION 2U

uint8_t Sop_Crc8(const uint8_t * data, uint32_t length)
{
    uint8_t crc = 0xFFU;

    for(uint32_t i = 0U; i < length; i++) {
        crc ^= data[i];
        for(uint32_t bit = 0U; bit < 8U; bit++) {
            crc = ((crc & 0x80U) != 0U) ? (uint8_t)((crc << 1) ^ 0x1DU) : (uint8_t)(crc << 1);
        }
    }
    return (uint8_t)(crc ^ 0xFFU);
}

uint8_t Sop_DecodePower(const uint8_t * data, uint32_t dlc, sop_power_t * out)
{
    uint8_t buf[9];

    if((dlc != 8U) || (data[5] != SOP_VERSION) || ((data[6] & 0xFEU) != 0U)) {
        return 0U;
    }
    /* CRC 输入：ID 高字节在前（04 A0），再接 Byte0..6，结果在 Byte7。 */
    buf[0] = (uint8_t)(SOP_ID_POWER >> 8);
    buf[1] = (uint8_t)(SOP_ID_POWER & 0xFFU);
    for(uint32_t i = 0U; i < 7U; i++) {
        buf[2U + i] = data[i];
    }
    if(Sop_Crc8(buf, sizeof(buf)) != data[7]) {
        return 0U;
    }
    out->discharge_dkw = (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    out->charge_dkw = (uint16_t)((uint16_t)data[2] | ((uint16_t)data[3] << 8));
    out->limits_valid = data[6] & 0x01U;
    return 1U;
}

uint8_t Sop_DecodeCurrent(const uint8_t * data, uint32_t dlc, sop_current_t * out)
{
    if((dlc != 8U) || ((data[7] >> 4) != SOP_VERSION)) {
        return 0U;
    }
    out->discharge_da = (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    out->charge_da = (uint16_t)((uint16_t)data[2] | ((uint16_t)data[3] << 8));
    out->inputs_valid = (data[6] >> 7) & 0x01U;
    return 1U;
}
