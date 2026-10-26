/**
  ******************************************************************************
  * @file    saf8100.c
  * @brief   SAF8100 传感器信号调理芯片驱动（OWI 单线接口）
  * @note    移植自 8100_OWI_Test_V1.0.0_251010 官方例程
  *          - 物理层：32kbps，起始位 + 8 数据位 + 偶校验 + 停止位
  *          - 时间基准：TIM1 自由运行 1MHz
  ******************************************************************************
  */

#include "saf8100.h"
#include "tim.h"
#include "product.h"
#include <string.h>

/* ================= 外部依赖 ================= */
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim14;

/* 通道映射表在 product.c 中定义 */
extern GPIO_TypeDef *OWI_PORTS[];
extern uint16_t      OWI_PINS[];
extern GPIO_TypeDef *VDD_PORTS[];
extern uint16_t      VDD_PINS[];

/* ================= 全局数据 ================= */
Saf8100Cfg  saf8100_cfg;
Saf8100Data saf8100_data[PRODUCT_NUM];

static GPIO_TypeDef *g_owi_port;
static uint16_t      g_owi_pin;
static GPIO_TypeDef *g_vdd_port;
static uint16_t      g_vdd_pin;
static uint8_t       g_err;

/* ================= 时间基准（TIM1，1MHz） ================= */
#define OWI_READ()  HAL_GPIO_ReadPin(g_owi_port, g_owi_pin)
#define OWI_LOW()   HAL_GPIO_WritePin(g_owi_port, g_owi_pin, GPIO_PIN_RESET)
#define OWI_HIGH()  HAL_GPIO_WritePin(g_owi_port, g_owi_pin, GPIO_PIN_SET)

static inline uint16_t tus(void)
{
    return (uint16_t)__HAL_TIM_GET_COUNTER(&htim1);
}

/* 等待到 t0 + dt（回绕安全，dt 需 < 32767） */
static inline void twait(uint16_t t0, uint16_t dt)
{
    while ((int16_t)(tus() - (uint16_t)(t0 + dt)) < 0)
    {
    }
}

static void tdelay(uint16_t us)
{
    uint16_t t0 = tus();
    twait(t0, us);
}

/* ================= 引脚方向 ================= */
static void owi_pin_out(void)
{
    GPIO_InitTypeDef s = {0};
    HAL_GPIO_WritePin(g_owi_port, g_owi_pin, GPIO_PIN_SET);
    s.Pin   = g_owi_pin;
    s.Mode  = GPIO_MODE_OUTPUT_OD;
    s.Pull  = GPIO_NOPULL;
    s.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(g_owi_port, &s);
}

static void owi_pin_in(void)
{
    GPIO_InitTypeDef s = {0};
    s.Pin  = g_owi_pin;
    s.Mode = GPIO_MODE_INPUT;
    s.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(g_owi_port, &s);
}

/* 通信期间关闭 TIM14（Modbus T3.5）中断，避免中断抖动破坏位时序 */
static void owi_isr_off(void)
{
    __HAL_TIM_DISABLE_IT(&htim14, TIM_IT_UPDATE);
}

static void owi_isr_on(void)
{
    __HAL_TIM_ENABLE_IT(&htim14, TIM_IT_UPDATE);
}

/* ================= 发送 ================= */
static void owi_tx_byte(uint8_t b)
{
    uint8_t i;
    uint8_t parity = 0;

    /* 起始位：低 50% */
    OWI_LOW();
    tdelay(SAF8100_OWI_T_START);
    OWI_HIGH();
    tdelay(SAF8100_OWI_T_BIT - SAF8100_OWI_T_START);

    /* 8 位数据，MSB 先发 */
    for (i = 0; i < 8; i++)
    {
        uint8_t bit = (b >> (7 - i)) & 0x01;
        parity ^= bit;
        if (bit)
        {
            OWI_LOW();
            tdelay(SAF8100_OWI_T_1LOW);
            OWI_HIGH();
            tdelay(SAF8100_OWI_T_BIT - SAF8100_OWI_T_1LOW);
        }
        else
        {
            OWI_LOW();
            tdelay(SAF8100_OWI_T_0LOW);
            OWI_HIGH();
            tdelay(SAF8100_OWI_T_BIT - SAF8100_OWI_T_0LOW);
        }
    }

    /* 校验位（偶校验）+ 停止位 */
    if (parity)
    {
        OWI_LOW();
        tdelay(SAF8100_OWI_T_1LOW);
        OWI_HIGH();
        tdelay(SAF8100_OWI_T_BIT - SAF8100_OWI_T_1LOW + SAF8100_OWI_T_STOP);
    }
    else
    {
        OWI_LOW();
        tdelay(SAF8100_OWI_T_0LOW);
        OWI_HIGH();
        tdelay(SAF8100_OWI_T_BIT - SAF8100_OWI_T_0LOW + SAF8100_OWI_T_STOP);
    }
}

static void owi_tx(const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    owi_pin_out();
    __disable_irq();
    for (i = 0; i < len; i++)
    {
        owi_tx_byte(buf[i]);
    }
    __enable_irq();
}

/* ================= 接收 ================= */
#define RX_MAX_PHASE   64

static uint16_t rxLow[RX_MAX_PHASE];
static uint16_t rxHigh[RX_MAX_PHASE];
static uint8_t  rxPhases;

/* 采样总线上的低/高电平持续时间，直到总线空闲（停止位后保持高） */
static void owi_capture(uint16_t start_timeout_us)
{
    uint16_t t0, d;
    uint8_t  i = 0;

    rxPhases = 0;
    owi_pin_in();

    /* 等待起始位下降沿 */
    t0 = tus();
    while (OWI_READ())
    {
        if ((uint16_t)(tus() - t0) > start_timeout_us)
        {
            return;
        }
    }

    while (i < RX_MAX_PHASE)
    {
        /* 低电平持续时间 */
        t0 = tus();
        while (!OWI_READ())
        {
            if ((uint16_t)(tus() - t0) > 100)
            {
                break;
            }
        }
        rxLow[i] = (uint16_t)(tus() - t0);

        /* 低电平过短，说明已到帧尾（总线保持高） */
        if ((i > 0) && (rxLow[i] < 3))
        {
            break;
        }

        /* 高电平持续时间 */
        t0 = tus();
        while (OWI_READ())
        {
            if ((uint16_t)(tus() - t0) > 120)
            {
                break;
            }
        }
        rxHigh[i] = (uint16_t)(tus() - t0);

        d = rxHigh[i];
        i++;

        /* 高电平超过 100us，总线已空闲，帧结束 */
        if (d > 100)
        {
            break;
        }
    }

    rxPhases = i;
}

/* 按占空比解码：低电平 < 37.5% 周期 → 1；< 87.5% → 0 */
static uint8_t owi_decode(uint8_t *buf, uint8_t maxlen)
{
    uint8_t  n = 0;
    uint8_t  i = 0;
    uint8_t  b, bit, parity;
    uint16_t per;

    while ((i < rxPhases) && (n < maxlen))
    {
        per = (uint16_t)(rxLow[i] + rxHigh[i]);
        if (per < 10)
        {
            i++;
            continue;
        }

        /* 起始位：低电平占 37.5%~62.5% */
        if ((rxLow[i] > (per * 3 / 8)) && (rxLow[i] < (per * 5 / 8)))
        {
            b = 0;
            parity = 0;

            for (uint8_t k = 0; k < 8; k++)
            {
                i++;
                if (i >= rxPhases)
                {
                    return n;
                }
                per = (uint16_t)(rxLow[i] + rxHigh[i]);
                if (rxLow[i] < (per * 3 / 8))
                {
                    bit = 1;
                }
                else if (rxLow[i] < (per * 7 / 8))
                {
                    bit = 0;
                }
                else
                {
                    return n;
                }
                b = (uint8_t)((b << 1) | bit);
                parity ^= bit;
            }

            /* 校验位 */
            i++;
            if (i >= rxPhases)
            {
                return n;
            }
            per = (uint16_t)(rxLow[i] + rxHigh[i]);
            bit = (rxLow[i] < (per * 3 / 8)) ? 1 : 0;
            if (bit != parity)
            {
                return n;   /* 校验错，丢弃本字节及其后数据 */
            }

            /* 第一个字节的起始位判定通过后，允许连收 */
            buf[n++] = b;
            i++;
        }
        else
        {
            i++;
        }
    }

    return n;
}

/* 发送并接收，返回收到的字节数 */
static uint8_t owi_transceive(const uint8_t *tx, uint8_t txlen,
                              uint8_t *rx, uint8_t rxmax,
                              uint16_t start_timeout_us)
{
    uint8_t n;

    owi_isr_off();
    owi_tx(tx, txlen);
    owi_capture(start_timeout_us);
    owi_isr_on();

    n = owi_decode(rx, rxmax);
    return n;
}

/* ================= 通道控制 ================= */
void saf8100_select(uint8_t ch)
{
    uint8_t row, col;

    if (ch >= PRODUCT_NUM)
    {
        return;
    }
    row = (uint8_t)(ch / JIG_COL);
    col = (uint8_t)(ch % JIG_COL);

    g_owi_port = OWI_PORTS[row];
    g_owi_pin  = OWI_PINS[row];
    g_vdd_port = VDD_PORTS[col];
    g_vdd_pin  = VDD_PINS[col];

    owi_pin_out();
}

void saf8100_power_off(void)
{
    /* 断电（本治具 VDD 控制为高有效，与官方 demo 相反） */
    HAL_GPIO_WritePin(g_vdd_port, g_vdd_pin, GPIO_PIN_RESET);
    owi_pin_out();
}

/* 官方 demo 的上电时序：断电 100ms → 上电 → 等 20ms */
static void saf8100_power_cycle(void)
{
    HAL_GPIO_WritePin(g_vdd_port, g_vdd_pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(g_vdd_port, g_vdd_pin, GPIO_PIN_SET);
    HAL_Delay(20);
}

/* ================= 基础命令 ================= */
uint8_t saf8100_last_error(void)
{
    return g_err;
}

uint8_t saf8100_login(void)
{
    uint8_t tx[2] = {SAF8100_CMD_LOGIN, SAF8100_CMD_LOGIN_ARG};
    uint8_t rx[4] = {0};
    uint8_t n;

    saf8100_power_cycle();
    n = owi_transceive(tx, 2, rx, 4, 3000);

    if ((n >= 1) && (rx[0] == SAF8100_ACK))
    {
        return SAF8100_ACK;
    }
    return 0xFF;
}

uint8_t saf8100_read_reg(uint8_t addr)
{
    uint8_t tx[2] = {SAF8100_CMD_READ, addr};
    uint8_t rx[4] = {0};
    uint8_t n;

    n = owi_transceive(tx, 2, rx, 4, 3000);
    if ((n >= 2) && (rx[0] == SAF8100_ACK))
    {
        return rx[1];
    }
    g_err = SAF8100_ERR_COMM;
    return 0xFF;
}

uint16_t saf8100_read_reg16(uint8_t addr)
{
    uint16_t h = saf8100_read_reg(addr);
    uint16_t l = saf8100_read_reg((uint8_t)(addr + 1));
    return (uint16_t)((h << 8) | l);
}

uint8_t saf8100_write_reg(uint8_t addr, uint8_t data)
{
    uint8_t tx[2] = {addr, data};
    uint8_t rx[4] = {0};
    uint8_t n;

    n = owi_transceive(tx, 2, rx, 4, 3000);
    if ((n >= 1) && (rx[0] == SAF8100_ACK))
    {
        return 0;
    }
    g_err = SAF8100_ERR_COMM;
    return 1;
}

void saf8100_write_reg16(uint8_t addr, uint16_t data)
{
    saf8100_write_reg(addr, (uint8_t)(data >> 8));
    saf8100_write_reg((uint8_t)(addr + 1), (uint8_t)(data & 0xFF));
}

uint16_t saf8100_read_temp(void)
{
    uint8_t tx[2] = {SAF8100_CMD_TEMP, 0x00};
    uint8_t rx[4] = {0};
    uint8_t n;

    n = owi_transceive(tx, 2, rx, 4, 3000);
    if ((n >= 3) && (rx[0] == SAF8100_ACK))
    {
        return (uint16_t)(((uint16_t)rx[1] << 8) | rx[2]);
    }
    g_err = SAF8100_ERR_COMM;
    return 0;
}

/* 触发 OTP 烧写：EFUSE_CMD = 0x81（WR=1 + BURW=1），保持 50ms */
uint8_t saf8100_fuse_otp(void)
{
    if (saf8100_write_reg(EFUSE_CMD_REG, 0x81) != 0)
    {
        return 1;
    }
    HAL_Delay(50);
    return 0;
}

/* ================= 位域组合 ================= */
uint8_t saf8100_make_pga(uint8_t g1, uint8_t g3, uint8_t g2)
{
    return (uint8_t)(((g1 & 0x07) << 5) | ((g3 & 0x07) << 2) | (g2 & 0x03));
}

uint8_t saf8100_make_adc_ctrl(uint8_t osr, uint8_t avg_num)
{
    return (uint8_t)(((osr & 0x03) << 3) | (avg_num & 0x07));
}

uint8_t saf8100_make_pga_ctrl(uint8_t vref_sel, uint8_t ref_gain, uint8_t vcm)
{
    return (uint8_t)(((vref_sel & 0x01) << 7) |
                     ((ref_gain & 0x01) << 6) |
                     (vcm & 0x03));
}

/* ================= 单通道测试流程 =================
 * 与官方 hal_saf8100_example() 保持一致：
 *   1. 上电 + 登录
 *   2. 读原始配置（GD / ZD / PGA / ADC / SYS / LUT）
 *   3. 写配置（GD / ZD / PGA / PGA_CTRL / ADC_CTRL / SYS_CTRL / LUT_SEL）
 *   4. 回读校验
 *   5. 延时 200ms 后读温度
 * ================================================== */
static void saf8100_read_all(Saf8100Data *out)
{
    out->sys_ctrl = saf8100_read_reg(SYS_CTRL_REG);
    out->pga_gain = saf8100_read_reg(PGA_GAIN_REG);
    out->adc_ctrl = saf8100_read_reg(ADC_CTRL_REG);
    out->pga_ctrl = saf8100_read_reg(PGA_CTRL_REG);
    out->lut_sel  = saf8100_read_reg(LUT_SEL_REG);
    out->act_gd   = saf8100_read_reg16(ACT_GDH_REG);
    out->act_zd   = saf8100_read_reg16(ACT_ZDH_REG);
}

static uint8_t saf8100_run(uint8_t ch, const Saf8100Cfg *cfg, Saf8100Data *out, uint8_t do_fuse)
{
    uint8_t rd;

    memset(out, 0, sizeof(Saf8100Data));
    g_err = SAF8100_ERR_NONE;

    saf8100_select(ch);

    /* 1. 登录 */
    out->login = saf8100_login();
    if (out->login != SAF8100_ACK)
    {
        out->err = SAF8100_ERR_LOGIN;
        goto done;
    }

    /* 2. 读原始配置 */
    saf8100_read_all(out);
    if (g_err != SAF8100_ERR_NONE)
    {
        out->err = SAF8100_ERR_COMM;
        goto done;
    }

    /* 3. 写配置 */
    if ((cfg != NULL) && cfg->valid)
    {
        g_err = SAF8100_ERR_NONE;
        saf8100_write_reg16(ACT_GDH_REG, cfg->act_gd);
        saf8100_write_reg16(ACT_ZDH_REG, cfg->act_zd);
        saf8100_write_reg(PGA_GAIN_REG, cfg->pga_gain);
        saf8100_write_reg(PGA_CTRL_REG, cfg->pga_ctrl);
        saf8100_write_reg(ADC_CTRL_REG, cfg->adc_ctrl);
        /* SYS_CTRL / LUT_SEL 官方 demo 不写，默认保持原值，
         * 仅当上位机显式要求（ext_en=1）时才写入，避免清掉芯片内已有校准位 */
        if (cfg->ext_en)
        {
            saf8100_write_reg(SYS_CTRL_REG, cfg->sys_ctrl);
            saf8100_write_reg(LUT_SEL_REG, cfg->lut_sel);
        }
        if (g_err != SAF8100_ERR_NONE)
        {
            out->err = SAF8100_ERR_COMM;
            goto done;
        }

        /* 4. 回读校验 */
        rd = saf8100_read_reg(PGA_GAIN_REG);
        if (rd != cfg->pga_gain)
        {
            out->err = SAF8100_ERR_VERIFY;
            goto done;
        }
        rd = saf8100_read_reg(ADC_CTRL_REG);
        if (rd != cfg->adc_ctrl)
        {
            out->err = SAF8100_ERR_VERIFY;
            goto done;
        }
        if (saf8100_read_reg16(ACT_GDH_REG) != cfg->act_gd)
        {
            out->err = SAF8100_ERR_VERIFY;
            goto done;
        }
        if (saf8100_read_reg16(ACT_ZDH_REG) != cfg->act_zd)
        {
            out->err = SAF8100_ERR_VERIFY;
            goto done;
        }
    }

    /* 5. 读回最终生效值 + 温度 */
    saf8100_read_all(out);
    HAL_Delay(200);
    out->temp_code = saf8100_read_temp();
    out->temp_x100 = (int16_t)(((float)out->temp_code - SAF8100_TEMP_OFFSET)
                               / SAF8100_TEMP_SLOPE * 100.0f);

    if (g_err != SAF8100_ERR_NONE)
    {
        out->err = SAF8100_ERR_COMM;
    }
    if (out->err != SAF8100_ERR_NONE)
    {
        goto done;
    }

    /* 6. OTP 烧写（由调用方决定是否执行）
     *    官方说明：寄存器 0x44~0xB5 会被写入 OTP，每颗芯片只能烧一次 */
    if (do_fuse)
    {
        if (saf8100_fuse_otp() != 0)
        {
            out->err = SAF8100_ERR_FUSE;
            goto done;
        }

        /* 烧写后回读关键配置，确认内容已固化 */
        HAL_Delay(10);
        rd = saf8100_read_reg(PGA_GAIN_REG);
        if (rd != out->pga_gain)
        {
            out->err = SAF8100_ERR_VERIFY;
            goto done;
        }
        if (saf8100_read_reg16(ACT_GDH_REG) != out->act_gd)
        {
            out->err = SAF8100_ERR_VERIFY;
            goto done;
        }
        out->fuse_done = 1;
    }

done:
    saf8100_power_off();
    return out->err;
}

uint8_t saf8100_test_channel(uint8_t ch, const Saf8100Cfg *cfg, Saf8100Data *out)
{
    return saf8100_run(ch, cfg, out, 0);
}

uint8_t saf8100_fuse_channel(uint8_t ch, const Saf8100Cfg *cfg, Saf8100Data *out)
{
    return saf8100_run(ch, cfg, out, 1);
}

/* 只测温（用于产线快速抽检） */
uint8_t saf8100_measure_channel(uint8_t ch, Saf8100Data *out)
{
    memset(out, 0, sizeof(Saf8100Data));
    g_err = SAF8100_ERR_NONE;

    saf8100_select(ch);
    out->login = saf8100_login();
    if (out->login != SAF8100_ACK)
    {
        out->err = SAF8100_ERR_LOGIN;
        saf8100_power_off();
        return out->err;
    }

    HAL_Delay(200);
    out->temp_code = saf8100_read_temp();
    out->temp_x100 = (int16_t)(((float)out->temp_code - SAF8100_TEMP_OFFSET)
                               / SAF8100_TEMP_SLOPE * 100.0f);
    if (g_err != SAF8100_ERR_NONE)
    {
        out->err = SAF8100_ERR_COMM;
    }

    saf8100_power_off();
    return out->err;
}

/* ================= 初始化 ================= */
void saf8100_init(void)
{
    uint16_t i;

    /* 确保 TIM1 以 1MHz 自由运行，作为 OWI 时间基准 */
    if (HAL_TIM_Base_Start(&htim1) != HAL_OK)
    {
        APP_ErrorHandler();
    }

    memset(&saf8100_cfg, 0, sizeof(saf8100_cfg));
    memset(saf8100_data, 0, sizeof(Saf8100Data) * PRODUCT_NUM);

    /* 关键：上电时所有通道必须标记为"未测试"。
     * 若保持 0，err=0 会被上位机误判为"全部通过"。 */
    for (i = 0; i < PRODUCT_NUM; i++)
    {
        saf8100_data[i].err = SAF8100_ERR_NOTTESTED;
    }

#if SAF8100_AUTO_VDD_ON
    /* 打开总电源（U9 TPS7B7702 的 EN，PB2）。
     * 不开则 VDD 与 +7.5V 都无输出，所有通道必然登录失败。
     * 若实际硬件为低有效，把这里改成 GPIO_PIN_RESET。 */
    HAL_GPIO_WritePin(VDD_EN_GPIO_Port, VDD_EN_Pin, GPIO_PIN_SET);
#endif

    /* 上电默认配置，取值与官方 demo 一致：
     *   ZD = 16383, GD = 10000
     *   VREF_SEL = 0（内部 2.5V）, REF_GAIN = 1（×1.6384）, VCM = 2.5V
     *   OSR = 128, AVG_NUM = 4
     *   G1 = 4x, G3 = 4x, G2 = 4x
     */
    saf8100_cfg.act_zd   = 16383;
    saf8100_cfg.act_gd   = 10000;
    saf8100_cfg.pga_ctrl = saf8100_make_pga_ctrl(0, 1, VCM_SEL_2_5_V);
    saf8100_cfg.adc_ctrl = saf8100_make_adc_ctrl(OSR_128, AVG_NUM_4);
    saf8100_cfg.pga_gain = saf8100_make_pga(PGA_G1_4X, PGA_G3_4X, PGA_G2_4X);
    saf8100_cfg.sys_ctrl = 0x00;
    saf8100_cfg.lut_sel  = 0x00;
    saf8100_cfg.valid    = 1;
}
