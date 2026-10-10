/* USER CODE BEGIN Firmware_Diagnostics */
#ifndef FIRMWARE_DIAG_H
#define FIRMWARE_DIAG_H

#include <stdint.h>

#define FW_DIAG_TASK_COUNT 6U
typedef enum {
    FW_TASK_DASHBOARD, FW_TASK_TOUCH, FW_TASK_GPS, FW_TASK_CAN,
    FW_TASK_IDLE, FW_TASK_TIMER
} firmware_task_id_t;

typedef struct {
    const char *name;
    uint32_t stack_size_bytes;
    uint32_t stack_free_min_bytes;
    uint32_t active;
} firmware_task_diag_t;

typedef struct {
    uint32_t sample_tick;
    uint32_t heap_free_bytes;
    uint32_t heap_free_min_bytes;
    uint32_t lvgl_free_bytes;
    uint32_t lvgl_largest_free_bytes;
    uint32_t lvgl_used_peak_bytes;
    uint32_t stack_warning_mask; /* Sticky: task had < 512 bytes left. */
    uint32_t display_loop_max_cycles;
    uint32_t flush_max_cycles;
    uint32_t flushed_pixels;
    uint32_t fault_code;
    uint32_t cfsr, hfsr, mmfar, bfar, msp, psp, ipsr;
    firmware_task_diag_t tasks[FW_DIAG_TASK_COUNT];
} firmware_diag_t;

extern volatile firmware_diag_t g_firmware_diag;
void Firmware_Diag_Init(void);
void Firmware_Diag_RegisterTask(firmware_task_id_t id, void *handle,
                               const char *name, uint32_t stack_size_bytes);
void Firmware_Diag_UnregisterTask(firmware_task_id_t id);
void Firmware_Diag_Sample(void); /* Only call from the LVGL owner task. */
void Firmware_Diag_RecordFault(uint32_t code);
void Firmware_Diag_LvglAssert(void);

#endif
/* USER CODE END Firmware_Diagnostics */
