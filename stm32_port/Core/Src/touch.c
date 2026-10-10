/* USER CODE BEGIN touch */
#include "touch.h"

#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "firmware_diag.h"

#if TOUCH_TYPE_CAP
#include "gt911.h"
#else
#include "spi.h"
#endif
#include "dashboard_ui.h"

#define TOUCH_HOR_RES 800U
#define TOUCH_VER_RES 480U

#if TOUCH_TYPE_CAP

static volatile uint16_t g_touch_state_x;
static volatile uint16_t g_touch_state_y;
static volatile uint8_t g_touch_state_pressed;
static volatile uint8_t g_touch_state_changed;

uint8_t Touch_Cap_Init(void)
{
    if(GT911_Init() != 0U) {
        return 1U;
    }
    return 0U;
}

uint8_t Touch_Cap_Scan(void)
{
    uint8_t pressed;

    if(GT911_Scan() == 0U) {
        return 0U;
    }

    pressed = (uint8_t)((g_gt911_dev.sta & GT911_TP_PRES_DOWN) ? 1U : 0U);

    if(pressed != 0U) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        g_touch_state_x = g_gt911_dev.x[0];
        g_touch_state_y = g_gt911_dev.y[0];
        g_touch_state_pressed = pressed;
        g_touch_state_changed = 1U;
        if(primask == 0U) {
            __enable_irq();
        }
    } else {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if(g_touch_state_pressed != 0U) {
            g_touch_state_changed = 1U;
        }
        g_touch_state_pressed = 0U;
        if(primask == 0U) {
            __enable_irq();
        }
    }

    return 1U;
}

void Touch_Process(void)
{
    uint16_t x;
    uint16_t y;
    uint8_t pressed;
    uint8_t changed;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    x = g_touch_state_x;
    y = g_touch_state_y;
    pressed = g_touch_state_pressed;
    changed = g_touch_state_changed;
    g_touch_state_changed = 0U;
    if(primask == 0U) {
        __enable_irq();
    }

    if((pressed != 0U) || (changed != 0U)) {
        Dashboard_UI_SubmitTouchState(x, y, pressed);
    }
}

void Touch_ServiceTask(void *argument)
{
    uint8_t init_ok;

    (void)argument;

    while(Dashboard_UI_IsStartupComplete() == 0U) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    init_ok = Touch_Cap_Init();
    if(init_ok == 0U) {
        Firmware_Diag_UnregisterTask(FW_TASK_TOUCH);
        vTaskDelete(NULL);
    }

    for(;;) {
        Touch_Cap_Scan();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

#else

#define TOUCH_CMD_READ_X 0x90U
#define TOUCH_CMD_READ_Y 0xD0U
#define TOUCH_CMD_READ_Z1 0xB0U
#define TOUCH_CMD_READ_Z2 0xC0U

#ifndef TOUCH_RAW_X_MIN
#define TOUCH_RAW_X_MIN 200U
#endif
#ifndef TOUCH_RAW_X_MAX
#define TOUCH_RAW_X_MAX 3900U
#endif
#ifndef TOUCH_RAW_Y_MIN
#define TOUCH_RAW_Y_MIN 200U
#endif
#ifndef TOUCH_RAW_Y_MAX
#define TOUCH_RAW_Y_MAX 3900U
#endif
#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X 0U
#endif
#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y 0U
#endif

#define TOUCH_SAMPLE_COUNT 5U
#define TOUCH_SAMPLE_DROP 1U
#define TOUCH_ERR_RANGE 50U
#define TOUCH_SPI_TIMEOUT_MS 10U
#define TOUCH_RAW_PRESS_MIN 100U

static volatile uint16_t g_touch_state_x;
static volatile uint16_t g_touch_state_y;
static volatile uint8_t g_touch_state_pressed;
static volatile uint8_t g_touch_state_changed;

static void Touch_Select(void)
{
    HAL_GPIO_WritePin(TOUCH_CS_GPIO_Port, TOUCH_CS_Pin, GPIO_PIN_RESET);
}

static void Touch_Unselect(void)
{
    HAL_GPIO_WritePin(TOUCH_CS_GPIO_Port, TOUCH_CS_Pin, GPIO_PIN_SET);
}

static uint16_t Touch_ReadAdc(uint8_t command)
{
    uint8_t tx[3] = {command, 0U, 0U};
    uint8_t rx[3] = {0U, 0U, 0U};
    uint16_t data;

    if(HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), TOUCH_SPI_TIMEOUT_MS) != HAL_OK) {
        return 0U;
    }

    data = (uint16_t)(((uint16_t)rx[1] << 8) | rx[2]);
    return (uint16_t)(data >> 3);
}

static uint16_t Touch_ReadFiltered(uint8_t command)
{
    uint16_t samples[TOUCH_SAMPLE_COUNT];
    uint32_t sum = 0U;
    uint16_t temp;
    uint8_t i;
    uint8_t j;

    for(i = 0U; i < TOUCH_SAMPLE_COUNT; i++) {
        samples[i] = Touch_ReadAdc(command);
    }

    for(i = 0U; i < (TOUCH_SAMPLE_COUNT - 1U); i++) {
        for(j = (uint8_t)(i + 1U); j < TOUCH_SAMPLE_COUNT; j++) {
            if(samples[i] > samples[j]) {
                temp = samples[i];
                samples[i] = samples[j];
                samples[j] = temp;
            }
        }
    }

    for(i = TOUCH_SAMPLE_DROP; i < (TOUCH_SAMPLE_COUNT - TOUCH_SAMPLE_DROP); i++) {
        sum += samples[i];
    }

    return (uint16_t)(sum / (TOUCH_SAMPLE_COUNT - (TOUCH_SAMPLE_DROP * 2U)));
}

static uint8_t Touch_ReadRawXY(uint16_t *x, uint16_t *y)
{
    uint16_t x_raw;
    uint16_t y_raw;
    uint16_t z1_raw;
    uint16_t z2_raw;

    if((x == NULL) || (y == NULL)) {
        return 0U;
    }

    Touch_Select();
    z1_raw = Touch_ReadAdc(TOUCH_CMD_READ_Z1);
    z2_raw = Touch_ReadAdc(TOUCH_CMD_READ_Z2);
    x_raw = Touch_ReadFiltered(TOUCH_CMD_READ_X);
    y_raw = Touch_ReadFiltered(TOUCH_CMD_READ_Y);
    Touch_Unselect();

    if((x_raw < TOUCH_RAW_PRESS_MIN) || (y_raw < TOUCH_RAW_PRESS_MIN)) {
        return 0U;
    }

    *x = x_raw;
    *y = y_raw;
    return 1U;
}

static uint16_t Touch_AbsDiff(uint16_t a, uint16_t b)
{
    return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint16_t Touch_MapRaw(uint16_t raw, uint16_t raw_min, uint16_t raw_max, uint16_t out_max, uint8_t invert)
{
    uint32_t value;

    if(raw_max <= raw_min) {
        return 0U;
    }
    if(raw < raw_min) raw = raw_min;
    if(raw > raw_max) raw = raw_max;

    value = ((uint32_t)(raw - raw_min) * (uint32_t)(out_max - 1U)) / (uint32_t)(raw_max - raw_min);
    if(invert != 0U) {
        value = (uint32_t)(out_max - 1U) - value;
    }

    return (uint16_t)value;
}

__weak uint8_t Touch_Driver_ReadPoint(uint16_t *x, uint16_t *y)
{
    return Touch_Xpt2046ReadPoint(x, y);
}

uint8_t Touch_Xpt2046ReadPoint(uint16_t *x, uint16_t *y)
{
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;

    if((x == NULL) || (y == NULL)) {
        return 0U;
    }

    if(Touch_ReadRawXY(&x1, &y1) == 0U) {
        return 0U;
    }

    if(Touch_ReadRawXY(&x2, &y2) == 0U) {
        return 0U;
    }

    if((Touch_AbsDiff(x1, x2) >= TOUCH_ERR_RANGE) || (Touch_AbsDiff(y1, y2) >= TOUCH_ERR_RANGE)) {
        return 0U;
    }

    *x = Touch_MapRaw((uint16_t)((x1 + x2) / 2U), TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX,
                      TOUCH_HOR_RES, TOUCH_INVERT_X);
    *y = Touch_MapRaw((uint16_t)((y1 + y2) / 2U), TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX,
                      TOUCH_VER_RES, TOUCH_INVERT_Y);

    return 1U;
}

uint8_t Touch_Xpt2046Init(void)
{
    Touch_Unselect();
    return 0U;
}

void Touch_Process(void)
{
    uint16_t x;
    uint16_t y;
    uint8_t pressed;
    uint8_t changed;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    x = g_touch_state_x;
    y = g_touch_state_y;
    pressed = g_touch_state_pressed;
    changed = g_touch_state_changed;
    g_touch_state_changed = 0U;
    if(primask == 0U) {
        __enable_irq();
    }

    if((pressed != 0U) || (changed != 0U)) {
        Dashboard_UI_SubmitTouchState(x, y, pressed);
    }
}

void Touch_ServiceTask(void *argument)
{
    uint16_t x = 0U;
    uint16_t y = 0U;
    uint8_t pressed = 0U;
    uint8_t last_pressed = 0U;

    (void)argument;

    while(Dashboard_UI_IsStartupComplete() == 0U) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if(Touch_Xpt2046Init() != 0U) {
        Firmware_Diag_UnregisterTask(FW_TASK_TOUCH);
        vTaskDelete(NULL);
    }

    for(;;) {
        pressed = Touch_Driver_ReadPoint(&x, &y);

        if(pressed != 0U) {
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            g_touch_state_x = x;
            g_touch_state_y = y;
            if(pressed != last_pressed) {
                g_touch_state_changed = 1U;
            }
            g_touch_state_pressed = pressed;
            if(primask == 0U) {
                __enable_irq();
            }
        }
        else {
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            if(pressed != last_pressed) {
                g_touch_state_changed = 1U;
            }
            g_touch_state_pressed = pressed;
            if(primask == 0U) {
                __enable_irq();
            }
        }

        last_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#endif

/* USER CODE END touch */
