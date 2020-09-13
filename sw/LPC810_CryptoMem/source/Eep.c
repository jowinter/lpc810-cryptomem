/**
 * @file
 * @brief EEPROM-style I2C Slave Interface
 *
 */
#include <Hal.h>
#include <Eep.h>

#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief FSM states of the I2C slave state machine
 */
typedef enum
{
	/**
	 * @brief Slave is ready.
	 */
	kEep_SlaveReady,

	/**
     * @brief Active data exchange with the slave is ongoing.
	 */
	kEep_SlaveAddress,

	/**
	 * @brief Read transaction (slave -> master) is ongoing.
	 */
	kEep_SlaveDataRead,

	/**
	 * @brief Write transaction (master -> slave) is ongoing.
	 */
	kEep_SlaveDataWrite
} Eep_SlaveFsmState_t;

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief State of the EEPROM-style I2C slave
 */
typedef struct Eep_Slave
{
	/**
	 * @brief FSM state
	 */
	Eep_SlaveFsmState_t state;

	/**
	 * @brief Current register/memory address.
	 */
	uint8_t reg_addr;
} Eep_Slave_t;

/**
 * @brief Global state of the EEPROM-style I2C slave
 */
static Eep_Slave_t gSlave;

#define I2C_SLAVE_DEV       I2C0
#define I2C_SLAVE_ADDR      0x20u
#define I2C_SLAVE_IRQ_FLAGS (I2C_INTSTAT_SLVPENDING_MASK | I2C_INTSTAT_SLVDESEL_MASK)
#define I2C_SLAVE_NVIC_IRQn I2C0_IRQn

/* definitions for SLVSTATE bits in I2C Status register STAT */
#define I2C_STAT_SLVST_ADDR (0u)
#define I2C_STAT_SLVST_RX   (1u)
#define I2C_STAT_SLVST_TX   (2u)


//---------------------------------------------------------------------------------------------------------------------
void Eep_I2CSetClockDivider(void)
{
	// Setup the I2C clock divider for standard speed (100 kHz)
	//
	// Calculation ported from MCUxpresso fsl_i2c driver (I2C_SlaveDivVal)
	//
	// NOTE: We assume a fixed system clock (DEFAULT_SYSTEM_CLOCK). This allows constant folding on the divider
	// calculation (and omits the __aeabi_udiv division code).

	/* divVal = (sourceClock_Hz / 1000000) * (dataSetupTime_ns / 1000) */
	const uint32_t data_setup_ns = 250u;
	const uint32_t divider = ((HAL_SYSTEM_CLOCK / 1000u) * data_setup_ns) / 1000000u;
	I2C_SLAVE_DEV->CLKDIV = (divider < I2C_CLKDIV_DIVVAL_MASK) ? divider : I2C_CLKDIV_DIVVAL_MASK;
}

//---------------------------------------------------------------------------------------------------------------------
void Eep_I2CStartSlave(void)
{
	// Stop any ongoing slave activity
	Eep_I2CStopSlave();

	// Enable the slave block and its interrupts
	I2C_SLAVE_DEV->INTENSET = I2C_SLAVE_IRQ_FLAGS;

	// Set the slave address
	I2C_SLAVE_DEV->SLVADR[0u] = I2C_SLVADR_SLVADR(I2C_SLAVE_ADDR) | I2C_SLVADR_SADISABLE(0);
	I2C_SLAVE_DEV->SLVADR[1u] = I2C_SLVADR_SLVADR(0)              | I2C_SLVADR_SADISABLE(1);
	I2C_SLAVE_DEV->SLVADR[2u] = I2C_SLVADR_SLVADR(0)              | I2C_SLVADR_SADISABLE(1);
	I2C_SLAVE_DEV->SLVADR[3u] = I2C_SLVADR_SLVADR(0)              | I2C_SLVADR_SADISABLE(1);

	// No qualifier for SLVADR0 (use SLVADR0 as-is)
	I2C_SLAVE_DEV->SLVQUAL0   = I2C_SLVQUAL0_SLVQUAL0(0) | I2C_SLVQUAL0_QUALMODE0(0);

	// Finally enable the controller
	I2C_SLAVE_DEV->CFG     |= I2C_CFG_SLVEN(1u);

	// Ensure that the I2C interrupt is enabled
	NVIC_EnableIRQ(I2C_SLAVE_NVIC_IRQn);
}

//---------------------------------------------------------------------------------------------------------------------
void Eep_I2CStopSlave(void)
{
	// Stop the I2C slave block and disable all slave
	I2C_SLAVE_DEV->CFG      &= ~I2C_CFG_SLVEN_MASK;
	I2C_SLAVE_DEV->INTENCLR  = I2C_SLAVE_IRQ_FLAGS;

	// Reset the slave to ready state
	gSlave.state = kEep_SlaveReady;
	gSlave.reg_addr = 0;
}

//---------------------------------------------------------------------------------------------------------------------
void Eep_I2CSlaveIrqHandler(void)
{
	const uint32_t stat = I2C_SLAVE_DEV->STAT;

	if ((stat & I2C_STAT_SLVDESEL_MASK) != 0U)
	{
		// Slave de-select event
		gSlave.state = kEep_SlaveReady;

		// Clear the slave de-select status bit
		I2C_SLAVE_DEV->STAT = I2C_STAT_SLVDESEL_MASK;
	}

	if ((stat & I2C_STAT_SLVPENDING_MASK) != 0)
	{
		// We have pending slave activity
		const uint32_t slvstate = (stat & I2C_STAT_SLVSTATE_MASK) >> I2C_STAT_SLVSTATE_SHIFT;

		if (slvstate == I2C_STAT_SLVST_ADDR)
		{
			// Slave Address Matched (we have seen a start condition)

			// Advance to sub-address state
			gSlave.state = kEep_SlaveAddress;

			// Continue the I2C transaction
			I2C_SLAVE_DEV->SLVCTL = I2C_SLVCTL_SLVCONTINUE_MASK;
		}
		else if (slvstate == I2C_STAT_SLVST_RX)
		{
			// Slave Receive (data is available)
			const uint8_t rx_data = I2C_SLAVE_DEV->SLVDAT;

			if (gSlave.state == kEep_SlaveAddress)
			{
				// Write to sub-address register (first write after address match)
				gSlave.reg_addr = rx_data;

				// Advance to data write state.
				gSlave.state = kEep_SlaveDataWrite;
			}
			else
			{
				// Data write from the master
				const uint8_t rx_addr = gSlave.reg_addr++;

				// Stay in data write state
				gSlave.state = kEep_SlaveDataWrite;

				// Process incoming data
				Eep_ByteWriteCallback(rx_addr, rx_data);
			}

			// Continue the I2C transaction
			I2C_SLAVE_DEV->SLVCTL = I2C_SLVCTL_SLVCONTINUE_MASK;
		}
		else if (slvstate == I2C_STAT_SLVST_TX)
		{
			// Slave Transmit (data can be transmitted)
			const uint8_t tx_addr = gSlave.reg_addr++;

			// Respond with one byte of data from the current address
			I2C_SLAVE_DEV->SLVDAT = Eep_ByteReadCallback(tx_addr);

			// Continue the transaction
			I2C_SLAVE_DEV->SLVCTL = I2C_SLVCTL_SLVCONTINUE_MASK;
		}
		else
		{
			// Reserved slave state
			//
			// Something weird happened (e.g. hardware lockup). We cannot safely resume
			// device operation, and therefore trigger a HAL halt/panic event.
			Hal_Halt();
		}
	}
}
