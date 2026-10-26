/*
 * product.c
 *
 *  Created on: Oct 23, 2025
 *      Author: ITry
 */
#include "product.h"
#include <string.h>
#include "CmdHelper.h"
#include "JH1101.h"

#define RETRY_COUNT 2
#define BREAK_ASSERT(b,v)  if ((v = b) != ERR_NONE) break;
#define BREAK_ASSERT2(b,v,e)  if (ERR_NONE != b) { v = e; break;}
#define COMM_TIMEOUT 8000

uint16_t lastChannel = 0xFFFF;

GPIO_TypeDef *OWI_PORTS[JIG_ROW] = { OUT1_GPIO_Port, OUT2_GPIO_Port};

uint16_t OWI_PINS[JIG_ROW] = {OUT1_Pin, OUT2_Pin};

GPIO_TypeDef *VDD_PORTS[JIG_COL] = { VDD1_EN_GPIO_Port, VDD2_EN_GPIO_Port, VDD3_EN_GPIO_Port,VDD4_EN_GPIO_Port};

uint16_t VDD_PINS[JIG_COL] = { VDD1_EN_Pin, VDD2_EN_Pin, VDD3_EN_Pin, VDD4_EN_Pin };

uint32_t ADC_CHANNELS[JIG_ROW] = {ADC_CHANNEL_2,ADC_CHANNEL_1};

uint8_t SetChannel(uint16_t ch) {
	uint8_t err = 0;
	uint8_t row = 0;
	uint8_t col = 0;
	if (ch == lastChannel)
		return err;
	if (ch >= PRODUCT_NUM) {
		err = 1;
	} else {
		row = ch / JIG_COL;
		col = ch % JIG_COL;
		OWI_Init(OWI_PORTS[row], OWI_PINS[row], VDD_PORTS[col], VDD_PINS[col], ADC_CHANNELS[row]);
		lastChannel = ch;
		//HAL_Delay(5);
	}
	return err;
}

    
void RTOS_SCAN(void) {
	uint16_t i = 0;
  uint16_t j = 0;
	uint8_t err = 0;
  uint8_t buf[17];
  
	task.isTaskBusy = 1;
	task.prog = 0;
	memset(product, 0, PRODUCT_NUM * sizeof(Product));    
	for (i = 0; i < PRODUCT_NUM; i++) {
		do {
			task.prog++;
			err = 0;
			
      SetChannel(i);
      BREAK_ASSERT2(CMD_MODE(), err, ERR_CMD);
      delay_us(300);
      WRITE_CMD(0xFE,0x02,1,COMM_TIMEOUT,buf);
      BREAK_ASSERT2(buf[0],err,ERR_ACK);
      delay_us(300);
      WRITE_CMD(0xF0,0x00,17,COMM_TIMEOUT,buf);
      BREAK_ASSERT2(buf[16],err, ERR_ACK);
      for(j=0;j<16;j++) product[i].otp[j]= buf[j];
      //
      product[i].IsOtp = (product[i].otp[4]+product[i].otp[5]) != 0 ? 1 : 0;
			//任务完成
			err = 0;
		} while (0);
        
    OWI_Idle();
		product[i].errCode = err;
		HAL_Delay(1);
        
	}
	task.isTaskBusy = 0;
	task.prog = PRODUCT_NUM;
	task.order = TS_IDLE;
}

void RTOS_RAWDATA(void){
	uint16_t i = 0;
	uint16_t j = 0;
	uint8_t err = 0;
  uint8_t buf[4];
  
	task.isTaskBusy = 1;
	task.prog = 0;
	for (i = 0; i < PRODUCT_NUM; i++) {
		do {
			task.prog++;
			for (j = 0; j < 5; j++) {
				product[i].rawdata[j] = 0;
			}
			//有效的才进行
			err = product[i].errCode;
			if (err != 0) {
				break;
			}
      SetChannel(i);
      BREAK_ASSERT2(CMD_MODE(), err, ERR_CMD);
      delay_us(300);
      for(j =0; j< REG_NUM;j++){
        WRITE_CMD(0x20+j, product[i].otp[j], 1, COMM_TIMEOUT, buf);
        BREAK_ASSERT2(buf[0], err, ERR_ACK);
        delay_us(300);
      }
      BREAK_ASSERT2(buf[0], err, ERR_ACK);
      BREAK_ASSERT2(NOR_MODE(), err, ERR_NOR);
      delay_us(300);
      memset(buf, 0, 4 * sizeof(uint8_t));  
      READ_DAT(4, buf);
      for (j = 0; j < 4; j++) {
				product[i].rawdata[j] = buf[j];
			}
			//任务完成
			err = 0;

		} while (0);
    OWI_Idle();
		product[i].errCode = err;
		HAL_Delay(1);
	}
	task.isTaskBusy = 0;
	task.prog = PRODUCT_NUM;
	task.order = TS_IDLE;
}

void RTOS_OTP(void){
	uint16_t i = 0;
  uint16_t j = 0;
	uint8_t err = 0;
  uint8_t buf[17];
  
	task.isTaskBusy = 1;
	task.prog = 0;
	for (i = 0; i < PRODUCT_NUM; i++) {
		do {
			task.prog++;
			//有效的才进行
			err = product[i].errCode;
			if (err != 0) {
				break;
			}
      SetChannel(i);
      BREAK_ASSERT2(CMD_MODE(), err, ERR_CMD);
      delay_us(300);
      for(j =0; j< REG_NUM;j++){
        WRITE_CMD(0x20+j, product[i].otp[j], 1, COMM_TIMEOUT, buf);
        BREAK_ASSERT2(buf[0], err, ERR_ACK);
        delay_us(300);
      }
      BREAK_ASSERT2(buf[0], err, ERR_ACK);
      //vpp
      SetProductVpp(0xFF);
			HAL_Delay(1);
      //
      WRITE_CMD(0x30,0x80,1,COMM_TIMEOUT,buf);
      BREAK_ASSERT2(buf[0],err,ERR_ACK);
      delay_us(300);
      WRITE_CMD(0xFF,0x00,1,COMM_TIMEOUT,buf);
      BREAK_ASSERT2(buf[0],err,ERR_ACK);
      HAL_Delay(20);//开始otp
      WRITE_CMD(0x30,0x00,1,COMM_TIMEOUT,buf);
      BREAK_ASSERT2(buf[0],err,ERR_ACK);
      delay_us(300);
      //回读比对
      WRITE_CMD(0xFE,0x02,1,COMM_TIMEOUT,buf);
      BREAK_ASSERT2(buf[0],err,ERR_ACK);
      delay_us(300);
      WRITE_CMD(0xF0,0x00,17,COMM_TIMEOUT,buf);
      BREAK_ASSERT2(buf[16],err, ERR_ACK);      
      for(j=0;j<16;j++) {
        if(product[i].otp[j] != buf[j]) break;
      }      
      err = (j == 17) ? 0 : ERR_OTP;
      
			//任务完成
			//err = 0;

		} while (0);
    SetProductVpp(0x00);
    OWI_Idle();
		product[i].errCode = err;
		//SetProductVpp(0x00);
		HAL_Delay(1);
	}
	task.isTaskBusy = 0;
	task.prog = PRODUCT_NUM;
	task.order = TS_IDLE;
}
uint32_t ad;
void RTOS_CALDATA(void){
	uint16_t i = 0;
	uint16_t j = 0;
	uint8_t err = 0;
  uint8_t buf[4];
  
  
	task.isTaskBusy = 1;
	task.prog = 0;
	for (i = 0; i < PRODUCT_NUM; i++) {
		do {
			task.prog++;
			for (j = 0; j < 5; j++) {
				product[i].rawdata[j] = 0;
			}
			//有效的才进行
			err = product[i].errCode;
			if (err != 0) {
				break;
			}
			
      SetChannel(i);
      
      //
      if(task.paras[0] == 0xFF) {
        BREAK_ASSERT2(CMD_MODE(), err, ERR_CMD);
        delay_us(300);
        for(j =0; j< REG_NUM;j++){
          WRITE_CMD(0x20+j, product[i].otp[j], 1, COMM_TIMEOUT, buf);
          BREAK_ASSERT2(buf[0], err, ERR_ACK);
          delay_us(300);
        }
        BREAK_ASSERT2(buf[0], err, ERR_ACK);
        BREAK_ASSERT2(NOR_MODE(), err, ERR_NOR);
      }
      
      //
      HAL_Delay(10);
      ad = GET_ADC_Value();
      product[i].rawdata[0] = (uint8_t)(ad & 0xFF);
      product[i].rawdata[1] = (uint8_t)((ad & 0xFF00) >> 8);
      product[i].rawdata[2] = (uint8_t)((ad & 0xFF0000) >> 16);
      product[i].rawdata[3] = (uint8_t)((ad >> 24) & 0xFF);

			//任务完成
			err = 0;

		} while (0);
    OWI_Idle();
		product[i].errCode = err;
		HAL_Delay(1);
	}
	task.isTaskBusy = 0;
	task.prog = PRODUCT_NUM;
	task.order = TS_IDLE;
}

void AsciiToByteArray(const  char* str, uint16_t* array) {
    for (int i = 0; i < strlen(str); i++) {
        array[i] = str[i];
    }
}
