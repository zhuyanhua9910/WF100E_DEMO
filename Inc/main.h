/**
  ******************************************************************************
  * @file    main.h
  * @author  MCU Application Team
  * @brief   Header for main.c file.
  *          This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "py32f0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* Private defines -----------------------------------------------------------*/

#define OUT1_Pin GPIO_PIN_4
#define OUT1_GPIO_Port GPIOA

#define OUT2_Pin GPIO_PIN_3
#define OUT2_GPIO_Port GPIOA

#define VDD_EN_Pin GPIO_PIN_2
#define VDD_EN_GPIO_Port GPIOB

#define VDD1_EN_Pin GPIO_PIN_1
#define VDD1_EN_GPIO_Port GPIOA

#define VDD2_EN_Pin GPIO_PIN_0
#define VDD2_EN_GPIO_Port GPIOA

#define VDD3_EN_Pin GPIO_PIN_0
#define VDD3_EN_GPIO_Port GPIOB

#define VDD4_EN_Pin GPIO_PIN_1
#define VDD4_EN_GPIO_Port GPIOB

#define VPP_EN_Pin GPIO_PIN_3
#define VPP_EN_GPIO_Port GPIOB


/* Exported variables prototypes ---------------------------------------------*/
void delay_us(uint32_t us);
/* Exported functions prototypes ---------------------------------------------*/
void APP_ErrorHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
