#include "jh1101.h"
#include "tim.h"
#include "adc.h"

#define DH HAL_GPIO_WritePin(VPP_EN_GPIO_Port,VPP_EN_Pin, GPIO_PIN_SET)
#define DL HAL_GPIO_WritePin(VPP_EN_GPIO_Port,VPP_EN_Pin, GPIO_PIN_RESET)


OWI_TypeDef JH1101;

#define JH1101_OWI_READ							HAL_GPIO_ReadPin(JH1101.GPIO_PORT, JH1101.GPIO_Pin)
#define JH1101_OWI_OUT_H() 					HAL_GPIO_WritePin(JH1101.GPIO_PORT, JH1101.GPIO_Pin, GPIO_PIN_SET)
#define JH1101_OWI_OUT_L() 					HAL_GPIO_WritePin(JH1101.GPIO_PORT, JH1101.GPIO_Pin, GPIO_PIN_RESET)

//OWI_OUT//////////////////////////////////////////////////////////////////////
void OWI_OUT(void)
{
  HAL_GPIO_WritePin(JH1101.GPIO_PORT, JH1101.GPIO_Pin,GPIO_PIN_SET);
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = JH1101.GPIO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(JH1101.GPIO_PORT, &GPIO_InitStruct);
}

//OWI初始化引脚
void OWI_Init(GPIO_TypeDef *GPIOx,uint16_t Pin,GPIO_TypeDef *VDDx,uint16_t VDD_Pin,uint32_t channel)
{
	JH1101.GPIO_PORT = GPIOx;
	JH1101.GPIO_Pin = Pin;
		
	JH1101.VDD_PORT = VDDx;
	JH1101.VDD_Pin = VDD_Pin;
	
	JH1101.Channel = channel;
  OWI_OUT();
//  SetVdd(1);
}

void SetVdd(uint8_t on_or_off)
{
	HAL_GPIO_WritePin(JH1101.VDD_PORT, JH1101.VDD_Pin,on_or_off ? GPIO_PIN_SET:GPIO_PIN_RESET);
}

//OWI_Idol//////////////////////////////////////////////////////////////////////
void OWI_Idle(void)
{
  SetVdd(0);
  OWI_OUT();
}

//OWI_IN//////////////////////////////////////////////////////////////////////
void OWI_IN(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = JH1101.GPIO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(JH1101.GPIO_PORT, &GPIO_InitStruct);
}

uint32_t wait_for_level(GPIO_PinState level,uint32_t timout)
{
  uint32_t err = 0;
  while(JH1101_OWI_READ != level)
  {
    delay_us(1);		
    if(++err>timout) return err;
  }
  return err;
}

//BIT//////////////////////////////////////////////////////////////////////
void OWI_WRITE_BIT(uint8_t bit)
{
	if(bit)
	{
		JH1101_OWI_OUT_L();
		delay_us(5);
		JH1101_OWI_OUT_H();
		delay_us(22);
	}
	else
	{
		JH1101_OWI_OUT_L();
		delay_us(22);
		JH1101_OWI_OUT_H();
		delay_us(5);
	}
}
//END////////////////////////////////////////////////////////////////////////
void OWI_WRITE_STAR(void)
{
  __disable_irq(); 
	JH1101_OWI_OUT_L();
  delay_us(14);
  JH1101_OWI_OUT_H();
  delay_us(14);
  __enable_irq();
}

//OWI写
void OWI_WRITE_DATA_BYTE(uint8_t data)
{
  __disable_irq(); 
  uint8_t cheek = 0;
  for (uint8_t i = 0; i < 8; i++) 
	{
		if(data & 0x80)
		{
			cheek ^= 1;
			OWI_WRITE_BIT(1);
		}
		else
		{
			cheek ^= 0;
			OWI_WRITE_BIT(0);
		}
		data <<= 1;
	}
	OWI_WRITE_BIT(cheek);
	delay_us(25);
	JH1101_OWI_OUT_H();
  __enable_irq();
}
//END////////////////////////////////////////////////////////////////////////

 uint8_t data = 0;

//OWI读
uint8_t OWI_READ_DATA_BYTE(uint32_t timout)
{
 __disable_irq(); 
 volatile uint32_t data_tim_l[8] ={0};
 volatile uint32_t data_tim_h[8] = {0};	
 uint8_t i = 0;
 data = 0;

 for(i = 0;i < 8;i++) {
    data_tim_l[i] = 0;
    data_tim_h[i] = 0;
  }
  //等待电平拉低，数据开始	
  wait_for_level(GPIO_PIN_RESET,timout);
	
  //同步头，电平拉低15us
  wait_for_level(GPIO_PIN_SET,200);
  
	//同步头，电平高15us	
  wait_for_level(GPIO_PIN_RESET,200);
  
	//数据位
	for(i = 0;i < 8;i++)
	{
    data_tim_l[i] = wait_for_level(GPIO_PIN_SET,200);
    data_tim_h[i] = wait_for_level(GPIO_PIN_RESET,200);
    if(i>0) data<<=1;
    data |=  (data_tim_h[i] > data_tim_l[i]) ? 1 : 0;   
  }
  wait_for_level(GPIO_PIN_SET,200);  
  __enable_irq();
	return data;

}
//END////////////////////////////////////////////////////////////////////////
	

uint8_t CMD_MODE(void)
{
  uint8_t dat = 0;
  SetVdd(0);
	HAL_Delay(10);
	SetVdd(1);
	HAL_Delay(1);
	OWI_WRITE_STAR();
	OWI_WRITE_DATA_BYTE(0x80);
	OWI_WRITE_DATA_BYTE(0x10);
	dat = OWI_READ_DATA_BYTE(5000);
	return dat;
}

uint8_t NOR_MODE(void)
{
	uint8_t dat;
	OWI_WRITE_STAR();
	OWI_WRITE_DATA_BYTE(0x80);
	OWI_WRITE_DATA_BYTE(0x30);
	dat = OWI_READ_DATA_BYTE(60000);
	return dat;
}

void WRITE_CMD(uint8_t cmd, uint8_t dat, uint8_t rlens, uint32_t timeout, uint8_t* rdata)
{
	uint8_t i;
	OWI_WRITE_STAR();
	OWI_WRITE_DATA_BYTE(cmd);
	OWI_WRITE_DATA_BYTE(dat);
	for (i = 0; i < rlens; i++) 
	{
		*rdata++ = OWI_READ_DATA_BYTE(timeout);
	}
}
uint32_t tim = 0;
void READ_DAT(uint8_t rlens, uint8_t* rdata)
{

  while(tim < 100)
  {
    tim = wait_for_level(GPIO_PIN_RESET,100);
  }
  for(uint8_t i = 0; i < rlens; i++)
  {
    rdata[i] = OWI_READ_DATA_BYTE(60000);
  }
}

uint32_t GET_ADC_Value()
{
	OWI_IN();
	return APP_ADCConvert(JH1101.Channel);
}
