/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.c
  * @brief   This file provides code for the configuration
  *          of the ADC instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "adc.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

ADC_HandleTypeDef hadc1;

/* ADC1 init function */
void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  hadc1.Instance = ADC1;
  
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;                /* Set ADC clock*/
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;                      /* 12-bit resolution for converted data */
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;                     /* Right-alignment for converted data */
  hadc1.Init.ScanConvMode          = ADC_SCAN_DIRECTION_FORWARD;              /* Scan sequence direction: forward */
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;                     /* Single Conversion */
  hadc1.Init.LowPowerAutoWait      = DISABLE;                                 /* Auto-delayed conversion feature disabled */
  hadc1.Init.ContinuousConvMode    = DISABLE;                                 /* Continuous mode disabled to have only 1 conversion at each conversion trig */
  hadc1.Init.DiscontinuousConvMode = DISABLE;                                 /* Disable discontinuous mode */
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;                      /* Software start to trig the 1st conversion manually, without external event */
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;           /* Parameter discarded because software trigger chosen */
  hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;                /* DR register is overwritten with the last conversion result in case of overrun */
  hadc1.Init.SamplingTimeCommon    = ADC_SAMPLETIME_239CYCLES_5;              /* The channel sampling time is 239.5 ADC clock cycles */
	__HAL_RCC_ADC_CLK_ENABLE();                                                 /* Enable ADC clock */
  if (HAL_ADC_Init(&hadc1) != HAL_OK)                                         /* ADC initialization */
  {
    APP_ErrorHandler();
  }
    
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)                           /* ADC Calibration */
  {
    APP_ErrorHandler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

uint32_t APP_ADCConvert(uint32_t Channel)
{
  uint16_t adcvalue = 0;
  ADC_ChannelConfTypeDef        sConfig;
  /* Configure channel need ADC is disable */
  if(READ_BIT(hadc1.Instance->CR, ADC_CR_ADEN) == ADC_CR_ADEN)
  {
    __HAL_ADC_DISABLE(&hadc1);
  }
    
  /* Config selected channels */
  sConfig.Rank         = ADC_RANK_CHANNEL_NUMBER;                             
  sConfig.Channel      = Channel;                                      
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)                       /* Configure ADC Channel */
  {
    APP_ErrorHandler();
  }
  
  /* ADC Start */
  HAL_ADC_Start(&hadc1);  
  
  /* Polling for ADC Conversion */
  HAL_ADC_PollForConversion(&hadc1, 1000000);        
  
  /* Get ADC Value */
  adcvalue = HAL_ADC_GetValue(&hadc1);     
  
  /* Disable ADC to clear channel configuration */
  __HAL_ADC_DISABLE(&hadc1);
  
  /* Clear the selected channels */
  sConfig.Rank         = ADC_RANK_NONE;                             
  sConfig.Channel      = Channel;                                      
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)                       /* Configure ADC Channel */
  {
    APP_ErrorHandler();
  }
  
  return (uint32_t)adcvalue;
}
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
