/* USER CODE BEGIN Firmware_Diagnostics */
#include "firmware_diag.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "../lvgl/lvgl.h"

volatile firmware_diag_t g_firmware_diag;
static TaskHandle_t g_diag_task_handles[FW_DIAG_TASK_COUNT];

void Firmware_Diag_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void Firmware_Diag_RegisterTask(firmware_task_id_t id, void *handle,
                               const char *name, uint32_t stack_size_bytes)
{
    configASSERT((uint32_t)id < FW_DIAG_TASK_COUNT);
    configASSERT(handle != NULL);
    taskENTER_CRITICAL();
    g_diag_task_handles[id] = (TaskHandle_t)handle;
    g_firmware_diag.tasks[id].name = name;
    g_firmware_diag.tasks[id].stack_size_bytes = stack_size_bytes;
    g_firmware_diag.tasks[id].stack_free_min_bytes = stack_size_bytes;
    g_firmware_diag.tasks[id].active = 1U;
    taskEXIT_CRITICAL();
}

void Firmware_Diag_UnregisterTask(firmware_task_id_t id)
{
    taskENTER_CRITICAL();
    g_diag_task_handles[id] = NULL;
    g_firmware_diag.tasks[id].active = 0U;
    taskEXIT_CRITICAL();
}

void Firmware_Diag_Sample(void)
{
    lv_mem_monitor_t memory;
    uint32_t now = HAL_GetTick();

    if((now - g_firmware_diag.sample_tick) < 1000U) return;
    if(g_diag_task_handles[FW_TASK_IDLE] == NULL) {
        Firmware_Diag_RegisterTask(FW_TASK_IDLE, xTaskGetIdleTaskHandle(),
                                  "IDLE", configMINIMAL_STACK_SIZE * sizeof(StackType_t));
        Firmware_Diag_RegisterTask(FW_TASK_TIMER, xTimerGetTimerDaemonTaskHandle(),
                                  "Tmr Svc", configTIMER_TASK_STACK_DEPTH * sizeof(StackType_t));
    }
    for(uint32_t id = 0U; id < FW_DIAG_TASK_COUNT; id++) {
        /* Prevent deletion/unregistration halfway through scanning a stack.
         * Interrupts remain enabled while scanning the fill pattern. */
        vTaskSuspendAll();
        if(g_diag_task_handles[id] != NULL) {
            uint32_t free_bytes = uxTaskGetStackHighWaterMark(g_diag_task_handles[id]) * sizeof(StackType_t);
            g_firmware_diag.tasks[id].stack_free_min_bytes = free_bytes;
            if(free_bytes < 512U) g_firmware_diag.stack_warning_mask |= 1UL << id;
        }
        (void)xTaskResumeAll();
    }
    g_firmware_diag.heap_free_bytes = xPortGetFreeHeapSize();
    g_firmware_diag.heap_free_min_bytes = xPortGetMinimumEverFreeHeapSize();
    lv_mem_monitor(&memory);
    g_firmware_diag.lvgl_free_bytes = memory.free_size;
    g_firmware_diag.lvgl_largest_free_bytes = memory.free_biggest_size;
    g_firmware_diag.lvgl_used_peak_bytes = memory.max_used;
    g_firmware_diag.sample_tick = now;
}

void Firmware_Diag_RecordFault(uint32_t code)
{
    __disable_irq();
    g_firmware_diag.fault_code = code;
    g_firmware_diag.cfsr = SCB->CFSR;
    g_firmware_diag.hfsr = SCB->HFSR;
    g_firmware_diag.mmfar = SCB->MMFAR;
    g_firmware_diag.bfar = SCB->BFAR;
    g_firmware_diag.msp = __get_MSP();
    g_firmware_diag.psp = __get_PSP();
    g_firmware_diag.ipsr = __get_IPSR();
}

void Firmware_Diag_LvglAssert(void)
{
    Firmware_Diag_RecordFault(5U);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
    for(;;) { }
}
/* USER CODE END Firmware_Diagnostics */
