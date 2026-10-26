#ifndef __SAF8100_REG_H
#define __SAF8100_REG_H

#include <stdint.h>

// EFUSE Registers
#define EFUSE_ADDR_REG    (0x02)    // [7:0] EFUSE read/write address

#define EFUSE_WDAT_REG    (0x03)    // [7:0] EFUSE write data

#define EFUSE_CMD_REG     (0x04)
typedef union {
    struct {
		uint8_t EFUSE_WR      : 1;  // Write operation (W1C)
		uint8_t EFUSE_RD      : 1;  // Read operation (W1C)
		uint8_t RESERVED      : 5;  // [6:2] Reserved
        uint8_t EFUSE_BURW    : 1;  // Burst write enable
    } bit;
    uint8_t reg;
} efuse_cmd_reg_t;

#define EFUSE_RDATA_REG    (0x05)   // [7:0] EFUSE read data

// Temperature Sensors
#define ACT_TH_REG        (0x06)
#define ACT_TL_REG        (0x07)
typedef union {
    struct {
        uint16_t ACT_TH : 8;  // Active temperature high byte
        uint16_t ACT_TL : 8;  // Active temperature low byte
    } reg;
    uint16_t value;
} act_t_reg_t;

// Gain DAC Registers
#define ACT_GDH_REG       (0x08)
#define ACT_GDL_REG       (0x09)
typedef union {
    struct {
        uint16_t ACT_GDH : 6;  // Active gain DAC high bits
        uint16_t ACT_GDL : 8;  // Active gain DAC low byte
    } reg;
    uint16_t value;
} act_gd_reg_t;

// Zero DAC Registers
#define ACT_ZDH_REG       (0x0A)
#define ACT_ZDL_REG       (0x0B)
typedef union {
    struct {
        uint16_t ACT_ZDH : 6;  // Active zero DAC high bits
        uint16_t ACT_ZDL : 8;  // Active zero DAC low byte
    } reg;
    uint16_t value;
} act_zd_reg_t;

// OWI Control Registers
#define OWI_QUIT_REG      (0x20)    // Write any value will exit the OWI

#define OWI_RECNTH_REG     (0x21)
#define OWI_RECNTL_REG     (0x22)
typedef union {
    struct {
        uint16_t OWI_RECNTH : 8;  // Re-entry counter high byte
        uint16_t OWI_RECNTL : 8;  // Re-entry counter low byte
    } reg;
    uint16_t value;
} owi_recnt_reg_t;

// Chop Control Register
#define CHOP_CTRL_REG      (0x23)
typedef union {
    struct {
		uint8_t CHOP_SEL1P : 1;  // PGA1 positive input chop clock select
		uint8_t CHOP_SEL1N : 1;  // PGA1 negative input chop clock select
		uint8_t CHOP_SEL2  : 1;  // PGA2 chop clock select
		uint8_t CHOP_SEL3  : 1;  // PGA3 chop clock select
		uint8_t MCHOP_SEL  : 1;  // ADC chop clock select (reserved)
		uint8_t MCHOP_PD   : 1;  // ADC chop enable/disable (reserved)
		uint8_t UPT_SEL    : 1;  // DAC update time select (reserved)
        uint8_t SCAN_RST   : 1;  // Scan reset select (reserved)
    } bit;
    uint8_t reg;
} chop_ctrl_reg_t;

// Trimming Registers
#define TC_TRIM_REG       (0x3D)
typedef union {
    struct {
		uint8_t TC_TRIM  : 7;  // Temperature trimming bits (reserved)
        uint8_t RESERVED : 1;
    } bit;
    uint8_t reg;
} tc_trim_reg_t;

#define CC_TRIM_REG       (0x3E)
typedef union {
    struct {
		uint8_t CC_TRIM     : 7;  // Current trimming bits (reserved)
        uint8_t RESERVED 	: 1;
    } bit;
    uint8_t reg;
} cc_trim_reg_t;

#define ABS_TRIM_REG      (0x3F)
typedef union {
    struct {
		uint8_t ABS_TRIM  : 6;  // Absolute trimming bits
		uint8_t RESERVED  : 1;
        uint8_t PNP_SEL   : 1;  // PNP bipolar select (reserved)
    } bit;
    uint8_t reg;
} abs_trim_reg_t;

#define GBIAS_TRIM_REG    (0x40)
typedef union {
    struct {
		uint8_t GBIAS_TRIM 		: 5;  // Bias trimming bits
        uint8_t REFOUT_TRIM_2_0 : 3;  // REFOUT_TRIM[2:0] (reserved)
    } bit;
    uint8_t reg;
} gbias_trim_reg_t;

#define OSC_TRIM_REG      (0x41)
typedef union {
    struct {
		uint8_t OSC_TRIM        : 5;  // Oscillator trimming bits
		uint8_t REFOUT_TRIM_4_3 : 2;  // REFOUT_TRIM[4:3] (reserved)
        uint8_t OPT_CTRL1_5     : 1;  // OPT_CTRL1[5] (reserved)
    } bit;
    uint8_t reg;
} osc_trim_reg_t;

#define PGA2_CMRR0_REG    (0x42)
typedef union {
    struct {
		uint8_t CMRR_TRIM0    : 5;  // PGA2 Gain0 CMRR trimming bits
		uint8_t OWI_FSRD      : 1;  // DWI fast read enable
        uint8_t OPT_CTRL1_7_6 : 2;  // OPT_CTRL1[7:6] (reserved)
    } bit;
    uint8_t reg;
} pga2_cmrr0_reg_t;

#define IDAC_TRIM_REG      (0x43)
typedef union {
    struct {
		uint8_t IDAC_TRIM  : 5;  // IDAC trimming bits
        uint8_t RESERVED   : 3;
    } bit;
    uint8_t reg;
} idac_trim_reg_t;

// System Control Register
#define SYS_CTRL_REG      (0x44)
typedef union {
    struct {
		uint8_t OWI_DIS    : 1;  // OWI communication disable
		uint8_t IDAC_EN    : 1;  // IDAC mode enable (0:Voltage, 1:Current)
		uint8_t R_MODE     : 1;  // Ratiometric mode (0:Normal, 1:Ratiometric)
		uint8_t SPIN_EN    : 1;  // Spin operation enable
		uint8_t SPIN_TYPE  : 1;  // Spin phase type (0:4-phase, 1:2-phase)
		uint8_t SWAP       : 1;  // Signal polarity select
		uint8_t VOCD_EN    : 1;  // Overcurrent protection enable
        uint8_t TBG_EN     : 1;  // Backgate drive enable
    } bit;
    uint8_t reg;
} sys_ctrl_reg_t;

// PGA Control Registers
#define PGA_GAIN_REG      (0x45)
typedef union {
    struct {
		uint8_t G2         : 2;  // Stage2 gain (x1/x2/x4/x6.5)
		uint8_t G3         : 3;  // Stage3 gain (x1-x3.6)
        uint8_t G1         : 3;  // Stage1 gain (x1-x32)
    } bit;
    uint8_t reg;
} pga_gain_reg_t;
/* G1  (bit7-5) */
enum PGA_Gain_G1 {
    PGA_G1_1X    = (0x00),   // default
    PGA_G1_2X    = (0x01),
    PGA_G1_4X    = (0x02),   
    PGA_G1_6_4X  = (0x03),
    PGA_G1_8X    = (0x04),
    PGA_G1_10_66X= (0x05),
    PGA_G1_16X   = (0x06),
    PGA_G1_32X   = (0x07)
};
/* G3  (bit4-2) */
enum PGA_Gain_G3 {
    PGA_G3_1X    = (0x00),   // default
    PGA_G3_2X    = (0x01),
    PGA_G3_2_4X  = (0x02),
    PGA_G3_3X    = (0x03),
    PGA_G3_4X    = (0x04),
    PGA_G3_6X    = (0x05),
    PGA_G3_9X    = (0x06),
    PGA_G3_3_6X  = (0x07)
};
/* G2  (bit1-0) */
enum PGA_Gain_G2 {
    PGA_G2_1X    = (0x00),   // default
    PGA_G2_2X    = (0x01),
    PGA_G2_4X    = (0x02),
    PGA_G2_6_5X  = (0x03)
};

// LUT Selection Register
#define LUT_SEL_REG       (0x47)
typedef union {
    struct {
		uint8_t FIX_CAL_EN : 1;  // Fix mode enable
		uint8_t LUT_CAL_EN : 1;  // LUT calibration enable
		uint8_t DAC_HP     : 1;  // DAC high power mode
        uint8_t LOAD_LUT   : 5;  // LUT load select
    } bit;
    uint8_t reg;
} lut_sel_reg_t;

// PGA Control Register
#define PGA_CTRL_REG      (0x48)
typedef union {
    struct {
		uint8_t VCM_SEL    : 2;  // VCM/REFOUT voltage select
		uint8_t SPIN_CLKS  : 2;  // Spin clock select 
		uint8_t CHOP_EN1   : 1;  // PGA1 chop enable
		uint8_t CHOP_EN2   : 1;  // PGA2 chop enable
		uint8_t REF_GAIN   : 1;  // DAC reference gain (0:reference X1, 1:reference X1.6384)
        uint8_t VREF_SEL   : 1;  // DAC reference select (0:Internal 2.5V, 1:VDD/2.0)
    } bit;
    uint8_t reg;
} pga_ctrl_reg_t;
/* SPIN_CLKS  (bit3-2) */
enum PGA_CTRL_SPIN_CLKS {
    SPIN_CLKS_1_MHZ    = (0x00),   // 1MHz
    SPIN_CLKS_2_MHZ    = (0x01),   // 2MHz
    SPIN_CLKS_0_5_MHZ  = (0x02),   // 0.5MHz
    SPIN_CLKS_0_25_MHZ = (0x03)    // 0.25MHz
};
/* VCM_SEL  (bit1-0) */
enum PGA_CTRL_VCM_SEL {
    VCM_SEL_1_5_V     = (0x00),   // 1.5V
    VCM_SEL_0_5_V     = (0x01),   // 0.5V
    VCM_SEL_1_65_V    = (0x02),   // 1.65V
    VCM_SEL_2_5_V     = (0x03)    // 2.5V
};

// Coefficient Registers
#define COEFF_C_REG       (0x4A)

// ADC Control Register
#define ADC_CTRL_REG      (0x4B)
typedef union {
    struct {
        uint8_t AVG_NUM    : 3;  // Average number
		uint8_t OSR        : 2;  // Over sampling rate
		uint8_t RESERVED   : 2;
		uint8_t SOT_EN     : 1;  // First or second order selection (both interpolation mode or fix mode)
    } bit;
    uint8_t reg;
} adc_ctrl_reg_t;
/* OSR  (bit4-3) */
enum ADC_CTRL_OSR {
    OSR_512    = (0x00),   
    OSR_256    = (0x01),   
    OSR_128    = (0x02),   
    OSR_64     = (0x03)    
};
/* AVG_NUM  (bit2-0) */
enum ADC_CTRL_AVG_NUM {
    AVG_NUM_128   = (0x00),   
    AVG_NUM_64    = (0x01),   
    AVG_NUM_32    = (0x02),   
    AVG_NUM_16    = (0x03),   
    AVG_NUM_8     = (0x04),   
    AVG_NUM_4     = (0x05),   
    AVG_NUM_2     = (0x06),   
    AVG_NUM_1     = (0x07)    
};

// Temperature Coefficients
#define Tx_H_REG(x)        (0x4C + (uint8_t)(x) * 6)    // x:0~2
#define Tx_L_REG(x)        (0x4D + (uint8_t)(x) * 6)    // x:0~2
typedef union {
    struct {
        uint16_t Tx_H : 8;  // Temperature N calibration high byte
        uint16_t Tx_L : 8;  // Temperature N calibration low byte
    } reg;
    uint16_t value;
} tx_reg_t;

// Gain DAC Coefficients
#define GDx_H_REG(x)        (0x4E + (uint8_t)(x) * 6)    // x:0~2
#define GDx_L_REG(x)        (0x4F + (uint8_t)(x) * 6)    // x:0~2
typedef union {
    struct {
        uint16_t GDx_H : 8;  // Temperature N GAIN DAC high byte
        uint16_t GDx_L : 8;  // Temperature N GAIN DAC low byte
    } reg;
    uint16_t value;
} gdx_reg_t;

// Zero DAC Coefficients
#define ZDx_H_REG(x)        (0x50 + (uint8_t)(x) * 6)    // x:0~2
#define ZDx_L_REG(x)        (0x51 + (uint8_t)(x) * 6)    // x:0~2
typedef union {
    struct {
        uint16_t ZDx_H : 8;  // Temperature N ZERO DAC high byte
        uint16_t ZDx_L : 8;  // Temperature N ZERO DAC low byte
    } reg;
    uint16_t value;
} zdx_reg_t;

// Coefficient A/B Registers
#define COEFF_AH_REG      (0xB2)
#define COEFF_AL_REG      (0xB3)
typedef union { 
    struct {
        uint16_t COEFF_AH  : 8;  // Coefficient A high byte
        uint16_t COEFF_AL  : 8;  // Coefficient A low byte
    } reg;
    int16_t value; // COEFF_A is a signed number (int16_t)
} coeff_a_reg_t;

#define COEFF_BH_REG      (0xB4)
#define COEFF_BL_REG      (0xB5)
typedef union { 
    struct {
        uint16_t COEFF_BH  : 8;  // Coefficient B high byte
        uint16_t COEFF_BL  : 8;  // Coefficient B low byte
    } reg;
    int16_t value; // COEFF_B is a signed number (int16_t)
} coeff_b_reg_t;

// PGA Offsets
#define PGA1_OFFP_REG     (0xB6)
typedef union {
    struct {
		uint8_t OFFSET1P       : 5;  // PGA1 positive offset trim (reserved)
        uint8_t OPT_CTRL2_2_0  : 3;  // OPT_CTRL2[2:0] (reserved)
    } bit;
    uint8_t reg;
} pga1_offp_reg_t;

#define PGA1_OFFN_REG     (0xB7)
typedef union {
    struct {
		uint8_t OFFSET1N       : 5;  // PGA1 negative offset trim (reserved)
        uint8_t OPT_CTRL2_5_3  : 3;  // OPT_CTRL2[5:3] (reserved)
    } bit;
    uint8_t reg;
} pga1_offn_reg_t;

#define PGA2_OFF_REG      (0xB8)
typedef union {
    struct {
		uint8_t OFFSET2           : 5;  // PGA2 offset trim (reserved)
		uint8_t TS_CAL_SIGN       : 1;  // Temperature sign (reserved)
        uint8_t OPT_CTRL2_7_6     : 2;  // OPT_CTRL2[7:6] (reserved)
    } bit;
    uint8_t reg;
} pga2_off_reg_t;

#define REFOUT_OFF_REG   (0xB9)
typedef union {
    struct {
		uint8_t PGA3_R2R  : 1;  // PGA3 R2R enable (reserved)
		uint8_t OFFSET3   : 4;  // PGA3 offset trim (reserved)
        uint8_t TS_OFF    : 3;  // TS_OFF Calibration, combined with TS_CAL_SIGN, for real ADC data (reserved)
    } bit;
    uint8_t reg;
} refout_off_reg_t;

// PGA CMRR Registers
#define PGA2_CMRR1_REG    (0xBA)
typedef union {
    struct {
		uint8_t CMRR1      : 5;  // PGA2 gain1 CMRR trim
        uint8_t CMRR2_2_0  : 3;  // PGA2 gain2 CMRR trim[2:0]
    } bit;
    uint8_t reg;
} pga2_cmrr1_reg_t;

#define PGA2_CMRR2_REG    (0xBB)
typedef union {
    struct {
		uint8_t CMRR2_4_3  : 2;  // PGA2 gain2 CMRR trim[4:3]
		uint8_t CMRR3      : 5;  // PGA2 gain3 CMRR trim
        uint8_t RESERVED   : 1;
    } bit;
    uint8_t reg;
} pga2_cmrr2_reg_t;

#endif // SAF8100_REG_H
