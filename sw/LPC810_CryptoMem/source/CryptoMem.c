/**
 * @file
 * @brief CryptoMem main
 */
#include <Hal.h>
#include <Eep.h>
#include <Sha256.h>

/**
 * @file
 * @brief Command dispatcher
 */
#include <Eep.h>
#include <Sha256.h>

// 0x000 |    RET_2   |    RET_1   |   RET_0    |    STAT    |    CMD     |   ARG_2    |   ARG_1    |   ARG_0    |

//---------------------------------------------------------------------------------------------------------------------
//
//       |         +7 |         +6 |         +5 |         +4 |         +3 |         +2 |         +1 |         +0 |
// ======+============+============+============+============+============+============+============+============+
// 0x000 |   DATA[639:0]                                                                                         |
// 0x008 |                                                                                                       |
// 0x010 |                                                                                                       |
// 0x018 |                                                                                                       |
// 0x020 |                                                                                                       |
// 0x028 |                                                                                                       |
// 0x030 |                                                                                                       |
// 0x038 |                                                                                                       |
// 0x040 |                                                                                                       |
// 0x048 |                                                                                                       |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x050 |    RET_2   |    RET_1   |   RET_0    |    STAT    |    CMD     |   ARG_2    |   ARG_1    |   ARG_0    |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x058 | VOLATILE_LOCKS[31:0]                              | VOLATILE_BITS[31:0]                               |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x060 | VOLATILE_COUNTER_1[31:0]                          | VOLATILE_COUNTER_0[31:0]                          |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x068 | RFU (WI/RAZ)                                                                                          |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x070 |                                                                                                       |
// 0x078 |                                                                                                       |
// 0x080 |                                                                                                       |
// 0x088 |                                                                                                       |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x090 | PCR_0[255:0]                                                                                          |
// 0x098 |                                                                                                       |
// 0x0A0 |                                                                                                       |
// 0x0A8 |                                                                                                       |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x0B0 | PCR_1[255:0]                                                                                          |
// 0x0B8 |                                                                                                       |
// 0x0C0 |                                                                                                       |
// 0x0C8 |                                                                                                       |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x0D0 | PCR_2[255:0]                                                                                          |
// 0x0D8 |                                                                                                       |
// 0x0E0 |                                                                                                       |
// 0x0E8 |                                                                                                       |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+
// 0x0F0 | DEVICE_UID[127:0]                                                                                     |
// 0x0F8 |                                                                                                       |
// ------+------------+------------+------------+------------+------------+------------+------------+------------+

//
// STAT: Status register
//     0xFF - Busy (Command Execution)
//     0xC3 - Ready for Next Command (Response Available)
//
//     (any other value indicates an internal processing error)
//
//     Write attempts to the status register are ignored.
//

//
// Write attempts to the ARG_x and CMD registers are ignored while a command is executing
//

// ARG_0, ARG_1: Command defined input arguments (see command description). Input argument registers are automatically
//     cleared when a command starts executing.
//
//
// ARG_2: User defined input argument (mirrored to RET_2); this argument register is mirrored to the RET_2
//     return register upon completion of the associated command. The command handler itself ignores this
//     this register. The user of the device can use this register to have simple sequence numbers, if unused
//     it can always be as zero.
//
//     The argument register is automatically cleared when the command starts executing.
//
// RET_1: Return code of the previous command (zero indicates successful execution)
//
// RET_2: User defined return register (mirror of ARG_2); this register mirrors the value that is received
//     form ARG_2 of the associated command (after completion of the command).
//

// Command: 0x00 - No Operation / Clear Data
//  The NOP command clears the DATA register
//  This command explicitly clears the DATA, CMD and ARG_0-ARG_2 areas
//


typedef union {
	struct
	{

		uint8_t DATA[80u];

		uint8_t ARG_0;
		uint8_t ARG_1;
		uint8_t ARG_2;
		uint8_t CMD;
		uint8_t STAT;
		uint8_t RET_0;
		uint8_t RET_1;
		uint8_t RET_2;

		volatile uint32_t VOLATILE_BITS;
		volatile uint32_t VOLATILE_LOCKS;

		uint32_t VOLATILE_COUNTER[2u];

		uint8_t RFU[40u];

		uint8_t PCR[3u][SHA256_HASH_LENGTH_BYTES];

		uint32_t DEVICE_UID[4];
	} regs;

	uint8_t raw[256u];
} CryptoMem_IoMem_t;

/**
 * @brief I/O memory structure
 */
static CryptoMem_IoMem_t gIoMem;

/**
 * @brief Mutex for the I/O memory structure
 *
 * Write access to the I/O memory structure (from the EEP write callback) is only allowed when no command
 * is running (write accesses are ignored when commands are active).
 */
static volatile bool gCommandActive;

_Static_assert(sizeof(gIoMem.raw) == 256u, "Size of I/O register structure (raw view) must be exactly 256 bytes.");
_Static_assert(sizeof(gIoMem.regs) == 256u, "Size of  I/O register structure (bitfield view) must be exactly 256 bytes.");


#define IOMEM_STAT_BUSY  UINT8_C(0xFFu)
#define IOMEM_STAT_READY UINT8_C(0xC3u)

/**
 * @brief Gets the byte-offset of a named register in the I/O memory block
 */
#define IOMEM_REG_OFF(name) (__builtin_offsetof(CryptoMem_IoMem_t, regs.name))


//---------------------------------------------------------------------------------------------------------------------
/**
 * NV memory
 */
typedef union {
	struct {
		/**
		 * @brief NV valid marker
		 */
		volatile uint32_t marker;

		uint8_t rfu[28u];

		/**
		 * @brief Device Root Key
		 */
		uint8_t root_key[32u];
	} fields;

	uint8_t raw[64u];
} CryptoMem_Nv_t;

HAL_NV_DATA const CryptoMem_Nv_t gNv =
{
	.fields =
	{
			.marker = UINT32_C(0xAACCEE55),

			.root_key = "don't feed the bugs!"
	}
};

_Static_assert(sizeof(gIoMem.raw) == 256u, "Size of I/O register structure (raw view) must be exactly 256 bytes.");
_Static_assert(sizeof(gIoMem.regs) == 256u, "Size of  I/O register structure (bitfield view) must be exactly 256 bytes.");


//---------------------------------------------------------------------------------------------------------------------
uint8_t Eep_ByteReadCallback(uint8_t address)
{
	// We always allow read access to the IO memory area
	switch (address)
	{
	case IOMEM_REG_OFF(STAT):
		// We always report report IOMEM_STAT_BUSY if there is an active command
		return gCommandActive ?	IOMEM_STAT_BUSY : gIoMem.raw[address];

	default:
		// Block reads below ARG_0 if there is a pending command
		//
		// (DATA may be used as scratchpad by the command handler)
		//
		// Updates to the
		if (address < IOMEM_REG_OFF(ARG_0))
		{
			if (gCommandActive)
			{
				return UINT8_C(0);
			}

		}

		// Allow the read (no command pending)
		return gIoMem.raw[address];
	}
}

//---------------------------------------------------------------------------------------------------------------------
void Eep_ByteWriteCallback(uint8_t address, uint8_t data)
{
	// Write access is only allowed when no command is active
	//
	// Any writes to the CMD register trigger activation of a command
	switch (address)
	{
	case IOMEM_REG_OFF(CMD):
		if (!gCommandActive)
		{
			// Update the status and RET registers (this ensures that we __always__ read consistent values
			// even if the next I2C read interrupt comes before we dispatch)
			gIoMem.regs.CMD = data;
			gIoMem.regs.STAT = IOMEM_STAT_BUSY;
			gIoMem.regs.RET_0 = 0u;
			gIoMem.regs.RET_1 = 0u;
			gIoMem.regs.RET_2 = 0u;

			// Write to the CMD register (activate a new command) and update
			gCommandActive = true;

			// Issue memory barrier (also for compiler) and set the event flag
			__DMB();
			__SEV();
		}
		break;

	case IOMEM_REG_OFF(VOLATILE_LOCKS) + 0u:
	case IOMEM_REG_OFF(VOLATILE_LOCKS) + 1u:
	case IOMEM_REG_OFF(VOLATILE_LOCKS) + 2u:
	case IOMEM_REG_OFF(VOLATILE_LOCKS) + 3u:
		// Update volatile lock bits (allow toggle from 0->1)
		{
			const uint8_t old_lock = gIoMem.raw[address];
			gIoMem.raw[address] = (uint8_t) (old_lock | data);
		}
		break;

	case IOMEM_REG_OFF(VOLATILE_BITS) + 0u:
	case IOMEM_REG_OFF(VOLATILE_BITS) + 1u:
	case IOMEM_REG_OFF(VOLATILE_BITS) + 2u:
	case IOMEM_REG_OFF(VOLATILE_BITS) + 3u:
		// Update volatile value bits (allow value changes only where lock bit is 0)
		//
		{
			const uint8_t lock_mask = gIoMem.raw[address - IOMEM_REG_OFF(VOLATILE_BITS) + IOMEM_REG_OFF(VOLATILE_LOCKS)];
			const uint8_t old_value = gIoMem.raw[address];

			gIoMem.raw[address] = (uint8_t) ((old_value & lock_mask) | (data & ~lock_mask));
		}
		break;

	default:
		// Allow the write if the address is below the "STAT" field
		if (address < IOMEM_REG_OFF(STAT))
		{
			if (!gCommandActive)
			{
				// Update the raw view of the I/O memory space
				gIoMem.raw[address] = data;
			}
		}
		break;
	}
}


//---------------------------------------------------------------------------------------------------------------------
void CryptoMem_Init(void)
{
	// I setup of the I/O memory structure
	__builtin_memset(&gIoMem, 0u, sizeof(gIoMem));

	// Latch the device ID
	Hal_ReadDeviceID(&gIoMem.regs.DEVICE_UID[0u]);

	gIoMem.regs.STAT = IOMEM_STAT_READY; // We are ready for operation
	gCommandActive = false; // No commands are active
}

//---------------------------------------------------------------------------------------------------------------------
static uint8_t CryptoMem_FinishCommandWithData(uint8_t result, size_t size)
{
	// Clear the data area
	__builtin_memset(&gIoMem.regs.DATA[0] + size, 0, sizeof(gIoMem.regs.DATA) - size);

	// Clear the command
	gIoMem.regs.CMD = 0u;

	// RET_1 is currently not used
	gIoMem.regs.RET_0 = result;

	// Copy ARG_2 to RET_2
	gIoMem.regs.RET_2 = gIoMem.regs.ARG_2;
	gIoMem.regs.ARG_0 = 0u;
	gIoMem.regs.ARG_1 = 0u;
	gIoMem.regs.ARG_2 = 0u;

	// Signal that we are ready again
	gIoMem.regs.STAT = IOMEM_STAT_READY;
	__DMB();

	gCommandActive = false;

	return result;
}

//---------------------------------------------------------------------------------------------------------------------
static uint8_t CryptoMem_FinishCommand(uint8_t result)
{
	return CryptoMem_FinishCommandWithData(result, 0u);
}


//---------------------------------------------------------------------------------------------------------------------
//
// Command: 0xE0 - Extend PCR
//   Input:
//     ARG_0: Target PCR Index and additional data to be extended
//      [7:4] Reserved (must be zero; non-zero values trigger a parameter error)
//      [3:0] Target PCR index (valid indices are 0-2, invalid indices trigger a parameter error)
//
//     ARG_1: Length of data to be extended (0-80 bytes; data provided in DATA field)
//
//  Output:
//     RET_0: Return code from command
//          0x00 - Command completed successfully
//          0xE1 - Parameter error
//
//     RET_1: Reserved (set to zero)
//
static uint8_t CryptoMem_HandleExtend(void)
{
	const uint8_t pcr_index = gIoMem.regs.ARG_0;
	const uint8_t extend_len = gIoMem.regs.ARG_1;

	if ((pcr_index > 2) || (extend_len > sizeof(gIoMem.regs.DATA)))
	{
		// Parameter error
		return CryptoMem_FinishCommand(0xE1u);
	}

	// Compute the new PCR value
	Sha256_Init();
	Sha256_Update(&gIoMem.regs.PCR[pcr_index][0], SHA256_HASH_LENGTH_BYTES);
	Sha256_Update(&gIoMem.regs.DATA[0], extend_len);
	Sha256_Final(&gIoMem.regs.PCR[pcr_index][0]);

	return CryptoMem_FinishCommand(0x00u);
}

//---------------------------------------------------------------------------------------------------------------------
//
// Command: 0xA0 - Quote PCRs
//   Input:
//     ARG_0: PCR bitmask to be quoted
//        [7] Include the device UUID
//        [6] Include the lockable (volatile) bits
//        [5] Include the lockable (volatile) counter #1
//        [4] Include the lockable (volatile) counter #0
//        [3] Replace the PCR #0 content with the quote result.
//        [2] Include PCR #2
//        [1] Include PCR #1
//        [0] Include PCR #0
//
//     ARG_1: Length of data to be included from the DATA area (0-80 bytes; data provided in DATA field)
//
//  Output:
//     RET_0: Return code from command
//          0x00 - Command completed successfully
//          0xE1 - Parameter error
//
//     RET_1: Reserved (set to zero)
//
static uint8_t CryptoMem_HandleQuote(void)
{
	const uint8_t pcr_mask = gIoMem.regs.ARG_0;
	const uint8_t extend_len = gIoMem.regs.ARG_1;

	if (extend_len > sizeof(gIoMem.regs.DATA))
	{
		// Parameter error
		return CryptoMem_FinishCommand(0xE1u);
	}

	// Quote as HMAC over the PCRs (and extra data - if needed)
	Sha256_HmacInit(&gNv.fields.root_key[0u], sizeof(gNv.fields.root_key));

	// "quot" marker, pcr mask and head data
	{
		uint32_t header[1u + 1u + 4u + 2u + 2u];
		uint32_t *item = &header[0];

		// Block IRQs (to ensure that the volatile I/O regs don't change)
		__disable_irq();

		*item++ = __REV(0x71756F74); // "quot"
		*item++ = pcr_mask;          // PCR mask

		// Device UUID
		if (0u != (pcr_mask & 0x80u))
		{
			*item++ = gIoMem.regs.DEVICE_UID[0u];
			*item++ = gIoMem.regs.DEVICE_UID[1u];
			*item++ = gIoMem.regs.DEVICE_UID[2u];
			*item++ = gIoMem.regs.DEVICE_UID[3u];
		}

		// Include the volatile locks (if selected)
		if (0u != (pcr_mask & 0x40))
		{
			*item++ = gIoMem.regs.VOLATILE_BITS;
			*item++ = gIoMem.regs.VOLATILE_LOCKS;
		}

		// Include the volatile counters
		if (0u != (pcr_mask & 0x20))
		{
			*item++ = gIoMem.regs.VOLATILE_COUNTER[1u];
		}

		if (0u != (pcr_mask & 0x10))
		{
			*item++ = gIoMem.regs.VOLATILE_COUNTER[0u];
		}

		// Enable IRQs again
		__enable_irq();

		Sha256_HmacUpdate(header, (item - header) * sizeof(uint32_t));
	}

	// All selected PCRs
	for (size_t i = 0u; i < 3u; ++i)
	{
		if (((pcr_mask >> i) & 1u) != 0u)
		{
			Sha256_HmacUpdate(&gIoMem.regs.PCR[i], SHA256_HASH_LENGTH_BYTES);
		}
	}

	// And the user-supplied extra data
	Sha256_HmacUpdate(&gIoMem.regs.DATA[0], extend_len);

	// Finalize the HMAC
	Sha256_HmacFinal(&gIoMem.regs.DATA[0]);

	// Replace PCR #0 content with the hash of the quote
	//
	// Why do we use the hash of the quote? ==> we want to record the fact that the PCR has changed!
	if (0u != (pcr_mask & 0x04u))
	{
		Sha256_Init();
		Sha256_Update(&gIoMem.regs.DATA[0u], SHA256_HASH_LENGTH_BYTES);
		Sha256_Final(&gIoMem.regs.PCR[0u][0u]);
	}

	return CryptoMem_FinishCommandWithData(0x00u, SHA256_HASH_LENGTH_BYTES);
}

//---------------------------------------------------------------------------------------------------------------------
//
// Command: 0xC0 - Increment Counter
//   Input:
//     ARG_0: Target counter index (0-1 is value; invalid index triggers a parameter error)
//     ARG_1: Increment value (0-255; integer overflow triggers a counter error)
//
//  Output:
//     RET_0: Return code from command
//          0x00 - Command completed successfully
//          0xE1 - Parameter error
//          0xE3 - Counter increment failed
//
//     RET_1: Reserved (set to zero)
//

static uint8_t CryptoMem_HandleIncrement(void)
{
	const uint8_t counter_index = gIoMem.regs.ARG_0;
	const uint32_t increment = gIoMem.regs.ARG_1;

	if (counter_index > 1u)
	{
		// Parameter error
		return CryptoMem_FinishCommand(0xE1u);
	}

	// Increment the counter
	const uint32_t old_value = gIoMem.regs.VOLATILE_COUNTER[counter_index];
	if ((UINT32_MAX - old_value) < increment)
	{
		// Counter overflow
		return CryptoMem_FinishCommand(0xE3u);
	}

	gIoMem.regs.VOLATILE_COUNTER[counter_index] = old_value + increment;

	return CryptoMem_FinishCommand(0x00u);
}

//---------------------------------------------------------------------------------------------------------------------
//
// Command: 0xFA - Enter Field Update Mode (ISP Entry)
//
// TODO: Authentication
//
//   Input:
//     ARG_0: Should be 0x69 ('i')
//     ARG_1: Should be 0x73 ('s')
//     ARG_2: Should be 0x70 ('p')
//
//  Output:
//     RET_0: Return code from command
//          0xE1 - Parameter error
//          0xE4 - Command execution failed
//          0xE5 - Command not allowed in this device state
//
//  RET_1: Reserved (set to zero)
//

static uint8_t CryptoMem_HandleFieldUpdate(void)
{
	if (gNv.fields.marker == UINT32_C(0xAACCEE55))
	{
		// Enter ISP mode (only returns on failure)
		Hal_EnterBootloader((uint32_t *)&gIoMem.regs.DATA[0]);

		return CryptoMem_FinishCommandWithData(0xE4u, 4u);
	}
	else
	{
		// No maintenance allowed
		return CryptoMem_FinishCommand(0xE5u);
	}
}

//---------------------------------------------------------------------------------------------------------------------
//
// Command: 0xF1 - Write to NV memory
//
// TODO: Authentication
//
//   Input:
//     ARG_0: Should be 0x6E ('n')
//     ARG_1: Should be 0x76 ('v')
//     ARG_2: Should be 0x21 ('!')
//
//     DATA[0x00..0x3F]: Data to be written on the NV config page
//
//  Output:
//     RET_0: Return code from command
//          0x00 - Success
//          0xE4 - Command execution failed
//          0xE5 - Command not allowed in this device state
//
//     RET_1: Reserved (set to zero)
//

static uint8_t CryptoMem_HandleNvWrite(void)
{
	if (gNv.fields.marker == UINT32_C(0xAACCEE55))
	{
		// Write to the flash
		if (!Hal_NvWrite(&gNv, &gIoMem.regs.DATA[0u]))
		{
			return CryptoMem_FinishCommand(0xE4);
		}

		return CryptoMem_FinishCommand(0x00u);
	}
	else
	{
		// No maintenance allowed
		return CryptoMem_FinishCommand(0xE5u);
	}
}

//---------------------------------------------------------------------------------------------------------------------
static uint8_t CryptoMem_HandleNop(void)
{
	return CryptoMem_FinishCommand(0x00u);
}

//---------------------------------------------------------------------------------------------------------------------
void CryptoMem_HandleCommand(void)
{
	// Dispatch the command
	switch (gIoMem.regs.CMD)
	{
	case 0x00u: // No operation
		(void) CryptoMem_HandleNop();
		break;

	case 0xA0u: // Quote
		(void) CryptoMem_HandleQuote();
		break;

	case 0xE0u: // Extend PCR
		(void) CryptoMem_HandleExtend();
		break;

	case 0xC0: // Increment counter
		(void) CryptoMem_HandleIncrement();
		break;

	case 0xFA: // ISP entry
		(void) CryptoMem_HandleFieldUpdate();
		break;

	case 0xF5: // Write our NV flash configuration
		(void) CryptoMem_HandleNvWrite();
		break;

	default:
		// Unknown command
		(void) CryptoMem_FinishCommand(0xE2u);
		break;
	}
}

//---------------------------------------------------------------------------------------------------------------------
int main(void)
{
	// Initialize the HAL layer
	Hal_Init();

	// Initialize the command layer
	CryptoMem_Init();

	// Finally start the I2C slave (after this point commands can be received at any time)
  	Eep_I2CStartSlave();

  	while (true)
  	{
  		// Signal that we are ready
  		Hal_SetReadyPin(true);

  		// Sleep while no command is active
  		while (!gCommandActive)
  		{
  			// System is idle (wait for interrupt)
  			Hal_Idle();
  		}

  		// Command processing starts
  		Hal_SetReadyPin(false);

  		// We now have an active command
  		CryptoMem_HandleCommand();
  	}

  	Hal_Halt();

  	// Not reachable (Hal_Halt does not return)
  	__builtin_unreachable();
}

