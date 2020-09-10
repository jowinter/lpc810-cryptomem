# Minimalistic Embedded Trusted Platform Module (on the NXP LPC810)

## Motivation
This toy firmware project aims at two main goals: Firstly, we demonstrate that non-trivial cryptographic functionality can be implemented even on
tiny ARM Cortex-M0+ microcontrollers such as the NXP LPC810. The firmware provides all primitives to implement a simple, yet fully functional, remote
attestation mechanism given the constraints of a tiny MCU.

The secondary goal of this project is to have a small reference environment (hardware plus firmware) for experimenting with side-channel analysis and
fault-attacks. In its current form the "LPC810 CryptoMem" firmware does *on purpose* not implement any hardening against side-channel or fault attacks:

* The SHA256 (and HMAC) implementation used in the firmware are pure text-book implementations without any countermeasures. Significant side-channel leakage
  is to be expected (and is expected to make a good side-channel target for further analysis with a ChipWhisperer).
  
* The I2C interface and the supported commands do not contany any *intentional* logic bugs. All commands are should be safe against buffer and integer overflows.
  (At a later stage we may consider to formally verify properties of selected parts of the implementation through Frama-C).
  
* The "Device Initialization" and "Firmware Update / Entry to ROM Bootloader" commands implement simple access policy checks (device locked?). These checks
  are realized using normal if statements without any hardening against fault-attacks (and therefore make a good first candidate for fault-injection through
  clock/power glitches using a ChipWhisperer).

## Firmware Overview
The "LPC810 CryptoMem" firmware implements a minimalistic embedded Trusted Platform Module (TPM) on based NXP LPC810 microcontroller. The LPC810 MCU is
an ARM Cortex-M0+ based embedded SoC providing 4K of in-system programmable flash memory, and 1K of SRAM. The IC is available in multiple packaging
options, including an through-hole mounted 8-pin DIP package.

The firmware in this repository implements a set of crypto primitives similar to a Trusted Platform Module. The focus of the command set supported by the
firmware is on remote attestation using HMAC-SHA256 as (symmetric) signature algorithm. Major firmware facilities include:

   * An I2C slave interface with a 256-byte register space. The I2C protocol used by the slave interface is compatible with AT24Cxx-style I2C EEPROMs
     such as the AT24C02 or the PCF8570. Communication with the I2C slave interface is possible through standard Linux I2C utilities like `i2c-utils` or `eeprog`.
     (It should also be possible to directly interact with the firmware through the unmodified `at24` and `eeprom` kernel drivers)
   
   * Platform Configuration Registers: Three 256-bit platform configuration registers (PCR0-PCR2) are implemented. The PCRs reset to all zero values on reset of the IC. The PCRs
     can be read directly through the register space. All three PCRs can be modified through a SHA256 based PCR `Extend` command. The `Extend` command takes the contatenation
     of the current (old) value of a PCR, appends user-provided data, and hashes the concatenation using the SHA256 algorithm to to form the new value of the PCR.
     
   * Volatile Counters: Two volatile 32-bit increment only counters are implemented. These counters reset to zero on IC reset, and can only be incremented afterwards.
     The current counter values can be read directly through the register space of the I2C slave. Current counter values can be included into the signed data blob that
     is produced by the `Quote` command.
     
   * Volatile Lockable Bits: The firmware provides a volatile 32-bit word with bit-wise lock functionality. Upon reset all value bits and there corresponding lock bits are
     reset to zero. Bit values can be modified by writing into the I2C register space of slave device. A lock bit mask controls whether individual value bits can be further   
     modified; once a lock bit is set the lock bit and its corresponding value bit become read-only until the device is reset again. The lock bit values and the lock mask can
     be included in the signed data blob that is produced by the `Quote` command.
    
   * Unique Device Identifier: Each LPC810 IC comes with a unique 128-bit device identifier that can be read via the LPC810 ROM API. This unique 128-bit device identifier is    
     visible through the I2C register address space. It can be included in the signed data blob that is produced by the `Quote` command.
     
   * Remote Attestation (Quote): Remote attestation using the "LPC810 CryptoMem" is possible through a HMAC-SHA256 based `Quote` command. The `Quote` command allows the user
     to select a subset of PCRs #0-2 and of the two volatile counters. Additionally the volatile lockable bits (value and lock mask), and/or the IC's unique device identifier
     can be included in the quote blob. Finally the user can provide up to 80 bytes of arbitrary data to be included in the quote. The `Quote` command formats a byte blob
     according to the user's selections and signs the blob using the HMAC-SHA256 algorithm. The secret signing key is a 256-bit key stored in the flash of the IC itself (the
     key cannot be read back via the I2C slave interface; the LPC810's code readout protection can be enabled to prevent readback of the key through SWD or through the ROM's
     UART-base ISP interfaced)
     
   * Device Initialization: The precompiled firmware image does not contain any secret key material. A dedicated device initialization command exists to provision the secret 
     signing key used by the `Quote` operation to the device. This command can be executed once (up to the next full flash erase) in the life-time of the device. Upon successful
     provisioning the device can be locked against further calls to the device initialization command and the firmware update command.
     
   * Firmware Update / Entry to ROM Bootloader: The LPC810 MCU comes with an integrated ROM-resident bootloader. The firmware implements a dedicated "Enter ISP Bootloader"
     command to allow activation of the ROM bootloader through the I2C interface (once the bootloader is activated, firmware can be reflashed through the UART ISP interface.
     Execution of the bootloader activation command is only allowed if the device has not been locked as part of the key provisioning process through the "device Initialization"
     command.
     
