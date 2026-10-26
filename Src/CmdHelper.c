/*
 * CmdHelper.c
 *
 *  Created on: Feb 17, 2022
 *      Author: ITry
 */

#include <string.h>
#include "main.h"
#include "CmdHelper.h"
#include "product.h"

//一类：系统指令
 
#define TYPE1_GET_VERSION 		0x104
#define TYPE1_SET_PRODUCT_VPP	0x200
#define TYPE1_SET_PRODUCT_VDD	0x201

//multiple channels operation START
#define TYPE5_GET_PRODUCTS_TASK	0x66
#define TYPE5_SET_PRODUCTS_TASK	0x77
//multiple channels operation END

//single channel operation START
#define TYPE5_GET_PRODUCT_STATUS	0x600//：errCode/isOtp
#define TYPE5_GET_PRODUCT_RAWDATA	0x602
#define TYPE5_GET_PRODUCT_OTPDATA	0x604
#define TYPE5_SET_PRODUCT_OTPDATA	0x702
#define TYPE5_CLOSE_CHANNEL			0x704//closes the channel by pc
//single channel operation  END

uint16_t cmdHead;
int _i = 0;
int _j = 0;
int _k = 0;
uint16_t _ch = 0;
uint16_t cmdStartChannel;


TasksOrder Byte2Enum(uint16_t v) {
	if (v < TS_MAX) {
		return (TasksOrder) v;
	} else {
		// 处理错误或返回一个默认值
		return TS_MAX;
	}
}

uint8_t SetProductVpp(uint8_t dp) {
	//低电平有效
	GPIO_PinState ps = dp == 0xFF ? GPIO_PIN_SET : GPIO_PIN_RESET;
	HAL_GPIO_WritePin(VPP_EN_GPIO_Port, VPP_EN_Pin, ps);
	return HAL_GPIO_ReadPin(VPP_EN_GPIO_Port, VPP_EN_Pin) == ps ? 1 : 0;
}

uint8_t SetProductVdd(uint8_t dp) {
	//低电平有效
	GPIO_PinState ps = dp == 0xFF ? GPIO_PIN_SET : GPIO_PIN_RESET;
	//开之前	
	HAL_GPIO_WritePin(VDD_EN_GPIO_Port, VDD_EN_Pin, ps);
	//关之后
	return HAL_GPIO_ReadPin(VDD_EN_GPIO_Port, VDD_EN_Pin) == ps ? 1 : 0;
}

void ParseSystemCmd(uint16_t *cmd, uint8_t cmd_lens, uint16_t *out, uint16_t out_lens) 
{
	cmdHead = cmd[1];
	switch (cmdHead) 
	{
		case TYPE1_GET_VERSION://104:获取版本号
			AsciiToByteArray((const char*)FIRMWARE_VERSION, out);
		break;
        case TYPE1_SET_PRODUCT_VPP:
            out[0] = SetProductVpp(cmd[2]);
        break;
        case TYPE1_SET_PRODUCT_VDD:
            out[0] = SetProductVdd(cmd[2]);
        break;
        
		default:
		break;
	}
}

void ParseProductCmd(uint16_t *cmd, uint8_t cmd_lens, uint16_t *out, uint16_t out_lens)
{
    cmdHead = cmd[1];
	switch (cmdHead) {
	case TYPE5_GET_PRODUCTS_TASK:
		out[0] = task.isTaskBusy;
		out[1] = task.order;
		out[2] = task.prog;
		out[3] = task.paras[0];
		out[4] = task.paras[1];
		out[5] = task.paras[2];
		out[6] = task.paras[3];
		break;
	case TYPE5_SET_PRODUCTS_TASK:
		task.paras[0] = cmd[3];
		task.paras[1] = cmd[4];
		task.paras[2] = cmd[5];
		task.paras[3] = cmd[6];
		task.order = Byte2Enum(cmd[2]);
		task.isTaskBusy = 1;
		task.prog = 0;
		break;

	case TYPE5_GET_PRODUCT_STATUS:
		//由SENDBUF_LENS决定，目前一次最多读120/1=120颗
		cmdStartChannel = cmd[2];
		for (_i = 0; _i < 120; _i++) {
			_ch = cmdStartChannel + _i;
			if (_ch >= PRODUCT_NUM)
				break;
			out[_i] = ((uint16_t) product[_ch].IsOtp & 0x01) << 15;
			out[_i] |= product[_ch].errCode;
		}
		break;

	case TYPE5_GET_PRODUCT_RAWDATA:
		//由SENDBUF_LENS决定，目前一次最多读120/3=40颗
		cmdStartChannel = cmd[2];
		for (_i = 0; _i < 40; _i++) {
			_ch = cmdStartChannel + _i;
			if (_ch >= PRODUCT_NUM)
				break;
			out[_i * 3 + 0] = product[_ch].errCode << 8 | product[_ch].rawdata[0];
			out[_i * 3 + 1] = product[_ch].rawdata[1] << 8 | product[_ch].rawdata[2];
			out[_i * 3 + 2] = product[_ch].rawdata[3] << 8 | product[_ch].rawdata[4];
		}
		break;

	case TYPE5_GET_PRODUCT_OTPDATA:
		//由SENDBUF_LENS决定，目前一次最多读120/8=15颗
		cmdStartChannel = cmd[2];
		for (_i = 0; _i < 15; _i++) {
			_ch = cmdStartChannel + _i;
			if (_ch >= PRODUCT_NUM)
				break;
			_k = (int)(REG_NUM/2);
			for (_j = 0; _j < _k; _j++) {
				out[_i * _k + _j] = product[_ch].otp[_j*2];
				out[_i * _k + _j] <<= 8;
				out[_i * _k + _j] |= product[_ch].otp[_j*2+1];
			}
		}
		break;

	case TYPE5_SET_PRODUCT_OTPDATA:
		_ch = cmd[2];
		if (_ch < PRODUCT_NUM) {
			for (_i = 0; _i < REG_NUM; _i++) {
				product[_ch].otp[_i] = (uint8_t)cmd[3 + _i];
			}
		}
		break;

	case TYPE5_CLOSE_CHANNEL:
		_ch = cmd[2];
		if (_ch < PRODUCT_NUM)
			product[_ch].errCode = cmd[3];
		break;
	}
}
/*
 *
 */
void Parse(uint16_t *cmd, uint8_t cmd_lens, uint16_t *out, uint16_t out_lens) {
	uint16_t cmdType = cmd[0];
	switch (cmdType) {
	case 1:
		ParseSystemCmd(cmd, cmd_lens, out, out_lens);
		break;
    case 5:
        ParseProductCmd(cmd, cmd_lens, out, out_lens);
        break;
	default:
		break;
	}
}


