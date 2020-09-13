/***********************************************************************************************************************
 * This file was generated by the MCUXpresso Config Tools. Any manual edits made to this file
 * will be overwritten if the respective MCUXpresso Config Tools is used to update this file.
 **********************************************************************************************************************/

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
!!GlobalInfo
product: Pins v7.0
processor: LPC810
package_id: LPC810M021FN8
mcu_data: ksdk2_0
processor_version: 7.0.1
pin_labels:
- {pin_num: '3', pin_signal: SWCLK/PIO0_3, label: SCL, identifier: SCL}
- {pin_num: '4', pin_signal: SWDIO/PIO0_2, label: SDA, identifier: SDA}
- {pin_num: '2', pin_signal: PIO0_4, label: RDY_N, identifier: RDY_N}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_swm.h"
#include "pin_mux.h"

/* FUNCTION ************************************************************************************************************
 *
 * Function Name : BOARD_InitBootPins
 * Description   : Calls initialization functions.
 *
 * END ****************************************************************************************************************/
void BOARD_InitBootPins(void)
{
    BOARD_InitPins();
}

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
BOARD_InitPins:
- options: {callFromInitBoot: 'true', coreID: core0, enableClock: 'true'}
- pin_list:
  - {pin_num: '3', peripheral: I2C0, signal: SCL, pin_signal: SWCLK/PIO0_3, opendrain: enabled}
  - {pin_num: '4', peripheral: I2C0, signal: SDA, pin_signal: SWDIO/PIO0_2, opendrain: enabled}
  - {pin_num: '2', peripheral: GPIO, signal: 'PIO0, 4', pin_signal: PIO0_4, direction: OUTPUT, gpio_init_state: 'true', mode: pullUp}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

/* FUNCTION ************************************************************************************************************
 *
 * Function Name : BOARD_InitPins
 * Description   : Configures pin routing and optionally pin electrical features.
 *
 * END ****************************************************************************************************************/
/* Function assigned for the Cortex-M0P */
void BOARD_InitPins(void)
{
    /* Enables clock for switch matrix.: Enable. */
    CLOCK_EnableClock(kCLOCK_Swm);
    /* Enables the clock for the GPIO0 module */
    CLOCK_EnableClock(kCLOCK_Gpio0);

    gpio_pin_config_t RDY_N_config = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 1U,
    };
    /* Initialize GPIO functionality on pin PIO0_4 (pin 2)  */
    GPIO_PinInit(BOARD_INITPINS_RDY_N_GPIO, BOARD_INITPINS_RDY_N_PORT, BOARD_INITPINS_RDY_N_PIN, &RDY_N_config);

    IOCON->PIO[6] = ((IOCON->PIO[6] &
                      /* Mask bits to zero which are setting */
                      (~(IOCON_PIO_OD_MASK)))

                     /* Open-drain mode.: Open-drain mode enabled. Remark: This is not a true open-drain mode. */
                     | IOCON_PIO_OD(PIO0_2_OD_ENABLED));

    IOCON->PIO[5] = ((IOCON->PIO[5] &
                      /* Mask bits to zero which are setting */
                      (~(IOCON_PIO_OD_MASK)))

                     /* Open-drain mode.: Open-drain mode enabled. Remark: This is not a true open-drain mode. */
                     | IOCON_PIO_OD(PIO0_3_OD_ENABLED));

    IOCON->PIO[4] = ((IOCON->PIO[4] &
                      /* Mask bits to zero which are setting */
                      (~(IOCON_PIO_MODE_MASK)))

                     /* Selects function mode (on-chip pull-up/pull-down resistor control).: Pull-up. Pull-up resistor
                      * enabled. */
                     | IOCON_PIO_MODE(PIO0_4_MODE_PULL_UP));

    /* I2C1_SDA connect to P0_2 */
    SWM_SetMovablePinSelect(SWM0, kSWM_I2C_SDA, kSWM_PortPin_P0_2);

    /* I2C1_SCL connect to P0_3 */
    SWM_SetMovablePinSelect(SWM0, kSWM_I2C_SCL, kSWM_PortPin_P0_3);

    /* Disable clock for switch matrix. */
    CLOCK_DisableClock(kCLOCK_Swm);
}
/***********************************************************************************************************************
 * EOF
 **********************************************************************************************************************/
