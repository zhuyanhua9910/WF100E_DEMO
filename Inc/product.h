#ifndef INC_PRODUCT_H_
#define INC_PRODUCT_H_

#include "main.h"
#include "saf8100.h"

#define FIRMWARE_VERSION "100E v1.1"
//从站地址
#define MODBUS_ADDRESS 1

//产品定义 START
#define JIG_ROW 2
#define JIG_COL 4
#define PRODUCT_NUM 8
#define REG_NUM 16

typedef struct Product {
	uint8_t errCode; //非0：跳过本产品
	uint8_t IsOtp : 1;
	uint8_t rawdata[5];//最近一次的原始数据
	uint8_t otp[REG_NUM];//
} Product;


//并发任务定义 START
typedef enum
{
  TS_IDLE = 0,
  TS_SCAN,
  TS_RAWDATA,
  TS_OTP,
  TS_CALDATA,
  TS_8100TEST = 5,   /* SAF8100 全流程测试：登录→读配置→写配置→回读→测温 */
  TS_8100MEAS = 6,   /* SAF8100 快速测温 */
  TS_8100FUSE = 7,   /* SAF8100 配置 + OTP 烧写 */
  TS_MAX
} TasksOrder;

typedef enum {
	ERR_NONE = 0xA5,
	ERR_CMD=1,
  ERR_NOR,
	ERR_ACK,
	ERR_PARAM,//参数异常
	ERR_SOFTRESET,//
  ERR_OTP,

	ERR_CLOSED_BYPC=0xF0, //上位机主动关闭
	ERR_MAX = 0xFF//单字节

} ERROR_CODE;//数据范围控制在单字节

typedef struct Task {
	TasksOrder order;
	uint8_t paras[5];
	uint8_t isTaskBusy : 1;
	uint16_t prog;//进度：当前通道号/PRODUCT_NUM
} Task;


extern struct Task task;
extern struct Product product[PRODUCT_NUM];

void RTOS_SCAN(void);
void RTOS_RAWDATA(void);
void RTOS_GET_OTPDATA(void);
void RTOS_SET_OTPDATA(void);
void RTOS_OTP(void);
void RTOS_CALDATA(void);
void RTOS_8100TEST(void);
void RTOS_8100MEAS(void);
void RTOS_8100FUSE(void);
void AsciiToByteArray(const char* str, uint16_t* array);
#endif//INC_PRODUCT_H_
