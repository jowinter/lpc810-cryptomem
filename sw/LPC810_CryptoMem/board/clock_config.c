/*
 * How to set up clock using clock driver functions:
 *
 * 1. Setup clock sources.
 *
 * 2. Set up all dividers.
 *
 * 3. Set up all selectors to provide selected clocks.
 */

/* TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
!!GlobalInfo
product: Clocks v7.0
processor: LPC810
package_id: LPC810M021FN8
mcu_data: ksdk2_0
processor_version: 7.0.1
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS **********/

#include "fsl_power.h"
#include "fsl_clock.h"
#include "clock_config.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* System clock frequency. */
extern uint32_t SystemCoreClock;

/*******************************************************************************
 ************************ BOARD_InitBootClocks function ************************
 ******************************************************************************/
void BOARD_InitBootClocks(void)
{
    BOARD_BootClockRUN();
}

/*******************************************************************************
 ********************** Configuration BOARD_BootClockRUN ***********************
 ******************************************************************************/
/* TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
!!Configuration
name: BOARD_BootClockRUN
called_from_default_init: true
outputs:
- {id: I2C0_clock.outFreq, value: 8 MHz}
- {id: LowPower_clock.outFreq, value: 10 kHz}
- {id: SPI0_clock.outFreq, value: 8 MHz}
- {id: SYSPLL_clock.outFreq, value: 24 MHz}
- {id: System_clock.outFreq, value: 8 MHz, locked: true, accuracy: '0.001'}
- {id: divto750k_clock.outFreq, value: 750 kHz}
settings:
- {id: SYSCON.CLKOUTDIV.scale, value: '1'}
- {id: SYSCON.CLKOUTSEL.sel, value: SYSCON.WDT_DIV}
- {id: SYSCON.DIV.scale, value: '256'}
- {id: SYSCON.MAINCLKSEL.sel, value: SYSCON.PLL}
- {id: SYSCON.MULT.scale, value: '256'}
- {id: SYSCON.M_MULT.scale, value: '2'}
- {id: SYSCON.SYSAHBCLKDIV.scale, value: '3'}
- {id: UARTFRGConfig, value: Enabled}
sources:
- {id: SYSCON.clk_in.outFreq, value: 8 MHz}
- {id: SYSCON.wwdt_osc.outFreq, value: 4 MHz}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS **********/

/*******************************************************************************
 * Variables for BOARD_BootClockRUN configuration
 ******************************************************************************/
/*******************************************************************************
 * Code for BOARD_BootClockRUN configuration
 ******************************************************************************/
void BOARD_BootClockRUN(void)
{
    /*!< Set up the clock sources */
    /*!< Set up IRC */
    POWER_DisablePD(kPDRUNCFG_PD_IRC_OUT);                   /*!< Ensure IRC_OUT is on  */
    POWER_DisablePD(kPDRUNCFG_PD_IRC);                      /*!< Ensure IRC is on  */
    POWER_DisablePD(kPDRUNCFG_PD_SYSOSC);                  /*!< Ensure Main osc is on */
    CLOCK_Select(kSYSPLL_From_Irc);                         /*!< set IRC to pll select */
    clock_sys_pll_t config;
    config.src = kCLOCK_SysPllSrcIrc;                           /*!< set pll src  */
    config.targetFreq = 24000000U;                     /*!< set pll target freq */
    CLOCK_InitSystemPll(&config);                           /*!< set parameters */
    CLOCK_SetMainClkSrc(kCLOCK_MainClkSrcSysPll);         /*!< select pll clock for main clock */
    CLOCK_Select(kCLKOUT_From_WdtOsc);                     /*!< select wwdt clock for CLKOUT */
    CLOCK_UpdateClkOUTsrc();                                   /*!< update CLKOUT src */
    CLOCK_SetCoreSysClkDiv(3U);
    /*!< Set SystemCoreClock variable. */
    SystemCoreClock = BOARD_BOOTCLOCKRUN_CORE_CLOCK;
}

