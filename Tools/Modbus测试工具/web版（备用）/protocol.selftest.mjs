/**
 * protocol.selftest.mjs — 协议核心自测（不需要串口 / 硬件）
 * 运行：node protocol.selftest.mjs
 */
import * as P from './protocol.js';

let pass = 0;
let fail = 0;

function eq(name, actual, expected) {
    const a = JSON.stringify(actual);
    const e = JSON.stringify(expected);
    if (a === e) {
        pass++;
        console.log(`  ok   ${name}`);
    } else {
        fail++;
        console.log(`  FAIL ${name}\n       expect ${e}\n       actual ${a}`);
    }
}

function near(name, actual, expected, tol = 1e-6) {
    if (Math.abs(actual - expected) <= tol) {
        pass++;
        console.log(`  ok   ${name}`);
    } else {
        fail++;
        console.log(`  FAIL ${name} expect≈${expected} actual=${actual}`);
    }
}

console.log('\n[1] CRC / 组帧');
eq('FC03 读保持 0x0000 x1', P.formatHex(P.buildRead(1, 0x03, 0, 1)), '01 03 00 00 00 01 84 0A');
eq('FC04 读输入 0x0000 x8', P.formatHex(P.buildRead(1, 0x04, 0, 8)), '01 04 00 00 00 08 F1 CC');
eq('CRC("123456789")', P.crc16([0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39]), 0x4B37);

console.log('\n[2] 命令块');
const taskFrame = P.buildWriteMultiple(1, 0, P.cmdSetTask(P.TASK.TEST, 0xFF));
eq('0x77 任务帧长度', taskFrame.length, 9 + 7 * 2);
eq('0x77 任务帧前 6 字节', P.formatHex(taskFrame.slice(0, 6)), '01 10 00 00 00 07');
{
    const v = P.cmdSetTask(5, 0x05);
    eq('cmdSetTask 内容', v, [5, 0x77, 5, 0x05, 0, 0, 0]);
}
{
    const v = P.cmdSetConfig({ actGd: 10000, actZd: 16383, pgaGain: 0x52, pgaCtrl: 0x43, adcCtrl: 0x15, sysCtrl: 0, lutSel: 0, valid: true, extEn: false });
    eq('cmdSetConfig 长度', v.length, 11);
    eq('cmdSetConfig 参数位置', v.slice(0, 11), [5, 0x780, 0, 10000, 16383, 0x52, 0x43, 0x15, 0, 0, 1]);
}
{
    const v = P.cmdSetConfig({ actGd: 0, actZd: 0, pgaGain: 0, pgaCtrl: 0, adcCtrl: 0, sysCtrl: 0, lutSel: 0, valid: true, extEn: true });
    eq('ext_en 置位到 bit1', v[10], 3);
}

console.log('\n[3] 应答解析');
{
    /* 01 04 04 00 55 00 00 + crc */
    const body = [0x01, 0x04, 0x04, 0x00, 0x55, 0x00, 0x00];
    const c = P.crc16(body);
    const frame = Uint8Array.from([...body, c & 0xFF, (c >> 8) & 0xFF]);
    const r = P.parseResponse(frame, 0x04);
    eq('FC04 解析成功', r.ok, true);
    eq('FC04 数值', r.values, [0x0055, 0x0000]);
}
{
    /* 异常应答 01 84 02 + crc */
    const body = [0x01, 0x84, 0x02];
    const c = P.crc16(body);
    const frame = Uint8Array.from([...body, c & 0xFF, (c >> 8) & 0xFF]);
    const r = P.parseResponse(frame, 0x04);
    eq('异常帧 ok=false', r.ok, false);
    eq('异常帧 exception', r.exception, 2);
}
{
    /* 带噪声前缀的帧提取 */
    const body = [0x01, 0x10, 0x00, 0x00, 0x00, 0x07];
    const c = P.crc16(body);
    const frame = Uint8Array.from([0x00, 0x00, ...body, c & 0xFF, (c >> 8) & 0xFF]);
    const got = P.extractFrame(frame, 0x10);
    eq('噪声前缀后可提取', got ? P.formatHex(got.frame) : null, P.formatHex(body.concat([c & 0xFF, (c >> 8) & 0xFF])));
}
{
    /* 半包：不应被误判 */
    const got = P.extractFrame(Uint8Array.from([0x01, 0x04, 0x04, 0x00, 0x55]), 0x04);
    eq('半包返回 null', got, null);
}

console.log('\n[4] 数据区解码');
{
    const regs = [
        (0x55 << 8) | 0x00, 10000, 16383, (0x52 << 8) | 0x15, (0x43 << 8) | 0x00, (0x00 << 8) | 0x00, 36000, 317,
        (0x55 << 8) | 0x01, 0, 0, 0, 0, 0, 0, 0,
        (0x00 << 8) | 0xFE, 0, 0, 0, 0, 0, 0, 0,
    ];
    const list = P.decodeChannels(regs, 0);
    eq('通道数', list.length, 3);
    eq('CH0 状态', list[0].status, 'ok');
    eq('CH0 登录', list[0].loginOk, true);
    eq('CH0 温度', list[0].temp, 3.17);
    eq('CH1 错误文字', list[1].errText, '登录失败');
    eq('CH1 状态', list[1].status, 'err');
    eq('CH2 状态', list[2].status, 'skip');
    eq('CH2 未测试文字', list[2].errText, '未测试');
    const g = P.decodePgaGain(0x52);
    eq('0x45=0x52 解码', [g.g1, g.g3, g.g2], [4, 4, 4]);
    const a = P.decodeAdcCtrl(0x15);
    eq('0x4B=0x15 解码', [a.osr, a.avg], [128, 4]);
    const c2 = P.decodePgaCtrl(0x43);
    eq('0x48=0x43 解码', [c2.vref, c2.refGain, c2.vcm], ['内部 2.5V', 1.6384, '2.5V']);
}
{
    const g = P.makePgaGain(2, 4, 2);
    eq('makePgaGain(4x,4x,4x)', g, 0x52);
    eq('makeAdcCtrl(OSR128,AVG4)', P.makeAdcCtrl(2, 5), 0x15);
    eq('makePgaCtrl(0,1,VCM2.5)', P.makePgaCtrl(0, 1, 3), 0x43);
}
{
    eq('负温度 temp_x100', P.toInt16(0xFF9C), -100);
    near('tempFromCode(36000)', P.tempFromCode(36000), 31.666, 0.01);
}
{
    eq('maskToText(0x05)', P.maskToText(0x05), '0, 2');
    eq('maskToText(0xFF)', P.maskToText(0xFF), '全部');
    eq('maskToText(0x00)', P.maskToText(0x00), '全部');
}

console.log('\n[5] 与固件实际行为的地址校验');
{
    /* 固件 FreeModbus 会先 +1 再判断 == 40001，所以 PDU 必须是 40000 */
    const w = P.buildWriteMultiple(1, P.PDU_ADDR.holding, P.cmdSetTask(5, 0xFF));
    eq('写命令帧 首 6 字节', P.formatHex(w.slice(0, 6)), '01 10 9C 40 00 07');
    const r = P.buildRead(1, 0x04, P.PDU_ADDR.input, 64);
    eq('读数据帧 首 6 字节', P.formatHex(r.slice(0, 6)), '01 04 75 30 00 40');
    eq('写帧总长(7 寄存器)', w.length, 9 + 14);
    eq('读帧总长', r.length, 8);
    /* CRC 自洽 */
    eq('写帧 CRC 正确', P.crc16(w.slice(0, w.length - 2)), w[w.length - 2] | (w[w.length - 1] << 8));
    eq('读帧 CRC 正确', P.crc16(r.slice(0, r.length - 2)), r[r.length - 2] | (r[r.length - 1] << 8));
}

console.log(`\n结果：${pass} 通过 / ${fail} 失败\n`);
process.exit(fail ? 1 : 0);
