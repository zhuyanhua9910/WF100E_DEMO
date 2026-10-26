/*
 * FreeModbus Libary: BARE Port
 * Copyright (C) 2006 Christian Walter <wolti@sil.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: $Id: porttimer.c,v 1.1 2006/08/22 21:35:13 wolti Exp $
 */

/* ----------------------- Platform includes --------------------------------*/
#include "port.h"
 
/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
#include "tim.h"
/* ----------------------- static functions ---------------------------------*/
//static void prvvTIMERExpiredISR( void );
 
/* -----------------------    variables     ---------------------------------*/
 uint8_t modbusStatus;

/* T3.5 静默计数：每收到一个字节清零，定时器每 tick 加 1。
 * 只有连续 2 个 tick（≥1.75ms）没有新字节，才认为一帧结束。
 * 原来的写法只用一个 modbusStatus 标志，定时器中断每 1.75ms 进来一次就
 * 无条件报"帧结束"，一旦帧的传输时间超过 1.75ms（115200 下 20 个字节），
 * tick 就会落在帧中间，把半截帧当完整帧去校验 → CRC 失败 → 从机静默丢弃。
 * 现象：13 字节读版本基本正常，23 字节的 0x77 任务命令时好时坏。 */
static volatile uint8_t t35_ticks;

/* ===== 通信质量计数（0x6A1 读取，用于判断丢帧原因） ===== */
volatile USHORT mb_rx_bytes;
volatile USHORT mb_frames_ok;
volatile USHORT mb_crc_fail;
volatile USHORT mb_ore_cnt;
volatile UCHAR  mb_max_gap;

void
vMBPortCountOverrun( void )
{
    if( mb_ore_cnt < 0xFFFFu )
    {
        mb_ore_cnt++;
    }
}

/* 帧内收到新字节时调用：记录"距上一个字节隔了几个 tick" */
void
vMBPortRecordByteGap( void )
{
    if( t35_ticks > mb_max_gap )
    {
        mb_max_gap = t35_ticks;
    }
    mb_rx_bytes++;
}

/* 判帧静默时间 = MB_T35_TICKS × 1.75ms。
 *
 * 标准 Modbus 在 115200 下 T3.5 = 1.75ms，但那是"线上一串连续字节"的
 * 假设。实际的 USB 转串口适配器会把一帧拆成几个 USB 包发出，包与包之间
 * 可能隔 1~10ms —— 从机按 1.75ms 判帧，就会把这帧截成"半帧"，CRC 校验
 * 失败后静默丢弃（FreeModbus 对坏帧不回应）。
 *
 * 现场表现完全对应：
 *   13 字节（读版本/查状态）→ 一次 USB 包发完 → 一直正常；
 *   23 字节（0x77 下发任务）→ 被拆成 2 个包 → 时好时坏、常常连丢 3 次；
 *   15 字节（0x610）→ 卡在包边界上 → 偶发丢。
 *
 * 所以放宽到 8 个 tick ≈ 14ms。代价是每帧处理晚 14ms（本应用完全可接受），
 * 换来的是不再因为 USB 包间隔被误判成半帧。 */
#define MB_T35_TICKS        20

/* ----------------------- Start implementation -----------------------------*/
BOOL
xMBPortTimersInit( USHORT usTim1Timerout50us )
{
	
  htim14.Instance = TIM14;                                         		 			/* TIM1 */
  htim14.Init.Prescaler         = (HAL_RCC_GetPCLK1Freq() / 1000000) - 1;   /* Period = 24000 - 1 */
  htim14.Init.Period       		 	= (usTim1Timerout50us * 50) - 1;           /* Prescaler = 1000 - 1 */
  htim14.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;          					/* ClockDivision = 0 */
  htim14.Init.CounterMode       = TIM_COUNTERMODE_UP;             				  /* Counter direction = Up */
  htim14.Init.RepetitionCounter = 0;                              				  /* Repetition = 0 */
  htim14.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;  			  	/* Auto-reload register not buffered */
  if (HAL_TIM_Base_Init(&htim14) != HAL_OK)                       				  /* Initialize TIM1 */
  {
    APP_ErrorHandler();
  }
  return TRUE;
}
 
 
void
vMBPortTimersEnable(  )
{
  /* 每收一个字节都会调到这里：清掉待处理的更新标志并把计数器归零，
   * 同时把"静默 tick 数"清零，重新开始计 T3.5。 */
    __HAL_TIM_CLEAR_FLAG(&htim14, TIM_FLAG_UPDATE);
    __HAL_TIM_SET_COUNTER(&htim14, 0);
    t35_ticks = 0;
    modbusStatus = 1;
}
 
void
vMBPortTimersDisable(  )
{
    modbusStatus = 0;
    t35_ticks = 0;
}

/* 由 TIM14 中断每 1.75ms 调用一次；返回 1 表示 T3.5 静默时间已满足。
 * 改成"计数"而不是"看标志"，就不会再出现 tick 落在帧中间误判的问题。 */
uint8_t
mbT35Tick( void )
{
    if( modbusStatus == 0 )
    {
        t35_ticks = 0;
        return 0;
    }
    if( ++t35_ticks >= MB_T35_TICKS )
    {
        t35_ticks = 0;
        return 1;
    }
    return 0;
}
 
/* Create an ISR which is called whenever the timer has expired. This function
* must then call pxMBPortCBTimerExpired( ) to notify the protocol stack that
* the timer has expired.
 
static void prvvTIMERExpiredISR( void )
{
( void )pxMBPortCBTimerExpired(  );
}
*/
