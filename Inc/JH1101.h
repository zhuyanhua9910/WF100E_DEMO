#ifndef __JH1101_H__
#define __JH1101_H__

#include "main.h"

typedef struct
{	
	uint16_t GPIO_Pin;
	GPIO_TypeDef *GPIO_PORT;

	uint16_t VDD_Pin;
	GPIO_TypeDef *VDD_PORT;
	
	uint32_t Channel;
	
} OWI_TypeDef;


void OWI_Init(GPIO_TypeDef *GPIOx,uint16_t Pin,GPIO_TypeDef *VDDx,uint16_t VDD_Pin,uint32_t channel);
//运行模式
uint8_t CMD_MODE(void);
uint8_t NOR_MODE(void);
//void OWI_OUT(void);
void OWI_Idle(void);
void SetVdd(uint8_t on_or_off);
//控制命令
void WRITE_CMD(uint8_t cmd, uint8_t dat, uint8_t rlens, uint32_t timeout, uint8_t* rdata);
//void READ_DATA(uint8_t rlens, uint8_t* rdata);
uint32_t GET_ADC_Value(void);
void READ_DAT(uint8_t rlens, uint8_t* rdata);
#endif//__JH1101_H__
