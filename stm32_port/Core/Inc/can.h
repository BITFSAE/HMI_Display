/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.h
  * @brief   This file contains all the function prototypes for
  *          the can.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H__
#define __CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern CAN_HandleTypeDef hcan1;

extern CAN_HandleTypeDef hcan2;

/* USER CODE BEGIN Private defines */

typedef struct {
  int32_t latitude_e7;
  int32_t longitude_e7;
  uint16_t ground_track_cdeg;
  uint16_t heading_cdeg;
  int16_t altitude_dm;
  uint8_t status_flags;
  uint8_t lap_diag_state;
  uint8_t heading_quality;
  uint32_t odometer_tenths_km;
  uint8_t fix_quality;
  uint8_t satellites;
  uint8_t signal_level;
  int8_t max_snr;
  int8_t avg_snr;
  uint8_t gsv_tracked_sats;
  uint8_t lap_count;
  uint16_t lap_current_cs;
  uint16_t lap_last_cs;
  uint16_t lap_best_cs;
  int16_t lap_delta_cs;
} CAN_GPSTelemetry_t;

/* Frame groups sent by CAN_SendGPSTelemetry(); the caller picks the due ones. */
#define CAN_GPS_TX_POSITION (1U << 0)  /* 0x067 */
#define CAN_GPS_TX_MOTION   (1U << 1)  /* 0x068 */
#define CAN_GPS_TX_STATUS   (1U << 2)  /* 0x069: odometer, GPS quality, lap count */
#define CAN_GPS_TX_LAP      (1U << 3)  /* 0x06A: lap timing */

/* USER CODE END Private defines */

void MX_CAN1_Init(void);
void MX_CAN2_Init(void);

/* USER CODE BEGIN Prototypes */
void CAN1_Filter_Config(void);
void CAN2_Filter_Config(void);
void User_CAN_Send(void);
void User_CAN_Send_sq(uint32_t CAN_ID_NEW,uint8_t* CAN_TxData_NEW);
void CAN1_SendHeartbeat(void);
void CAN_ServiceTask(void *argument);
void CAN_StartServiceTask(void);
void CAN_SendGPSSpeed(int32_t speed_kmh_tenths);
void CAN_SendGPSTelemetry(const CAN_GPSTelemetry_t * telemetry, uint32_t frames);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __CAN_H__ */

