#ifndef INC_PRODUCT_H_
#define INC_PRODUCT_H_

#include "main.h"
#include "saf8100.h"

#define FIRMWARE_VERSION "8100 v1.0"
//从站地址
#define MODBUS_ADDRESS 1

//治具通道定义 START
#define JIG_ROW 2
#define JIG_COL 4
#define PRODUCT_NUM 8

//任务定义 START
typedef enum
{
  TS_IDLE = 0,
  TS_8100TEST = 5,   /* SAF8100 全流程测试：登录→读配置→写配置→回读→测温 */
  TS_8100MEAS = 6,   /* SAF8100 快速测温 */
  TS_8100FUSE = 7,   /* SAF8100 配置 + OTP 烧写 */
  TS_MAX
} TasksOrder;

typedef struct Task {
	TasksOrder order;
	uint8_t paras[5];
	uint8_t isTaskBusy : 1;
	uint16_t prog;//进度：当前通道号/PRODUCT_NUM
} Task;

extern struct Task task;

void RTOS_8100TEST(void);
void RTOS_8100MEAS(void);
void RTOS_8100FUSE(void);
void AsciiToByteArray(const char* str, uint16_t* array);
#endif//INC_PRODUCT_H_
