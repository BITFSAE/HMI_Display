/* USER CODE BEGIN odometer_flash */
#include "odometer_flash.h"
#include "main.h"

/* Sector 7 of the 512 KiB F407ZE flash. STM32F407ZETX_FLASH.ld limits the
 * program image to the first 384 KiB so the linker can never place code here.
 *
 * Layout: 8-byte slots filled in order. word 0 = total in metres, word 1 =
 * check value of word 0. The check word is programmed last, so a slot cut
 * short by a power loss never validates. The newest valid slot wins, and a
 * damaged slot is skipped, never reused. */
#define ODO_FLASH_BASE      0x08060000UL
#define ODO_FLASH_BYTES     0x00020000UL
#define ODO_SLOT_BYTES      8UL
#define ODO_SLOT_COUNT      (ODO_FLASH_BYTES / ODO_SLOT_BYTES)
#define ODO_CHECK_SALT      0x4F444F32UL
#define ODO_BLANK_WORD      0xFFFFFFFFUL
#define ODO_SAVE_ATTEMPTS   2U

/* Host tests replace both hooks. */
#ifndef ODO_FLASH_READ32
#define ODO_FLASH_READ32(address) (*(const volatile uint32_t *)(address))
#endif
/* Programming does not update the F4 data cache; drop stale 0xFF lines before
 * reading a slot back. */
#ifndef ODO_FLUSH_DATA_CACHE
#define ODO_FLUSH_DATA_CACHE() \
    do { \
        if((FLASH->ACR & FLASH_ACR_DCEN) != 0U) { \
            __HAL_FLASH_DATA_CACHE_DISABLE(); \
            __HAL_FLASH_DATA_CACHE_RESET(); \
            __HAL_FLASH_DATA_CACHE_ENABLE(); \
        } \
    } while(0)
#endif

typedef enum {
    ODO_SLOT_BLANK = 0U,
    ODO_SLOT_VALID,
    ODO_SLOT_DAMAGED
} odo_slot_state_t;

volatile uint32_t g_odometer_flash_save_fail_count;
volatile uint32_t g_odometer_flash_erase_count;

static uint32_t g_odo_initial_m;
static uint32_t g_odo_next_slot;
static uint8_t g_odo_ready;
static uint8_t g_odo_writable;

static uint32_t odo_check(uint32_t total_m)
{
    uint32_t x = total_m ^ ODO_CHECK_SALT;

    x ^= x >> 16;
    x *= 0x7FEB352DUL;
    x ^= x >> 15;
    x *= 0x846CA68BUL;
    x ^= x >> 16;
    return x;
}

static uint32_t odo_slot_address(uint32_t slot)
{
    return (uint32_t)(ODO_FLASH_BASE + (slot * ODO_SLOT_BYTES));
}

static odo_slot_state_t odo_read_slot(uint32_t slot, uint32_t * total_m)
{
    uint32_t address = odo_slot_address(slot);
    uint32_t value = ODO_FLASH_READ32(address);
    uint32_t check = ODO_FLASH_READ32(address + 4U);

    if((value == ODO_BLANK_WORD) && (check == ODO_BLANK_WORD)) return ODO_SLOT_BLANK;
    if(check != odo_check(value)) return ODO_SLOT_DAMAGED;
    *total_m = value;
    return ODO_SLOT_VALID;
}

/* The write position follows the last slot that is not blank, so a gap left by
 * a damaged slot is never filled. */
static void odo_scan(uint32_t * last_m, uint8_t * has_valid, uint32_t * next_slot)
{
    *has_valid = 0U;
    *next_slot = 0U;
    for(uint32_t slot = 0U; slot < ODO_SLOT_COUNT; slot++) {
        uint32_t total_m = 0U;
        odo_slot_state_t state = odo_read_slot(slot, &total_m);

        if(state == ODO_SLOT_BLANK) continue;
        *next_slot = slot + 1U;
        if(state == ODO_SLOT_VALID) {
            *last_m = total_m;
            *has_valid = 1U;
        }
    }
}

static void odo_clear_flash_flags(void)
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
}

static uint8_t odo_program_slot(uint32_t slot, uint32_t total_m)
{
    uint32_t address = odo_slot_address(slot);
    uint32_t stored_m = 0U;
    uint8_t programmed;

    if((ODO_FLASH_READ32(address) != ODO_BLANK_WORD) ||
       (ODO_FLASH_READ32(address + 4U) != ODO_BLANK_WORD)) {
        return 0U;
    }
    if(HAL_FLASH_Unlock() != HAL_OK) return 0U;
    odo_clear_flash_flags();
    programmed = (uint8_t)((HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, total_m) == HAL_OK) &&
                           (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + 4U,
                                              odo_check(total_m)) == HAL_OK));
    (void)HAL_FLASH_Lock();
    ODO_FLUSH_DATA_CACHE();
    return (uint8_t)((programmed != 0U) &&
                     (odo_read_slot(slot, &stored_m) == ODO_SLOT_VALID) &&
                     (stored_m == total_m));
}

static uint8_t odo_erase_sector(void)
{
    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_SECTORS,
        .Sector = FLASH_SECTOR_7,
        .NbSectors = 1U,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3
    };
    uint32_t sector_error = 0U;
    HAL_StatusTypeDef status;

    if(HAL_FLASH_Unlock() != HAL_OK) return 0U;
    odo_clear_flash_flags();
    status = HAL_FLASHEx_Erase(&erase, &sector_error);
    (void)HAL_FLASH_Lock();
    ODO_FLUSH_DATA_CACHE();
    if((status != HAL_OK) || (sector_error != 0xFFFFFFFFUL)) return 0U;
    for(uint32_t address = ODO_FLASH_BASE; address < (ODO_FLASH_BASE + ODO_FLASH_BYTES);
        address += 4U) {
        if(ODO_FLASH_READ32(address) != ODO_BLANK_WORD) return 0U;
    }
    g_odometer_flash_erase_count++;
    return 1U;
}

void Odometer_Flash_Init(void)
{
    uint32_t last_m = 0U;
    uint32_t next_slot = 0U;
    uint8_t has_valid = 0U;

    odo_scan(&last_m, &has_valid, &next_slot);
    g_odo_initial_m = (has_valid != 0U) ? last_m : 0U;
    g_odo_writable = 1U;
    if((next_slot >= ODO_SLOT_COUNT) || ((has_valid == 0U) && (next_slot != 0U))) {
        /* Full sector, or only leftovers (earlier firmware image, interrupted
         * erase): start clean and carry the latest total into slot 0. The
         * running value stays in RAM, so a failed erase or write only stops
         * saving; the next boot repeats the attempt. */
        next_slot = 0U;
        if(odo_erase_sector() == 0U) {
            g_odo_writable = 0U;
        }
        else if(has_valid != 0U) {
            (void)odo_program_slot(0U, last_m);
            next_slot = 1U;
        }
    }
    g_odo_next_slot = next_slot;
    g_odo_ready = 1U;
}

uint8_t Odometer_Flash_GetInitial(uint32_t * total_m)
{
    if((total_m == NULL) || (g_odo_ready == 0U)) return 0U;
    *total_m = g_odo_initial_m;
    return 1U;
}

uint8_t Odometer_Flash_Save(uint32_t total_m)
{
    if((g_odo_ready != 0U) && (g_odo_writable != 0U)) {
        for(uint32_t attempt = 0U;
            (attempt < ODO_SAVE_ATTEMPTS) && (g_odo_next_slot < ODO_SLOT_COUNT); attempt++) {
            if(odo_program_slot(g_odo_next_slot++, total_m) != 0U) return 1U;
        }
    }
    g_odometer_flash_save_fail_count++;
    return 0U;
}
/* USER CODE END odometer_flash */
