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
/* USER CODE END Includes */

/* USER CODE BEGIN 0 */
CAN_TxHeaderTypeDef TxHeader;
uint8_t CAN_TxData[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };
uint32_t CAN1_TxMail;
uint32_t CAN1_ID = 0x102;
static uint8_t g_can_heartbeat_counter;
/* Only the CAN service task owns the decode workspace. IRQs enqueue compact
 * frames, never scan dashboard state or run protocol/UI logic. */
static CAN_RxHeaderTypeDef RxHeader;
static uint8_t CAN_RxData[8];
#define USER_CAN_RX_QUEUE_CAPACITY 64U

typedef struct {
  uint16_t id;
  uint8_t dlc, bus, fifo;
  uint8_t data[8];
} User_CAN_RxItem_t;

static StaticQueue_t g_can_rx_queue_control;
static uint8_t g_can_rx_queue_storage[USER_CAN_RX_QUEUE_CAPACITY * sizeof(User_CAN_RxItem_t)];
static QueueHandle_t g_can_rx_queue;
static StaticTask_t g_can_task_control;
static StackType_t g_can_task_stack[1024U];
volatile uint32_t g_can_rx_drop_count;
volatile uint32_t g_can_rx_invalid_count;
volatile uint32_t g_can_rx_queue_peak;
static void User_CAN_ProcessFifo0(const User_CAN_RxItem_t *frame);
static void User_CAN_ProcessFifo1(const User_CAN_RxItem_t *frame);

void CAN_StartServiceTask(void)
{
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
    frame.fifo = fifo == CAN_RX_FIFO1 ? 1U : 0U;
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
/* Bank4: FanController_Status(0x5A2 BO_1442), BrakePedal forward(0x306 BO_774) */
uint16_t CAN1_RX_MSG_ID_BANK4[4] = {0x5A2, 0x306, 0x5A2, 0x306};
/* Bank3: IMU_Raw(0x50 BO_80) -> FIFO1 */
uint16_t CAN1_RX_MSG_ID_BANK3[4] = {0x050, 0x050, 0x050, 0x050};
/* Bank5: Tire-temperature cells 1..16, four cells per frame. */
uint16_t CAN1_RX_MSG_ID_BANK5[4] = {0x071, 0x072, 0x073, 0x074};
/* Bank14: Vehicle_CanB.dbc steering wheel on CAN2 - DriveMode(0x310), SlipLevel(0x700), RecorderToggle(0x701), ErrorClear(0x702) */
uint16_t CAN2_RX_MSG_ID_BANK0[4] = {0x310, 0x700, 0x701, 0x702};

#define CAN_ID_GPS_POSITION        0x067U
#define CAN_ID_GPS_MOTION          0x068U
#define CAN_ID_GPS_STATUS          0x069U
#define CAN_ID_GPS_LAP             0x06AU
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
static dashboard_data_t g_can_dashboard_data = {
  .speed = 24,             /* Startup placeholder; GPS speed is submitted separately. */
  .soc = 24,               /* DBC BO_1200 BatterySOC (%) */
  .mode_index = 0,         /* DBC BO_1289 Debug9: ModeFlag [-8,7] */
  .torque = {24, 24, 24, 24},     /* DBC BO_1282 Debug2: ActualTorque (1,0) */
  .motor_enable = {1, 1, 1, 1},   /* DBC BO_1289 Debug9: AMK_bEnable */
  .rpm = {24, 24, 24, 24},        /* DBC BO_1285 Debug5: ActualVelocity (1,0) */
  .sum_voltage = 24,        /* DBC BO_1200 BatteryVoltage (V) */
  .sum_current = 24,        /* DBC BO_1200 BatteryCurrent (A) */
  .max_temperature = 24,    /* 无DBC源: BO_1200无电池温度信号 */
  .motor_temp = {48, 47, 49, 50},  /* DBC BO_1286 Debug6: Motor_temperature (0.1,0) degC */
  .aps_open_pct = 0,        /* DBC BO_773 DataLogger: APS_OpenPct (0.1,0) % */
  .steering_angle = 0,      /* DBC BO_773 DataLogger: SteeringWheelAngle (0.1,0) deg */
  .oil_pressure = 0,        /* DBC BO_773 DataLogger: OilPressure_Kpa (0.001,0) Kpa, raw count */
  .igbt_temp = {0, 0, 0, 0},       /* DBC BO_1288 Debug8: IGBT_temperature (0.1,0) degC */
  .inverter_temp = {0, 0, 0, 0},   /* DBC BO_1287 Debug7: Inverter_temperature (0.1,0) degC */
  .diag_num = {0, 0, 0, 0},        /* DBC BO_1283/1284 Debug3/4: Diagnostic_number */
  .imu_accel = {0, 0, 0},   /* DBC BO_97 IMU_Accel: raw int16, scale=0.00048828125 g */
  .imu_gyro = {0, 0, 0},    /* DBC BO_98 IMU_Gyro: raw int16, scale=0.0610352 deg/s */
  .imu_roll = 0,            /* DBC BO_99 IMU_Roll: raw int16, scale=0.005493 deg */
  .imu_pitch = 0,           /* DBC BO_100 IMU_Pitch: raw int16, scale=0.005493 deg */
  .imu_yaw = 0,             /* DBC BO_101 IMU_Yaw: raw int16, scale=0.005493 deg */
  .imu_mag = {0, 0, 0},     /* DBC BO_102 IMU_Magnetic: raw int16, scale=1 */
  .signal_level = 0,        /* 非DBC */
  .alert_active = 1,        /* 非DBC */
  .odometer_tenths = 0,     /* GPS-owned odometer, not populated from CAN */
  .brake_pct = 10,          /* 非DBC: 制动 0~100% */
};

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
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
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
  hcan2.Init.AutoBusOff = DISABLE;
  hcan2.Init.AutoWakeUp = DISABLE;
  hcan2.Init.AutoRetransmission = DISABLE;
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

	/* Bank 4: FanController_Status(0x5A2 BO_1442), three fan RPM + two PWM duty */
	CAN_FilterInitStructure.FilterBank = 0x04;
	CAN_FilterInitStructure.FilterIdHigh = CAN1_RX_MSG_ID_BANK4[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN1_RX_MSG_ID_BANK4[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN1_RX_MSG_ID_BANK4[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN1_RX_MSG_ID_BANK4[3] << 5;
	HAL_CAN_ConfigFilter(&hcan1, &CAN_FilterInitStructure);

	/* Bank 3: IMU_Raw(0x50) -> FIFO1 */
	CAN_FilterInitStructure.FilterBank = 0x03;
	CAN_FilterInitStructure.FilterFIFOAssignment = CAN_FILTER_FIFO1;
	CAN_FilterInitStructure.FilterIdHigh = CAN1_RX_MSG_ID_BANK3[0] << 5;
	CAN_FilterInitStructure.FilterIdLow = CAN1_RX_MSG_ID_BANK3[1] << 5;
	CAN_FilterInitStructure.FilterMaskIdHigh = CAN1_RX_MSG_ID_BANK3[2] << 5;
	CAN_FilterInitStructure.FilterMaskIdLow = CAN1_RX_MSG_ID_BANK3[3] << 5;
	HAL_CAN_ConfigFilter(&hcan1, &CAN_FilterInitStructure);

	/* Bank 5: tire-temperature frames 0x071..0x074 -> FIFO0 */
	CAN_FilterInitStructure.FilterBank = 0x05;
	CAN_FilterInitStructure.FilterFIFOAssignment = CAN_FILTER_FIFO0;
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
 * 0x700/0x701/0x702 are SteeringWheel->Display only and
 * are merged into a single 0x703 (BO_1795) before being sent to the ECU.
 *   BO_784  DriveMode_Request 0x310  DriveModeCode byte0 = ASCII '0'..'3' (48..51)
 *   BO_1792 SlipLevel_0x700   0x700  SlipLevel byte0 [0..7]
 *   BO_1793 RecorderToggle    0x701  RecorderToggle bit0, sent once per press
 *   BO_1794 ErrorClear_0x702  0x702  ErrorClearActive bit0, sent once per press
 */
void CAN2_Filter_Config(void)
{
	CAN_FilterTypeDef CAN_FilterInitStructure;

	/* Bank 14: steering wheel frames 0x310/0x700/0x701/0x702 -> FIFO0 */
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

static void User_CAN_SendDlc(uint32_t can_id, const uint8_t * data, uint32_t dlc);

void User_CAN_Send()
{
	User_CAN_SendDlc(CAN1_ID, CAN_TxData, 8U);
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

void CAN1_SendHeartbeat(void)
{
  uint8_t heartbeat_data[8];

  heartbeat_data[0] = 0xA5;
  heartbeat_data[1] = 0x5A;
  heartbeat_data[2] = g_can_heartbeat_counter++;
  heartbeat_data[3] = 0xF4;
  heartbeat_data[4] = 0x07;
  heartbeat_data[5] = 0x40;
  heartbeat_data[6] = 0x00;
  heartbeat_data[7] = 0x01;

  User_CAN_Send_sq(CAN1_ID, heartbeat_data);
}

void CAN_ServiceTask(void *argument)
{
  /* USER CODE BEGIN CAN_ServiceTask */
  User_CAN_RxItem_t frame;
  (void)argument;
  for(;;) {
    if(xQueueReceive(g_can_rx_queue, &frame, portMAX_DELAY) == pdPASS) {
      if(frame.fifo == 0U) User_CAN_ProcessFifo0(&frame);
      else User_CAN_ProcessFifo1(&frame);
    }
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
 *   BO_773  DataLogger=0x305, BO_774 BrakePedal_0x306 (ECU gateway forward of
 *   CANC 0x270 IBS_Info brake pedal), BO_769 GPS_Speed=0x301(Display->ECU),
 *   BO_1440 PDM_LowVoltageBus=0x5A0, BO_1441 PDM_LowVoltageBattery=0x5A1,
 *   BO_1442 FanController_Status=0x5A2 (Fan1/2/3_RPM + PWM1/2_Duty),
 *   BO_103..106 GPS telemetry=0x067..0x06A(Display->WirelessGateway),
 *   BO_113..116 tire-temperature cells 1..16=0x071..0x074,
 *   BO_80   IMU_Raw=0x50,
 *   Vehicle_CanB.dbc BO_1792=0x700(SlipLevel), BO_1793=0x701(RecorderToggle), BO_1794=0x702(ErrorClear),
 *   all SteeringWheel->Display only; merged into BO_1795=0x703(SteeringCmd, Display->ECU)
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

/*
 * @func: Send the consolidated steering command 0x703 (DBC BO_1795) to the ECU.
 * 0x700/0x701/0x702 are SteeringWheel->Display only and must NOT be forwarded
 * verbatim; the ECU consumes only this merged frame.
 *   byte0      = SlipLevel 0..7
 *   byte1 bit0 = RecorderToggle (one-shot)
 *   byte1 bit1 = ErrorClearActive (one-shot)
 */
static void User_CAN_SendSteeringCmd(uint8_t slip_level,
                                     uint8_t recorder_toggle,
                                     uint8_t error_clear)
{
  uint8_t cmd[2] = {0};

  cmd[0] = slip_level & 0x07U;
  if(recorder_toggle != 0U) cmd[1] |= 0x01U;
  if(error_clear != 0U)     cmd[1] |= 0x02U;

  User_CAN_SendDlc(0x703U, cmd, 2U);
}

static void User_CAN_HandleSteeringWheel(const CAN_RxHeaderTypeDef * rx,
                                         const uint8_t data[8])
{
  static uint8_t s_slip_level = 0U;

  if((rx == NULL) || (data == NULL)) return;

  switch(rx->StdId)
  {
    /* Vehicle_CanB.dbc BO_784 DriveMode_Request 0x310: DriveModeCode byte0 = ASCII '0'..'3' (48..51).
     * Drive mode is relayed verbatim to the ECU, except while a sprint/lap
     * timing session holds the mode lock: then the request is not relayed and
     * Dashboard_UI_SubmitDriveMode() raises the MODE LOCK alert instead. */
    case 0x310:
    {
      int32_t mode;
      if(Dashboard_UI_IsTimingModeLocked() == 0U) {
        User_CAN_SendDlc(rx->StdId, data, rx->DLC);
      }
      if(rx->DLC < 1U) break;
      mode = (int32_t)data[0] - 48;
      if(mode < 0) mode = 0;
      if(mode > 3) mode = 3;
      Dashboard_UI_SubmitDriveMode(mode);
      break;
    }

    /* Vehicle_CanB.dbc BO_1792 SlipLevel 0x700: byte0 [0..7] -> merge into 0x703 */
    case 0x700:
    {
      if((rx->DLC >= 1U) && (data[0] <= 7U)) {
        s_slip_level = data[0];
        Dashboard_UI_SubmitSlipLevel((int32_t)data[0]);
      }
      User_CAN_SendSteeringCmd(s_slip_level, 0U, 0U);
      break;
    }

    /* Vehicle_CanB.dbc BO_1793 RecorderToggle 0x701: bit0, one-shot -> merge into 0x703 */
    case 0x701:
    {
      uint8_t toggle = 0U;
      if((rx->DLC >= 1U) && ((data[0] & 0x01U) != 0U)) {
        Dashboard_UI_RequestLapToggle();
        toggle = 1U;
      }
      User_CAN_SendSteeringCmd(s_slip_level, toggle, 0U);
      break;
    }

    /* Vehicle_CanB.dbc BO_1794 ErrorClear_0x702: bit0, one-shot -> merge into 0x703 */
    case 0x702:
    {
      uint8_t clear = 0U;
      if((rx->DLC >= 1U) && ((data[0] & 0x01U) != 0U)) {
        Dashboard_UI_RequestAlertClear();
        clear = 1U;
      }
      User_CAN_SendSteeringCmd(s_slip_level, 0U, clear);
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
    case 0x305: min_dlc = 6U; break;
    case 0x509: min_dlc = 5U; break;
    default: break;
  }
  if(frame->dlc < min_dlc) { g_can_rx_invalid_count++; return; }
  for(uint32_t i = 0U; i < 8U; i++) CAN_RxData[i] = frame->data[i];

	switch(RxHeader.StdId)
	{
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

	  /* DBC BO_1200 BMS_PackStatus 0x4B0, DLC=7:
	   *   BatteryVoltage(byte0-1,BE,0.1V), BatteryCurrent(byte2-3,BE,int16,0.1A),
	   *   BatterySOC(byte4,%), validity(byte5), AlarmLevel/State(byte6).
	   * The on-car BMS often sends the validity byte without all bits set, so
	   * the display takes the data bytes unconditionally and ignores byte5. */
	  case 0x4B0:
	  {
	    g_can_dashboard_data.sum_voltage = (int32_t)((((uint16_t)CAN_RxData[0] << 8) | (uint16_t)CAN_RxData[1]) / 10);
	    int16_t curr_raw = (int16_t)(((uint16_t)CAN_RxData[2] << 8) | (uint16_t)CAN_RxData[3]);
	    g_can_dashboard_data.sum_current = (int32_t)(curr_raw / 10);
	    g_can_dashboard_data.soc = (int32_t)CAN_RxData[4];
	    break;
	  }

	  /* DBC BO_773 DataLogger 0x305: SteeringWheelAngle(0|16@1-), APS_OpenPct(16|16@1+), OilPressure_Kpa(32|16@1+) */
	  case 0x305:
	  {
	    /* SG_ SteeringWheelAngle: 0|16@1- (0.1,0) deg, raw=int16*0.1 */
	    g_can_dashboard_data.steering_angle = (int32_t)(int16_t)(CAN_RxData[0] | ((uint16_t)CAN_RxData[1] << 8));
	    /* SG_ APS_OpenPct: 16|16@1+ (0.1,0) %, raw=uint16*0.1, store integer pct */
	    {
	      uint16_t aps_raw = (uint16_t)CAN_RxData[2] | ((uint16_t)CAN_RxData[3] << 8);
	      g_can_dashboard_data.aps_open_pct = (int32_t)(aps_raw / 10U);
	    }
	    /* SG_ OilPressure_Kpa: 32|16@1+ (0.001,0) Kpa, raw count */
	    {
	      uint16_t oil_raw = (uint16_t)CAN_RxData[4] | ((uint16_t)CAN_RxData[5] << 8);
	      g_can_dashboard_data.oil_pressure = (int32_t)oil_raw;
	    }
	    break;
	  }

	  /* DBC BO_774 BrakePedal_0x306: ECU gateway forward of CANC 0x270 IBS_Info
	   * (WCBS). SG_ BrakePedalPct: 0|16@1+ (0.1,0) %, store integer pct;
	   * SG_ BrakePedalSwtSts: 16|1@1+ (1=pressed). */
	  case 0x306:
	  {
	    uint16_t brake_raw = (uint16_t)CAN_RxData[0] | ((uint16_t)CAN_RxData[1] << 8);
	    g_can_dashboard_data.brake_pct = (int32_t)(brake_raw / 10U);
	    break;
	  }

	  /* DBC BO_1282 Debug2 0x502: FR/FL/RR/RL_ActualTorque (1,0) Nm (DBC 单位文字误写为 0.1%Mn), 16-bit signed each */
	  case 0x502:
	  {
	    /* DBC order: RL(0), RR(1), FL(2), FR(3) -> Dashboard: LF(0), LR(1), RF(2), RR(3) */
	    static const uint8_t dbc_map[4] = {1U, 3U, 0U, 2U};
	    uint32_t i;
	    for(i = 0U; i < 4U; i++) {
	      int16_t raw = (int16_t)(CAN_RxData[i * 2U] | ((uint16_t)CAN_RxData[i * 2U + 1U] << 8));
	      g_can_dashboard_data.torque[dbc_map[i]] = (int32_t)raw;
	    }
	    break;
	  }

	  /* DBC BO_1285 Debug5 0x505: FR/FL/RR/RL_ActualVelocity (1,0), 16-bit signed each */
	  case 0x505:
	  {
	    static const uint8_t dbc_map[4] = {1U, 3U, 0U, 2U};
	    uint32_t i;
	    for(i = 0U; i < 4U; i++) {
	      int16_t raw = (int16_t)(CAN_RxData[i * 2U] | ((uint16_t)CAN_RxData[i * 2U + 1U] << 8));
	      g_can_dashboard_data.rpm[dbc_map[i]] = (int32_t)raw;
	    }
	    break;
	  }

	  /* DBC BO_1286 Debug6 0x506: FR/FL/RR/RL_Motor_temperature (0.1,0) degC, 16-bit signed each */
	  case 0x506:
	  {
	    static const uint8_t dbc_map[4] = {1U, 3U, 0U, 2U};
	    uint32_t i;
	    for(i = 0U; i < 4U; i++) {
	      int16_t raw = (int16_t)(CAN_RxData[i * 2U] | ((uint16_t)CAN_RxData[i * 2U + 1U] << 8));
	      g_can_dashboard_data.motor_temp[dbc_map[i]] = (int32_t)(raw / 10);
	    }
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

	    /* SG_ ModeFlag: 4|4@1- (1,0) [-8,7], signed 4-bit in byte0 high nibble */
	    {
	      int32_t mode_val = (int32_t)((CAN_RxData[0] >> 4) & 0x0FU);
	      if(mode_val & 8) mode_val -= 16;
	      g_can_dashboard_data.mode_index = mode_val;
	      Dashboard_UI_SubmitDriveMode(mode_val);
	    }
	    break;
	  }

	  /* DBC BO_1288 Debug8 0x508: FR/FL/RR/RL_IGBT_temperature (0.1,0) degC, 16-bit signed each */
	  case 0x508:
	  {
	    static const uint8_t dbc_map[4] = {1U, 3U, 0U, 2U};
	    uint32_t i;
	    for(i = 0U; i < 4U; i++) {
	      int16_t raw = (int16_t)(CAN_RxData[i * 2U] | ((uint16_t)CAN_RxData[i * 2U + 1U] << 8));
	      g_can_dashboard_data.igbt_temp[dbc_map[i]] = (int32_t)(raw / 10);
	    }
	    break;
	  }

	  /* DBC BO_1287 Debug7 0x507: FR/FL/RR/RL_Inverter_temperature (0.1,0) degC, 16-bit signed each */
	  case 0x507:
	  {
	    static const uint8_t dbc_map[4] = {1U, 3U, 0U, 2U};
	    uint32_t i;
	    for(i = 0U; i < 4U; i++) {
	      int16_t raw = (int16_t)(CAN_RxData[i * 2U] | ((uint16_t)CAN_RxData[i * 2U + 1U] << 8));
	      g_can_dashboard_data.inverter_temp[dbc_map[i]] = (int32_t)(raw / 10);
	    }
	    break;
	  }

	  /* DBC BO_1284 Debug4 0x504: Diagnostic_number_3(0|32), Diagnostic_number_4(32|32), 32-bit each */
	  case 0x504:
	  {
	    g_can_dashboard_data.diag_num[2] = (uint32_t)CAN_RxData[0]
	      | ((uint32_t)CAN_RxData[1] << 8)
	      | ((uint32_t)CAN_RxData[2] << 16)
	      | ((uint32_t)CAN_RxData[3] << 24);
	    g_can_dashboard_data.diag_num[3] = (uint32_t)CAN_RxData[4]
	      | ((uint32_t)CAN_RxData[5] << 8)
	      | ((uint32_t)CAN_RxData[6] << 16)
	      | ((uint32_t)CAN_RxData[7] << 24);
	    break;
	  }

	  /* DBC BO_1283 Debug3 0x503: Diagnostic_number_1(0|32), Diagnostic_number_2(32|32), 32-bit each */
	  case 0x503:
	  {
	    g_can_dashboard_data.diag_num[0] = (uint32_t)CAN_RxData[0]
	      | ((uint32_t)CAN_RxData[1] << 8)
	      | ((uint32_t)CAN_RxData[2] << 16)
	      | ((uint32_t)CAN_RxData[3] << 24);
	    g_can_dashboard_data.diag_num[1] = (uint32_t)CAN_RxData[4]
	      | ((uint32_t)CAN_RxData[5] << 8)
	      | ((uint32_t)CAN_RxData[6] << 16)
	      | ((uint32_t)CAN_RxData[7] << 24);
	    break;
	  }

	  /* DBC BO_1440 PDM_LowVoltageBus 0x5A0: BusVoltage(7|16@0- 0.001V),
	   * BusCurrent(23|16@0- 0.01A), BusPower(39|16@0+ 0.1W), BusEnergy(55|16@0+).
	   * Stored in raw engineering units: mV / cA / dW. */
	  case 0x5A0:
	  {
	    g_can_dashboard_data.lv_bus_voltage_mV =
	      (int32_t)(int16_t)(((uint16_t)CAN_RxData[0] << 8) | (uint16_t)CAN_RxData[1]);
	    g_can_dashboard_data.lv_bus_current_cA =
	      (int32_t)(int16_t)(((uint16_t)CAN_RxData[2] << 8) | (uint16_t)CAN_RxData[3]);
	    g_can_dashboard_data.lv_bus_power_dW =
	      (int32_t)(((uint16_t)CAN_RxData[4] << 8) | (uint16_t)CAN_RxData[5]);
	    break;
	  }

	  /* DBC BO_1441 PDM_LowVoltageBattery 0x5A1: battery-side mirror of 0x5A0.
	   * UI shows the bus values; battery values are kept for diagnostics. */
	  case 0x5A1:
	  {
	    g_can_dashboard_data.lv_batt_voltage_mV =
	      (int32_t)(int16_t)(((uint16_t)CAN_RxData[0] << 8) | (uint16_t)CAN_RxData[1]);
	    g_can_dashboard_data.lv_batt_current_cA =
	      (int32_t)(int16_t)(((uint16_t)CAN_RxData[2] << 8) | (uint16_t)CAN_RxData[3]);
	    g_can_dashboard_data.lv_batt_power_dW =
	      (int32_t)(((uint16_t)CAN_RxData[4] << 8) | (uint16_t)CAN_RxData[5]);
	    break;
	  }

	  /* DBC BO_1442 FanController_Status 0x5A2, 100ms: Fan1/2/3_RPM (7|16@0+, 1rpm),
	   * Fan_PWM1_Duty(55|8 %), Fan_PWM2_Duty(63|8 %). The DBC defines only two
	   * measured duty cycles; fan 3 has no independent duty signal. */
	  case 0x5A2:
	  {
	    uint32_t fan;
	    for(fan = 0U; fan < 3U; fan++) {
	      g_can_dashboard_data.fan_rpm[fan] = (int32_t)(
	        ((uint16_t)CAN_RxData[fan * 2U] << 8) |
	        (uint16_t)CAN_RxData[fan * 2U + 1U]);
	    }
	    g_can_dashboard_data.fan_pwm_duty[0] = (int32_t)CAN_RxData[6];
	    g_can_dashboard_data.fan_pwm_duty[1] = (int32_t)CAN_RxData[7];
	    break;
	  }

	  default:
	    break;
    }

    /* MAX T: highest of motor/inverter/IGBT temperature across the four wheels.
     * Stays 0 until the first Debug6/7/8 frame arrives. */
    {
      int32_t max_temp = 0;
      uint32_t wheel;
      for(wheel = 0U; wheel < 4U; wheel++) {
        if(g_can_dashboard_data.motor_temp[wheel] > max_temp) {
          max_temp = g_can_dashboard_data.motor_temp[wheel];
        }
        if(g_can_dashboard_data.inverter_temp[wheel] > max_temp) {
          max_temp = g_can_dashboard_data.inverter_temp[wheel];
        }
        if(g_can_dashboard_data.igbt_temp[wheel] > max_temp) {
          max_temp = g_can_dashboard_data.igbt_temp[wheel];
        }
      }
      g_can_dashboard_data.max_temperature = max_temp;
    }

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
 * 0x067: latitude/longitude; 0x068: motion/heading; 0x069: status/odometer;
 * 0x06A: lap timing. Tire inputs use 0x071..0x074 and are not used for GPS TX.
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

/*
 * @func: CAN FIFO1 callback - IMU raw data relay/parse
 * DBC BO_80 IMU_Raw 0x50: IMU_Header(0|8), IMU_SubType(8|8), IMU_RawData(16|48)
 * SubType dispatches to DBC parsed messages:
 *   0x50=Time(BO_96), 0x51=Accel(BO_97), 0x52=Gyro(BO_98),
 *   0x53=Angle(BO_99~101 SubCmd 1/2/3), 0x54=Magnetic(BO_102)
 */
void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  User_CAN_ReceiveIRQ(hcan, CAN_RX_FIFO1);
}

static void User_CAN_ProcessFifo1(const User_CAN_RxItem_t *frame)
{
  if(frame->bus != 1U || frame->dlc != 8U) { g_can_rx_invalid_count++; return; }
  RxHeader.StdId = frame->id;
  RxHeader.DLC = frame->dlc;
  for(uint32_t i = 0U; i < 8U; i++) CAN_RxData[i] = frame->data[i];

	/* DBC: IMU_Header must be 0x55 */
	if(RxHeader.StdId != 0x50U) return;
	if(CAN_RxData[0] != 0x55U) return;

	switch(CAN_RxData[1])
	{
	  case 0x50U:  /* DBC IMU_SubType=0x50: Time -> relay 0x60 BO_96 */
	    User_CAN_Send_sq(0x60, CAN_RxData);
	    break;
	  case 0x51U:  /* DBC IMU_SubType=0x51: Accel -> relay 0x61 BO_97 */
	  {
	    User_CAN_Send_sq(0x61, CAN_RxData);
	    /* DBC BO_97: IMU_AccelX(0|16), IMU_AccelY(16|16), IMU_AccelZ(32|16), raw int16 */
	    g_can_dashboard_data.imu_accel[0] = (int32_t)(int16_t)(CAN_RxData[2] | ((uint16_t)CAN_RxData[3] << 8));
	    g_can_dashboard_data.imu_accel[1] = (int32_t)(int16_t)(CAN_RxData[4] | ((uint16_t)CAN_RxData[5] << 8));
	    g_can_dashboard_data.imu_accel[2] = (int32_t)(int16_t)(CAN_RxData[6] | ((uint16_t)CAN_RxData[7] << 8));
	    break;
	  }
	  case 0x52U:  /* DBC IMU_SubType=0x52: Gyro -> relay 0x62 BO_98 */
	  {
	    User_CAN_Send_sq(0x62, CAN_RxData);
	    /* DBC BO_98: IMU_GyroX(0|16), IMU_GyroY(16|16), IMU_GyroZ(32|16), raw int16 */
	    g_can_dashboard_data.imu_gyro[0] = (int32_t)(int16_t)(CAN_RxData[2] | ((uint16_t)CAN_RxData[3] << 8));
	    g_can_dashboard_data.imu_gyro[1] = (int32_t)(int16_t)(CAN_RxData[4] | ((uint16_t)CAN_RxData[5] << 8));
	    g_can_dashboard_data.imu_gyro[2] = (int32_t)(int16_t)(CAN_RxData[6] | ((uint16_t)CAN_RxData[7] << 8));
	    break;
	  }
	  case 0x53U:  /* DBC IMU_SubType=0x53: Angle(SubCmd) -> relay 0x63/0x64/0x65 BO_99/100/101 */
	  {
	    if(CAN_RxData[2] == 0x01U)
	    {
	      User_CAN_Send_sq(0x63, CAN_RxData);
	      /* DBC BO_99 IMU_Roll: IMU_Angle_Roll(0|16), TODO: verify byte offset with IMU firmware */
	      g_can_dashboard_data.imu_roll = (int32_t)(int16_t)(CAN_RxData[2] | ((uint16_t)CAN_RxData[3] << 8));
	    }
	    else if(CAN_RxData[2] == 0x02U)
	    {
	      User_CAN_Send_sq(0x64, CAN_RxData);
	      /* DBC BO_100 IMU_Pitch: IMU_Angle_Pitch(0|16), TODO: verify byte offset with IMU firmware */
	      g_can_dashboard_data.imu_pitch = (int32_t)(int16_t)(CAN_RxData[2] | ((uint16_t)CAN_RxData[3] << 8));
	    }
	    else if(CAN_RxData[2] == 0x03U)
	    {
	      User_CAN_Send_sq(0x65, CAN_RxData);
	      /* DBC BO_101 IMU_Yaw: IMU_Angle_Yaw(0|16), TODO: verify byte offset with IMU firmware */
	      g_can_dashboard_data.imu_yaw = (int32_t)(int16_t)(CAN_RxData[2] | ((uint16_t)CAN_RxData[3] << 8));
	    }
	    break;
	  }
	  case 0x54U:  /* DBC IMU_SubType=0x54: Magnetic -> relay 0x66 BO_102 */
	  {
	    User_CAN_Send_sq(0x66, CAN_RxData);
	    /* DBC BO_102: IMU_MagX(0|16), IMU_MagY(16|16), IMU_MagZ(32|16), raw int16 */
	    g_can_dashboard_data.imu_mag[0] = (int32_t)(int16_t)(CAN_RxData[2] | ((uint16_t)CAN_RxData[3] << 8));
	    g_can_dashboard_data.imu_mag[1] = (int32_t)(int16_t)(CAN_RxData[4] | ((uint16_t)CAN_RxData[5] << 8));
	    g_can_dashboard_data.imu_mag[2] = (int32_t)(int16_t)(CAN_RxData[6] | ((uint16_t)CAN_RxData[7] << 8));
	    break;
	  }
	  default:
	    break;
	}
}

/* USER CODE END 1 */
