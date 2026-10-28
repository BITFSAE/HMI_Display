/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.c
  * @brief   This file provides code for the configuration
  *          of the CAN instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "can.h"

/* USER CODE BEGIN Includes */
#include "cmsis_os2.h"
#include "dashboard_ui.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "firmware_diag.h"
#include "sop.h"
#include "brake_pedal.h"
/* USER CODE END Includes */

/* USER CODE BEGIN 0 */
CAN_TxHeaderTypeDef TxHeader;
/* Only the CAN service task owns the decode workspace. IRQs enqueue compact
 * frames, never scan dashboard state or run protocol/UI logic. */
static CAN_RxHeaderTypeDef RxHeader;
static uint8_t CAN_RxData[8];
#define USER_CAN_RX_QUEUE_CAPACITY 64U

typedef struct {
  uint16_t id;
  uint8_t dlc, bus;
  uint8_t data[8];
} User_CAN_RxItem_t;

static StaticQueue_t g_can_rx_queue_control;
static uint8_t g_can_rx_queue_storage[USER_CAN_RX_QUEUE_CAPACITY * sizeof(User_CAN_RxItem_t)];
static QueueHandle_t g_can_rx_queue;
static StaticTask_t g_can_task_control;
static StackType_t g_can_task_stack[1024U];
static brake_pedal_t g_brake_pedal;
volatile uint32_t g_can_rx_drop_count;
volatile uint32_t g_can_rx_invalid_count;
volatile uint32_t g_can_rx_queue_peak;
/* Bus-off events seen via HAL_CAN_ErrorCallback; recovery itself is ABOM in hardware. */
volatile uint32_t g_can_bus_off_count;
static void User_CAN_ProcessFifo0(const User_CAN_RxItem_t *frame);

void CAN_StartServiceTask(void)
{
  BrakePedal_Init(&g_brake_pedal);
  g_can_rx_queue = xQueueCreateStatic(USER_CAN_RX_QUEUE_CAPACITY, sizeof(User_CAN_RxItem_t),
                                     g_can_rx_queue_storage, &g_can_rx_queue_control);
  configASSERT(g_can_rx_queue != NULL);
  TaskHandle_t handle = xTaskCreateStatic(CAN_ServiceTask, "CAN", 1024U, NULL,
                                          (UBaseType_t)osPriorityNormal + 2U,
                                          g_can_task_stack, &g_can_task_control);
  Firmware_Diag_RegisterTask(FW_TASK_CAN, handle, "CAN", sizeof(g_can_task_stack));
}

/* Capture at most the FIFO occupancy seen on entry (hardware depth is three).
 * No unbounded drain loop can keep the CPU trapped in a busy-bus interrupt. */
static void User_CAN_ReceiveIRQ(CAN_HandleTypeDef *hcan, uint32_t fifo)
{
  BaseType_t wake = pdFALSE;
  uint32_t count = HAL_CAN_GetRxFifoFillLevel(hcan, fifo);
  if(count > 3U) count = 3U;
  for(uint32_t i = 0U; i < count; i++) {
    CAN_RxHeaderTypeDef header;
    User_CAN_RxItem_t frame = {0};
    if(HAL_CAN_GetRxMessage(hcan, fifo, &header, frame.data) != HAL_OK) {
      g_can_rx_invalid_count++;
      break;
    }
    if(header.IDE != CAN_ID_STD || header.RTR != CAN_RTR_DATA || header.DLC > 8U) {
      g_can_rx_invalid_count++;
      continue;
    }
    if(g_can_rx_queue == NULL || Dashboard_UI_IsStartupComplete() == 0U) continue;
    frame.id = (uint16_t)header.StdId;
    frame.dlc = (uint8_t)header.DLC;
    frame.bus = hcan == &hcan2 ? 2U : 1U;
    if(xQueueSendFromISR(g_can_rx_queue, &frame, &wake) != pdPASS) {
      g_can_rx_drop_count++;
    }
    uint32_t depth = uxQueueMessagesWaitingFromISR(g_can_rx_queue);
    if(depth > g_can_rx_queue_peak) g_can_rx_queue_peak = depth;
  }
  portYIELD_FROM_ISR(wake);
}

/* DBC Vehicle_CanB.dbc filter ID list */
/* Bank0: BMS_PackStatus(0x4B0 BO_1200), DataLogger(0x305 BO_773), Debug2_Torque(0x502 BO_1282), Debug5_Velocity(0x505 BO_1285) */
uint16_t CAN1_RX_MSG_ID_BANK0[4] = {0x4B0, 0x305, 0x502, 0x505};
/* Bank1: Debug6_MotorTemp(0x506 BO_1286), Debug9_Status(0x509 BO_1289), Debug8_IGBT(0x508 BO_1288), Debug7_Inverter(0x507 BO_1287) */
uint16_t CAN1_RX_MSG_ID_BANK1[4] = {0x506, 0x509, 0x508, 0x507};
/* Bank2: Debug3_Diag12(0x503 BO_1283), Debug4_Diag34(0x504 BO_1284) */
/* Bank2: Debug3_Diag12(0x503 BO_1283), Debug4_Diag34(0x504 BO_1284),
 * PDM_LowVoltageBus(0x5A0 BO_1440), PDM_LowVoltageBattery(0x5A1 BO_1441) */
uint16_t CAN1_RX_MSG_ID_BANK2[4] = {0x503, 0x504, 0x5A0, 0x5A1};
/* Bank3: BMS SOP V2 — BMS_SOPLimits(0x4A0 BO_1184), BMS_SOPStatus(0x4A3 BO_1187). 车手屏不收 ECU 的 0x4A4 确认 */
uint16_t CAN1_RX_MSG_ID_BANK3[4] = {SOP_ID_POWER, SOP_ID_CURRENT, SOP_ID_POWER, SOP_ID_CURRENT};
/* Bank4: FanController_Status(0x5A2 BO_1442) 与 BRT 踏板编码器自动上报帧 0x001 */
uint16_t CAN1_RX_MSG_ID_BANK4[4] = {0x5A2, 0x001, 0x5A2, 0x001};
/* Bank5: Tire-temperature cells 1..16, four cells per frame. */
uint16_t CAN1_RX_MSG_ID_BANK5[4] = {0x071, 0x072, 0x073, 0x074};
/* Bank14: Vehicle_CanB.dbc steering wheel on CAN2 - DriveMode_Request(0x310), SteeringCmd(0x703).
 * 16-bit ID list needs four entries; the two IDs are repeated. */
uint16_t CAN2_RX_MSG_ID_BANK0[4] = {0x310, 0x703, 0x310, 0x703};

#define CAN_ID_GPS_POSITION        0x067U
#define CAN_ID_GPS_MOTION          0x068U
/* Slow telemetry-only frames use high IDs so they never win arbitration
 * against SOP, BMS fault or other control traffic. */
#define CAN_ID_GPS_STATUS          0x7A0U
#define CAN_ID_GPS_LAP             0x7A1U
#define USER_CAN_TX_QUEUE_CAPACITY 32U

typedef struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} User_CAN_TxItem_t;

static User_CAN_TxItem_t g_user_can_tx_queue[USER_CAN_TX_QUEUE_CAPACITY];
static volatile uint8_t g_user_can_tx_head;
static volatile uint8_t g_user_can_tx_tail;
static volatile uint8_t g_user_can_tx_count;
static volatile uint8_t g_user_can_tx_draining;
static volatile uint32_t g_user_can_tx_drop_count;
/* Nothing is valid until its frame arrives: valid_mask starts at 0 and the UI
 * shows "--" (see User_CAN_CheckTimeouts for the drop-out path). */
static dashboard_data_t g_can_dashboard_data;

/* DBC order of the four per-wheel signals is RL, RR, FL, FR; dashboard order
 * is 0=LF, 1=LR, 2=RF, 3=RR. */
static const uint8_t k_can_dbc_to_dash[4] = {1U, 3U, 0U, 2U};

/* Receive freshness. Each family of decoded frames owns DASH_VALID_* bits that
 * are dropped after `timeout_ms` of silence. Timeouts are several nominal
 * periods: BMS 0x4B0 500 ms, PDM/fan 100 ms, ECU frames 10..50 ms (still
 * tentative in the interface repo, hence the generous second). */
typedef enum {
  CAN_SRC_BMS,
  CAN_SRC_APS,
  CAN_SRC_MECH_BRAKE,
  CAN_SRC_BRAKE_ANGLE,
  CAN_SRC_TORQUE,
  CAN_SRC_RPM,
  CAN_SRC_MOTOR_TEMP,
  CAN_SRC_INV_TEMP,
  CAN_SRC_IGBT_TEMP,
  CAN_SRC_AMK_STATE,
  CAN_SRC_DIAG_RL_RR,
  CAN_SRC_DIAG_FL_FR,
  CAN_SRC_PDM_BUS,
  CAN_SRC_FAN,
  CAN_SRC_SOP_POWER,
  CAN_SRC_SOP_CURRENT,
  CAN_SRC_COUNT
} can_src_t;

#define CAN_TIMEOUT_BMS_MS  2000U
#define CAN_TIMEOUT_ECU_MS  1000U
#define CAN_TIMEOUT_PDM_MS  1500U  /* CANB接口.md 8.2: PDM lost after 1500 ms */
#define CAN_TIMEOUT_FAN_MS  1500U  /* CANB接口.md 8.2: 0x5Ax stale after 1500 ms */
/* SOP：0x4A0 周期 10 ms，ECU 暂定 300 ms 超时(CANB接口.md 4.1)，车手屏取同值，
 * 使显示的限值与 ECU 正在采用的一致；0x4A3 周期 500 ms，取 3 个周期。
 * BMS 在自检、待机、预充、故障和充电时停发，此时界面显示 "--"。 */
#define CAN_TIMEOUT_SOP_POWER_MS    300U
#define CAN_TIMEOUT_SOP_CURRENT_MS  1500U
#define CAN_TIMEOUT_BRAKE_ANGLE_MS  BRAKE_PEDAL_ENCODER_TIMEOUT_MS
/* 20 Hz 检查使 150 ms 的踏板编码器超时最迟约 200 ms 反映到界面。 */
#define CAN_TIMEOUT_POLL_MS 50U

static const struct {
  uint32_t valid_bits;
  uint32_t timeout_ms;
} k_can_src[CAN_SRC_COUNT] = {
  [CAN_SRC_BMS]        = {DASH_VALID_SOC | DASH_VALID_HV_V | DASH_VALID_HV_I, CAN_TIMEOUT_BMS_MS},
  [CAN_SRC_APS]        = {DASH_VALID_APS,        CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_MECH_BRAKE] = {DASH_VALID_BRAKE,      CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_BRAKE_ANGLE] = {DASH_VALID_REGEN,     CAN_TIMEOUT_BRAKE_ANGLE_MS},
  [CAN_SRC_TORQUE]     = {DASH_VALID_TORQUE,     CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_RPM]        = {DASH_VALID_RPM,        CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_MOTOR_TEMP] = {DASH_VALID_MOTOR_TEMP, CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_INV_TEMP]   = {DASH_VALID_INV_TEMP,   CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_IGBT_TEMP]  = {DASH_VALID_IGBT_TEMP,  CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_AMK_STATE]  = {DASH_VALID_AMK_EN,     CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_DIAG_RL_RR] = {DASH_VALID_DIAG_RL_RR, CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_DIAG_FL_FR] = {DASH_VALID_DIAG_FL_FR, CAN_TIMEOUT_ECU_MS},
  [CAN_SRC_PDM_BUS]    = {DASH_VALID_LV_BUS,     CAN_TIMEOUT_PDM_MS},
  [CAN_SRC_FAN]        = {DASH_VALID_FAN,        CAN_TIMEOUT_FAN_MS},
  [CAN_SRC_SOP_POWER]  = {DASH_VALID_SOP_POWER,  CAN_TIMEOUT_SOP_POWER_MS},
  [CAN_SRC_SOP_CURRENT] = {DASH_VALID_SOP_CURRENT, CAN_TIMEOUT_SOP_CURRENT_MS},
};
static uint32_t g_can_src_tick[CAN_SRC_COUNT];  /* 0 = never received */
static uint32_t g_can_timeout_check_tick;

/* Records a frame of `src`; call before touching valid_mask for it. */
static void User_CAN_NoteRx(can_src_t src, uint32_t now)
{
  g_can_src_tick[src] = (now != 0U) ? now : 1U;
}

static void User_CAN_SetValid(uint32_t bits, uint8_t valid)
{
  if(valid != 0U) g_can_dashboard_data.valid_mask |= bits;
  else g_can_dashboard_data.valid_mask &= ~bits;
}

/* MAX T: highest fresh motor/inverter/IGBT temperature across the four wheels. */
static void User_CAN_UpdateMaxTemp(void)
{
  const uint32_t valid = g_can_dashboard_data.valid_mask;
  int32_t max_temp = 0;

  for(uint32_t wheel = 0U; wheel < 4U; wheel++) {
    if(((valid & DASH_VALID_MOTOR_TEMP) != 0U) && (g_can_dashboard_data.motor_temp[wheel] > max_temp)) {
      max_temp = g_can_dashboard_data.motor_temp[wheel];
    }
    if(((valid & DASH_VALID_INV_TEMP) != 0U) && (g_can_dashboard_data.inverter_temp[wheel] > max_temp)) {
      max_temp = g_can_dashboard_data.inverter_temp[wheel];
    }
    if(((valid & DASH_VALID_IGBT_TEMP) != 0U) && (g_can_dashboard_data.igbt_temp[wheel] > max_temp)) {
      max_temp = g_can_dashboard_data.igbt_temp[wheel];
    }
  }
  g_can_dashboard_data.max_temperature = max_temp;
}

/* Drops the valid bits of every source that has gone quiet. Returns non-zero
 * when anything changed and the dashboard needs a fresh submit. */
static uint8_t User_CAN_CheckTimeouts(uint32_t now)
{
  uint8_t changed = 0U;

  for(uint32_t src = 0U; src < (uint32_t)CAN_SRC_COUNT; src++) {
    if((g_can_src_tick[src] != 0U) &&
       ((now - g_can_src_tick[src]) > k_can_src[src].timeout_ms) &&
       ((g_can_dashboard_data.valid_mask & k_can_src[src].valid_bits) != 0U)) {
      g_can_dashboard_data.valid_mask &= ~k_can_src[src].valid_bits;
      changed = 1U;
    }
  }
  return changed;
}

/* Rate-limited wrapper called from the CAN task after every wake-up. */
static void User_CAN_ServiceTimeouts(uint32_t now)
{
  if((now - g_can_timeout_check_tick) < CAN_TIMEOUT_POLL_MS) return;
  g_can_timeout_check_tick = now;
  if(User_CAN_CheckTimeouts(now) != 0U) {
    User_CAN_UpdateMaxTemp();
    Dashboard_UI_SubmitData(&g_can_dashboard_data);
  }
}

static uint16_t can_u16_le(const uint8_t * d, uint32_t i)
{
  return (uint16_t)((uint16_t)d[i] | ((uint16_t)d[i + 1U] << 8));
}

static uint16_t can_u16_be(const uint8_t * d, uint32_t i)
{
  return (uint16_t)(((uint16_t)d[i] << 8) | (uint16_t)d[i + 1U]);
}

static uint32_t can_u32_le(const uint8_t * d, uint32_t i)
{
  return (uint32_t)d[i] | ((uint32_t)d[i + 1U] << 8) |
         ((uint32_t)d[i + 2U] << 16) | ((uint32_t)d[i + 3U] << 24);
}

static int32_t User_CAN_DecodeTireTemperatureCenti(uint8_t integer_part,
                                                   uint8_t fractional_part)
{
  if(fractional_part > 99U) fractional_part = 99U;
  return ((int32_t)integer_part * 100) + (int32_t)fractional_part;
}

static void User_CAN_SubmitTireTemperatureFrame(uint32_t can_id,
                                                const uint8_t data[8])
{
  /* Frame order follows the screen-cell numbering:
   * 0x71: RF outside->inside (#1..#4)
   * 0x72: LF inside->outside (#5..#8)
   * 0x73: LR outside->inside (#9..#12)
   * 0x74: RR inside->outside (#13..#16). */
  static const uint8_t dashboard_wheel[4] = {2U, 0U, 1U, 3U};
  static const uint8_t reverse_segment_order[4] = {0U, 1U, 0U, 1U};
  uint32_t frame_index = can_id - 0x071U;
  int32_t temperatures_centi[4];

  if((data == NULL) || (frame_index >= 4U)) return;
  for(uint32_t payload_index = 0U; payload_index < 4U; payload_index++) {
    uint32_t segment_index = (reverse_segment_order[frame_index] != 0U) ?
                             (3U - payload_index) : payload_index;
    temperatures_centi[segment_index] =
        User_CAN_DecodeTireTemperatureCenti(data[payload_index * 2U],
                                            data[payload_index * 2U + 1U]);
  }
  Dashboard_UI_SubmitTireTemperaturesCenti(dashboard_wheel[frame_index],
                                           temperatures_centi);
}
/* USER CODE END 0 */

CAN_HandleTypeDef hcan1;
CAN_HandleTypeDef hcan2;

/* CAN1 init function */
void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 6;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_12TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_1TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = ENABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}
/* CAN2 init function */
void MX_CAN2_Init(void)
{

  /* USER CODE BEGIN CAN2_Init 0 */

  /* USER CODE END CAN2_Init 0 */

  /* USER CODE BEGIN CAN2_Init 1 */

  /* USER CODE END CAN2_Init 1 */
  hcan2.Instance = CAN2;
  hcan2.Init.Prescaler = 6;
  hcan2.Init.Mode = CAN_MODE_NORMAL;
  hcan2.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan2.Init.TimeSeg1 = CAN_BS1_12TQ;
  hcan2.Init.TimeSeg2 = CAN_BS2_1TQ;
  hcan2.Init.TimeTriggeredMode = DISABLE;
  hcan2.Init.AutoBusOff = ENABLE;
  hcan2.Init.AutoWakeUp = DISABLE;
  hcan2.Init.AutoRetransmission = ENABLE;
  hcan2.Init.ReceiveFifoLocked = DISABLE;
  hcan2.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN2_Init 2 */

  /* USER CODE END CAN2_Init 2 */

}

static uint32_t HAL_RCC_CAN1_CLK_ENABLED=0;

void HAL_CAN_MspInit(CAN_HandleTypeDef* canHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspInit 0 */

  /* USER CODE END CAN1_MspInit 0 */
    /* CAN1 clock enable */
    HAL_RCC_CAN1_CLK_ENABLED++;
    if(HAL_RCC_CAN1_CLK_ENABLED==1){
      __HAL_RCC_CAN1_CLK_ENABLE();
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**CAN1 GPIO Configuration
    PB8     ------> CAN1_RX
    PB9     ------> CAN1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* CAN1 interrupt Init */
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_SetPriority(CAN1_RX1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX1_IRQn);
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
  /* USER CODE BEGIN CAN1_MspInit 1 */

  /* USER CODE END CAN1_MspInit 1 */
  }
  else if(canHandle->Instance==CAN2)
  {
  /* USER CODE BEGIN CAN2_MspInit 0 */

  /* USER CODE END CAN2_MspInit 0 */
    /* CAN2 clock enable */
    __HAL_RCC_CAN2_CLK_ENABLE();
    HAL_RCC_CAN1_CLK_ENABLED++;
    if(HAL_RCC_CAN1_CLK_ENABLED==1){
      __HAL_RCC_CAN1_CLK_ENABLE();
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**CAN2 GPIO Configuration
    PB12     ------> CAN2_RX
    PB13     ------> CAN2_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* CAN2 interrupt Init */
    HAL_NVIC_SetPriority(CAN2_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN2_RX0_IRQn);
    HAL_NVIC_SetPriority(CAN2_RX1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN2_RX1_IRQn);
    HAL_NVIC_SetPriority(CAN2_SCE_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN2_SCE_IRQn);
  /* USER CODE BEGIN CAN2_MspInit 1 */

  /* USER CODE END CAN2_MspInit 1 */
  }
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef* canHandle)
{

  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspDeInit 0 */

  /* USER CODE END CAN1_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_RCC_CAN1_CLK_ENABLED--;
    if(HAL_RCC_CAN1_CLK_ENABLED==0){
      __HAL_RCC_CAN1_CLK_DISABLE();
    }

    /**CAN1 GPIO Configuration
    PB8     ------> CAN1_RX
    PB9     ------> CAN1_TX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8|GPIO_PIN_9);

    /* CAN1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_RX1_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
  /* USER CODE BEGIN CAN1_MspDeInit 1 */

  /* USER CODE END CAN1_MspDeInit 1 */
  }
  else if(canHandle->Instance==CAN2)
  {
  /* USER CODE BEGIN CAN2_MspDeInit 0 */

  /* USER CODE END CAN2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CAN2_CLK_DISABLE();
    HAL_RCC_CAN1_CLK_ENABLED--;
    if(HAL_RCC_CAN1_CLK_ENABLED==0){
      __HAL_RCC_CAN1_CLK_DISABLE();
    }

    /**CAN2 GPIO Configuration
    PB12     ------> CAN2_RX
    PB13     ------> CAN2_TX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_12|GPIO_PIN_13);

    /* CAN2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(CAN2_RX0_IRQn);
    HAL_NVIC_DisableIRQ(CAN2_RX1_IRQn);
    HAL_NVIC_DisableIRQ(CAN2_SCE_IRQn);
  /* USER CODE BEGIN CAN2_MspDeInit 1 */

  /* USER CODE END CAN2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/*
 * @func: 锟斤拷锟斤拷CAN锟斤拷锟剿诧拷
 *
 */
void CAN1_Filter_Config(void)
{
	CAN_FilterTypeDef CAN_FilterInitStructure;

	/* Bank 0: BMS_PackStatus(0x4B0 BO_1200), DataLogger(0x305 BO_773), Debug2_Torque(0x502 BO_1282), Debug5_Velocity(0x505 BO_1285) */
	CAN_FilterInitStructure.FilterActivation = ENABLE;
	CAN_FilterInitStructure.FilterBank = 0x00;
	CAN_FilterInitStructure.FilterFIFOAssignment = CAN_FILTER_FIFO0;
	CAN_FilterInitStructure.FilterIdHigh = CAN1_RX_MSG_ID_BANK0[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN1_RX_MSG_ID_BANK0[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN1_RX_MSG_ID_BANK0[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN1_RX_MSG_ID_BANK0[3] << 5;
	CAN_FilterInitStructure.FilterMode = CAN_FILTERMODE_IDLIST;
	CAN_FilterInitStructure.FilterScale = CAN_FILTERSCALE_16BIT;
	CAN_FilterInitStructure.SlaveStartFilterBank = 14;
	HAL_CAN_ConfigFilter(&hcan1, &CAN_FilterInitStructure);

	/* Bank 1: Debug6_MotorTemp(0x506), Debug9_Status(0x509), Debug8_IGBT(0x508), Debug7_Inverter(0x507) */
	CAN_FilterInitStructure.FilterBank = 0x01;
	CAN_FilterInitStructure.FilterIdHigh = CAN1_RX_MSG_ID_BANK1[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN1_RX_MSG_ID_BANK1[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN1_RX_MSG_ID_BANK1[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN1_RX_MSG_ID_BANK1[3] << 5;
	HAL_CAN_ConfigFilter(&hcan1, &CAN_FilterInitStructure);

	/* Bank 2: Debug3_Diag12(0x503), Debug4_Diag34(0x504),
	 * PDM_LowVoltageBus(0x5A0 BO_1440), PDM_LowVoltageBattery(0x5A1 BO_1441) */
	CAN_FilterInitStructure.FilterBank = 0x02;
	CAN_FilterInitStructure.FilterIdHigh = CAN1_RX_MSG_ID_BANK2[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN1_RX_MSG_ID_BANK2[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN1_RX_MSG_ID_BANK2[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN1_RX_MSG_ID_BANK2[3] << 5;
	HAL_CAN_ConfigFilter(&hcan1, &CAN_FilterInitStructure);

	/* Bank 3: BMS SOP V2 0x4A0/0x4A3 */
	CAN_FilterInitStructure.FilterBank = 0x03;
	CAN_FilterInitStructure.FilterIdHigh = CAN1_RX_MSG_ID_BANK3[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN1_RX_MSG_ID_BANK3[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN1_RX_MSG_ID_BANK3[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN1_RX_MSG_ID_BANK3[3] << 5;
	HAL_CAN_ConfigFilter(&hcan1, &CAN_FilterInitStructure);

	/* Bank 4: FanController_Status(0x5A2 BO_1442) 与踏板编码器 0x001 */
	CAN_FilterInitStructure.FilterBank = 0x04;
	CAN_FilterInitStructure.FilterIdHigh = CAN1_RX_MSG_ID_BANK4[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN1_RX_MSG_ID_BANK4[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN1_RX_MSG_ID_BANK4[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN1_RX_MSG_ID_BANK4[3] << 5;
	HAL_CAN_ConfigFilter(&hcan1, &CAN_FilterInitStructure);

	/* Bank 5: tire-temperature frames 0x071..0x074 -> FIFO0 */
	CAN_FilterInitStructure.FilterBank = 0x05;
	CAN_FilterInitStructure.FilterIdHigh = CAN1_RX_MSG_ID_BANK5[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN1_RX_MSG_ID_BANK5[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN1_RX_MSG_ID_BANK5[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN1_RX_MSG_ID_BANK5[3] << 5;
	HAL_CAN_ConfigFilter(&hcan1, &CAN_FilterInitStructure);
}

/*
 * @func: CAN2 (steering wheel) filter config
 * Vehicle_CanB.dbc: steering wheel sits on the CAN2 bus. 0x310 is relayed
 * verbatim to the ECU while no sprint/lap timing session holds the mode lock;
 * 0x703 is built by the wheel and always relayed verbatim.
 *   BO_784  DriveMode_Request 0x310  DriveModeCode byte0 = mode + 48 (47..51)
 *   BO_1795 SteeringCmd_0x703 0x703  SlipLevel byte0 bit0..2; byte1 bit0
 *                                    DisableRequest, bit1 ErrorClearActive,
 *                                    bit2 LapTimerToggle
 */
void CAN2_Filter_Config(void)
{
	CAN_FilterTypeDef CAN_FilterInitStructure;

	/* Bank 14: steering wheel frames 0x310/0x703 -> FIFO0 */
	CAN_FilterInitStructure.FilterActivation = ENABLE;
	CAN_FilterInitStructure.FilterBank = 14;
	CAN_FilterInitStructure.FilterFIFOAssignment = CAN_FILTER_FIFO0;
	CAN_FilterInitStructure.FilterIdHigh = CAN2_RX_MSG_ID_BANK0[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN2_RX_MSG_ID_BANK0[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN2_RX_MSG_ID_BANK0[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN2_RX_MSG_ID_BANK0[3] << 5;
	CAN_FilterInitStructure.FilterMode = CAN_FILTERMODE_IDLIST;
	CAN_FilterInitStructure.FilterScale = CAN_FILTERSCALE_16BIT;
	CAN_FilterInitStructure.SlaveStartFilterBank = 14;
	HAL_CAN_ConfigFilter(&hcan2, &CAN_FilterInitStructure);
}

/*
 * @func: CAN报文发送[标准格式、数据帧]
 */
static void User_CAN_TxQueueDrain(void)
{
  CAN_TxHeaderTypeDef tx_header;
  uint32_t tx_mailbox;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  if(g_user_can_tx_draining != 0U) {
    __set_PRIMASK(primask);
    return;
  }
  g_user_can_tx_draining = 1U;

  while((g_user_can_tx_count > 0U) &&
        (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0U)) {
    User_CAN_TxItem_t * item = &g_user_can_tx_queue[g_user_can_tx_head];

    tx_header.RTR = CAN_RTR_DATA;
    tx_header.IDE = CAN_ID_STD;
    tx_header.StdId = item->id;
    tx_header.TransmitGlobalTime = DISABLE;
    tx_header.DLC = item->dlc;
    if(HAL_CAN_AddTxMessage(&hcan1, &tx_header, item->data, &tx_mailbox) != HAL_OK) {
      break;
    }
    g_user_can_tx_head = (uint8_t)((g_user_can_tx_head + 1U) % USER_CAN_TX_QUEUE_CAPACITY);
    g_user_can_tx_count--;
  }

  g_user_can_tx_draining = 0U;
  __set_PRIMASK(primask);
}

static uint8_t User_CAN_TxQueuePush(uint32_t can_id, const uint8_t * data, uint32_t dlc)
{
  uint32_t primask;
  User_CAN_TxItem_t * item;

  if((data == NULL) || (dlc > 8U)) return 0U;

  primask = __get_PRIMASK();
  __disable_irq();
  if(g_user_can_tx_count >= USER_CAN_TX_QUEUE_CAPACITY) {
    g_user_can_tx_drop_count++;
    __set_PRIMASK(primask);
    return 0U;
  }

  item = &g_user_can_tx_queue[g_user_can_tx_tail];
  item->id = can_id;
  item->dlc = (uint8_t)dlc;
  for(uint32_t index = 0U; index < 8U; index++) {
    item->data[index] = (index < dlc) ? data[index] : 0U;
  }
  g_user_can_tx_tail = (uint8_t)((g_user_can_tx_tail + 1U) % USER_CAN_TX_QUEUE_CAPACITY);
  g_user_can_tx_count++;
  __set_PRIMASK(primask);

  User_CAN_TxQueueDrain();
  return 1U;
}

/*
 * @func: CAN paper send[standard form、data]
 */
//shaoqi_add
//发送指定ID
static void User_CAN_SendDlc(uint32_t can_id, const uint8_t * data, uint32_t dlc)
{
	(void)User_CAN_TxQueuePush(can_id, data, dlc);
}

void User_CAN_Send_sq(uint32_t CAN_ID_NEW,uint8_t* CAN_TxData_NEW)
{
	User_CAN_SendDlc(CAN_ID_NEW, CAN_TxData_NEW, 8U);
}

/* 0x310 DriveMode_Request: byte0 = mode + 48（-1..3 → 47..51），byte1..7 为 0。
 * 默认模式编码按项目负责人要求类推，中央库已登记候选值，仍需 ECU 上板验证。
 * DLC 8 与方向盘发送格式一致；返回 0 时不改界面，由调用方提示发送失败。 */
uint8_t CAN_SendDriveModeRequest(int32_t mode_index)
{
  uint8_t data[8] = {0U};

  if((mode_index < -1) || (mode_index > 3)) return 0U;
  data[0] = (uint8_t)(48 + mode_index);
  return User_CAN_TxQueuePush(0x310U, data, 8U);
}

void CAN_ServiceTask(void *argument)
{
  /* USER CODE BEGIN CAN_ServiceTask */
  User_CAN_RxItem_t frame;
  (void)argument;
  for(;;) {
    /* Wake at least every CAN_TIMEOUT_POLL_MS so silent sources time out. */
    if(xQueueReceive(g_can_rx_queue, &frame, pdMS_TO_TICKS(CAN_TIMEOUT_POLL_MS)) == pdPASS) {
      User_CAN_ProcessFifo0(&frame);
    }
    User_CAN_ServiceTimeouts(HAL_GetTick());
  }
  /* USER CODE END CAN_ServiceTask */
}

/*
 * @func: CAN1_Filter_Config
 * DBC: Vehicle_CanB.dbc
 * Message-ID mapping: DBC decimal -> HEX
 *   BO_1289 Debug9=0x509, BO_1288 Debug8=0x508, BO_1287 Debug7=0x507,
 *   BO_1286 Debug6=0x506, BO_1285 Debug5=0x505, BO_1282 Debug2=0x502,
 *   BO_1284 Debug4=0x504, BO_1283 Debug3=0x503,
 *   BO_773  DataLogger=0x305 (油门、油压；机械制动条由油压推算),
 *   BRT25 踏板编码器自动上报 0x001 (单圈绝对值，供能量回收条),
 *   BO_769 GPS_Speed=0x301(Display->ECU),
 *   BO_1440 PDM_LowVoltageBus=0x5A0, BO_1441 PDM_LowVoltageBattery=0x5A1,
 *   BO_1442 FanController_Status=0x5A2 (Fan1/2/3_RPM + PWM1/2_Duty),
 *   BO_103/104 GPS telemetry=0x067/0x068, BO_1952/1953=0x7A0/0x7A1(Display->WirelessGateway),
 *   BO_113..116 tire-temperature cells 1..16=0x071..0x074,
 *   Vehicle_CanB.dbc BO_1795=0x703(SteeringCmd): built by the wheel on CAN2, relayed verbatim to the ECU
 * Wheel order mapping: DBC {RL,RR,FL,FR}(0,1,2,3) -> Dashboard {LF,LR,RF,RR}(0,1,2,3)
 *   dash[0]=LF <- DBC[2]=FL, dash[1]=LR <- DBC[0]=RL,
 *   dash[2]=RF <- DBC[3]=FR, dash[3]=RR <- DBC[1]=RR
 */
void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan)
{
  if((hcan != NULL) && (hcan->Instance == CAN1)) User_CAN_TxQueueDrain();
}

void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *hcan)
{
  if((hcan != NULL) && (hcan->Instance == CAN1)) User_CAN_TxQueueDrain();
}

void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *hcan)
{
  if((hcan != NULL) && (hcan->Instance == CAN1)) User_CAN_TxQueueDrain();
}

/* Only BUSOFF/ERROR notifications are armed (main.c). ABOM recovers the
 * controller in hardware, so this just counts the event for diagnosis. */
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
  if((hcan != NULL) && ((HAL_CAN_GetError(hcan) & HAL_CAN_ERROR_BOF) != 0U)) g_can_bus_off_count++;
}

/* 0x703 SteeringCmd (Vehicle_CanB.dbc BO_1795) byte1 bits. */
#define STEERING_CMD_MIN_DLC        2U
#define STEERING_CMD_DISABLE_REQ    0x01U
#define STEERING_CMD_ERROR_CLEAR    0x02U
#define STEERING_CMD_LAP_TOGGLE     0x04U

/* 0x703 button bits act on the rising edge. The wheel firmware sets
 * LapTimerToggle / ErrorClearActive in one frame when the key is released and
 * sends no 0-frame for it; DisableRequest is 1 every 20 ms while the chord is
 * held and ends with a 0-frame. Read from the wheel code and its host tests,
 * not yet confirmed on the bus. A frame counts as a new press when the
 * previous frame had the bit at 0, or when the previous 1 is older than
 * CAN_BUTTON_REPEAT_GAP_MS. Two single-key presses with no other 0x703 frame
 * in between and less than 300 ms apart therefore count as one. */
#define CAN_BUTTON_REPEAT_GAP_MS 300U

typedef struct {
  uint8_t level;
  uint32_t last_active_tick;
} User_CAN_Button_t;

static uint8_t User_CAN_ButtonPressed(User_CAN_Button_t * button, uint8_t active, uint32_t now)
{
  uint8_t pressed = 0U;

  if(active != 0U) {
    if((button->level == 0U) || ((now - button->last_active_tick) > CAN_BUTTON_REPEAT_GAP_MS)) {
      pressed = 1U;
    }
    button->last_active_tick = now;
  }
  button->level = active;
  return pressed;
}

static void User_CAN_HandleSteeringWheel(const CAN_RxHeaderTypeDef * rx,
                                         const uint8_t data[8])
{
  static User_CAN_Button_t s_lap_button;
  static User_CAN_Button_t s_clear_button;
  static User_CAN_Button_t s_disable_request;

  if((rx == NULL) || (data == NULL)) return;

  switch(rx->StdId)
  {
    /* Vehicle_CanB.dbc BO_784 DriveMode_Request 0x310: DriveModeCode byte0 = mode + 48 (47..51).
     * Drive mode is relayed verbatim to the ECU, except while a sprint/lap
     * timing session holds the mode lock: then the request is not relayed and
     * Dashboard_UI_SubmitDriveMode() raises the MODE LOCK alert instead.
     * Codes outside 47..51 are dropped, neither relayed nor shown. */
    case 0x310:
    {
      if((data[0] < 47U) || (data[0] > 51U)) {
        g_can_rx_invalid_count++;
        break;
      }
      if(Dashboard_UI_IsTimingModeLocked() == 0U) {
        User_CAN_SendDlc(rx->StdId, data, rx->DLC);
      }
      Dashboard_UI_RequestDriveMode((int32_t)data[0] - 48);
      break;
    }

    /* Vehicle_CanB.dbc BO_1795 SteeringCmd 0x703. The wheel builds the whole
     * frame; it is relayed to the ECU with the same ID, DLC and payload, also
     * while the timing mode lock is active, because DisableRequest is the
     * driver's exit-enable request and must never be held back here. Only a
     * frame shorter than 2 bytes is dropped. A slip value above 7 is still
     * relayed (the ECU checks its own range) but is counted and not shown. */
    case 0x703:
    {
      const uint32_t now = HAL_GetTick();

      if(rx->DLC < STEERING_CMD_MIN_DLC) {
        g_can_rx_invalid_count++;
        break;
      }
      User_CAN_SendDlc(rx->StdId, data, rx->DLC);
      if(data[0] <= 7U) {
        Dashboard_UI_SubmitSlipLevel((int32_t)data[0]);
      } else {
        g_can_rx_invalid_count++;
      }
      if(User_CAN_ButtonPressed(&s_lap_button, data[1] & STEERING_CMD_LAP_TOGGLE, now) != 0U) {
        Dashboard_UI_RequestLapToggle();
      }
      if(User_CAN_ButtonPressed(&s_clear_button, data[1] & STEERING_CMD_ERROR_CLEAR, now) != 0U) {
        Dashboard_UI_RequestAlertClear();
      }
      if(User_CAN_ButtonPressed(&s_disable_request, data[1] & STEERING_CMD_DISABLE_REQ, now) != 0U) {
        Dashboard_UI_RequestDisableAlert();
      }
      break;
    }

    default:
      break;
  }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  User_CAN_ReceiveIRQ(hcan, CAN_RX_FIFO0);
}

static void User_CAN_ProcessFifo0(const User_CAN_RxItem_t *frame)
{
  const uint32_t now = HAL_GetTick();

  RxHeader.StdId = frame->id;
  RxHeader.DLC = frame->dlc;
  if(frame->bus == 2U) {
    if(frame->dlc < 1U) { g_can_rx_invalid_count++; return; }
    User_CAN_HandleSteeringWheel(&RxHeader, frame->data);
    return;
  }
  uint32_t min_dlc = 8U;
  switch(frame->id) {
    case 0x4B0: min_dlc = 7U; break;
    case 0x001: min_dlc = 4U; break;
    case 0x305: min_dlc = 6U; break;
    case 0x509: min_dlc = 5U; break;
    default: break;
  }
  if(frame->dlc < min_dlc) { g_can_rx_invalid_count++; return; }
  for(uint32_t i = 0U; i < 8U; i++) CAN_RxData[i] = frame->data[i];

  switch(RxHeader.StdId)
  {
    /* BRT25-C0M4096-RT1 自动上报：ID=0x001，DLC=7。
     * DLC=4 的命令应答和其他 FUNC 不进入位置跟踪；编码器只收不发。 */
    case 0x001:
    {
      uint16_t count;
      int32_t regen_pct;
      if(BrakePedal_DecodePosition(CAN_RxData, RxHeader.DLC, &count) == 0U) break;
      User_CAN_NoteRx(CAN_SRC_BRAKE_ANGLE, now);
      BrakePedal_OnEncoder(&g_brake_pedal, count, now);
      regen_pct = BrakePedal_GetRegenPct(&g_brake_pedal, now);
      g_can_dashboard_data.regen_pct = regen_pct;
      User_CAN_SetValid(DASH_VALID_REGEN, (regen_pct >= 0) ? 1U : 0U);
      break;
    }

    /* Vehicle_CanB.dbc BO_113..116, cell order #1..#16.
     * Each pair is integer byte followed by hundredths byte. */
    case 0x071:
    case 0x072:
    case 0x073:
    case 0x074:
    {
      if(RxHeader.DLC == 8U) {
        User_CAN_SubmitTireTemperatureFrame(RxHeader.StdId, CAN_RxData);
      }
      return;
    }

    /* DBC BO_1200 BMS_PackStatus 0x4B0, DLC=7 (same layout as CAN1 0x186050F4):
     *   BatteryVoltage(byte0-1,BE,0.1V), BatteryCurrent(byte2-3,BE,int16,0.1A),
     *   BatterySOC(byte4,%), validity(byte5), AlarmLevel/State(byte6).
     * Byte5 bit0/1/2 qualify voltage/current/SOC. Bit3/4 (cells/temperatures
     * complete) do not concern these three fields. Current sign: charge +,
     * discharge - (BMS .../电流检测与SOC模块.md, confirmed on a charging capture). */
    case 0x4B0:
    {
      const uint8_t flags = CAN_RxData[5];
      const uint16_t voltage_dv = can_u16_be(CAN_RxData, 0U);
      const int16_t current_da = (int16_t)can_u16_be(CAN_RxData, 2U);
      const uint8_t soc = CAN_RxData[4];

      User_CAN_NoteRx(CAN_SRC_BMS, now);
      if((flags & 0x01U) != 0U) {
        g_can_dashboard_data.sum_voltage_dv = (int32_t)voltage_dv;
        g_can_dashboard_data.sum_voltage = (int32_t)(voltage_dv / 10U);
      }
      if((flags & 0x02U) != 0U) {
        g_can_dashboard_data.sum_current_da = (int32_t)current_da;
        g_can_dashboard_data.sum_current = (int32_t)(current_da / 10);
      }
      if(((flags & 0x04U) != 0U) && (soc <= 100U)) {
        g_can_dashboard_data.soc = (int32_t)soc;
        User_CAN_SetValid(DASH_VALID_SOC, 1U);
      }
      else {
        User_CAN_SetValid(DASH_VALID_SOC, 0U);
      }
      User_CAN_SetValid(DASH_VALID_HV_V, flags & 0x01U);
      User_CAN_SetValid(DASH_VALID_HV_I, (flags >> 1) & 0x01U);
      break;
    }

    /* DBC BO_1184 BMS_SOPLimits 0x4A0: 双向功率上限(0.1 kW)，单帧 CRC。
     * 非法帧(DLC/版本/保留位/CRC)不更新数据也不刷新接收时间，由超时撤销有效位；
     * LimitsValid=0 的合法帧功率为 0，按无限值处理显示 "--"。 */
    case SOP_ID_POWER:
    {
      sop_power_t sop;
      if(Sop_DecodePower(CAN_RxData, RxHeader.DLC, &sop) == 0U) {
        g_can_rx_invalid_count++;
        break;
      }
      User_CAN_NoteRx(CAN_SRC_SOP_POWER, now);
      g_can_dashboard_data.sop_discharge_power_dkw = (int32_t)sop.discharge_dkw;
      g_can_dashboard_data.sop_charge_power_dkw = (int32_t)sop.charge_dkw;
      User_CAN_SetValid(DASH_VALID_SOP_POWER, sop.limits_valid);
      break;
    }

    /* DBC BO_1187 BMS_SOPStatus 0x4A3: 允许放电/回充电流(0.1 A)，无 CRC。
     * InputHealth bit7=0 表示 SOP 输入不全有效，电流限值不可信，显示 "--"。 */
    case SOP_ID_CURRENT:
    {
      sop_current_t sop;
      if(Sop_DecodeCurrent(CAN_RxData, RxHeader.DLC, &sop) == 0U) {
        g_can_rx_invalid_count++;
        break;
      }
      User_CAN_NoteRx(CAN_SRC_SOP_CURRENT, now);
      g_can_dashboard_data.sop_discharge_current_da = (int32_t)sop.discharge_da;
      g_can_dashboard_data.sop_charge_current_da = (int32_t)sop.charge_da;
      User_CAN_SetValid(DASH_VALID_SOP_CURRENT, sop.inputs_valid);
      break;
    }

    /* DBC BO_773 DataLogger 0x305: SteeringWheelAngle(0|16@1-), APS_OpenPct(16|16@1+), OilPressure_Kpa(32|16@1+) */
    case 0x305:
    {
      /* SG_ SteeringWheelAngle: 0|16@1- (0.1,0) deg, stored as the raw 0.1 deg count */
      g_can_dashboard_data.steering_angle = (int32_t)(int16_t)can_u16_le(CAN_RxData, 0U);
      /* SG_ APS_OpenPct: 16|16@1+ (0.1,0) %, store integer pct */
      g_can_dashboard_data.aps_open_pct = (int32_t)(can_u16_le(CAN_RxData, 2U) / 10U);
      /* SG_ OilPressure_Kpa: 32|16@1+ (0.001,0) Kpa, raw count */
      const uint16_t oil_raw = can_u16_le(CAN_RxData, 4U);
      g_can_dashboard_data.oil_pressure = (int32_t)oil_raw;
      BrakePedal_OnOil(&g_brake_pedal, oil_raw, now);
      User_CAN_NoteRx(CAN_SRC_APS, now);
      User_CAN_SetValid(DASH_VALID_APS, 1U);
      /* 机械制动条继续由油压推算；与油门同帧同新鲜度。 */
      g_can_dashboard_data.brake_pct = BrakePedal_GetMechanicalPct(&g_brake_pedal, now);
      User_CAN_NoteRx(CAN_SRC_MECH_BRAKE, now);
      User_CAN_SetValid(DASH_VALID_BRAKE, 1U);
      break;
    }

    /* DBC BO_1282 Debug2 0x502: RL/RR/FL/FR_ActualTorque (1,0) Nm, 16-bit signed each */
    case 0x502:
    {
      for(uint32_t i = 0U; i < 4U; i++) {
        g_can_dashboard_data.torque[k_can_dbc_to_dash[i]] = (int32_t)(int16_t)can_u16_le(CAN_RxData, i * 2U);
      }
      User_CAN_NoteRx(CAN_SRC_TORQUE, now);
      User_CAN_SetValid(DASH_VALID_TORQUE, 1U);
      break;
    }

    /* DBC BO_1285 Debug5 0x505: RL/RR/FL/FR_ActualVelocity (1,0) rpm, 16-bit signed each */
    case 0x505:
    {
      for(uint32_t i = 0U; i < 4U; i++) {
        g_can_dashboard_data.rpm[k_can_dbc_to_dash[i]] = (int32_t)(int16_t)can_u16_le(CAN_RxData, i * 2U);
      }
      User_CAN_NoteRx(CAN_SRC_RPM, now);
      User_CAN_SetValid(DASH_VALID_RPM, 1U);
      break;
    }

    /* DBC BO_1286 Debug6 0x506: RL/RR/FL/FR_Motor_temperature (0.1,0) degC, 16-bit signed each */
    case 0x506:
    {
      for(uint32_t i = 0U; i < 4U; i++) {
        g_can_dashboard_data.motor_temp[k_can_dbc_to_dash[i]] = (int32_t)((int16_t)can_u16_le(CAN_RxData, i * 2U) / 10);
      }
      User_CAN_NoteRx(CAN_SRC_MOTOR_TEMP, now);
      User_CAN_SetValid(DASH_VALID_MOTOR_TEMP, 1U);
      break;
    }

    /* DBC BO_1289 Debug9 0x509: AMK status flags + ModeFlag, DLC=5 */
    case 0x509:
    {
      /* SG_ FR_AMK_bEnable(20|1), FL_AMK_bEnable(21|1), RR_AMK_bEnable(22|1), RL_AMK_bEnable(23|1) */
      /* DBC byte2: bit4=FR, bit5=FL, bit6=RR, bit7=RL -> Dashboard: LF=FL, LR=RL, RF=FR, RR=RR */
      g_can_dashboard_data.motor_enable[0] = (CAN_RxData[2] >> 5) & 0x01U;
      g_can_dashboard_data.motor_enable[1] = (CAN_RxData[2] >> 7) & 0x01U;
      g_can_dashboard_data.motor_enable[2] = (CAN_RxData[2] >> 4) & 0x01U;
      g_can_dashboard_data.motor_enable[3] = (CAN_RxData[2] >> 6) & 0x01U;
      User_CAN_NoteRx(CAN_SRC_AMK_STATE, now);
      User_CAN_SetValid(DASH_VALID_AMK_EN, 1U);

      /* SG_ ModeFlag: 4|4@1- (1,0) [-8,7], signed 4-bit in byte0 high nibble.
       * Values (vehicle-interfaces archive/CANRS485_G473_REFERENCE/cana_canb_protocol.md,
       * not in the formal DBC): -1 default mode (12.3 kW per motor; the ECU
       * returns to it after every LV power-up), 0 AC/acceleration, 1 AX/autocross,
       * 2 8/skidpad, 3 EN/endurance - the same numbering as 0x310 code - 48.
       * -1 显示 DF/Default；其他未定义值由 UI 忽略。 */
      {
        int32_t mode_val = (int32_t)((CAN_RxData[0] >> 4) & 0x0FU);
        if(mode_val & 8) mode_val -= 16;
        g_can_dashboard_data.mode_index = mode_val;
        Dashboard_UI_SubmitDriveMode(mode_val);
      }
      break;
    }

    /* DBC BO_1288 Debug8 0x508: RL/RR/FL/FR_IGBT_temperature (0.1,0) degC, 16-bit signed each */
    case 0x508:
    {
      for(uint32_t i = 0U; i < 4U; i++) {
        g_can_dashboard_data.igbt_temp[k_can_dbc_to_dash[i]] = (int32_t)((int16_t)can_u16_le(CAN_RxData, i * 2U) / 10);
      }
      User_CAN_NoteRx(CAN_SRC_IGBT_TEMP, now);
      User_CAN_SetValid(DASH_VALID_IGBT_TEMP, 1U);
      break;
    }

    /* DBC BO_1287 Debug7 0x507: RL/RR/FL/FR_Inverter_temperature (0.1,0) degC, 16-bit signed each */
    case 0x507:
    {
      for(uint32_t i = 0U; i < 4U; i++) {
        g_can_dashboard_data.inverter_temp[k_can_dbc_to_dash[i]] = (int32_t)((int16_t)can_u16_le(CAN_RxData, i * 2U) / 10);
      }
      User_CAN_NoteRx(CAN_SRC_INV_TEMP, now);
      User_CAN_SetValid(DASH_VALID_INV_TEMP, 1U);
      break;
    }

    /* DBC BO_1284 Debug4 0x504: Diagnostic_number_3(0|32), Diagnostic_number_4(32|32).
     * Diagnostic_number_1..4 are AMK1..4 = RL, RR, FL, FR (archive/.../cana_canb_protocol.md),
     * i.e. the same wheel order as the other per-wheel frames. */
    case 0x504:
    {
      g_can_dashboard_data.diag_num[k_can_dbc_to_dash[2]] = can_u32_le(CAN_RxData, 0U);
      g_can_dashboard_data.diag_num[k_can_dbc_to_dash[3]] = can_u32_le(CAN_RxData, 4U);
      User_CAN_NoteRx(CAN_SRC_DIAG_FL_FR, now);
      User_CAN_SetValid(DASH_VALID_DIAG_FL_FR, 1U);
      break;
    }

    /* DBC BO_1283 Debug3 0x503: Diagnostic_number_1(0|32) = RL, Diagnostic_number_2(32|32) = RR */
    case 0x503:
    {
      g_can_dashboard_data.diag_num[k_can_dbc_to_dash[0]] = can_u32_le(CAN_RxData, 0U);
      g_can_dashboard_data.diag_num[k_can_dbc_to_dash[1]] = can_u32_le(CAN_RxData, 4U);
      User_CAN_NoteRx(CAN_SRC_DIAG_RL_RR, now);
      User_CAN_SetValid(DASH_VALID_DIAG_RL_RR, 1U);
      break;
    }

    /* DBC BO_1440 PDM_LowVoltageBus 0x5A0: BusVoltage(7|16@0- 0.001V),
     * BusCurrent(23|16@0- 0.01A), BusPower(39|16@0+ 0.1W), BusEnergy(55|16@0+ 0.01Wh).
     * All four at their placeholders (0x7FFF, 0x7FFF, 0xFFFF, 0xFFFF) mean the
     * INA226 is offline; a single saturated field does not. Stored in raw
     * engineering units: mV / cA / dW. */
    case 0x5A0:
    {
      const uint8_t offline = (can_u16_be(CAN_RxData, 0U) == 0x7FFFU) &&
                              (can_u16_be(CAN_RxData, 2U) == 0x7FFFU) &&
                              (can_u16_be(CAN_RxData, 4U) == 0xFFFFU) &&
                              (can_u16_be(CAN_RxData, 6U) == 0xFFFFU);
      User_CAN_NoteRx(CAN_SRC_PDM_BUS, now);
      if(offline == 0U) {
        g_can_dashboard_data.lv_bus_voltage_mV = (int32_t)(int16_t)can_u16_be(CAN_RxData, 0U);
        g_can_dashboard_data.lv_bus_current_cA = (int32_t)(int16_t)can_u16_be(CAN_RxData, 2U);
        g_can_dashboard_data.lv_bus_power_dW = (int32_t)can_u16_be(CAN_RxData, 4U);
      }
      User_CAN_SetValid(DASH_VALID_LV_BUS, (offline == 0U) ? 1U : 0U);
      break;
    }

    /* DBC BO_1441 PDM_LowVoltageBattery 0x5A1: battery-side mirror of 0x5A0.
     * UI shows the bus values; battery values are kept for diagnostics. */
    case 0x5A1:
    {
      g_can_dashboard_data.lv_batt_voltage_mV = (int32_t)(int16_t)can_u16_be(CAN_RxData, 0U);
      g_can_dashboard_data.lv_batt_current_cA = (int32_t)(int16_t)can_u16_be(CAN_RxData, 2U);
      g_can_dashboard_data.lv_batt_power_dW = (int32_t)can_u16_be(CAN_RxData, 4U);
      break;
    }

    /* DBC BO_1442 FanController_Status 0x5A2, 100ms: Fan1/2/3_RPM (7|16@0+, 1rpm),
     * Fan_PWM1_Duty(55|8 %), Fan_PWM2_Duty(63|8 %). The DBC defines only two
     * measured duty cycles; fan 3 has no independent duty signal. */
    case 0x5A2:
    {
      for(uint32_t fan = 0U; fan < 3U; fan++) {
        g_can_dashboard_data.fan_rpm[fan] = (int32_t)can_u16_be(CAN_RxData, fan * 2U);
      }
      g_can_dashboard_data.fan_pwm_duty[0] = (int32_t)CAN_RxData[6];
      g_can_dashboard_data.fan_pwm_duty[1] = (int32_t)CAN_RxData[7];
      User_CAN_NoteRx(CAN_SRC_FAN, now);
      User_CAN_SetValid(DASH_VALID_FAN, 1U);
      break;
    }

    default:
      break;
  }

  User_CAN_UpdateMaxTemp();
  Dashboard_UI_SubmitData(&g_can_dashboard_data);
}

/*
 * @func: CAN_SendGPSSpeed - send GPS speed to ECU
 * DBC BO_769 GPS_Speed 0x301, DLC=2:
 * GroundSpeed bits 0..15, Intel unsigned, factor 0.1 km/h, Display->ECU.
 * Source: GNSS RMC knots -> 0.01 km/h internal -> rounded 0.1 km/h CAN,
 * periodic 50 ms; invalid or stale RMC data is transmitted as zero.
 */
void CAN_SendGPSSpeed(int32_t speed_kmh_tenths)
{
  uint8_t speed_data[2] = {0};

  if(speed_kmh_tenths < 0) speed_kmh_tenths = 0;
  if(speed_kmh_tenths > 3000) speed_kmh_tenths = 3000;

  speed_data[0] = (uint8_t)(speed_kmh_tenths & 0xFFU);
  speed_data[1] = (uint8_t)((speed_kmh_tenths >> 8) & 0xFFU);

  g_can_dashboard_data.speed = (speed_kmh_tenths + 5) / 10;

  User_CAN_SendDlc(0x301, speed_data, 2U);
}

static void can_pack_u16_le(uint8_t * data, uint16_t value)
{
  data[0] = (uint8_t)(value & 0xFFU);
  data[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void can_pack_u32_le(uint8_t * data, uint32_t value)
{
  data[0] = (uint8_t)(value & 0xFFU);
  data[1] = (uint8_t)((value >> 8) & 0xFFU);
  data[2] = (uint8_t)((value >> 16) & 0xFFU);
  data[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void can_pack_u24_le(uint8_t * data, uint32_t value)
{
  data[0] = (uint8_t)(value & 0xFFU);
  data[1] = (uint8_t)((value >> 8) & 0xFFU);
  data[2] = (uint8_t)((value >> 16) & 0xFFU);
}

/*
 * GPS telemetry snapshot, all standard IDs and all new payloads use DLC=8.
 * 0x067: latitude/longitude; 0x068: motion/heading; 0x7A0: status/odometer;
 * 0x7A1: lap timing. Tire inputs use 0x071..0x074 and are not used for GPS TX.
 * `frames` is a mask of CAN_GPS_TX_* groups; each group has its own period.
 */
void CAN_SendGPSTelemetry(const CAN_GPSTelemetry_t * telemetry, uint32_t frames)
{
  uint8_t position_data[8];
  uint8_t motion_data[8];
  uint8_t status_data[8];
  uint8_t lap_data[8];
  uint16_t packed_status;
  uint32_t odometer_tenths;

  if(telemetry == NULL) return;

  can_pack_u32_le(&position_data[0], (uint32_t)telemetry->latitude_e7);
  can_pack_u32_le(&position_data[4], (uint32_t)telemetry->longitude_e7);

  can_pack_u16_le(&motion_data[0], telemetry->ground_track_cdeg);
  can_pack_u16_le(&motion_data[2], telemetry->heading_cdeg);
  can_pack_u16_le(&motion_data[4], (uint16_t)telemetry->altitude_dm);
  packed_status = (uint16_t)(telemetry->status_flags & 0x0FU);
  packed_status |= (uint16_t)((telemetry->lap_diag_state > 7U ? 7U : telemetry->lap_diag_state) << 4);
  packed_status |= (uint16_t)((telemetry->heading_quality > 7U ? 7U : telemetry->heading_quality) << 7);
  packed_status |= (uint16_t)((telemetry->fix_quality > 7U ? 7U : telemetry->fix_quality) << 10);
  packed_status |= (uint16_t)((telemetry->signal_level > 7U ? 7U : telemetry->signal_level) << 13);
  can_pack_u16_le(&motion_data[6], packed_status);

  odometer_tenths = telemetry->odometer_tenths_km;
  if(odometer_tenths > 0xFFFFFFU) odometer_tenths = 0xFFFFFFU;
  can_pack_u24_le(&status_data[0], odometer_tenths);
  status_data[3] = (uint8_t)telemetry->max_snr;
  status_data[4] = (uint8_t)telemetry->avg_snr;
  status_data[5] = telemetry->satellites;
  status_data[6] = telemetry->gsv_tracked_sats;
  status_data[7] = telemetry->lap_count;

  can_pack_u16_le(&lap_data[0], telemetry->lap_current_cs);
  can_pack_u16_le(&lap_data[2], telemetry->lap_last_cs);
  can_pack_u16_le(&lap_data[4], telemetry->lap_best_cs);
  can_pack_u16_le(&lap_data[6], (uint16_t)telemetry->lap_delta_cs);

  if((frames & CAN_GPS_TX_POSITION) != 0U) User_CAN_SendDlc(CAN_ID_GPS_POSITION, position_data, 8U);
  if((frames & CAN_GPS_TX_MOTION) != 0U) User_CAN_SendDlc(CAN_ID_GPS_MOTION, motion_data, 8U);
  if((frames & CAN_GPS_TX_STATUS) != 0U) User_CAN_SendDlc(CAN_ID_GPS_STATUS, status_data, 8U);
  if((frames & CAN_GPS_TX_LAP) != 0U) User_CAN_SendDlc(CAN_ID_GPS_LAP, lap_data, 8U);
}

/* USER CODE END 1 */
