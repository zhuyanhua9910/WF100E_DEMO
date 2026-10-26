/*
 * product.c
 *
 *  Created on: Oct 23, 2025
 *      Author: ITry
 *
 *  任务调度与治具通道映射（SAF8100 测试专用）
 */

#include "product.h"
#include <string.h>
#include "CmdHelper.h"
#include "saf8100.h"

/* ==================== 治具通道映射 ====================
 * 2 行 × 4 列 = 8 通道
 *   行(row) 决定 OWI 数据线（每行一根）
 *   列(col) 决定该通道的 VDD 使能脚
 * ===================================================== */
GPIO_TypeDef *OWI_PORTS[JIG_ROW] = { OUT1_GPIO_Port, OUT2_GPIO_Port };
uint16_t      OWI_PINS[JIG_ROW]  = { OUT1_Pin, OUT2_Pin };

GPIO_TypeDef *VDD_PORTS[JIG_COL] = { VDD1_EN_GPIO_Port, VDD2_EN_GPIO_Port,
                                     VDD3_EN_GPIO_Port, VDD4_EN_GPIO_Port };
uint16_t      VDD_PINS[JIG_COL]  = { VDD1_EN_Pin, VDD2_EN_Pin,
                                     VDD3_EN_Pin, VDD4_EN_Pin };

/* ==================== SAF8100 测试任务 ====================
 * 流程与官方 hal_saf8100_example() 一致：
 *   登录 → 读原始配置 → 写配置 → 回读校验 → 测温
 * 结果存入 saf8100_data[i]，由 Modbus 命令 0x610 回读
 *
 * 通道选择：task.paras[0] 为通道掩码（bit0~bit7 对应 0~7 号通道）
 *   0x00 或 0xFF → 全部 8 通道
 *   例如 0x01 → 只测 0 号通道，0x05 → 测 0 和 2 号
 * 未参与本次测试的通道结果置 err = 0xFE
 * ========================================================== */

#define CH_NOT_TESTED   0xFE

static uint8_t saf8100_channel_mask(void)
{
	uint8_t m = (uint8_t) task.paras[0];
	if ((m == 0x00) || (m == 0xFF)) {
		return 0xFF;
	}
	return m;
}

static void saf8100_mark_skipped(uint8_t mask)
{
	uint16_t i;
	for (i = 0; i < PRODUCT_NUM; i++) {
		if ((mask & (1u << i)) == 0) {
			memset(&saf8100_data[i], 0, sizeof(Saf8100Data));
			saf8100_data[i].err = CH_NOT_TESTED;
		}
	}
}

void RTOS_8100TEST(void)
{
	uint16_t i;
	uint8_t mask = saf8100_channel_mask();

	task.isTaskBusy = 1;
	task.prog = 0;
	saf8100_mark_skipped(mask);
	for (i = 0; i < PRODUCT_NUM; i++) {
		if ((mask & (1u << i)) == 0) {
			continue;
		}
		task.prog++;
		saf8100_test_channel((uint8_t)i, &saf8100_cfg, &saf8100_data[i]);
		HAL_Delay(1);
	}
	task.isTaskBusy = 0;
	task.prog = PRODUCT_NUM;
	task.order = TS_IDLE;
}

void RTOS_8100MEAS(void)
{
	uint16_t i;
	uint8_t mask = saf8100_channel_mask();

	task.isTaskBusy = 1;
	task.prog = 0;
	saf8100_mark_skipped(mask);
	for (i = 0; i < PRODUCT_NUM; i++) {
		if ((mask & (1u << i)) == 0) {
			continue;
		}
		task.prog++;
		saf8100_measure_channel((uint8_t)i, &saf8100_data[i]);
		HAL_Delay(1);
	}
	task.isTaskBusy = 0;
	task.prog = PRODUCT_NUM;
	task.order = TS_IDLE;
}

void RTOS_8100FUSE(void)
{
	uint16_t i;
	uint8_t mask = saf8100_channel_mask();

	task.isTaskBusy = 1;
	task.prog = 0;
	saf8100_mark_skipped(mask);
	for (i = 0; i < PRODUCT_NUM; i++) {
		if ((mask & (1u << i)) == 0) {
			continue;
		}
		task.prog++;
		saf8100_data[i].fuse_done = 0;
		/* 先按配置完整测试一遍，通过后再烧写 OTP */
		saf8100_fuse_channel((uint8_t)i, &saf8100_cfg, &saf8100_data[i]);
		HAL_Delay(1);
	}
	task.isTaskBusy = 0;
	task.prog = PRODUCT_NUM;
	task.order = TS_IDLE;
}

/* 把 ASCII 字符串逐字符转成 uint16 数组（版本号回读用） */
void AsciiToByteArray(const char* str, uint16_t* array)
{
	int i;
	for (i = 0; i < (int) strlen(str); i++) {
		array[i] = (uint16_t) str[i];
	}
}
