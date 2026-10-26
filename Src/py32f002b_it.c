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
extern  void vMBPortCountOverrun(void);
extern uint8_t mbT35Tick(void);   /* porttimer.c：T3.5 静默计数 */
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
    /* 只有连续 2 个 tick（≥1.75ms）没有收到新字节，才认为一帧结束。
     * 原来写的是 if(modbusStatus)pxMBPortCBTimerExpired(); —— 定时器每
     * 1.75ms 无条件报一次"帧结束"，帧长超过 1.75ms（115200 下 20 字节）
     * 就会被从中间截断，表现为 0x77 这种 23 字节的命令时好时坏。 */
    if(mbT35Tick())pxMBPortCBTimerExpired();
  HAL_TIM_IRQHandler(&htim14);	
  /* USER CODE BEGIN TIM3_IRQn 1 */

  /* USER CODE END TIM3_IRQn 1 */
}


void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */
	/* 溢出标志置位说明有字节因 CPU 没及时取而被硬件丢掉，累计用于诊断 */
	if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) != RESET) {
		vMBPortCountOverrun();
	}

	uint32_t tmp_flag = __HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE);
		uint32_t tmp_it_source = __HAL_UART_GET_IT_SOURCE(&huart1, UART_IT_RXNE);

		if ((tmp_flag != RESET) && (tmp_it_source != RESET)) {
			pxMBFrameCBByteReceived();
			/* 注意：__HAL_UART_CLEAR_PEFLAG 的实现是"读 SR 再读 DR"，
			 * 而"读 DR"正是清 RXNE 的动作。若此刻下一个字节已经到达
			 * （本中断被 TIM14 里的 eMBPoll 拖慢时非常常见），这一读会
			 * 把这个字节直接吞掉 —— 帧中间少一个字节，CRC 失败，
			 * FreeModbus 静默丢帧，表现就是"发出去的请求没有应答"或
			 * "应答缺字节"。所以只在 RXNE 确实为空时才清错误标志。 */
			if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) == RESET) {
				__HAL_UART_CLEAR_PEFLAG(&huart1);
			}
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
