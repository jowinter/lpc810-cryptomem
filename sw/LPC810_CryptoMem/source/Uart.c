/**
 * @file
 * @brief UART interface (based on ChipWhisperer's simple serial v1.1 protocol)
 *
 * The (experimental) UART interface to the CryptoMem example implements a version of
 * ChipWhisperer's "SimpleSerial v1.1" protocol. We use the default baudrate (38400 baud)
 * for communication with the host.
 *
 * We map our I2C protocol to a subset of the ChipWhisperer protocol (the 'v', 'y' and 'w'
 * commands are omitted for simplicity). Packets supported by this implementation are:
 *
 * - Write to Device Register (Simulate an I2C write)
 *   Cmd: ['W', addr_hi, addr_lo, cnt_hi, cnt_lo, dat..., '\n']
 *   Rsp: ['z', 0x00, '\n' ]
 *
 * - Read from Device Register (Simulate an I2C read)
 *   Cmd: ['R', addr_hi, addr_lo, cnt_hi, cnt_lo, '\n']
 *   Rsp: ['r', dat..., '\n']
 *        ['z', 0x00', '\n' ]
 *
 * - Data bytes are transmitted/received as hex-encoded strings, i.e. 0xCA is transmitted as 'C' 'A'.
 * - Byte counters (cnt_hi/cnt_lo) count the number of raw data bytes (not the number of hex digits).
 * - Invalid data bytes/hex encodings are silently treated as 0x00
 *
 * See https://github.com/newaetech/chipwhisperer/blob/develop/docs/simpleserial.rst and
 * https://github.com/newaetech/chipwhisperer/blob/develop/hardware/victims/firmware/simpleserial/simpleserial.c for
 * details on ChipWhisperer's protocol variants.
 */
#include <Config.h>
#include <Hal.h>
#include <Eep.h> // EEPROM emulation core (read byte and write byte callbacks)

#if (CONFIG_WIRED_IF_TYPE == CONFIG_WIRED_IF_UART)

//---------------------------------------------------------------------------------------------------------------------
// LPC UART ROM driver interface (cf. UM10601, 25.4 API description)
//

// UART configuration
typedef struct UART_CONFIG
{
	uint32_t sys_clk_in_hz;   // Sytem clock in hz.
	uint32_t baudrate_in_hz;  // Baudrate in hz
	uint8_t config; //bit 1:0 // 00: 7 bits length, 01: 8 bits lenght, others: reserved
					//bit3:2  // 00: No Parity, 01: reserved, 10: Even, 11: Odd
					//bit4    // 0: 1 Stop bit, 1: 2 Stop bits

	uint8_t sync_mod; //bit0: 0(Async mode), 1(Sync mode)
					  //bit1: 0(Un_RXD is sampled on the falling edge of SCLK)  1(Un_RXD is sampled on the rising edge of SCLK)
					  //bit2: 0(Start and stop bits are transmitted as in asynchronous mode) // 1(Start and stop bits are not transmitted)
					  //bit3: 0(the UART is a slave on Sync mode) 1(the UART is a master on Sync mode)
	uint16_t error_en; //Bit0: OverrunEn, bit1: UnderrunEn, bit2: FrameErrEn, bit3: ParityErrEn, bit4: RxNoiseEn
} UART_CONFIG_T;

// UART parameter struct for uart_get_line and uart_put_line (not used in this code)
typedef struct UART_PARAM UART_PARAM_T;

// UART object handler
typedef void* UART_HANDLE_T; // define TYPE for uart handle pointer

// UART API binding structu
typedef struct UARTD_API
{
	// index of all the uart driver functions
	uint32_t      (*uart_get_mem_size)(void);
	UART_HANDLE_T (*uart_setup)(uint32_t base_addr, uint8_t *ram);
	uint32_t      (*uart_init)(UART_HANDLE_T handle, UART_CONFIG_T *set);

	//--polling functions--//
	uint8_t      (*uart_get_char)(UART_HANDLE_T handle);
	void         (*uart_put_char)(UART_HANDLE_T handle, uint8_t data);

	uint32_t (*uart_get_line)(UART_HANDLE_T handle, UART_PARAM_T * param);
	uint32_t (*uart_put_line)(UART_HANDLE_T handle, UART_PARAM_T * param);

	//--interrupt functions--//
	void (*uart_isr)(UART_HANDLE_T handle);
} UARTD_API_T;

typedef struct _ROM_API {
	const uint32_t unused[3];
	const void *pPWRD;
	const uint32_t p_dev1;
	const void *pI2CD;
	const uint32_t p_dev3;
	const uint32_t p_dev4;
	const uint32_t p_dev5;
	const UARTD_API_T *pUARTD;
} ROM_API_T;

// And ROM API alias
#define ROM_DRIVER_BASE (0x1FFF1FF8UL)
#define LPC_UART_API ((UARTD_API_T *) ((*(ROM_API_T * *) (ROM_DRIVER_BASE))->pUARTD))

// Workspace for the UART
static uint32_t gUartRam[16u];

// Global UART handle
static UART_HANDLE_T ghUart;

static void Eep_UartTxByte(uint8_t);
//---------------------------------------------------------------------------------------------------------------------
void Eep_UartStartSlave(void)
{
	// Initialize the UART handle
	ghUart = (LPC_UART_API->uart_setup)(USART0_BASE, (uint8_t *) &gUartRam[0u]);

	UART_CONFIG_T config =
	{
		.sys_clk_in_hz  = SystemCoreClock,
		.baudrate_in_hz = 38400u,
		.config         = 0x00000001u, // 8n1
		.sync_mod       = 0x00000000u, // Async mode
		.error_en       = 0x00000000u  // Ignore errors
	};

	// Start the UART
	(LPC_UART_API->uart_init)(ghUart, &config);

	// Configure the RX IRQ
	USART0->INTENSET = USART_INTENSET_RXRDYEN(1);

	NVIC_EnableIRQ(USART0_IRQn);
}

//---------------------------------------------------------------------------------------------------------------------
void Eep_UartStopSlave(void)
{
	// Disable the UART IRQ
	NVIC_DisableIRQ(USART0_IRQn);

	// Wait until pending transmissions have settled
	while (0u != (USART0->STAT & USART_STAT_TXIDLE_MASK))
	{
		__NOP();
	}

	// Disable the USART
	USART0->CFG &= ~USART_CFG_ENABLE_MASK;

	// Ensure that the interrupt sources are cleared
	USART0->STAT = USART_INTSTAT_RXRDY_MASK;

}

//---------------------------------------------------------------------------------------------------------------------
static uint8_t Eep_UartRxByte(void)
{
	return (LPC_UART_API->uart_get_char)(ghUart);
}

//---------------------------------------------------------------------------------------------------------------------
static void Eep_UartTxByte(uint8_t c)
{
	(LPC_UART_API->uart_put_char)(ghUart, c);
}

//---------------------------------------------------------------------------------------------------------------------
static void Eep_UartSendAck(uint8_t status)
{
	Eep_UartTxByte('z');
	Eep_UartTxByte(status);
	Eep_UartTxByte('\n');
}

//---------------------------------------------------------------------------------------------------------------------
static void Eep_UartWriteHexByte(uint8_t data)
{
	static const uint8_t xdigit[16u] = "0123456789ABCDEF";

	Eep_UartTxByte(xdigit[(data >> 4u) & 0x0F]);
	Eep_UartTxByte(xdigit[data & 0x0F]);
}

//---------------------------------------------------------------------------------------------------------------------
static uint32_t Eep_UartHexToNibble(uint8_t c)
{
	return (c >= '0' && c <= '9') ? (c - '0') :
		(c >= 'A' && c <= 'F') ? (c - 'A' + 0xAu) :
		(c >= 'a' && c <= 'f') ? (c - 'a' + 0xAu) : 0xFFFFFFFFu;
}

//---------------------------------------------------------------------------------------------------------------------
static uint32_t Eep_UartReadHexByte(void)
{
	uint32_t v = 0u;
	v |= Eep_UartHexToNibble(Eep_UartRxByte()) << 4u;
	v |= Eep_UartHexToNibble(Eep_UartRxByte());
	return v;
}

//---------------------------------------------------------------------------------------------------------------------
static uint8_t Eep_UartSlaveWrite(void)
{
	uint32_t reg_addr = Eep_UartReadHexByte();
	uint32_t reg_cnt  = Eep_UartReadHexByte();
	if (reg_addr > 0xFFu || reg_cnt > 0xFFu)
	{
		// Invalid command
		return 0x01;
	}

	for (uint32_t i = 0u; i < reg_cnt; ++i)
	{
		// Receive (ignore invalid command bytes)
		uint32_t wdat = Eep_UartReadHexByte();
		if (wdat > 0xFFu)
		{
			// Invalid command
			return 0x01;
		}

		Eep_ByteWriteCallback((reg_addr + i) & 0xFFu, wdat);
	}

	if ('\n' != Eep_UartRxByte())
	{
		// Invalid command
		return 0x01;
	}

	return 0x00; // OK
}

//---------------------------------------------------------------------------------------------------------------------
static uint8_t Eep_UartSlaveRead(void)
{
	uint32_t reg_addr = Eep_UartReadHexByte();
	uint32_t reg_cnt  = Eep_UartReadHexByte();
	uint32_t eoc      = Eep_UartRxByte();
	if (reg_addr > 0xFFu || reg_cnt > 0xFFu || eoc != '\n')
	{
		// Invalid command
		return 0x01;
	}

	Eep_UartTxByte('r');

	for (uint32_t i = 0u; i < reg_cnt; ++i)
	{
		/// Transmit
		uint8_t tx_byte = Eep_ByteReadCallback((reg_addr + i) & 0xFFu);
		Eep_UartWriteHexByte(tx_byte);
	}

	Eep_UartTxByte('\n');

	return 0x00; // OK
}

//---------------------------------------------------------------------------------------------------------------------
void Eep_UartIrqHandler(void)
{
	uint32_t int_stat = USART0->INTSTAT;

	if (0u != (int_stat & USART_STAT_RXRDY_MASK))
	{
		// Disarm the RX interrupt and clear the pending flag
		USART0->INTENCLR = USART_INTENCLR_RXRDYCLR_MASK;
		USART0->STAT     = USART_STAT_RXRDY_MASK;

		// Dispatch based on the command code
		uint8_t cmd = Eep_UartRxByte();
		uint8_t status = 0x01u; // Invalid command (in simple serial v2.1)

		switch (cmd)
		{
		case 'W':
			// Simulate I2C write
			status = Eep_UartSlaveWrite();
			break;

		case 'R':
			// Simulate I2C write
			status = Eep_UartSlaveRead();
			break;

		default:
			// Bad command
			status = 0x01u; // Invalid command (in simpleserial v2.1)
			break;
		}

		// Re-arm the RX interrupt
		USART0->INTENSET = USART_INTENSET_RXRDYEN_MASK;

		// Acknowledge with a status code
		Eep_UartSendAck(status);
	}
}

#endif
