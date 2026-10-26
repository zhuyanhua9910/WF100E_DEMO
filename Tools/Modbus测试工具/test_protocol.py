# -*- coding: utf-8 -*-
"""协议自测（无需硬件）：python test_protocol.py"""
import saf8100_tool as T

ok = 0
fail = 0


def eq(name, actual, expected):
    global ok, fail
    if actual == expected:
        ok += 1
        print("  ok   %s" % name)
    else:
        fail += 1
        print("  FAIL %s\n       expect %r\n       actual %r" % (name, expected, actual))


print("\n[1] CRC / 组帧")
eq("FC03 读保持 0 基", T.fmt_hex(T.build_read(1, 3, 0, 1)), "01 03 00 00 00 01 84 0A")
_f = T.build_read(1, 4, 30000, 8)
eq("FC04 读输入 30000 x8 前缀", T.fmt_hex(_f[:6]), "01 04 75 30 00 08")
eq("FC04 帧尾 CRC 自校验", T.crc16(_f[:-2]), _f[-2] | (_f[-1] << 8))
eq("CRC('123456789')", T.crc16(b"123456789"), 0x4B37)

print("\n[2] 命令块")
eq("cmd_task", T.cmd_task(5, 0x05), [5, 0x77, 5, 0x05, 0, 0, 0])
eq("cmd_query", T.cmd_query(), [5, 0x66])
eq("cmd_data", T.cmd_data(3), [5, 0x610, 3])
cfg = {"gd": 10000, "zd": 16383, "pga": 0x52, "pctrl": 0x43, "adc": 0x15,
       "sys": 0, "lut": 0, "valid": True, "ext": False}
eq("cmd_config 长度", len(T.cmd_config(cfg)), 11)
eq("cmd_config 内容", T.cmd_config(cfg), [5, 0x780, 0, 10000, 16383, 0x52, 0x43, 0x15, 0, 0, 1])
cfg["ext"] = True
eq("ext_en -> flags bit1", T.cmd_config(cfg)[10], 3)

print("\n[3] 应答解析")
body = bytes([1, 4, 4, 0x00, 0x55, 0x00, 0x00])
c = T.crc16(body)
eq("FC04 正常", T.parse_response(body + bytes([c & 0xFF, c >> 8]), 4)[:2], (True, [0x55, 0x00]))
body = bytes([1, 0x84, 0x02])
c = T.crc16(body)
ok2, _, err = T.parse_response(body + bytes([c & 0xFF, c >> 8]), 4)
eq("异常帧 ok", ok2, False)
eq("异常帧提示", err, "从机异常码 0x02")
eq("帧长判断 FC04", T.frame_len(bytes([1, 4, 4, 0, 0]), 4), 9)
eq("帧长判断 半包", T.frame_len(bytes([1, 4]), 4), None)

print("\n[4] 地址（固件 +1 后 == 40001/30001）")
eq("写帧首 6 字节", T.fmt_hex(T.build_write_multi(1, 40000, T.cmd_task(5, 0xFF))[:6]), "01 10 9C 40 00 07")
eq("读帧首 6 字节", T.fmt_hex(T.build_read(1, 4, 30000, 64)[:6]), "01 04 75 30 00 40")

print("\n[5] 数据解码")
regs = [(0x55 << 8) | 0x00, 10000, 16383, (0x52 << 8) | 0x15, (0x43 << 8) | 0x00,
        (0x00 << 8) | 0x00, 36000, 317,
        (0x55 << 8) | 0x01, 0, 0, 0, 0, 0, 0, 0,
        (0x00 << 8) | 0xFE, 0, 0, 0, 0, 0, 0, 0]
chs = T.decode_channels(regs, 0)
eq("通道数", len(chs), 3)
eq("CH0 状态", chs[0]["status"], "ok")
eq("CH0 温度", chs[0]["temp"], 3.17)
eq("CH1 错误", chs[1]["err_text"], "登录失败")
eq("CH2 未测试", chs[2]["status_text"], "未测试")
p = T.decode_pga(0x52)
eq("0x45=0x52", (p["g1"], p["g3"], p["g2"]), (4, 4, 4))
a = T.decode_adc(0x15)
eq("0x4B=0x15", (a["osr"], a["avg"]), (128, 4))
c2 = T.decode_pctrl(0x43)
eq("0x48=0x43", (c2["vref"], c2["ref"], c2["vcm"]), ("内部2.5V", 1.6384, "2.5V"))
eq("make_pga(2,4,2)", T.make_pga(2, 4, 2), 0x52)
eq("make_adc(2,5)", T.make_adc(2, 5), 0x15)
eq("make_pctrl(0,1,3)", T.make_pctrl(0, 1, 3), 0x43)
eq("负温度", T.to_int16(0xFF9C), -100)
eq("mask 0x05", T.mask_text(0x05), "0, 2")

print("\n结果：%d 通过 / %d 失败\n" % (ok, fail))
raise SystemExit(1 if fail else 0)
