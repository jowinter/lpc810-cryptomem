/**
 * @file
 * @brief EEPROM-style I2C Slave Interface
 *
 * This module implements an I2C slave interface that is modeled after AT24Cxx-style I2C EEPROMs.
 *
 */
#ifndef EEP_H_
#define EEP_H_

#include <Hal.h>

/**
 * @brief Set the I2C clock divider
 *
 * @remarks This function should be called as part of hardware initialization, before the
 *   slave is enabled for the first time. It is kept separately from the slave startup as
 *   provision for simple integration in dual-role master/slave devices.
 */
extern void Eep_I2CSetClockDivider(void);

/**
 * @brief Starts the I2C slave interface.
 */
extern void Eep_I2CStartSlave(const uint8_t i2c_addr);

/**
 * @brief Stops the I2C slave interface.
 */
extern void Eep_I2CStopSlave(void);

/**
 * @brief IRQ handler for slave interrupts.
 */
extern void Eep_I2CSlaveIrqHandler(void);

/**
 * @brief Provides EEPROM byte read data.
 */
extern uint8_t Eep_ByteReadCallback(uint8_t address);

/**
 * @brief Processes EEPROM byte write data.
 */
extern void Eep_ByteWriteCallback(uint8_t address, uint8_t data);

#endif /* EEP_H_ */
