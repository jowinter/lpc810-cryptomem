/**
 * @file
 * @brief Compile-time configuration of the LPC810 CryptoMem example project
 */
#ifndef CONFIG_H_
#define CONFIG_H_


// I2C wired interface option
#define CONFIG_WIRED_IF_I2C  0x01

// UART wired interface option
#define CONFIG_WIRED_IF_UART 0x02

// Select the wired interface option (default to UART if not set)
#if !defined(CONFIG_WIRED_IF_TYPE)
# define CONFIG_WIRED_IF_TYPE CONFIG_WIRED_IF_UART
#endif

#endif /* CONFIG_H_ */
