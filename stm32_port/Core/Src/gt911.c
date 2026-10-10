/* USER CODE BEGIN gt911 */
#include "gt911.h"
#include "main.h"
#include "touch.h"
#include "FreeRTOS.h"
#include "task.h"

#if TOUCH_TYPE_CAP

extern I2C_HandleTypeDef hi2c1;

#define GT911_HOR_RES  800U
#define GT911_VER_RES  480U
#define GT911_I2C_DELAY_COUNT  80U

gt911_dev_t g_gt911_dev;
static uint8_t g_gt911_i2c_addr = GT911_I2C_ADDR_5D;

/* Preserve the verified bit-bang delay when the project switches to -Os. */
static void __attribute__((optimize("O0"))) GT911_I2CDelay(void)
{
    for(volatile uint32_t i = 0U; i < GT911_I2C_DELAY_COUNT; i++) {
        __NOP();
    }
}

static void GT911_DelayMs(uint32_t duration_ms)
{
    if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
    }
    else {
        HAL_Delay(duration_ms);
    }
}

static void GT911_I2CSetScl(uint8_t level)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void GT911_I2CSetSda(uint8_t level)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t GT911_I2CGetSda(void)
{
    return (uint8_t)((HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) ? 1U : 0U);
}

static void GT911_I2CInitPins(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    (void)HAL_I2C_DeInit(&hi2c1);
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GT911_I2CSetScl(1U);
    GT911_I2CSetSda(1U);
    GT911_I2CDelay();
}

static void GT911_I2CStart(void)
{
    GT911_I2CSetSda(1U);
    GT911_I2CSetScl(1U);
    GT911_I2CDelay();
    GT911_I2CSetSda(0U);
    GT911_I2CDelay();
    GT911_I2CSetScl(0U);
    GT911_I2CDelay();
}

static void GT911_I2CStop(void)
{
    GT911_I2CSetScl(0U);
    GT911_I2CSetSda(0U);
    GT911_I2CDelay();
    GT911_I2CSetScl(1U);
    GT911_I2CDelay();
    GT911_I2CSetSda(1U);
    GT911_I2CDelay();
}

static uint8_t GT911_I2CWriteByte(uint8_t data)
{
    for(uint8_t i = 0U; i < 8U; i++) {
        GT911_I2CSetSda((data & 0x80U) ? 1U : 0U);
        data <<= 1;
        GT911_I2CDelay();
        GT911_I2CSetScl(1U);
        GT911_I2CDelay();
        GT911_I2CSetScl(0U);
        GT911_I2CDelay();
    }

    GT911_I2CSetSda(1U);
    GT911_I2CDelay();
    GT911_I2CSetScl(1U);
    GT911_I2CDelay();
    uint8_t nack = GT911_I2CGetSda();
    GT911_I2CSetScl(0U);
    GT911_I2CDelay();
    return (uint8_t)(nack == 0U ? 1U : 0U);
}

static uint8_t GT911_I2CReadByte(uint8_t ack)
{
    uint8_t data = 0U;

    GT911_I2CSetSda(1U);
    for(uint8_t i = 0U; i < 8U; i++) {
        data <<= 1;
        GT911_I2CSetScl(1U);
        GT911_I2CDelay();
        if(GT911_I2CGetSda() != 0U) {
            data |= 1U;
        }
        GT911_I2CSetScl(0U);
        GT911_I2CDelay();
    }

    GT911_I2CSetSda(ack ? 0U : 1U);
    GT911_I2CDelay();
    GT911_I2CSetScl(1U);
    GT911_I2CDelay();
    GT911_I2CSetScl(0U);
    GT911_I2CSetSda(1U);
    GT911_I2CDelay();

    return data;
}

static uint8_t GT911_WriteRegister(uint16_t reg, uint8_t value)
{
    GT911_I2CStart();
    if(GT911_I2CWriteByte(g_gt911_i2c_addr) == 0U) {
        GT911_I2CStop();
        return 0U;
    }
    if(GT911_I2CWriteByte((uint8_t)(reg >> 8)) == 0U) {
        GT911_I2CStop();
        return 0U;
    }
    if(GT911_I2CWriteByte((uint8_t)reg) == 0U) {
        GT911_I2CStop();
        return 0U;
    }
    if(GT911_I2CWriteByte(value) == 0U) {
        GT911_I2CStop();
        return 0U;
    }
    GT911_I2CStop();
    return 1U;
}

static uint8_t GT911_ReadRegister(uint16_t reg, uint8_t cnt, uint8_t *data)
{
    if((data == NULL) || (cnt == 0U)) {
        return 0U;
    }

    GT911_I2CStart();
    if(GT911_I2CWriteByte(g_gt911_i2c_addr) == 0U) {
        GT911_I2CStop();
        return 0U;
    }
    if(GT911_I2CWriteByte((uint8_t)(reg >> 8)) == 0U) {
        GT911_I2CStop();
        return 0U;
    }
    if(GT911_I2CWriteByte((uint8_t)reg) == 0U) {
        GT911_I2CStop();
        return 0U;
    }

    GT911_I2CStart();
    if(GT911_I2CWriteByte((uint8_t)(g_gt911_i2c_addr | 0x01U)) == 0U) {
        GT911_I2CStop();
        return 0U;
    }

    for(uint8_t i = 0U; i < cnt; i++) {
        data[i] = GT911_I2CReadByte((uint8_t)(i < (uint8_t)(cnt - 1U)));
    }
    GT911_I2CStop();
    return 1U;
}

static uint8_t GT911_TryAddress(uint8_t addr)
{
    uint8_t id_buf[4];

    g_gt911_i2c_addr = addr;
    if(GT911_ReadRegister(GT911_ID_ADDR, 4U, id_buf) == 0U) {
        return 0U;
    }
    return (uint8_t)(((id_buf[0] == '9') && (id_buf[1] == '1') && (id_buf[2] == '1')) ? 1U : 0U);
}

static void GT911_IntOut(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = IPS_INT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(IPS_INT_GPIO_Port, &GPIO_InitStruct);
}

static void GT911_IntIn(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = IPS_INT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(IPS_INT_GPIO_Port, &GPIO_InitStruct);
}

static void GT911_RstLow(void)
{
    HAL_GPIO_WritePin(IPS_RST_GPIO_Port, IPS_RST_Pin, GPIO_PIN_RESET);
}

static void GT911_RstHigh(void)
{
    HAL_GPIO_WritePin(IPS_RST_GPIO_Port, IPS_RST_Pin, GPIO_PIN_SET);
}

static void GT911_ResetForAddress(uint8_t addr)
{
    HAL_NVIC_DisableIRQ(EXTI4_IRQn);
    __HAL_GPIO_EXTI_CLEAR_IT(IPS_INT_Pin);

    GT911_IntOut();
    GT911_RstHigh();
    GT911_DelayMs(20);
    GT911_RstLow();
    GT911_DelayMs(20);
    HAL_GPIO_WritePin(IPS_INT_GPIO_Port, IPS_INT_Pin, GPIO_PIN_RESET);
    GT911_DelayMs(20);
    HAL_GPIO_WritePin(IPS_INT_GPIO_Port, IPS_INT_Pin,
                      (addr == GT911_I2C_ADDR_14) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    GT911_DelayMs(20);
    GT911_RstHigh();

    HAL_GPIO_WritePin(IPS_INT_GPIO_Port, IPS_INT_Pin, GPIO_PIN_RESET);
    GT911_DelayMs(50);
    GT911_IntIn();
    __HAL_GPIO_EXTI_CLEAR_IT(IPS_INT_Pin);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    GT911_DelayMs(20);
}

uint8_t GT911_Init(void)
{
    GT911_I2CInitPins();

    GT911_ResetForAddress(GT911_I2C_ADDR_5D);
    if(GT911_TryAddress(GT911_I2C_ADDR_5D) != 0U) {
        return 1U;
    }

    GT911_ResetForAddress(GT911_I2C_ADDR_14);
    if(GT911_TryAddress(GT911_I2C_ADDR_14) != 0U) {
        return 1U;
    }
    return 0U;
}

uint8_t GT911_Scan(void)
{
    uint8_t buf[42] = {0U};
    uint8_t i;
    uint8_t res = 0U;
    uint8_t temp;

    static uint8_t t = 0U;
    t++;
    if((t % 10U) == 0U || t < 10U) {
        if(GT911_ReadRegister(GT911_READ_ADDR, 42U, buf) == 0U) {
            return 0U;
        }
        res = 1U;

        if((buf[0] & 0x80U) && ((buf[0] & 0x0FU) != 0U) && ((buf[0] & 0x0FU) < 6U)) {
            temp = 0U;
            for(i = 0U; i < (buf[0] & 0x0FU); i++) {
                switch(buf[1U + (uint32_t)i * 8U]) {
                    case 4: temp |= 1U << 4; break;
                    case 3: temp |= 1U << 3; break;
                    case 2: temp |= 1U << 2; break;
                    case 1: temp |= 1U << 1; break;
                    case 0: temp |= 1U << 0; break;
                    default: break;
                }
            }
            g_gt911_dev.sta = temp | GT911_TP_PRES_DOWN | GT911_TP_CATH_PRES;
            g_gt911_dev.x[4] = g_gt911_dev.x[0];
            g_gt911_dev.y[4] = g_gt911_dev.y[0];

            for(i = 0U; i < GT911_MAX_TOUCH; i++) {
                if(g_gt911_dev.sta & (1U << i)) {
                    g_gt911_dev.x[i] = (uint16_t)(((uint16_t)buf[3U + (uint32_t)i * 8U] << 8) + buf[2U + (uint32_t)i * 8U]);
                    g_gt911_dev.y[i] = (uint16_t)(((uint16_t)buf[5U + (uint32_t)i * 8U] << 8) + buf[4U + (uint32_t)i * 8U]);
                }
            }

            if((g_gt911_dev.x[0] >= GT911_HOR_RES) || (g_gt911_dev.y[0] >= GT911_VER_RES)) {
                if((g_gt911_dev.x[0] < GT911_VER_RES) && (g_gt911_dev.y[0] < GT911_HOR_RES)) {
                    uint16_t swapped = g_gt911_dev.x[0];
                    g_gt911_dev.x[0] = g_gt911_dev.y[0];
                    g_gt911_dev.y[0] = swapped;
                }
            }

            if(g_gt911_dev.x[0] >= GT911_HOR_RES) g_gt911_dev.x[0] = GT911_HOR_RES - 1U;
            if(g_gt911_dev.y[0] >= GT911_VER_RES) g_gt911_dev.y[0] = GT911_VER_RES - 1U;
            t = 0U;
        }

        if(buf[0] & 0x80U) {
            GT911_WriteRegister(GT911_READ_ADDR, 0U);
        }
    }

    if((buf[0] & 0x8FU) == 0x80U) {
        if(g_gt911_dev.sta & GT911_TP_PRES_DOWN) {
            g_gt911_dev.sta &= (uint8_t)(~(1U << 7));
        } else {
            g_gt911_dev.x[0] = 0xFFFFU;
            g_gt911_dev.y[0] = 0xFFFFU;
            g_gt911_dev.sta &= 0xE0U;
        }
    }

    if(t > 240U) t = 10U;
    return res;
}

#endif /* TOUCH_TYPE_CAP */

/* USER CODE END gt911 */
