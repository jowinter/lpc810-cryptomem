/**
 * @file
 * @brief Hardware abstraction layer
 */

#ifndef HAL_H_
#define HAL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Pull-in the common device header
#include "LPC810.h"

extern void Hal_Init(void);
extern void Hal_Idle(void);
extern __NO_RETURN void Hal_Halt(void);

extern void Hal_SetReadyPin(bool ready);

extern void Hal_ReadDeviceID(uint32_t device_id[4]);
extern __NO_RETURN void Hal_EnterBootloader(void);

#define HAL_NV_FLASH_START      (0x00000000u)
#define HAL_NV_PAGE_SIZE        (64u)
#define HAL_NV_PAGES_PER_SECTOR (16u)
#define HAL_NV_NUM_TOTAL_PAGES  (64u)

#define HAL_NV_DATA \
	__attribute__((__section__(".nv"), __used__, __aligned__((HAL_NV_PAGE_SIZE))))

extern bool Hal_NvWrite(const void* addr, const uint8_t nv_page[HAL_NV_PAGE_SIZE]);

#endif /* HAL_H_ */
