/* USER CODE BEGIN sd_log */
#include "sd_log.h"
#include "fatfs.h"
#include "sdio.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "firmware_diag.h"

static const char g_test_text[] = "Manba Outlaws\r\nSD OK\r\n";

volatile uint32_t g_sd_diag_stage = SD_DIAG_NOT_RUN;
volatile uint32_t g_sd_diag_fresult;
volatile uint32_t g_sd_diag_bytes;
volatile uint32_t g_sd_diag_hal_error;

#define SD_ODOMETER_MAGIC       0x4F444F31UL
#define SD_ODOMETER_CHECK_SALT  0x6D2B79F5UL
#define SD_ODOMETER_FILE_A      "0:/ODO_A.DAT"
#define SD_ODOMETER_FILE_B      "0:/ODO_B.DAT"

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t total_m;
    uint32_t checksum;
} sd_odometer_record_t;

typedef enum {
    SD_ODO_EMPTY = 0U,
    SD_ODO_VALID,
    SD_ODO_IO_ERROR
} sd_odometer_result_t;

static TaskHandle_t g_sd_odometer_task_handle;
static StaticTask_t g_sd_odometer_task_control;
static StackType_t g_sd_odometer_task_stack[768U];
static volatile uint32_t g_sd_odometer_pending_m;
static volatile uint8_t g_sd_odometer_save_pending;
static uint32_t g_sd_odometer_sequence;
static uint8_t g_sd_odometer_next_file;
static StaticSemaphore_t g_sd_mutex_control;
static SemaphoreHandle_t g_sd_mutex;
static uint32_t g_sd_initial_odometer_m;
static volatile uint8_t g_sd_initial_odometer_ready;
volatile uint32_t g_sd_odometer_save_fail_count;
volatile uint32_t g_sd_odometer_load_fail_count;

static uint8_t sd_lock(void)
{
    if(g_sd_mutex == NULL) return 0U;
    return xSemaphoreTake(g_sd_mutex, portMAX_DELAY) == pdTRUE;
}


static uint32_t sd_odometer_checksum(uint32_t sequence, uint32_t total_m)
{
    return sequence ^ total_m ^ (total_m << 13) ^ (total_m >> 7) ^
           SD_ODOMETER_CHECK_SALT;
}

static sd_odometer_result_t sd_odometer_read_file(const char * path,
                                                 sd_odometer_record_t * record)
{
    FIL fil;
    UINT bytes_read = 0U;
    FRESULT fr;
    FRESULT close_fr;

    fr = f_open(&fil, path, FA_READ);
    if(fr == FR_NO_FILE) return SD_ODO_EMPTY;
    if(fr != FR_OK) return SD_ODO_IO_ERROR;
    fr = f_read(&fil, record, sizeof(*record), &bytes_read);
    close_fr = f_close(&fil);
    if((fr != FR_OK) || (close_fr != FR_OK)) return SD_ODO_IO_ERROR;
    if((bytes_read != sizeof(*record)) ||
       (record->magic != SD_ODOMETER_MAGIC) ||
       (record->checksum !=
        sd_odometer_checksum(record->sequence, record->total_m))) {
        return SD_ODO_EMPTY;
    }
    return SD_ODO_VALID;
}

static sd_odometer_result_t sd_odometer_load_locked(uint32_t * total_m)
{
    sd_odometer_record_t record_a;
    sd_odometer_record_t record_b;
    sd_odometer_result_t result_a;
    sd_odometer_result_t result_b;
    FRESULT unmount_fr;

    if((total_m == NULL) || (g_sd_ready == 0U)) return SD_ODO_IO_ERROR;
    if(f_mount(&SDFatFS, SDPath, 1) != FR_OK) {
        /* FatFs registers the volume before trying the physical mount. */
        (void)f_mount(NULL, SDPath, 0);
        return SD_ODO_IO_ERROR;
    }
    result_a = sd_odometer_read_file(SD_ODOMETER_FILE_A, &record_a);
    result_b = sd_odometer_read_file(SD_ODOMETER_FILE_B, &record_b);
    unmount_fr = f_mount(NULL, SDPath, 0);
    /* An unreadable file may contain a newer valid record. Do not turn an
     * I/O failure into an empty odometer and overwrite that record later. */
    if((result_a == SD_ODO_IO_ERROR) || (result_b == SD_ODO_IO_ERROR) ||
       (unmount_fr != FR_OK)) return SD_ODO_IO_ERROR;

    if((result_a == SD_ODO_VALID) &&
       ((result_b != SD_ODO_VALID) ||
        ((int32_t)(record_a.sequence - record_b.sequence) > 0))) {
        g_sd_odometer_sequence = record_a.sequence;
        g_sd_odometer_next_file = 1U;
        *total_m = record_a.total_m;
        return SD_ODO_VALID;
    }
    if(result_b == SD_ODO_VALID) {
        g_sd_odometer_sequence = record_b.sequence;
        g_sd_odometer_next_file = 0U;
        *total_m = record_b.total_m;
        return SD_ODO_VALID;
    }
    g_sd_odometer_sequence = 0U;
    g_sd_odometer_next_file = 0U;
    *total_m = 0U;
    return SD_ODO_EMPTY;
}

uint8_t SD_Odometer_Load(uint32_t *total_m)
{
    if(sd_lock() == 0U) return 0U;
    sd_odometer_result_t loaded = sd_odometer_load_locked(total_m);
    (void)xSemaphoreGive(g_sd_mutex);
    return loaded == SD_ODO_VALID;
}

static uint8_t sd_odometer_save_locked(uint32_t total_m)
{
    sd_odometer_record_t record;
    const char * path;
    FIL fil;
    UINT bytes_written = 0U;
    FRESULT fr;
    FRESULT close_fr;

    if((g_sd_ready == 0U) || (g_sd_initial_odometer_ready == 0U)) return 0U;
    record.magic = SD_ODOMETER_MAGIC;
    record.sequence = g_sd_odometer_sequence + 1U;
    record.total_m = total_m;
    record.checksum = sd_odometer_checksum(record.sequence, record.total_m);
    /* Preserve the file actually loaded, independently of sequence parity. */
    path = (g_sd_odometer_next_file != 0U) ?
           SD_ODOMETER_FILE_B : SD_ODOMETER_FILE_A;

    if(f_mount(&SDFatFS, SDPath, 1) != FR_OK) {
        (void)f_mount(NULL, SDPath, 0);
        return 0U;
    }
    fr = f_open(&fil, path, FA_CREATE_ALWAYS | FA_WRITE);
    if(fr == FR_OK) {
        fr = f_write(&fil, &record, sizeof(record), &bytes_written);
        if((fr == FR_OK) && (bytes_written == sizeof(record))) {
            fr = f_sync(&fil);
        }
        close_fr = f_close(&fil);
        if((fr == FR_OK) && (close_fr != FR_OK)) {
            fr = close_fr;
        }
    }
    f_mount(NULL, SDPath, 0);
    if((fr != FR_OK) || (bytes_written != sizeof(record))) return 0U;
    g_sd_odometer_sequence = record.sequence;
    g_sd_odometer_next_file ^= 1U;
    return 1U;
}

static uint8_t sd_odometer_save(uint32_t total_m)
{
    if(sd_lock() == 0U) return 0U;
    uint8_t saved = sd_odometer_save_locked(total_m);
    (void)xSemaphoreGive(g_sd_mutex);
    return saved;
}

static uint8_t sd_odometer_initialize(void)
{
    uint32_t total_m = 0U;
    if(sd_lock() == 0U) return 0U;
    sd_odometer_result_t loaded = sd_odometer_load_locked(&total_m);
    (void)xSemaphoreGive(g_sd_mutex);
    if(loaded == SD_ODO_IO_ERROR) {
        g_sd_odometer_load_fail_count++;
        return 0U;
    }
    taskENTER_CRITICAL();
    g_sd_initial_odometer_m = total_m;
    g_sd_initial_odometer_ready = 1U;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t SD_Odometer_GetInitial(uint32_t *total_m)
{
    if(total_m == NULL || g_sd_initial_odometer_ready == 0U) return 0U;
    taskENTER_CRITICAL();
    *total_m = g_sd_initial_odometer_m;
    taskEXIT_CRITICAL();
    return 1U;
}

static void SD_Odometer_Task(void * argument)
{
    uint32_t pending_m;
    uint8_t initialized;

    (void)argument;
    initialized = sd_odometer_initialize();
    SD_Log_InitAndWrite(NULL);
    while(initialized == 0U) {
        /* GPS configuration and telemetry continue while only storage waits. */
        vTaskDelay(pdMS_TO_TICKS(10000U));
        initialized = sd_odometer_initialize();
    }
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
        if(g_sd_odometer_save_pending == 0U) continue;

        taskENTER_CRITICAL();
        pending_m = g_sd_odometer_pending_m;
        taskEXIT_CRITICAL();
        if(sd_odometer_save(pending_m) != 0U) {
            taskENTER_CRITICAL();
            if(g_sd_odometer_pending_m == pending_m) {
                g_sd_odometer_save_pending = 0U;
            }
            taskEXIT_CRITICAL();
        }
        else {
            g_sd_odometer_save_fail_count++;
            /* Missing or faulty cards are retried slowly without blocking GPS. */
            vTaskDelay(pdMS_TO_TICKS(9000U));
        }
    }
}

void SD_Odometer_StartTask(void)
{
    if(g_sd_odometer_task_handle != NULL) return;
    g_sd_mutex = xSemaphoreCreateMutexStatic(&g_sd_mutex_control);
    configASSERT(g_sd_mutex != NULL);
    g_sd_odometer_task_handle =
        xTaskCreateStatic(SD_Odometer_Task, "ODO_Save", 768U, NULL,
                          tskIDLE_PRIORITY + 1U, g_sd_odometer_task_stack,
                          &g_sd_odometer_task_control);
    Firmware_Diag_RegisterTask(FW_TASK_SD, g_sd_odometer_task_handle, "SD", sizeof(g_sd_odometer_task_stack));
}

void SD_Odometer_RequestSave(uint32_t total_m)
{
    taskENTER_CRITICAL();
    g_sd_odometer_pending_m = total_m;
    g_sd_odometer_save_pending = 1U;
    taskEXIT_CRITICAL();
}

static void led_red_on(void)  { HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET); }

static void sd_log_init_and_write_locked(const dashboard_data_t * data)
{
    FRESULT fr = FR_OK;
    FIL     fil;
    UINT    bw = 0U;
    uint8_t file_open = 0U;

    (void)data;

    g_sd_diag_stage = SD_DIAG_NOT_RUN;
    g_sd_diag_fresult = FR_OK;
    g_sd_diag_bytes = 0U;
    g_sd_diag_hal_error = hsd.ErrorCode;

    if (!g_sd_ready) {
        g_sd_diag_stage = SD_DIAG_INIT_FAILED;
        g_sd_diag_hal_error = hsd.ErrorCode;
        led_red_on();
        return;
    }

    fr = f_mount(&SDFatFS, SDPath, 1);
    if (fr != FR_OK) {
        g_sd_diag_stage = SD_DIAG_MOUNT_FAILED;
        g_sd_diag_fresult = fr;
        g_sd_diag_hal_error = hsd.ErrorCode;
        led_red_on();
        (void)f_mount(NULL, SDPath, 0);
        return;
    }

    fr = f_open(&fil, "0:/manba.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        g_sd_diag_stage = SD_DIAG_OPEN_FAILED;
        g_sd_diag_fresult = fr;
        g_sd_diag_hal_error = hsd.ErrorCode;
        led_red_on();
        f_mount(NULL, SDPath, 0);
        return;
    }
    file_open = 1U;

    fr = f_write(&fil, g_test_text, sizeof(g_test_text) - 1U, &bw);
    g_sd_diag_bytes = bw;
    if((fr != FR_OK) || (bw != (sizeof(g_test_text) - 1U))) {
        g_sd_diag_stage = SD_DIAG_WRITE_FAILED;
        g_sd_diag_fresult = fr;
        goto log_failed;
    }

    fr = f_sync(&fil);
    if(fr != FR_OK) {
        g_sd_diag_stage = SD_DIAG_SYNC_FAILED;
        g_sd_diag_fresult = fr;
        goto log_failed;
    }

    fr = f_close(&fil);
    file_open = 0U;
    if(fr != FR_OK) {
        g_sd_diag_stage = SD_DIAG_CLOSE_FAILED;
        g_sd_diag_fresult = fr;
        goto log_failed;
    }
    f_mount(NULL, SDPath, 0);

    g_sd_diag_stage = SD_DIAG_OK;
    g_sd_diag_fresult = FR_OK;
    g_sd_diag_hal_error = hsd.ErrorCode;

    /* success: green off -> on (visible change) */
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(300U));
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
    return;

log_failed:
    g_sd_diag_hal_error = hsd.ErrorCode;
    if(file_open != 0U) {
        (void)f_close(&fil);
    }
    (void)f_mount(NULL, SDPath, 0);
    led_red_on();
}

void SD_Log_InitAndWrite(const dashboard_data_t *data)
{
    if(sd_lock() == 0U) return;
    sd_log_init_and_write_locked(data);
    (void)xSemaphoreGive(g_sd_mutex);
}
/* USER CODE END sd_log */
