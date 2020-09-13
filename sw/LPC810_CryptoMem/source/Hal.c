/**
 * @file
 * @brief Hardware abstraction layer.
 */

#include <Hal.h>
#include <Eep.h>

#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"

#include "fsl_swm.h"
#include "fsl_iap.h"
#include "fsl_power.h"
#include "fsl_gpio.h"
#include "fsl_wwdt.h"

#include "LPC810.h"


//---------------------------------------------------------------------------------------------------------------------
// Set Code Read Protection to CRP2 (No SWD, only full erase allowed before firmware update)
//
#include <NXP/crp.h>

__CRP const unsigned int CRP_WORD = CRP_CRP2;

//---------------------------------------------------------------------------------------------------------------------
HAL_INIT_CODE void Hal_Init(void)
{
	// Disable the SWD interface pins (we need them as I2C pins)
	CLOCK_EnableClock(kCLOCK_Swm);
	SWM_SetFixedPinSelect(SWM0, kSWM_SWCLK, false);
	SWM_SetFixedPinSelect(SWM0, kSWM_SWDIO, false);
	CLOCK_DisableClock(kCLOCK_Swm);

	// Regular board pin setup
	BOARD_InitBootPins();
	BOARD_InitBootClocks();
	BOARD_InitBootPeripherals();

	// Setup the I2C slave
	CLOCK_EnableClock(kCLOCK_I2c0);
	Eep_I2CSetClockDivider();
}

//---------------------------------------------------------------------------------------------------------------------
void Hal_SwitchToExtClock(void)
{
	__disable_irq();

	// First switch to 12 MHz IRC clock
	CLOCK_SetMainClkSrc(kCLOCK_MainClkSrcIrc);
	CLOCK_SetCoreSysClkDiv(1u);

	// Stop the SYSTEM PLL
	CLOCK_DenitSystemPll();

	// Start the IOCON and SWM clocks
	CLOCK_EnableClock(kCLOCK_Swm);
	CLOCK_EnableClock(kCLOCK_Iocon);

	// Initialize the external clock input
	CLOCK_InitExtClkin(HAL_SYSTEM_CLOCK);

	// Stop the IOCON and SWM clocks
	CLOCK_DisableClock(kCLOCK_Iocon);
	CLOCK_DisableClock(kCLOCK_Swm);

	// Switch to the external clock input
	CLOCK_SetMainClkSrc(kCLOCK_MainClkSrcSysPllin);

	__enable_irq();
}

//---------------------------------------------------------------------------------------------------------------------
void Hal_Idle(void)
{
	// Idle-time processing (wait for interrupt/event)
	__WFE();
}

//---------------------------------------------------------------------------------------------------------------------
__NO_RETURN void Hal_Halt(void)
{
	// Interrupts off
	__disable_irq();

	// Stop the I2C slave interface
	Eep_I2CStopSlave();

	// Signal an error
	Hal_SetReadyPin(false);

	// NOTE: At this point the WWDT interrupt is blocked. We will (eventually) run into a WDT timeout (and reset).

	// Now sleep forever
	while (true)
	{
		Hal_Idle();
	}
}

//---------------------------------------------------------------------------------------------------------------------
void Hal_ReadDeviceID(uint32_t device_id[4u])
{
	// Read via LPC81x IAP ROM API
	if (kStatus_IAP_Success != IAP_ReadUniqueID(&device_id[0u]))
	{
		// IAP read error, set device ID to 0xFFFFFF pattern
		device_id[0u] = 0xFFFFFFFFu;
		device_id[1u] = 0xFFFFFFFFu;
		device_id[2u] = 0xFFFFFFFFu;
		device_id[3u] = 0xFFFFFFFFu;
	}
}

//---------------------------------------------------------------------------------------------------------------------
__NO_RETURN void Hal_EnterBootloader(void)
{
	uint32_t isp_error;

	// Ensure that IRQs are off
	__disable_irq();

	// Disable the watchdog
	WWDT_Disable(WWDT);

	// Stop the I2C slave interface
	Eep_I2CStopSlave();

	// Ensure that we are running on the 12 MHz IRC clock (with divider 1)
	CLOCK_SetMainClkSrc(kCLOCK_MainClkSrcIrc);
	CLOCK_SetCoreSysClkDiv(1u);

	// Now try to enable the ISP
	IAP_ReinvokeISP(1u, &isp_error);

	// Entry into ISP mode failed, we force a device reset
	NVIC_SystemReset();

	// Unreachable (system reset does not return)
	__builtin_unreachable();
}

//---------------------------------------------------------------------------------------------------------------------
bool Hal_NvWrite(const void* data, const uint8_t nv_page[64u])
{
	// Prepare+Erase
	const uint32_t addr = (uint32_t) data;
	const uint32_t page =  (addr - HAL_NV_FLASH_START) / HAL_NV_PAGE_SIZE;
	const uint32_t sector = page / HAL_NV_PAGES_PER_SECTOR;
	if (kStatus_IAP_Success != IAP_PrepareSectorForWrite(sector, sector))
	{
		// Prepare failed
		return false;
	}

	if (kStatus_IAP_Success != IAP_ErasePage(page, page, BOARD_BOOTCLOCKRUN_CORE_CLOCK))
	{
		// Erase failed
		return false;
	}

	// Prepare+Write
	if (kStatus_IAP_Success != IAP_PrepareSectorForWrite(sector, sector))
	{
		// Prepare failed
		return false;
	}

	// And write
	if (kStatus_IAP_Success != IAP_CopyRamToFlash(addr, (uint32_t *) nv_page, HAL_NV_PAGE_SIZE, HAL_SYSTEM_CLOCK))
	{
		// Write failed
		return false;
	}

	return true;
}

//---------------------------------------------------------------------------------------------------------------------
void Hal_SetReadyPin(bool ready)
{
	GPIO_PinWrite(BOARD_INITPINS_RDY_N_GPIO, BOARD_INITPINS_RDY_N_PORT, BOARD_INITPINS_RDY_N_PIN, ready ? 0u : 1u);

}

//---------------------------------------------------------------------------------------------------------------------
void I2C0_IRQHandler(void)
{
	Eep_I2CSlaveIrqHandler();
}

//---------------------------------------------------------------------------------------------------------------------
//
// We alias all unimplemented IRQ handlers to Hal_Halt()
//

#define HAL_UNHANDLED_IRQ(irq) \
	__attribute__((__alias__("Hal_Halt"))) void irq(void)

HAL_UNHANDLED_IRQ(IntDefaultHandler);
HAL_UNHANDLED_IRQ(SysTick_Handler);
HAL_UNHANDLED_IRQ(PendSV_Handler);
HAL_UNHANDLED_IRQ(SVC_Handler);
HAL_UNHANDLED_IRQ(HardFault_Handler);
HAL_UNHANDLED_IRQ(NMI_Handler);

HAL_UNHANDLED_IRQ(SPI0_IRQHandler);
HAL_UNHANDLED_IRQ(SPI1_IRQHandler);
HAL_UNHANDLED_IRQ(USART0_IRQHandler);
HAL_UNHANDLED_IRQ(USART1_IRQHandler);
HAL_UNHANDLED_IRQ(USART2_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved18_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved22_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved23_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved30_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved32_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved33_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved34_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved35_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved36_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved37_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved38_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved39_IRQHandler);
HAL_UNHANDLED_IRQ(Reserved40_IRQHandler);
HAL_UNHANDLED_IRQ(SCT0_IRQHandler);
HAL_UNHANDLED_IRQ(MRT0_IRQHandler);
HAL_UNHANDLED_IRQ(CMP_IRQHandler);
HAL_UNHANDLED_IRQ(WDT_IRQHandler);
HAL_UNHANDLED_IRQ(BOD_IRQHandler);
HAL_UNHANDLED_IRQ(WKT_IRQHandler);
HAL_UNHANDLED_IRQ(PIN_INT0_IRQHandler);
HAL_UNHANDLED_IRQ(PIN_INT1_IRQHandler);
HAL_UNHANDLED_IRQ(PIN_INT2_IRQHandler);
HAL_UNHANDLED_IRQ(PIN_INT3_IRQHandler);
HAL_UNHANDLED_IRQ(PIN_INT4_IRQHandler);
HAL_UNHANDLED_IRQ(PIN_INT5_IRQHandler);
HAL_UNHANDLED_IRQ(PIN_INT6_IRQHandler);
HAL_UNHANDLED_IRQ(PIN_INT7_IRQHandler);

