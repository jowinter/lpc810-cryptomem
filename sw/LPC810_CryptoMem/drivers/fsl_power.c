/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016, NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "fsl_common.h"
#include "fsl_power.h"
/* Component ID definition, used by tools. */
#ifndef FSL_COMPONENT_ID
#define FSL_COMPONENT_ID "platform.drivers.power_no_lib"
#endif

/*******************************************************************************
 * Code
 ******************************************************************************/
void EnableDeepSleepIRQ(IRQn_Type interrupt)
{
    uint32_t intNumber = (uint32_t)interrupt;
	
    if(intNumber >= 24u) 
    {
        /* enable pin interrupt wake up in the STARTERP0 register */
        SYSCON->STARTERP0 |= 1UL << (intNumber - 24u); 
    }        
    else
    {
        /* enable interrupt wake up in the STARTERP1 register */	 
        SYSCON->STARTERP1 |= 1UL << intNumber;          
    }
    /* also enable interrupt at NVIC */
    (void)EnableIRQ(interrupt); 
}

void DisableDeepSleepIRQ(IRQn_Type interrupt)
{
    uint32_t intNumber = (uint32_t)interrupt;
	
    /* also disable interrupt at NVIC */
    (void)DisableIRQ(interrupt); 
	
    if(intNumber >= 24u) 
    {
        /* disable pin interrupt wake up in the STARTERP0 register */
        SYSCON->STARTERP0 &= ~(1UL << (intNumber - 24u)); 
    }  
    else
    {
        /* disable interrupt wake up in the STARTERP1 register */	
        SYSCON->STARTERP1 &= ~(1UL << intNumber);         
    }
}
