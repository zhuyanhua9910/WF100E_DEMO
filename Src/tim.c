/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.c
  * @brief   This file provides code for the configuration
  *          of the TIM instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "tim.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim14;

/* TIM1 init function */
void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */
	TIM_MasterConfigTypeDef       sMasterConfig;
	
  htim1.Instance = TIM1;                                          /* TIM1 */
  htim1.Init.Prescaler         = 24 - 1;                      		/* Period = 24000 - 1 */
  htim1.Init.Period       		 = 65535 - 1;                       /* Prescaler = 1000 - 1 */
  htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;          /* ClockDivision = 0 */
  htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;              /* Counter direction = Up */
  htim1.Init.RepetitionCounter = 0;                               /* Repetition = 0 */
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;  /* Auto-reload register not buffered */
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)                        /* Initialize TIM1 */
  {
    APP_ErrorHandler();
  }
  /* Timer TRGO selection */
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;                
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;        /* Disable master/slave mode */
  HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig);  /* Configure TIM1 */
  if (HAL_TIM_Base_Start(&htim1) != HAL_OK)                       /* Start TIM1 */
  {
    APP_ErrorHandler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}
/* TIM14 init function */
void MX_TIM14_Init(void)
{

  /* USER CODE BEGIN TIM14_Init 0 */

  /* USER CODE END TIM14_Init 0 */
	TIM_MasterConfigTypeDef sMasterConfig;
	
  htim14.Instance = TIM14;                                         		 		/* TIM14 */
  htim14.Init.Prescaler         = 24 - 1;                   						  /* Period = 24000 - 1 */
  htim14.Init.Period       		 	= 65535 - 1;                      				/* Prescaler = 1000 - 1 */
  htim14.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;          				/* ClockDivision = 0 */
  htim14.Init.CounterMode       = TIM_COUNTERMODE_UP;             				/* Counter direction = Up */
  htim14.Init.RepetitionCounter = 0;                              				/* Repetition = 0 */
  htim14.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;  			  /* Auto-reload register not buffered */
  if (HAL_TIM_Base_Init(&htim14) != HAL_OK)                       				/* Initialize TIM14 */
  {
    APP_ErrorHandler();
  }
  
  /* Timer TRGO selection */
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;                
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;       			/* Disable master/slave mode */
  HAL_TIMEx_MasterConfigSynchronization(&htim14, &sMasterConfig);  				/* Configure TIM14 */
  if (HAL_TIM_Base_Start(&htim14) != HAL_OK)                       				/* Start TIM14 */
  {
    APP_ErrorHandler();
  }
  /* USER CODE BEGIN TIM14_Init 2 */

  /* USER CODE END TIM14_Init 2 */

}


void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* tim_baseHandle)
{
  if(tim_baseHandle->Instance==TIM1)
  {
  /* USER CODE BEGIN TIM1_MspInit 0 */

  /* USER CODE END TIM1_MspInit 0 */
    /* TIM1 clock enable */
    __HAL_RCC_TIM1_CLK_ENABLE();

    /* TIM1 interrupt Init */
//    HAL_NVIC_SetPriority(TIM1_BRK_UP_TRG_COM_IRQn, 3, 0);
//    HAL_NVIC_EnableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
  /* USER CODE BEGIN TIM1_MspInit 1 */

  /* USER CODE END TIM1_MspInit 1 */
  }
  else if(tim_baseHandle->Instance==TIM14)
  {
  /* USER CODE BEGIN TIM2_MspInit 0 */

  /* USER CODE END TIM2_MspInit 0 */

    /* TIM2 clock enable */
    __HAL_RCC_TIM14_CLK_ENABLE();

    /* TIM2 interrupt Init */
    HAL_NVIC_SetPriority(TIM14_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(TIM14_IRQn);
  /* USER CODE BEGIN TIM2_MspInit 1 */

  /* USER CODE END TIM2_MspInit 1 */
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* tim_baseHandle)
{

  if(tim_baseHandle->Instance==TIM1)
  {
  /* USER CODE BEGIN TIM1_MspDeInit 0 */

  /* USER CODE END TIM1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_TIM1_CLK_DISABLE();

    /* TIM1 interrupt Deinit */
//    HAL_NVIC_DisableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
//    HAL_NVIC_DisableIRQ(TIM1_CC_IRQn);
  /* USER CODE BEGIN TIM1_MspDeInit 1 */

  /* USER CODE END TIM1_MspDeInit 1 */
  }
  else if(tim_baseHandle->Instance==TIM14)
  {
  /* USER CODE BEGIN TIM2_MspDeInit 0 */

  /* USER CODE END TIM2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_TIM14_CLK_DISABLE();

    /* TIM2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(TIM14_IRQn);
  /* USER CODE BEGIN TIM2_MspDeInit 1 */

  /* USER CODE END TIM2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
