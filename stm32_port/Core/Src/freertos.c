/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cmsis_os2.h"
#include "can.h"
#include "dashboard_ui.h"
#include "../lvgl/lvgl.h"
#include "touch.h"
#include "gps.h"
#include "sd_log.h"
#include "firmware_diag.h"

extern void LVGL_Port_Init(void);
extern volatile uint32_t g_lvgl_flush_count;
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

#define DASHBOARD_TASK_STACK_WORDS 3072U

static StaticTask_t g_dashboard_task_control;
static StackType_t g_dashboard_task_stack[DASHBOARD_TASK_STACK_WORDS];
static TaskHandle_t g_dashboard_task_handle;

volatile uint32_t g_freertos_fault_code;
volatile const char * g_freertos_fault_task_name;

static StaticTask_t g_touch_task_control;
static StackType_t g_touch_task_stack[1024U];
osThreadId_t touchTaskHandle;
const osThreadAttr_t touchTask_attributes = {
  .name = "touchTask",
  .cb_mem = &g_touch_task_control,
  .cb_size = sizeof(g_touch_task_control),
  .stack_mem = g_touch_task_stack,
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

static void Dashboard_ServiceTask(void * argument);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* creation of touchTask */
  configASSERT(defaultTaskHandle != NULL);
  touchTaskHandle = osThreadNew(Touch_ServiceTask, NULL, &touchTask_attributes);
  Firmware_Diag_RegisterTask(FW_TASK_TOUCH, touchTaskHandle, "Touch", sizeof(g_touch_task_stack));
  CAN_StartServiceTask();
  GPS_Init();
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;

  /* All LVGL operations, including initialization and the first frame,
   * run on a monitored 12 KiB task stack rather than the boot MSP. */
  g_dashboard_task_handle = xTaskCreateStatic(Dashboard_ServiceTask,
                                               "Dashboard",
                                               DASHBOARD_TASK_STACK_WORDS,
                                               NULL,
                                               (UBaseType_t)osPriorityNormal,
                                               g_dashboard_task_stack,
                                               &g_dashboard_task_control);
  if(g_dashboard_task_handle == NULL) {
    taskDISABLE_INTERRUPTS();
    Firmware_Diag_RecordFault(4U);
    g_freertos_fault_code = 4U;
    g_freertos_fault_task_name = "Dashboard";
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
    for(;;) { }
  }

  Firmware_Diag_RegisterTask(FW_TASK_DASHBOARD, g_dashboard_task_handle,
                            "Dashboard", sizeof(g_dashboard_task_stack));

  vTaskDelete(NULL);
  /* Execution must not continue after deleting the bootstrap task. */
  for(;;) { }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

static void Dashboard_ServiceTask(void * argument)
{
  uint32_t delay_ms;
  uint32_t last_heartbeat_tick = 0U;
  uint32_t loop_start_cycles;
  uint32_t last_can_heartbeat_tick = 0U;

  (void)argument;
  LVGL_Port_Init();

  for(;;)
  {
    loop_start_cycles = DWT->CYCCNT;
    Dashboard_UI_Process();
    if(Dashboard_UI_IsStartupComplete() != 0U) {
      Touch_Process();
    }
    delay_ms = lv_timer_handler();

    if((Dashboard_UI_IsStartupComplete() != 0U) &&
       ((HAL_GetTick() - last_heartbeat_tick) >= 250U)) {
      last_heartbeat_tick = HAL_GetTick();
      HAL_GPIO_TogglePin(GPIOD, LED_GREEN_Pin);
    }

    if((Dashboard_UI_IsStartupComplete() != 0U) &&
       ((HAL_GetTick() - last_can_heartbeat_tick) >= 500U)) {
      last_can_heartbeat_tick = HAL_GetTick();
      CAN1_SendHeartbeat();
    }

    uint32_t loop_cycles = DWT->CYCCNT - loop_start_cycles;
    if(loop_cycles > g_firmware_diag.display_loop_max_cycles) {
      g_firmware_diag.display_loop_max_cycles = loop_cycles;
    }
    Firmware_Diag_Sample();

    if(delay_ms < 5U) {
      delay_ms = 5U;
    }
    if(delay_ms > 20U) {
      delay_ms = 20U;
    }
    if((Dashboard_UI_IsStartupComplete() == 0U) && (delay_ms > 16U)) {
      delay_ms = 16U;
    }

    osDelay(delay_ms);
  }
}

void Fault_Diagnostic_Assert(void)
{
  Firmware_Diag_RecordFault(1U);
  g_freertos_fault_code = 1U;
  g_freertos_fault_task_name = "configASSERT";
  /* Both LEDs solid identifies a FreeRTOS configASSERT.  CPU fault handlers
   * use red solid / green off instead. */
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char * task_name)
{
  (void)task;
  taskDISABLE_INTERRUPTS();
  Firmware_Diag_RecordFault(2U);
  g_freertos_fault_code = 2U;
  g_freertos_fault_task_name = task_name;
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
  for(;;) { }
}

void vApplicationMallocFailedHook(void)
{
  taskDISABLE_INTERRUPTS();
  Firmware_Diag_RecordFault(3U);
  g_freertos_fault_code = 3U;
  g_freertos_fault_task_name = "FreeRTOS heap";
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
  for(;;) { }
}

/* USER CODE END Application */

