/**
  ******************************************************************************
  * @file    main.c
  * @author  MCU Application Team
  * @brief   Main program body
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
#include "gpio.h"
#include "tim.h"
#include "adc.h"
#include "uart.h"
#include "mb.h"
#include "product.h"
/* Private define ------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

/* Private user code ---------------------------------------------------------*/
struct Task task;
/* Private macro -------------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/

/**
  * @brief  Main program.
  * @retval int
  */
int main(void)
{
  /* Reset of all peripherals, Initializes the Systick. */ 
	
  HAL_Init();
  
  /* Initialize GPIO */
  MX_GPIO_Init();

	/* Initialize TIM */

  MX_TIM1_Init();
	
	/* Initialize UART */

	MX_USART1_UART_Init();
	
  /* Initialize ADC */
	
  MX_ADC1_Init();

	 
	HAL_TIM_Base_Start(&htim1);    

	/* 初始化 SAF8100 驱动（时间基准 + 默认配置） */
	saf8100_init();

	eMBInit(MB_RTU, 1, 1, 115200, MB_PAR_NONE);

	eMBEnable();
  
	HAL_TIM_Base_Start_IT(&htim14);    
  while (1)
  { 
		switch (task.order) {
		case TS_IDLE:
			//注意：此处不要清 isTaskBusy。上位机可能刚下发新指令，若在这里清忙标志会被误判为空闲，导致任务被提前终止
			//isTaskBusy = 0;
			break;

		case TS_8100TEST:
			RTOS_8100TEST();
			break;

		case TS_8100MEAS:
			RTOS_8100MEAS();
			break;

		case TS_8100FUSE:
			RTOS_8100FUSE();
			break;

		default:
			task.isTaskBusy = 0;
			task.order = TS_IDLE;
			break;
		}
		HAL_Delay(1);
	}
}

/**
  * @brief  us delay 
  * @param  us
  * @retval None
  */
/**
  * @brief  us delay（基于 TIM1 计数）
  * @note   TIM1 由 saf8100 驱动作为 1MHz 时基常驻运行，
  *         这里不能再 Start/Stop，否则会把 OWI 时序的时基停掉。
  */
void delay_us2(uint32_t us)
{
  uint16_t delay=0xffff-(us-2)-5;
  __HAL_TIM_SET_COUNTER(&htim1,delay);
  while(delay<0xffff-5)
  {
     delay=__HAL_TIM_GET_COUNTER(&htim1);
  }
}

void delay_us(uint32_t us)
{
  __IO uint32_t Delay = us * 3;
  do
  {
    __NOP();
  }
  while (Delay --);
}
/**
  * @brief  TIM configuration function
  * @param  None
  * @retval None
  */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  None
  * @retval None
  */
void APP_ErrorHandler(void)
{
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
