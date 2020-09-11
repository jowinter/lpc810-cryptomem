/**
 * @file
 * @brief EEPROM-style I2C Slave Interface
 *
 */

#include <Eep.h>

#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"

#include "fsl_i2c.h"

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
	 * @brief Slave is awaiting the register addresss.
	 */
	kEep_SlaveAddress,

	/**
	 * @brief Setup phase byte write
	 */
	kEep_SlaveWriteSetup,

	/**
	 * @brief Data phase of byte write.
	 */
	kEep_SlaveWriteData,

	/**
	 * @brief Setup phase byte read
	 */
	kEep_SlaveReadSetup,

	/**
	 * @brief Data phase of byte read
	 */
	kEep_SlaveReadData,

	/**
	 * @brief Slave error state.
	 */
	kEep_SlaveError
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

	/**
	 * @brief Page address of current page write operation.
	 */
	uint8_t page_addr;

	/**
	 * @brief Receive length of the page buffer
	 */
	uint8_t page_len;

	/**
	 * @brief Slave page buffer.
	 */
	uint8_t page_buf[8u];
} Eep_Slave_t;

/**
 * @brief Global state of the EEPROM-style I2C slave
 */
static Eep_Slave_t gSlave;

//---------------------------------------------------------------------------------------------------------------------
void Eep_I2CStartSlave(void)
{
	// Ensure that any ongoing slave activities are halted
	Eep_I2CStopSlave();

    // Start the I2C slave
    I2C_SlaveTransferNonBlocking(I2C0_PERIPHERAL, &I2C0_handle, kI2C_SlaveAddressMatchEvent);
}

//---------------------------------------------------------------------------------------------------------------------
void Eep_I2CStopSlave(void)
{
	// Stop any pending I2C transfers
	I2C_SlaveTransferAbort(I2C0_PERIPHERAL, &I2C0_handle);

	// Reset the slave to ready state
	gSlave.state = kEep_SlaveReady;
	gSlave.reg_addr = 0u;
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief Setup a byte read operation.
 */
static void Eep_I2CSetupByteRead(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	uint8_t address = gSlave.reg_addr;

	// Get the read-back data
	gSlave.page_buf[0u] = Eep_ByteReadCallback(address);

	// Advance the read address and the FSM state
	gSlave.reg_addr = (gSlave.reg_addr + 1u) & 0xFFu;
	gSlave.state = kEep_SlaveReadData;

	// Setup a single-byte transfer
	I2C_SlaveSetSendBuffer(base, transfer, &gSlave.page_buf[0u], 1u, (kI2C_SlaveCompletionEvent | kI2C_SlaveDeselectedEvent));
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief Setup a byte write operation.
 */
static void Eep_I2CSetupByteWrite(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	// Latch the page offset
	uint8_t offset = gSlave.reg_addr - gSlave.page_addr;

	// Advance to data stage (adjustment of addresses is handled there)
	gSlave.state = kEep_SlaveWriteData;

	// Setup a single-byte transfer
	I2C_SlaveSetReceiveBuffer(base, transfer, &gSlave.page_buf[offset], 1u, (kI2C_SlaveCompletionEvent | kI2C_SlaveDeselectedEvent));
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief Completes a byte write operation.
 */
static void Eep_I2CCompleteByteWrite(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	// Latch the page offset
	uint8_t offset = gSlave.reg_addr - gSlave.page_addr;

	// Advance the page offset (modulo 8) and re-align the register address
	offset = (offset + 1u) % sizeof(gSlave.page_buf);
	gSlave.reg_addr = gSlave.page_addr + offset;

	// Increment the page write length
	if (gSlave.page_len < sizeof(gSlave.page_buf))
	{
		gSlave.page_len += 1u;
	}

	// Advance to setup stage (after completion)
	gSlave.state = kEep_SlaveWriteSetup;
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief Completes a byte (or page) write operation upon reception of the stop condition.
 */
static void Eep_I2CFinalizeWrite(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	// An AT24Cxx style EEPROM typically would only interpret 1 byte and 8 byte writes
	//
	// We accept any write sequence with length between 0 and 8 bytes, and break it down into a sequence of
	// single byte write callback invocations.

	for (unsigned i = 0u, len = gSlave.page_len; i < len; ++i)
	{
		Eep_ByteWriteCallback((gSlave.page_addr + i) & 0xFFu, gSlave.page_buf[i]);
	}

	// Reset the page length
	gSlave.page_len = 0u;

	// Advance to ready stage (after completion)
	gSlave.state = kEep_SlaveReady;
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief State handler for the "ready" state.
 */
static void Eep_ReadyStateHandler(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	const i2c_slave_transfer_event_t event = transfer->event;

	if (event == kI2C_SlaveReceiveEvent)
	{
		// Master is writing the address byte
		//
		// This is the start of a new "Byte Write", "Page Write", or "Random Read" transaction.
		// We latch the first incoming byte as new register address.
		//
		I2C_SlaveSetReceiveBuffer(base, transfer, &gSlave.reg_addr, 1, (kI2C_SlaveCompletionEvent | kI2C_SlaveDeselectedEvent));
		gSlave.state = kEep_SlaveAddress;
	}
	else if (event == kI2C_SlaveTransmitEvent)
	{
		// Master is reading data from this slave
		//
		// This is the start of a new "Current Address Read" or "Sequential Read" transaction (or the
		// read phase of a "Random Read" - which for all practical purposes can be handled like a sequential read).
		Eep_I2CSetupByteRead(base, transfer);
	}
	else
	{
		// Unexpected event (should not happen with current I2C slave driver)
		gSlave.state = kEep_SlaveError;
	}
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief State handler for the "address" state.
 */
static void Eep_AddressStateHandler(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	const i2c_slave_transfer_event_t event = transfer->event;

	if (event == kI2C_SlaveCompletionEvent)
	{
		// We have received a new slave address, and can advance to the write setup state.
		//
		// We latch the "page address" for the completion of the write phase.
		gSlave.page_addr = gSlave.reg_addr;
		gSlave.page_len = 0u;

		gSlave.state = kEep_SlaveWriteSetup;
	}
	else
	{
		// Unexpected event (should not happen with current I2C slave driver)
		//
		// We advance to the error state
		gSlave.state = kEep_SlaveError;
	}
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief State handler for the "read-setup" state.
 */
static void Eep_ReadSetupStateHandler(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	const i2c_slave_transfer_event_t event = transfer->event;

	if (event == kI2C_SlaveTransmitEvent)
	{
		// The master is requesting the next data byte
		Eep_I2CSetupByteRead(base, transfer);
	}
	else if (event == kI2C_SlaveDeselectedEvent)
	{
		// The master has issued a stop condition
		gSlave.state = kEep_SlaveReady;
	}
	else
	{
		// Unexpected event (should not happen with current I2C slave driver)
		//
		// We advance to the error state
		gSlave.state = kEep_SlaveError;
	}
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief State handler for the "read-data" state.
 */
static void Eep_ReadDataStateHandler(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	const i2c_slave_transfer_event_t event = transfer->event;

	if (event == kI2C_SlaveCompletionEvent)
	{
		// Slave data transmission is complete, go back to setup state
		gSlave.state = kEep_SlaveReadSetup;
	}
	else
	{
		// Unexpected event (should not happen with current I2C slave driver)
		//
		// We advance to the error state
		gSlave.state = kEep_SlaveError;
	}
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief State handler for the "write-setup" state.
 */
static void Eep_WriteSetupStateHandler(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	const i2c_slave_transfer_event_t event = transfer->event;

	if (event == kI2C_SlaveReceiveEvent)
	{
		// The master is requesting the next data byte
		Eep_I2CSetupByteWrite(base, transfer);
	}
	else if (event == kI2C_SlaveDeselectedEvent)
	{
		// The master has issued a stop condition, process the page (or byte write)
		Eep_I2CFinalizeWrite(base, transfer);
	}
	else if (event == kI2C_SlaveTransmitEvent)
	{
		// The master sent a repeated start condition, and toggled from write to read

		// Terminate the write
		Eep_I2CFinalizeWrite(base, transfer);

		// And start the read
		Eep_I2CSetupByteRead(base, transfer);
	}
	else
	{
		// Unexpected event (should not happen with current I2C slave driver)
		//
		// We advance to the error state
		gSlave.state = kEep_SlaveError;
	}
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief State handler for the "read-data" state.
 */
static void Eep_WriteDataStateHandler(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	const i2c_slave_transfer_event_t event = transfer->event;

	if (event == kI2C_SlaveCompletionEvent)
	{
		// Slave data transmission is complete, go back to setup state
		Eep_I2CCompleteByteWrite(base, transfer);
	}
	else
	{
		// Unexpected event (should not happen with current I2C slave driver)
		//
		// We advance to the error state
		gSlave.state = kEep_SlaveError;
	}
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief State handler for the "error" state.
 */
static void Eep_ErrorStateHandler(I2C_Type *base, volatile i2c_slave_transfer_t *transfer)
{
	const i2c_slave_transfer_event_t event = transfer->event;

	// We ended up here as result of a protocol error (something really weird happened)
	//
	// There is currently not much that we can do.
	if (event == kI2C_SlaveReceiveEvent)
	{
		// The master is trying to send a byte.
		//
		// We setup an empty transfer, monitor the deselect event and stay in error state.
		I2C_SlaveSetReceiveBuffer(base, transfer, NULL, 0u, kI2C_SlaveDeselectedEvent);
		gSlave.state = kEep_SlaveError;
	}
	else if (event == kI2C_SlaveTransmitEvent)
	{
		// The master is trying to read a byte.
		//
		// We setup respond with a NUL byte, monitor the deselect event and stay in error state.
		static const uint8_t kNull = 0x00u;
		I2C_SlaveSetSendBuffer(base, transfer, &kNull, 1u, kI2C_SlaveDeselectedEvent);
		gSlave.state = kEep_SlaveError;
	}
	else if (event == kI2C_SlaveDeselectedEvent)
	{
		// We received a clean stop condition. Try to recover through "ready" state.
		gSlave.state = kEep_SlaveReady;
	}
	else
	{
		// Unknown/unexpected event. We (explicitly) stay in error state.
		gSlave.state = kEep_SlaveError;
	}
}

//---------------------------------------------------------------------------------------------------------------------
void Eep_I2CHandleSlaveEvent(I2C_Type *base, volatile i2c_slave_transfer_t *transfer, void *userData)
{
	switch (gSlave.state)
	{
	case kEep_SlaveReady:
		Eep_ReadyStateHandler(base, transfer);
		break;

	case kEep_SlaveAddress:
		Eep_AddressStateHandler(base, transfer);
		break;

	case kEep_SlaveReadSetup:
		Eep_ReadSetupStateHandler(base, transfer);
		break;

	case kEep_SlaveReadData:
		Eep_ReadDataStateHandler(base, transfer);
		break;

	case kEep_SlaveWriteSetup:
		Eep_WriteSetupStateHandler(base, transfer);
		break;

	case kEep_SlaveWriteData:
		Eep_WriteDataStateHandler(base, transfer);
		break;


	case kEep_SlaveError:
	default:
		// Unexpected or unhandled state (we try best-effort recovery through the error state)
		Eep_ErrorStateHandler(base, transfer);
		break;
	}
}
