/*
 * FreeModbus Libary: BARE Port
 * Copyright (C) 2006 Christian Walter <wolti@sil.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: $Id: porttimer.c,v 1.1 2006/08/22 21:35:13 wolti Exp $
 */

/* ----------------------- Platform includes --------------------------------*/
#include "port.h"
 
/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
#include "tim.h"
/* ----------------------- static functions ---------------------------------*/
//static void prvvTIMERExpiredISR( void );
 
/* -----------------------    variables     ---------------------------------*/
 uint8_t modbusStatus;
/* ----------------------- Start implementation -----------------------------*/
BOOL
xMBPortTimersInit( USHORT usTim1Timerout50us )
{
	
  htim14.Instance = TIM14;                                         		 			/* TIM1 */
  htim14.Init.Prescaler         = (HAL_RCC_GetPCLK1Freq() / 1000000) - 1;   /* Period = 24000 - 1 */
  htim14.Init.Period       		 	= (usTim1Timerout50us * 50) - 1;           /* Prescaler = 1000 - 1 */
  htim14.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;          					/* ClockDivision = 0 */
  htim14.Init.CounterMode       = TIM_COUNTERMODE_UP;             				  /* Counter direction = Up */
  htim14.Init.RepetitionCounter = 0;                              				  /* Repetition = 0 */
  htim14.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;  			  	/* Auto-reload register not buffered */
  if (HAL_TIM_Base_Init(&htim14) != HAL_OK)                       				  /* Initialize TIM1 */
  {
    APP_ErrorHandler();
  }
  return TRUE;
}
 
 
void
vMBPortTimersEnable(  )
{
  /* Enable the timer with the timeout passed to xMBPortTimersInit( ) */
  //HAL_TIM_Base_Start_IT(&htim14);
    __HAL_TIM_SET_COUNTER(&htim14, 0);	
    modbusStatus = 1;
}
 
void
vMBPortTimersDisable(  )
{
  /* Disable any pending timers. */
  //HAL_TIM_Base_Stop_IT(&htim14);
    modbusStatus =0;
}
 
/* Create an ISR which is called whenever the timer has expired. This function
* must then call pxMBPortCBTimerExpired( ) to notify the protocol stack that
* the timer has expired.
 
static void prvvTIMERExpiredISR( void )
{
( void )pxMBPortCBTimerExpired(  );
}
*/
