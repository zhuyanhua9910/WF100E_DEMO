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
 * File: $Id: portserial.c,v 1.1 2006/08/22 21:35:13 wolti Exp $
 */

#include "port.h"
 
/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
 
/* ----------------------- static functions ---------------------------------*/
//static void prvvUARTTxReadyISR( void );
//static void prvvUARTRxISR( void );
 
/* -----------------------    variables     ---------------------------------*/
extern UART_HandleTypeDef huart1;
 
/* ----------------------- Start implementation -----------------------------*/
void
vMBPortSerialEnable( BOOL xRxEnable, BOOL xTxEnable )
{
  /* If xRXEnable enable serial receive interrupts. If xTxENable enable
  * transmitter empty interrupts.
  */
  
  if (xRxEnable) {        
    /* 发送期间接收是关着的，若此时线上有字节/溢出，会留下 ORE 等错误标志，
     * 重新开接收时容易把残留字节当成新帧的第一字节，导致下一条命令被丢弃。
     * 开接收前先清掉这些标志 —— 但 __HAL_UART_CLEAR_PEFLAG 内部会读 DR，
     * 而读 DR 会清 RXNE，此刻若已有有效字节等着取就会被吞掉，所以只在
     * RXNE 为空时才清。 */
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) == RESET) {
      __HAL_UART_CLEAR_PEFLAG(&huart1);
    }
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
  } else {    
    __HAL_UART_DISABLE_IT(&huart1, UART_IT_RXNE);
  }
  
  if (xTxEnable) {    
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_TXE);
  } else {
    __HAL_UART_DISABLE_IT(&huart1, UART_IT_TXE);
  }  
}
 
void vMBPortClose( void )
{
 __HAL_UART_DISABLE_IT(&huart1, UART_IT_TC|UART_IT_RXNE);
 __HAL_UART_DISABLE(&huart1);
}

BOOL
xMBPortSerialInit( UCHAR ucPORT, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity )
{
  /* 
  Do nothing, Initialization is handled by MX_USART1_UART_Init()
  Fixed port, baudrate, databit and parity  
  */
	__HAL_UART_ENABLE_IT(&huart1,UART_IT_RXNE);
	__HAL_UART_ENABLE(&huart1);
			
  return TRUE;
}
 
BOOL
xMBPortSerialPutByte( CHAR ucByte )
{
  /* Put a byte in the UARTs transmit buffer. This function is called
   * by the protocol stack if pxMBFrameCBTransmitterEmpty( ) has been
   * called. */
  /* 注意：本函数是在 TXE 中断里被调用的，此时 TXE 已置位，直接把字节写进
   * 数据寄存器即可。原实现用 HAL_UART_Transmit(&huart1, &ucByte, 1, 10)，
   * 那是"阻塞式"发送：每写一个字节还要等 TC（115200 下约 87us），并且在
   * 中断里反复加锁/判状态。发短帧（几个字节）看不出问题，但发长帧时
   * （例如读 64 个输入寄存器 = 133 字节，要连续占用中断约 12ms）会出现
   * 丢字节，导致整帧 CRC 对不上、上位机只收到半包。
   * 实测：读 16 个寄存器（37 字节）正常，读 64 个（133 字节）只收到
   * 110~127 字节。改成直接写 DR 后，字节由 TXE 中断连续送出，不再丢。 */
  /* TXE 未置位说明上一字节还占着数据寄存器，此时写 DR 会被硬件直接忽略，
   * 而 FreeModbus 的发送状态机不看返回值、照样把发送计数减 1 —— 结果就是
   * 帧中间静默少一个字节，但 CRC 照样发在末尾，
   * 上位机看到的就是"字节数不够 + 末尾 CRC 不匹配"。
   * 所以这里先等 TXE 置位再写；上限约 100us（一个字节 87us），不会死等。 */
  {
    uint32_t guard = 0;
    while( __HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) == RESET )
    {
      if( ++guard > 3000u )
      {
        return FALSE;
      }
    }
  }
  huart1.Instance->DR = (uint8_t) ucByte;
  return TRUE;
}
 
BOOL
xMBPortSerialGetByte( CHAR * pucByte )
{
  /* Return the byte in the UARTs receive buffer. This function is called
  * by the protocol stack after pxMBFrameCBByteReceived( ) has been called.
  */
  *pucByte = (uint8_t)(huart1.Instance->DR & (uint8_t)0x00FF);
  return TRUE;
}
 
/* Create an interrupt handler for the transmit buffer empty interrupt
* (or an equivalent) for your target processor. This function should then
* call pxMBFrameCBTransmitterEmpty( ) which tells the protocol stack that
* a new character can be sent. The protocol stack will then call 
* xMBPortSerialPutByte( ) to send the character.

static void prvvUARTTxReadyISR( void )
{
pxMBFrameCBTransmitterEmpty(  );
}
*/
 
/* Create an interrupt handler for the receive interrupt for your target
* processor. This function should then call pxMBFrameCBByteReceived( ). The
* protocol stack will then call xMBPortSerialGetByte( ) to retrieve the
* character.

static void prvvUARTRxISR( void )
{
pxMBFrameCBByteReceived(  );
}
*/
