/* USER CODE BEGIN odometer_flash */
#ifndef ODOMETER_FLASH_H
#define ODOMETER_FLASH_H

#include <stdint.h>

/* Persistent odometer in internal flash sector 7 (0x08060000, 128 KiB), which
 * the linker script keeps out of the program image. Storage details are in
 * docs/odometer_storage.md. */

extern volatile uint32_t g_odometer_flash_save_fail_count;
extern volatile uint32_t g_odometer_flash_erase_count;

/* Scan the sector and, only when it is full or holds leftovers without a valid
 * record, erase it. An erase stalls the whole CPU for seconds, so call this
 * once before the scheduler and the CAN controllers start. */
void Odometer_Flash_Init(void);

/* Total (metres) found at startup; 0 for an empty sector. Returns 0 until
 * Odometer_Flash_Init() has run. */
uint8_t Odometer_Flash_GetInitial(uint32_t * total_m);

/* Append one record. Stalls the CPU for tens of microseconds per word. Returns
 * 0 on failure (counted in g_odometer_flash_save_fail_count); nothing is
 * erased here, so a full sector refuses saves until the next boot. */
uint8_t Odometer_Flash_Save(uint32_t total_m);

#endif

/* USER CODE END odometer_flash */
