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
 * @brief Starts the I2C slave interface.
 */
extern void Eep_I2CStartSlave(void);

/**
 * @brief Stops the I2C slave interface.
 */
extern void Eep_I2CStopSlave(void);

/**
 * @brief Provides EEPROM byte read data.
 */
extern uint8_t Eep_ByteReadCallback(uint8_t address);

/**
 * @brief Processes EEPROM byte write data.
 */
extern void Eep_ByteWriteCallback(uint8_t address, uint8_t data);

#endif /* EEP_H_ */
