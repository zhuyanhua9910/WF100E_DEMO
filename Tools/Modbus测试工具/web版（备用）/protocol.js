/**
 * protocol.js — SAF8100 治具 Modbus RTU 协议核心
 *
 * 纯函数实现，不依赖 DOM / 串口，浏览器与 Node 均可直接使用。
 * 与 WF100E_DEMO 固件（FreeModbus RTU 从机）一一对应：
 *   Src/CmdHelper.c  命令解析
 *   Drivers/FreeModbus/Modbus.c  寄存器回调与缓冲区
 *
 * 关键约定（容易踩坑，务必先读）：
 *   1. 命令是"写保持寄存器触发"的：必须用 FC10 从 40001（PDU 地址 0）开始
 *      一次性写完整个命令块，固件 eMBRegHoldingCB() 在写入时调用 Parse()。
 *      不要用 FC06 单寄存器写——只会把 recvBuf[0] 写进去，命令无法成立。
 *   2. 结果从输入寄存器 30001（PDU 地址 0）读取，用 FC04。
 *   3. 每次写入都会 memset(sendBuf)，所以"先写命令、再读结果"的顺序不能反。
 */

/* ============================ 常量 ============================ */

export const SLAVE_DEFAULT = 1;
export const REG_HOLDING_BASE = 40001; /* 保持寄存器起始（固件内常量） */
export const REG_INPUT_BASE = 30001;   /* 输入寄存器起始（固件内常量） */
export const CHANNEL_COUNT = 8;        /* JIG_ROW(2) * JIG_COL(4) */
export const REGS_PER_CHIP = 8;

/**
 * 关键：PDU 里真正要填的起始地址。
 *
 * 本固件用的 FreeModbus 在 mbfuncholding.c / mbfuncinput.c 里先做
 *     usRegAddress = PDU 地址;  usRegAddress++;
 * 再交给 eMBRegHoldingCB() / eMBRegInputCB()，而回调判断的是
 *     (usAddress == 40001)  /  (usAddress == 30001)
 * 所以主站 PDU 里必须填 40000 / 30000，而不是 0。
 *
 * 若换成标准 0 基从机（如普通 PLC），把 addrMode 切到 'zero'。
 */
export const PDU_ADDR = {
    holding: 40000, /* → 回调收到 40001，即逻辑寄存器 40001 */
    input: 30000,   /* → 回调收到 30001，即逻辑寄存器 30001 */
    zero: 0,        /* 标准 0 基从机 */
};

/* 命令大类 / 具体命令码，与 CmdHelper.c 一致 */
export const CMD = {
    GET_VERSION: 0x104,
    SET_VDD: 0x201,
    GET_TASK: 0x66,
    SET_TASK: 0x77,
    GET_8100_DATA: 0x610,
    SET_8100_CONFIG: 0x780,
};

/* 任务号，与 product.h 的 TasksOrder 一致 */
export const TASK = {
    TEST: 5,
    MEAS: 6,
    FUSE: 7,
};
export const TASK_NAME = {
    5: '全流程测试',
    6: '快速测温',
    7: 'OTP 烧写',
};

/* 错误码，与 saf8100.h 一致 */
export const ERR_TEXT = {
    0x00: '正常',
    0x01: '登录失败',
    0x02: 'OWI 通信失败',
    0x03: '回读不一致',
    0x04: 'OTP 烧写失败',
    0xFE: '未测试',
};

/* 温度换算：T(℃) = (code - 32383.8) / 114.2 */
export const TEMP_OFFSET = 32383.8;
export const TEMP_SLOPE = 114.2;

/* ========================= CRC16 / 组帧 ========================= */

/** Modbus CRC16（多项式 0xA001，初值 0xFFFF，低字节先发） */
export function crc16(bytes) {
    let crc = 0xFFFF;
    for (let i = 0; i < bytes.length; i++) {
        crc ^= bytes[i];
        for (let b = 0; b < 8; b++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc & 0xFFFF;
}

/** 16 位数值拆成 [高字节, 低字节] */
export function hiLo(v) {
    return [(v >> 8) & 0xFF, v & 0xFF];
}

/** 字节序列 → "01 03 00 00 00 01 84 0A" */
export function formatHex(bytes) {
    return Array.from(bytes, (b) => b.toString(16).toUpperCase().padStart(2, '0')).join(' ');
}

/** 组完整 RTU 帧：从站地址 + PDU + CRC(低字节在前) */
export function buildFrame(slave, pduValues) {
    const body = [slave & 0xFF].concat(pduValues.map((v) => v & 0xFF));
    const c = crc16(body);
    return Uint8Array.from(body.concat([c & 0xFF, (c >> 8) & 0xFF]));
}

/** FC03 读保持寄存器 / FC04 读输入寄存器。addr 为 PDU 地址（0 基） */
export function buildRead(slave, func, addr, count) {
    return buildFrame(slave, [func, ...hiLo(addr), ...hiLo(count)]);
}

/** FC16 写多个保持寄存器。addr 为 PDU 地址（0 基） */
export function buildWriteMultiple(slave, addr, values) {
    const pdu = [0x10, ...hiLo(addr), ...hiLo(values.length), values.length * 2];
    for (const v of values) {
        pdu.push(...hiLo(v));
    }
    return buildFrame(slave, pdu);
}

/** FC06 写单个保持寄存器（固件不推荐，仅用于手动调试） */
export function buildWriteSingle(slave, addr, value) {
    return buildFrame(slave, [0x06, ...hiLo(addr), ...hiLo(value)]);
}

/* ========================= 应答解析 ========================= */

/**
 * 从接收缓冲区里找出一个完整应答帧，容忍前导噪声。
 * @returns {{frame:Uint8Array, rest:Uint8Array}|null} null 表示数据还不完整
 */
export function extractFrame(buf, func) {
    if (!buf || buf.length < 2) {
        return null;
    }
    for (let i = 0; i + 1 < buf.length; i++) {
        const f = buf[i + 1];
        if (f !== func && f !== (func | 0x80)) {
            continue;
        }
        let need;
        if (f & 0x80) {
            need = 5; /* 地址 + 功能码|0x80 + 异常码 + CRC(2) */
        } else if (func === 0x03 || func === 0x04) {
            if (buf.length < i + 3) {
                return null;
            }
            need = 3 + buf[i + 2] + 2;
        } else {
            need = 8; /* FC06 / FC10 都是 8 字节回显 */
        }
        if (buf.length - i < need) {
            return null;
        }
        return { frame: buf.slice(i, i + need), rest: buf.slice(i + need) };
    }
    return null;
}

/**
 * 校验并解析应答帧。
 * @returns {{ok:true, values:number[], note?:string} | {ok:false, error:string, exception?:number}}
 */
export function parseResponse(frame, func) {
    if (!frame || frame.length < 5) {
        return { ok: false, error: '应答帧长度不足' };
    }
    const len = frame.length;
    const got = frame[len - 2] | (frame[len - 1] << 8);
    if (got !== crc16(frame.slice(0, len - 2))) {
        return { ok: false, error: 'CRC 校验错' };
    }
    const f = frame[1];
    if (f & 0x80) {
        const code = frame[2];
        return { ok: false, error: `从机异常码 0x${code.toString(16).toUpperCase().padStart(2, '0')}`, exception: code };
    }
    if (f !== func) {
        return { ok: false, error: `功能码不匹配（期望 ${func}，收到 ${f}）` };
    }
    if (func === 0x03 || func === 0x04) {
        const n = frame[2];
        const values = [];
        for (let i = 0; i < n / 2; i++) {
            values.push((frame[3 + i * 2] << 8) | frame[4 + i * 2]);
        }
        return { ok: true, values };
    }
    /* FC06 / FC10：回显 地址 + 数量 */
    const addr = (frame[2] << 8) | frame[3];
    const cnt = (frame[4] << 8) | frame[5];
    return { ok: true, values: [], note: `写入确认 地址=${addr} 数量=${cnt}` };
}

/* ========================= 命令块 ========================= */

/** 读固件版本：40001=1, 40002=0x104，结果 ASCII 存输入寄存器 */
export function cmdGetVersion(type = 1) {
    return [type, CMD.GET_VERSION];
}

/** 总电源：dp=0xFF 输出高（本治具有效电平），dp=0x00 输出低 */
export function cmdSetVdd(on) {
    return [1, CMD.SET_VDD, on ? 0xFF : 0x00];
}

/**
 * 下发任务：40001=5, 40002=0x77, 40003=任务号, 40004=通道掩码
 * mask：bit0~bit7 对应 0~7 号通道；0x00/0xFF 均为全部
 */
export function cmdSetTask(task, mask) {
    return [5, CMD.SET_TASK, task & 0xFF, mask & 0xFF, 0, 0, 0];
}

/** 查任务状态：回读 30001..30007 = busy, order, prog, paras0..3 */
export function cmdGetTask() {
    return [5, CMD.GET_TASK];
}

/** 读 8100 测试数据：40003=起始通道号，每颗 8 个输入寄存器 */
export function cmdGetData(startChannel = 0) {
    return [5, CMD.GET_8100_DATA, startChannel & 0xFF];
}

/**
 * 写 8100 配置（下一次任务生效）。
 * 固件按下标取参：cmd[3]=act_gd ... cmd[10]=flags，
 * 因此 cmd[2] 是占位（保留 0），实际参数从 40004 开始。
 * flags：bit0 = valid（1 生效），bit1 = ext_en（1 才写 0x44 / 0x47）
 */
export function cmdSetConfig(cfg) {
    const flags = (cfg.valid ? 1 : 0) | (cfg.extEn ? 2 : 0);
    return [
        5,
        CMD.SET_8100_CONFIG,
        0,
        cfg.actGd & 0xFFFF,
        cfg.actZd & 0xFFFF,
        cfg.pgaGain & 0xFF,
        cfg.pgaCtrl & 0xFF,
        cfg.adcCtrl & 0xFF,
        cfg.sysCtrl & 0xFF,
        cfg.lutSel & 0xFF,
        flags,
    ];
}

/* ========================= 位域拼装 / 解码 ========================= */

export const PGA_G1 = [1, 2, 4, 6.4, 8, 10.66, 16, 32];
export const PGA_G3 = [1, 2, 2.4, 3, 4, 6, 9, 3.6];
export const PGA_G2 = [1, 2, 4, 6.5];
export const VCM_LIST = ['1.5V', '0.5V', '1.65V', '2.5V'];
export const SPIN_CLK_LIST = ['1MHz', '2MHz', '0.5MHz', '0.25MHz'];
export const OSR_LIST = [512, 256, 128, 64];
export const AVG_LIST = [128, 64, 32, 16, 8, 4, 2, 1];

/** 0x45 = (G1<<5) | (G3<<2) | G2 */
export function makePgaGain(g1, g3, g2) {
    return ((g1 & 0x07) << 5) | ((g3 & 0x07) << 2) | (g2 & 0x03);
}

/** 0x4B = (OSR<<3) | AVG_NUM */
export function makeAdcCtrl(osr, avg) {
    return ((osr & 0x03) << 3) | (avg & 0x07);
}

/** 0x48 = (VREF_SEL<<7) | (REF_GAIN<<6) | (SPIN_CLKS<<2) | VCM_SEL */
export function makePgaCtrl(vrefSel, refGain, vcm, spinClk = 0) {
    return ((vrefSel & 0x01) << 7) | ((refGain & 0x01) << 6) | ((spinClk & 0x03) << 2) | (vcm & 0x03);
}

export function decodePgaGain(v) {
    const g1 = (v >> 5) & 0x07;
    const g3 = (v >> 2) & 0x07;
    const g2 = v & 0x03;
    return { g1: PGA_G1[g1], g3: PGA_G3[g3], g2: PGA_G2[g2], text: `G1×${PGA_G1[g1]} / G3×${PGA_G3[g3]} / G2×${PGA_G2[g2]}` };
}

export function decodePgaCtrl(v) {
    const vcm = v & 0x03;
    const spin = (v >> 2) & 0x03;
    return {
        vcm: VCM_LIST[vcm],
        spinClk: SPIN_CLK_LIST[spin],
        chopEn1: !!(v & 0x10),
        chopEn2: !!(v & 0x20),
        refGain: (v & 0x40) ? 1.6384 : 1,
        vref: (v & 0x80) ? 'VDD/2.0' : '内部 2.5V',
        text: `VCM ${VCM_LIST[vcm]} / 基准 ${(v & 0x80) ? 'VDD/2.0' : '内部 2.5V'} ×${(v & 0x40) ? '1.6384' : '1'}`,
    };
}

export function decodeAdcCtrl(v) {
    const osr = (v >> 3) & 0x03;
    const avg = v & 0x07;
    return { osr: OSR_LIST[osr], avg: AVG_LIST[avg], sot: !!(v & 0x80), text: `OSR ${OSR_LIST[osr]} / 平均 ${AVG_LIST[avg]} 次` };
}

export function decodeSysCtrl(v) {
    const names = ['OWI_DIS', 'IDAC_EN', 'R_MODE', 'SPIN_EN', 'SPIN_TYPE', 'SWAP', 'VOCD_EN', 'TBG_EN'];
    const bits = names.map((n, i) => ({ name: n, on: !!((v >> i) & 1) }));
    return {
        bits,
        text: [
            (v & 0x01) ? 'OWI 已禁' : 'OWI 允许',
            (v & 0x02) ? '电流驱动' : '电压驱动',
            (v & 0x04) ? '比例式' : '普通',
        ].join(' / '),
    };
}

export function decodeLutSel(v) {
    const names = ['FIX_CAL_EN', 'LUT_CAL_EN', 'DAC_HP'];
    return {
        bits: names.map((n, i) => ({ name: n, on: !!((v >> i) & 1) })),
        loadLut: (v >> 3) & 0x1F,
        text: `LOAD_LUT=${(v >> 3) & 0x1F}${(v & 0x01) ? ' / 固定校准' : ''}${(v & 0x02) ? ' / LUT 校准' : ''}`,
    };
}

/** 温度原始码 → 摄氏度 */
export function tempFromCode(code) {
    return (code - TEMP_OFFSET) / TEMP_SLOPE;
}

/** 16 位无符号 → 有符号 */
export function toInt16(v) {
    return v >= 0x8000 ? v - 0x10000 : v;
}

/**
 * 解析 0x610 数据区，每颗芯片 8 个寄存器：
 *   +0 login<<8 | err
 *   +1 act_gd      +2 act_zd
 *   +3 pga_gain<<8 | adc_ctrl
 *   +4 pga_ctrl<<8 | sys_ctrl
 *   +5 lut_sel<<8  | fuse_done
 *   +6 temp_code   +7 temp_x100(有符号)
 * @param {number[]} regs 输入寄存器值
 * @param {number} startChannel 本次读取的起始通道号
 */
export function decodeChannels(regs, startChannel = 0) {
    const out = [];
    const n = Math.floor(regs.length / REGS_PER_CHIP);
    for (let i = 0; i < n; i++) {
        const b = i * REGS_PER_CHIP;
        const ch = startChannel + i;
        const login = (regs[b] >> 8) & 0xFF;
        const err = regs[b] & 0xFF;
        const tempCode = regs[b + 6];
        const tempX100 = toInt16(regs[b + 7]);
        out.push({
            ch,
            login,
            loginOk: login === 0x55,
            err,
            errText: ERR_TEXT[err] !== undefined ? ERR_TEXT[err] : `未知错误 0x${err.toString(16).toUpperCase()}`,
            actGd: regs[b + 1],
            actZd: regs[b + 2],
            pgaGain: (regs[b + 3] >> 8) & 0xFF,
            adcCtrl: regs[b + 3] & 0xFF,
            pgaCtrl: (regs[b + 4] >> 8) & 0xFF,
            sysCtrl: regs[b + 4] & 0xFF,
            lutSel: (regs[b + 5] >> 8) & 0xFF,
            fuseDone: regs[b + 5] & 0xFF,
            tempCode,
            tempX100,
            temp: tempX100 / 100,
            status: err === 0xFE ? 'skip' : err === 0x00 ? 'ok' : 'err',
        });
    }
    return out;
}

/** 通道掩码 → 文字，例如 0x05 → "0, 2" */
export function maskToText(mask) {
    if (mask === 0x00 || mask === 0xFF) {
        return '全部';
    }
    const list = [];
    for (let i = 0; i < CHANNEL_COUNT; i++) {
        if (mask & (1 << i)) {
            list.push(i);
        }
    }
    return list.length ? list.join(', ') : '无';
}
