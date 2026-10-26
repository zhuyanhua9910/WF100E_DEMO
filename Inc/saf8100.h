/**
  ******************************************************************************
  * @file    saf8100.h
  * @brief   SAF8100 传感器信号调理芯片驱动（OWI 单线接口）
  * @note    移植自 8100_OWI_Test_V1.0.0_251010 官方例程
  ******************************************************************************
  */

#ifndef __SAF8100_H
#define __SAF8100_H

#include "main.h"
#include "saf8100_reg.h"

/* ========================= OWI 物理层参数 =========================
 * 官方例程：owiSetTxPhysics(32000, 32, 50, 75, 25)
 *   - 位速率  32000 Hz → 位周期 31.25us
 *   - 停止位  额外高电平 32us
 *   - 起始位  占空比 50%
 *   - 逻辑 1  占空比 75%（低电平占 25%）
 *   - 逻辑 0  占空比 25%（低电平占 75%）
 * 帧格式：起始位 + 8 位数据（MSB 先发）+ 偶校验位 + 停止位
 * ================================================================= */
#define SAF8100_OWI_T_BIT        31   /* 位周期 (us)，31.25us 取整 */
#define SAF8100_OWI_T_START      15   /* 起始位低电平 (us) */
#define SAF8100_OWI_T_1LOW       8    /* 逻辑 1：低电平时间 (us) */
#define SAF8100_OWI_T_0LOW       23   /* 逻辑 0：低电平时间 (us) */
#define SAF8100_OWI_T_STOP       32   /* 停止位附加高电平 (us) */

/* ========================= 命令码 ========================= */
#define SAF8100_CMD_LOGIN        0xF3 /* 登录 */
#define SAF8100_CMD_LOGIN_ARG    0xA0
#define SAF8100_CMD_READ         0xF4 /* 读寄存器：0xF4 + 地址 */
#define SAF8100_CMD_TEMP         0xF8 /* 读温度：0xF8 + 0x00 */
#define SAF8100_ACK              0x55 /* 应答首字节 */

/* ========================= 温度换算 =========================
 * 官方例程：T(℃) = (code - 32383.8) / 114.2
 * ============================================================ */
#define SAF8100_TEMP_OFFSET      32383.8f
#define SAF8100_TEMP_SLOPE       114.2f

/* ========================= 错误码 ========================= */
#define SAF8100_ERR_NONE         0x00
#define SAF8100_ERR_LOGIN        0x01 /* 登录失败 */
#define SAF8100_ERR_COMM         0x02 /* OWI 通信失败 */
#define SAF8100_ERR_VERIFY       0x03 /* 写后回读不一致 */
#define SAF8100_ERR_FUSE         0x04 /* OTP 烧写失败 */
#define SAF8100_ERR_NOTTESTED    0xFE /* 本通道未测试 / 上电初始态 */

/* ========================= 编译开关 =========================
 * 1 = 上电自动打开总电源 VDD_EN(PB2)
 * 若实际硬件为低有效，改为 0 或修改 saf8100_init() 中的电平
 * =========================================================== */
#define SAF8100_AUTO_VDD_ON      1

/* ========================= 数据结构 ========================= */
/* 单颗芯片的测试结果 */
typedef struct
{
    uint8_t  err;        /* 0 = 正常，见上面错误码 */
    uint8_t  login;      /* 登录应答，0x55 = 正常 */
    uint8_t  sys_ctrl;   /* 0x44 SYS_CTRL  系统控制 */
    uint8_t  pga_gain;   /* 0x45 PGA_GAIN  三级增益 */
    uint8_t  adc_ctrl;   /* 0x4B ADC_CTRL  采样率/平均次数 */
    uint8_t  pga_ctrl;   /* 0x48 PGA_CTRL  VREF/VCOM/斩波 */
    uint8_t  lut_sel;    /* 0x47 LUT_SEL   校准模式选择 */
    uint16_t act_gd;     /* 0x08/0x09 增益 DAC（14bit） */
    uint16_t act_zd;     /* 0x0A/0x0B 零点 DAC（14bit） */
    uint16_t temp_code;  /* 温度原始码 */
    int16_t  temp_x100;  /* 温度 × 100（℃） */
    uint8_t  fuse_done;  /* 1 = 本轮已执行 OTP 烧写且校验通过 */
} Saf8100Data;

/* 由上位机下发的配置 */
typedef struct
{
    uint16_t act_gd;     /* 增益 DAC 设定值 */
    uint16_t act_zd;     /* 零点 DAC 设定值 */
    uint8_t  pga_gain;   /* 0x45 */
    uint8_t  pga_ctrl;   /* 0x48 */
    uint8_t  adc_ctrl;   /* 0x4B */
    uint8_t  sys_ctrl;   /* 0x44（仅 ext_en = 1 时写入） */
    uint8_t  lut_sel;    /* 0x47（仅 ext_en = 1 时写入） */
    uint8_t  valid;      /* 1 = 已由上位机写入过 */
    uint8_t  ext_en;     /* 1 = 额外写 SYS_CTRL / LUT_SEL（默认 0，与官方 demo 一致） */
} Saf8100Cfg;

/* ========================= 全局变量 ========================= */
extern Saf8100Cfg  saf8100_cfg;
extern Saf8100Data saf8100_data[];   /* 长度 = PRODUCT_NUM，在 .c 中定义 */

/* ========================= 接口 ========================= */
void     saf8100_init(void);
void     saf8100_select(uint8_t ch);
void     saf8100_power_off(void);

uint8_t  saf8100_login(void);
uint8_t  saf8100_read_reg(uint8_t addr);
uint16_t saf8100_read_reg16(uint8_t addr);
uint8_t  saf8100_write_reg(uint8_t addr, uint8_t data);
void     saf8100_write_reg16(uint8_t addr, uint16_t data);
uint16_t saf8100_read_temp(void);
uint8_t  saf8100_fuse_otp(void);
uint8_t  saf8100_last_error(void);

/* 诊断：单通道 OWI 探测。返回登录应答（0x55 正常 / 0xFF 失败），
 * 同时把最近一次应答的原始相位、VDD 使能脚电平、OWI 线空闲电平带出去。
 * 用来区分"芯片完全没应答"和"应答了但解码失败"。 */
uint8_t  saf8100_owi_probe(uint8_t ch, uint16_t *phases,
                           uint16_t *low, uint16_t *high,
                           uint8_t *vdd, uint8_t *idle);

/* 位域组合辅助：把枚举值拼成寄存器字节 */
uint8_t  saf8100_make_pga(uint8_t g1, uint8_t g3, uint8_t g2);
uint8_t  saf8100_make_adc_ctrl(uint8_t osr, uint8_t avg_num);
uint8_t  saf8100_make_pga_ctrl(uint8_t vref_sel, uint8_t ref_gain, uint8_t vcm);

/* 单通道操作 */
uint8_t  saf8100_test_channel(uint8_t ch, const Saf8100Cfg *cfg, Saf8100Data *out);
uint8_t  saf8100_fuse_channel(uint8_t ch, const Saf8100Cfg *cfg, Saf8100Data *out);
uint8_t  saf8100_measure_channel(uint8_t ch, Saf8100Data *out);

#endif /* __SAF8100_H */
