/* USER CODE BEGIN sd_log */
#ifndef SD_LOG_H
#define SD_LOG_H

#include "dashboard_ui.h"

typedef enum {
    SD_DIAG_NOT_RUN = 0U,
    SD_DIAG_INIT_FAILED,
    SD_DIAG_MOUNT_FAILED,
    SD_DIAG_OPEN_FAILED,
    SD_DIAG_WRITE_FAILED,
    SD_DIAG_SYNC_FAILED,
    SD_DIAG_CLOSE_FAILED,
    SD_DIAG_OK
} sd_diag_stage_t;

extern volatile uint32_t g_sd_diag_stage;
extern volatile uint32_t g_sd_diag_fresult;
extern volatile uint32_t g_sd_diag_bytes;
extern volatile uint32_t g_sd_diag_hal_error;

void SD_Log_InitAndWrite(const dashboard_data_t * data);
uint8_t SD_Odometer_Load(uint32_t * total_m);
/* Nonblocking startup snapshot produced by the SD task. */
uint8_t SD_Odometer_GetInitial(uint32_t *total_m);
void SD_Odometer_StartTask(void);
void SD_Odometer_RequestSave(uint32_t total_m);

#endif

/* USER CODE END sd_log */
