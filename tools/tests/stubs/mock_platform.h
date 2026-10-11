/* USER CODE BEGIN Host_Test_Platform */
#ifndef MOCK_PLATFORM_H
#define MOCK_PLATFORM_H
#define __MAIN_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t, StackType_t;
typedef struct {int unused;} StaticTask_t;
typedef void *TaskHandle_t;
typedef struct {unsigned held;} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
typedef struct {uint8_t *data; unsigned capacity,size,head,tail,count;} StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define tskIDLE_PRIORITY 0
#define pdMS_TO_TICKS(x) (x)
#define configASSERT(x) assert(x)
extern uint32_t test_tick, test_primask, test_dma_ndtr;
extern unsigned test_mutex_depth;
static inline uint32_t __get_PRIMASK(void) {return test_primask;}
static inline void __disable_irq(void) {test_primask=1;}
static inline void __enable_irq(void) {test_primask=0;}
static inline void __set_PRIMASK(uint32_t value) {test_primask=value;}
#define taskENTER_CRITICAL() __disable_irq()
#define taskEXIT_CRITICAL() __enable_irq()
#define portYIELD_FROM_ISR(x) ((void)(x))
static inline void vTaskDelay(uint32_t ticks) {test_tick+=ticks;}
static inline void vTaskDelayUntil(TickType_t *last,uint32_t ticks) {*last+=ticks;test_tick=*last;}
static inline TickType_t xTaskGetTickCount(void) {return test_tick;}
static inline TaskHandle_t xTaskCreateStatic(void (*fn)(void *),const char *name,uint32_t size,void *arg,UBaseType_t priority,StackType_t *stack,StaticTask_t *cb) {(void)fn;(void)name;(void)size;(void)arg;(void)priority;(void)stack;return cb;}
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *cb) {cb->held=0;return cb;}
#define xSemaphoreCreateRecursiveMutexStatic xSemaphoreCreateMutexStatic
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t cb,uint32_t timeout) {(void)timeout;assert(cb);cb->held++;test_mutex_depth++;return pdTRUE;}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t cb) {assert(cb&&cb->held);cb->held--;test_mutex_depth--;return pdTRUE;}
#define xSemaphoreTakeRecursive xSemaphoreTake
#define xSemaphoreGiveRecursive xSemaphoreGive
static inline QueueHandle_t xQueueCreateStatic(unsigned count,unsigned size,uint8_t *storage,StaticQueue_t *cb) {*cb=(StaticQueue_t){storage,count,size,0,0,0};return cb;}
static inline BaseType_t xQueueSendFromISR(QueueHandle_t q,const void *item,BaseType_t *wake) {(void)wake;if(q->count==q->capacity)return pdFALSE;memcpy(q->data+q->tail*q->size,item,q->size);q->tail=(q->tail+1)%q->capacity;q->count++;return pdPASS;}
static inline BaseType_t xQueueReceive(QueueHandle_t q,void *item,uint32_t timeout) {(void)timeout;if(!q->count)return pdFALSE;memcpy(item,q->data+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;q->count--;return pdPASS;}
static inline UBaseType_t uxQueueMessagesWaitingFromISR(QueueHandle_t q) {return q->count;}
#define osPriorityNormal 24

typedef struct {void *Instance;} CAN_HandleTypeDef;
typedef struct {uint32_t StdId,ExtId,IDE,RTR,DLC,Timestamp,FilterMatchIndex;} CAN_RxHeaderTypeDef;
typedef struct {uint32_t StdId,ExtId,IDE,RTR,DLC,TransmitGlobalTime;} CAN_TxHeaderTypeDef;
typedef struct {uint32_t FilterActivation,FilterBank,FilterFIFOAssignment,FilterIdHigh,FilterIdLow,FilterMaskIdHigh,FilterMaskIdLow,FilterMode,FilterScale,SlaveStartFilterBank;} CAN_FilterTypeDef;
#define CAN1 ((void *)1)
#define CAN2 ((void *)2)
#define HAL_OK 0
#define HAL_ERROR 1
#define CAN_ID_STD 0
#define CAN_RTR_DATA 0
#define CAN_RX_FIFO0 0
#define CAN_RX_FIFO1 1
#define CAN_FILTER_FIFO0 0
#define CAN_FILTER_FIFO1 1
#define CAN_FILTERMODE_IDLIST 1
#define CAN_FILTERSCALE_16BIT 1
#define ENABLE 1
#define DISABLE 0
int HAL_CAN_ConfigFilter(CAN_HandleTypeDef *,CAN_FilterTypeDef *);
#define HAL_CAN_ERROR_BOF 0x00000004U
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *,uint32_t);
int HAL_CAN_GetRxMessage(CAN_HandleTypeDef *,uint32_t,CAN_RxHeaderTypeDef *,uint8_t *);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *);
int HAL_CAN_AddTxMessage(CAN_HandleTypeDef *,CAN_TxHeaderTypeDef *,uint8_t *,uint32_t *);
void Error_Handler(void);
typedef struct {int unused;} UART_HandleTypeDef, DMA_HandleTypeDef;
#define __HAL_DMA_GET_COUNTER(handle) (test_dma_ndtr)
int HAL_UART_Transmit(UART_HandleTypeDef *,uint8_t *,uint16_t,uint32_t);
static inline uint32_t HAL_GetTick(void) {return test_tick;}
#define LED_RED_GPIO_Port ((void *)1)
#define LED_GREEN_GPIO_Port ((void *)1)
#define LED_RED_Pin 1
#define LED_GREEN_Pin 2
#define GPIOF ((void *)3)
#define GPIO_PIN_9 512
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
#define GPIO_MODE_OUTPUT_PP 1
#define GPIO_MODE_AF_PP 2
#define GPIO_NOPULL 0
#define GPIO_SPEED_FREQ_LOW 0
#define GPIO_AF9_TIM14 9
#define TIM14 ((void *)14)
#define TIM_COUNTERMODE_UP 0
#define TIM_CLOCKDIVISION_DIV1 0
#define TIM_OCMODE_PWM1 1
#define TIM_OCPOLARITY_HIGH 1
#define TIM_OCFAST_DISABLE 0
#define TIM_CHANNEL_1 1
#define __HAL_RCC_TIM14_CLK_ENABLE() ((void)0)
#define __HAL_TIM_SET_COMPARE(a,b,c) ((void)(a),(void)(b),(void)(c))
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
typedef struct {void *Instance;struct {uint32_t Prescaler,CounterMode,Period,ClockDivision;} Init;} TIM_HandleTypeDef;
typedef struct {uint32_t OCMode,Pulse,OCPolarity,OCFastMode;} TIM_OC_InitTypeDef;
static inline void HAL_GPIO_Init(void *port,GPIO_InitTypeDef *config) {(void)port;(void)config;}
static inline void HAL_GPIO_WritePin(void *port,uint32_t pin,int value) {(void)port;(void)pin;(void)value;}
static inline int HAL_TIM_PWM_Init(TIM_HandleTypeDef *h) {(void)h;return HAL_OK;}
static inline int HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *h,TIM_OC_InitTypeDef *c,uint32_t ch) {(void)h;(void)c;(void)ch;return HAL_OK;}
static inline int HAL_TIM_PWM_Start(TIM_HandleTypeDef *h,uint32_t ch) {(void)h;(void)ch;return HAL_OK;}

typedef int HAL_StatusTypeDef;
typedef struct {uint32_t TypeErase,Banks,Sector,NbSectors,VoltageRange;} FLASH_EraseInitTypeDef;
#define FLASH_TYPEPROGRAM_WORD 2U
#define FLASH_TYPEERASE_SECTORS 0U
#define FLASH_VOLTAGE_RANGE_3 2U
#define FLASH_SECTOR_7 7U
#define FLASH_FLAG_EOP 0x01U
#define FLASH_FLAG_OPERR 0x02U
#define FLASH_FLAG_WRPERR 0x10U
#define FLASH_FLAG_PGAERR 0x20U
#define FLASH_FLAG_PGPERR 0x40U
#define FLASH_FLAG_PGSERR 0x80U
#define __HAL_FLASH_CLEAR_FLAG(flags) ((void)(flags))
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t,uint32_t,uint64_t);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *,uint32_t *);
#endif
/* USER CODE END Host_Test_Platform */
