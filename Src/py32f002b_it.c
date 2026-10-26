/**
  ******************************************************************************
  * @file    py32f002b_it.c
  * @author  MCU Application Team
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2023 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "uart.h"
#include "py32f002b_it.h"
#include "mb.h"
/* Private includes ----------------------------------------------------------*/
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private user code ---------------------------------------------------------*/
/* External variables --------------------------------------------------------*/
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim14;
extern  uint8_t modbusStatus;
/******************************************************************************/
/*          Cortex-M0+ Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  while (1)
  {
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/* PY32F002B Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file.                                          */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
void ADC_COMP_IRQHandler(void)
{
  HAL_ADC_IRQHandler(&hadc1);
}

void TIM1_BRK_UP_TRG_COM_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim1);
}

void TIM14_IRQHandler(void)
{
  /* USER CODE BEGIN TIM3_IRQn 0 */
	
  /* USER CODE END TIM3_IRQn 0 */
    eMBPoll();
    if(modbusStatus)pxMBPortCBTimerExpired();
  HAL_TIM_IRQHandler(&htim14);	
  /* USER CODE BEGIN TIM3_IRQn 1 */

  /* USER CODE END TIM3_IRQn 1 */
}


void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */
	uint32_t tmp_flag = __HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE);
		uint32_t tmp_it_source = __HAL_UART_GET_IT_SOURCE(&huart1, UART_IT_RXNE);

		if ((tmp_flag != RESET) && (tmp_it_source != RESET)) {
			pxMBFrameCBByteReceived();
			__HAL_UART_CLEAR_PEFLAG(&huart1);
			return;
		}

		if ((__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) != RESET) && (__HAL_UART_GET_IT_SOURCE(&huart1, UART_IT_TXE) != RESET)) {
			pxMBFrameCBTransmitterEmpty();
			return;
		}
  /* USER CODE END USART2_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART2_IRQn 1 */

  /* USER CODE END USART2_IRQn 1 */
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
