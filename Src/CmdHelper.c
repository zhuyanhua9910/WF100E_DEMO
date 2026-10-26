/*
 * CmdHelper.c
 *
 *  Created on: Feb 17, 2022
 *      Author: ITry
 *
 *  Modbus 命令解析（SAF8100 测试专用）
 */

#include <string.h>
#include "main.h"
#include "CmdHelper.h"
#include "product.h"

//一类：系统指令
#define TYPE1_GET_VERSION 		0x104
#define TYPE1_SET_PRODUCT_VPP	0x200
#define TYPE1_SET_PRODUCT_VDD	0x201

//任务控制
#define TYPE5_GET_PRODUCTS_TASK	0x66
#define TYPE5_SET_PRODUCTS_TASK	0x77

//SAF8100 操作
#define TYPE5_GET_8100_DATA		0x610
#define TYPE5_SET_8100_CONFIG	0x780

uint16_t cmdHead;
int _i = 0;
uint16_t _ch = 0;
uint16_t cmdStartChannel;


TasksOrder Byte2Enum(uint16_t v) {
	/* 只接受实际存在的任务号，避免上位机下发已删除的任务（1~4）
	 * 后静默不执行、现场难排查 */
	switch (v) {
	case TS_IDLE:
	case TS_8100TEST:
	case TS_8100MEAS:
	case TS_8100FUSE:
		return (TasksOrder) v;
	default:
		return TS_MAX;
	}
}

/* VPP 母线总闸（PB3）。注意：原理图上 VPP 是 8 个通道的高压/激励总开关，
 * 打开后 VPP1~VPP4 才有输出，操作前请确认被测芯片的耐压。 */
uint8_t SetProductVpp(uint8_t dp) {
	GPIO_PinState ps = dp == 0xFF ? GPIO_PIN_SET : GPIO_PIN_RESET;
	HAL_GPIO_WritePin(VPP_EN_GPIO_Port, VPP_EN_Pin, ps);
	return HAL_GPIO_ReadPin(VPP_EN_GPIO_Port, VPP_EN_Pin) == ps ? 1 : 0;
}

uint8_t SetProductVdd(uint8_t dp) {
	//低电平有效
	GPIO_PinState ps = dp == 0xFF ? GPIO_PIN_SET : GPIO_PIN_RESET;
	HAL_GPIO_WritePin(VDD_EN_GPIO_Port, VDD_EN_Pin, ps);
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

		case TYPE1_SET_PRODUCT_VPP://200:VPP 母线总闸
			out[0] = SetProductVpp(cmd[2]);
		break;

        case TYPE1_SET_PRODUCT_VDD://201:总电源
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

	/* 读任务状态：busy / order / prog / paras[0..3] */
	case TYPE5_GET_PRODUCTS_TASK:
		out[0] = task.isTaskBusy;
		out[1] = task.order;
		out[2] = task.prog;
		out[3] = task.paras[0];
		out[4] = task.paras[1];
		out[5] = task.paras[2];
		out[6] = task.paras[3];
		break;

	/* 下发任务：cmd[2] = 任务号，cmd[3] = 通道掩码，cmd[4..6] = 保留 */
	case TYPE5_SET_PRODUCTS_TASK:
		task.paras[0] = cmd[3];
		task.paras[1] = cmd[4];
		task.paras[2] = cmd[5];
		task.paras[3] = cmd[6];
		task.order = Byte2Enum(cmd[2]);
		task.isTaskBusy = 1;
		task.prog = 0;
		break;

	/* 读 SAF8100 测试数据：起始通道号，每颗 8 个寄存器
	 *   [0] login<<8 | err
	 *   [1] act_gd  (0x08/0x09 增益 DAC, 14bit)
	 *   [2] act_zd  (0x0A/0x0B 零点 DAC, 14bit)
	 *   [3] pga_gain<<8 | adc_ctrl
	 *   [4] pga_ctrl<<8 | sys_ctrl
	 *   [5] lut_sel<<8 | fuse_done
	 *   [6] temp_code
	 *   [7] temp_x100（温度 ×100，有符号）
	 * 120/8 = 15 颗，一次最多读 15 颗
	 */
	case TYPE5_GET_8100_DATA:
		cmdStartChannel = cmd[2];
		for (_i = 0; _i < 15; _i++) {
			_ch = cmdStartChannel + _i;
			if (_ch >= PRODUCT_NUM)
				break;
			out[_i * 8 + 0] = ((uint16_t) saf8100_data[_ch].login << 8) |
			                  saf8100_data[_ch].err;
			out[_i * 8 + 1] = saf8100_data[_ch].act_gd;
			out[_i * 8 + 2] = saf8100_data[_ch].act_zd;
			out[_i * 8 + 3] = ((uint16_t) saf8100_data[_ch].pga_gain << 8) |
			                  saf8100_data[_ch].adc_ctrl;
			out[_i * 8 + 4] = ((uint16_t) saf8100_data[_ch].pga_ctrl << 8) |
			                  saf8100_data[_ch].sys_ctrl;
			out[_i * 8 + 5] = ((uint16_t) saf8100_data[_ch].lut_sel << 8) |
			                  saf8100_data[_ch].fuse_done;
			out[_i * 8 + 6] = saf8100_data[_ch].temp_code;
			out[_i * 8 + 7] = (uint16_t) saf8100_data[_ch].temp_x100;
		}
		break;

	/* 写 SAF8100 配置（全局，下一次任务生效）
	 *   cmd[3] act_gd    0x08/0x09
	 *   cmd[4] act_zd    0x0A/0x0B
	 *   cmd[5] pga_gain  0x45
	 *   cmd[6] pga_ctrl  0x48
	 *   cmd[7] adc_ctrl  0x4B
	 *   cmd[8] sys_ctrl  0x44
	 *   cmd[9] lut_sel   0x47
	 *   cmd[10] 标志：bit0 = valid（1 生效），bit1 = ext_en（1 才写 0x44/0x47）
	 */
	case TYPE5_SET_8100_CONFIG:
		saf8100_cfg.act_gd   = cmd[3];
		saf8100_cfg.act_zd   = cmd[4];
		saf8100_cfg.pga_gain = (uint8_t) cmd[5];
		saf8100_cfg.pga_ctrl = (uint8_t) cmd[6];
		saf8100_cfg.adc_ctrl = (uint8_t) cmd[7];
		saf8100_cfg.sys_ctrl = (uint8_t) cmd[8];
		saf8100_cfg.lut_sel  = (uint8_t) cmd[9];
		saf8100_cfg.valid    = (uint8_t) (cmd[10] & 0x01);
		saf8100_cfg.ext_en   = (uint8_t) ((cmd[10] >> 1) & 0x01);
		out[0] = 1;
		break;

	default:
		break;
	}
}

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
