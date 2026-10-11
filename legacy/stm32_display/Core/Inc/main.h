/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED1_Pin GPIO_PIN_4
#define LED1_GPIO_Port GPIOA
#define LED2_Pin GPIO_PIN_5
#define LED2_GPIO_Port GPIOA
#define key1_Pin GPIO_PIN_6
#define key1_GPIO_Port GPIOA
#define key1_EXTI_IRQn EXTI9_5_IRQn
#define key2_Pin GPIO_PIN_7
#define key2_GPIO_Port GPIOA
#define key2_EXTI_IRQn EXTI9_5_IRQn
#define key3_Pin GPIO_PIN_0
#define key3_GPIO_Port GPIOB
#define key3_EXTI_IRQn EXTI0_IRQn
#define key4_Pin GPIO_PIN_1
#define key4_GPIO_Port GPIOB
#define key4_EXTI_IRQn EXTI1_IRQn
#define Wireless_TX_Pin GPIO_PIN_9
#define Wireless_TX_GPIO_Port GPIOA
#define Wireless_RX_Pin GPIO_PIN_10
#define Wireless_RX_GPIO_Port GPIOA
#define AUX_Pin GPIO_PIN_5
#define AUX_GPIO_Port GPIOB
#define AUX_EXTI_IRQn EXTI9_5_IRQn
#define MOD_Pin GPIO_PIN_7
#define MOD_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define g 9.8
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
