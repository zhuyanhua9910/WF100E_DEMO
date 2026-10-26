#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SAF8100 Modbus 测试工具（Windows 桌面版，单文件源码）

功能：
  · 连接串口（Modbus RTU 主站，115200 8N1），下发 T5/T6/T7 测试任务
  · 轮询任务进度、读回 8 通道测试数据并按芯片寄存器位域展开
  · 下发 0x780 芯片配置、读版本、总电源控制、手动指令 / 原始帧调试
  · 导出 CSV

协议要点（与 WF100E_DEMO 固件一一对应）：
  命令：FC10 一次写入保持寄存器，PDU 起始地址 40000（固件 +1 后 == 40001）
  结果：FC04 读输入寄存器，PDU 起始地址 30000（固件 +1 后 == 30001）
  详见 README.md
"""

import csv
import datetime
import json
import os
import queue
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import serial
from serial.tools import list_ports

APP_TITLE = "SAF8100 Modbus 测试工具"
APP_VERSION = "v1.9"
SLAVE = 1
PDU_HOLD = 40000
PDU_INPUT = 30000
CHANNELS = 8
REGS_PER_CHIP = 8

TASKS = [
    (5, "T5 · 全流程测试"),
    (6, "T6 · 快速测温"),
    (7, "T7 · OTP 烧写（不可逆）"),
]

ERR_TEXT = {
    0x00: "正常",
    0x01: "登录失败",
    0x02: "OWI 通信失败",
    0x03: "回读不一致",
    0x04: "OTP 烧写失败",
    0xF1: "读取失败（通信丢帧）",
    0xFE: "未测试",
}
ERR_SHORT = {0x00: "正常", 0x01: "登录失败", 0x02: "通信失败", 0x03: "回读不一致", 0x04: "烧写失败",
             0xF1: "读取失败", 0xFE: "未测试"}

PGA_G1 = [1, 2, 4, 6.4, 8, 10.66, 16, 32]
PGA_G3 = [1, 2, 2.4, 3, 4, 6, 9, 3.6]
PGA_G2 = [1, 2, 4, 6.5]
VCM_LIST = ["1.5V", "0.5V", "1.65V", "2.5V"]
SPIN_LIST = ["1MHz", "2MHz", "0.5MHz", "0.25MHz"]
OSR_LIST = [512, 256, 128, 64]
AVG_LIST = [128, 64, 32, 16, 8, 4, 2, 1]

TEMP_OFFSET = 32383.8
TEMP_SLOPE = 114.2

# ----------------------------------------------------------------------------
# 颜色
# ----------------------------------------------------------------------------
BG = "#eef1f6"
PANEL = "#ffffff"
HEAD_BG = "#f7f9fc"
LINE = "#dce3ed"
TEXT = "#16202c"
MUTED = "#6b7a8e"
ACCENT = "#1b62d6"
ACCENT_DARK = "#12489f"
OK_C = "#0b7a49"
ERR_C = "#b8291f"
WARN_C = "#8a5b00"
SKIP_C = "#78859a"

FONT = ("Microsoft YaHei UI", 9)
FONT_B = ("Microsoft YaHei UI", 9, "bold")
FONT_T = ("Microsoft YaHei UI", 9, "bold")
MONO = ("Consolas", 9)


# ============================================================================
# 协议核心（纯函数，无界面依赖）
# ============================================================================

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def hi_lo(v):
    v &= 0xFFFF
    return [(v >> 8) & 0xFF, v & 0xFF]


def build_frame(slave, pdu_values):
    body = bytearray([slave & 0xFF])
    for v in pdu_values:
        body.append(v & 0xFF)
    c = crc16(body)
    body.append(c & 0xFF)
    body.append((c >> 8) & 0xFF)
    return bytes(body)


def build_read(slave, func, addr, count):
    return build_frame(slave, [func] + hi_lo(addr) + hi_lo(count))


def build_write_multi(slave, addr, values):
    pdu = [0x10] + hi_lo(addr) + hi_lo(len(values)) + [len(values) * 2]
    for v in values:
        pdu += hi_lo(v)
    return build_frame(slave, pdu)


def fmt_hex(data):
    return " ".join("%02X" % (b & 0xFF) for b in data)


def parse_hex(text):
    clean = "".join(ch for ch in text.replace("0x", "").replace("0X", "") if ch in "0123456789abcdefABCDEF")
    if len(clean) < 4:
        raise ValueError("十六进制长度不足")
    return bytes(bytearray(int(clean[i:i + 2], 16) for i in range(0, len(clean) - 1, 2)))


def frame_len(buf, func):
    """根据已收到的字节判断整帧长度，返回 None 表示还不够"""
    if len(buf) < 2:
        return None
    f = buf[1]
    if f == (func | 0x80):
        return 5
    if f != func:
        return None
    if func in (3, 4):
        if len(buf) < 3:
            return None
        return 3 + buf[2] + 2
    if func in (6, 0x10):
        return 8
    return 5


def parse_response(resp, func):
    """返回 (ok, values, err)"""
    if len(resp) < 5:
        return False, None, "应答帧长度不足"
    got = resp[-2] | (resp[-1] << 8)
    if got != crc16(resp[:-2]):
        return False, None, "CRC 校验错"
    f = resp[1]
    if f & 0x80:
        return False, None, "从机异常码 0x%02X" % resp[2]
    if f != func:
        return False, None, "功能码不匹配（期望 %d，收到 %d）" % (func, f)
    if func in (3, 4):
        n = resp[2] // 2
        vals = [(resp[3 + i * 2] << 8) | resp[4 + i * 2] for i in range(n)]
        return True, vals, None
    return True, [], None


# ---------------------------- 命令块 ---------------------------------------

def cmd_version():
    return [1, 0x104]


def cmd_vdd(on):
    return [1, 0x201, 0xFF if on else 0x00]


def cmd_task(task, mask):
    return [5, 0x77, task & 0xFF, mask & 0xFF, 0, 0, 0]


def cmd_query():
    return [5, 0x66]


def cmd_data(start=0):
    return [5, 0x610, start & 0xFF]


def cmd_quality():
    """0x6A1 通信质量计数"""
    return [5, 0x6A1, 0]


def cmd_probe(ch):
    """0x6A0 单通道 OWI 探测（诊断用：会真的断电→上电该通道）"""
    return [5, 0x6A0, ch & 0xFF]


def cmd_config(cfg):
    flags = (1 if cfg["valid"] else 0) | (2 if cfg["ext"] else 0)
    return [5, 0x780, 0, cfg["gd"] & 0xFFFF, cfg["zd"] & 0xFFFF,
            cfg["pga"] & 0xFF, cfg["pctrl"] & 0xFF, cfg["adc"] & 0xFF,
            cfg["sys"] & 0xFF, cfg["lut"] & 0xFF, flags]


# ---------------------------- 位域 -----------------------------------------

def make_pga(g1, g3, g2):
    return ((g1 & 7) << 5) | ((g3 & 7) << 2) | (g2 & 3)


def make_adc(osr, avg):
    return ((osr & 3) << 3) | (avg & 7)


def make_pctrl(vref, ref_gain, vcm, spin=0):
    return ((vref & 1) << 7) | ((ref_gain & 1) << 6) | ((spin & 3) << 2) | (vcm & 3)


def decode_pga(v):
    g1, g3, g2 = (v >> 5) & 7, (v >> 2) & 7, v & 3
    return {"g1": PGA_G1[g1], "g3": PGA_G3[g3], "g2": PGA_G2[g2],
            "total": PGA_G1[g1] * PGA_G3[g3] * PGA_G2[g2]}


def decode_adc(v):
    return {"osr": OSR_LIST[(v >> 3) & 3], "avg": AVG_LIST[v & 7], "sot": bool(v & 0x80)}


def decode_pctrl(v):
    return {
        "vcm": VCM_LIST[v & 3],
        "spin": SPIN_LIST[(v >> 2) & 3],
        "chop1": bool(v & 0x10),
        "chop2": bool(v & 0x20),
        "ref": 1.6384 if (v & 0x40) else 1.0,
        "vref": "VDD/2.0" if (v & 0x80) else "内部2.5V",
    }


SYS_BITS = ["OWI_DIS", "IDAC_EN", "R_MODE", "SPIN_EN", "SPIN_TYPE", "SWAP", "VOCD_EN", "TBG_EN"]
LUT_BITS = ["FIX_CAL_EN", "LUT_CAL_EN", "DAC_HP"]


def bits_on(v, names):
    return [n for i, n in enumerate(names) if (v >> i) & 1]


def sys_text(v):
    return "%s / %s / %s" % ("OWI 已禁" if v & 1 else "OWI 允许",
                             "电流驱动" if v & 2 else "电压驱动",
                             "比例式" if v & 4 else "普通")


def lut_text(v):
    return "LOAD_LUT=%d%s%s" % ((v >> 3) & 0x1F,
                               " / 固定校准" if v & 1 else "",
                               " / LUT 校准" if v & 2 else "")


def temp_from_code(code):
    return (code - TEMP_OFFSET) / TEMP_SLOPE


def to_int16(v):
    return v - 0x10000 if v >= 0x8000 else v


def decode_channels(regs, start=0):
    out = []
    for i in range(len(regs) // REGS_PER_CHIP):
        b = i * REGS_PER_CHIP
        login = (regs[b] >> 8) & 0xFF
        err = regs[b] & 0xFF
        tx100 = to_int16(regs[b + 7])
        status = "skip" if err == 0xFE else ("ok" if err == 0 else "err")
        out.append({
            "ch": start + i,
            "login": login,
            "login_ok": login == 0x55,
            "err": err,
            "err_text": ERR_TEXT.get(err, "未知 0x%02X" % err),
            "status": status,
            "status_text": "正常" if status == "ok" else ERR_SHORT.get(err, ERR_TEXT.get(err, "异常")),
            "gd": regs[b + 1],
            "zd": regs[b + 2],
            "pga": (regs[b + 3] >> 8) & 0xFF,
            "adc": regs[b + 3] & 0xFF,
            "pctrl": (regs[b + 4] >> 8) & 0xFF,
            "sys": regs[b + 4] & 0xFF,
            "lut": (regs[b + 5] >> 8) & 0xFF,
            "fuse": regs[b + 5] & 0xFF,
            "tcode": regs[b + 6],
            "tx100": tx100,
            "temp": tx100 / 100.0,
        })
    return out


def mask_text(mask):
    if mask in (0x00, 0xFF):
        return "全部"
    chs = [str(i) for i in range(CHANNELS) if mask & (1 << i)]
    return ", ".join(chs) if chs else "无"


# ============================================================================
# Modbus 主站
# ============================================================================

class ModbusMaster(object):
    def __init__(self, log_cb):
        self.log_cb = log_cb
        self.ser = None
        self.lock = threading.RLock()
        self.write_addr = PDU_HOLD
        self.read_addr = PDU_INPUT
        self.show_raw = True
        self.timeout = 0.5
        self._dump_path = None
        self.retries = 2

    @property
    def is_open(self):
        return self.ser is not None and self.ser.is_open

    def open(self, port, baud):
        self.ser = serial.Serial(port=port, baudrate=baud, bytesize=8,
                                 parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                 timeout=0.05, write_timeout=1.0)
        try:
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
        except Exception:
            pass

    def close(self):
        try:
            if self.ser is not None:
                self.ser.close()
        except Exception:
            pass
        self.ser = None

    # ---------------------------------------------------------------- 收发
    def _read_frame(self, func, timeout):
        """返回 (完整帧 或 None, 已收到的字节)"""
        buf = bytearray()
        deadline = time.monotonic() + timeout
        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                return None, bytes(buf)
            try:
                self.ser.timeout = min(0.03, remain)
                chunk = self.ser.read(64)
            except Exception:
                return None, bytes(buf)
            if not chunk:
                continue
            buf.extend(chunk)
            if buf and buf[0] != SLAVE:
                # 线上有残留（上一帧没发完/串帧）：对齐到从站地址再解析
                idx = buf.find(bytes([SLAVE]))
                if idx < 0:
                    buf.clear()
                elif idx:
                    del buf[:idx]
                if not buf:
                    continue
            n = frame_len(buf, func)
            if n and len(buf) >= n:
                return bytes(buf[:n]), b""

    def _dump(self, kind, data):
        """把原始帧追加写入 raw_frames.log，便于事后精确分析"""
        if not self._dump_path:
            return
        try:
            with open(self._dump_path, "a", encoding="utf-8") as f:
                f.write("%s %-4s %s\n" % (
                    datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3],
                    kind, fmt_hex(data)))
        except Exception:
            self._dump_path = None

    def drain(self, wait=0.06):
        """放弃一帧后把线上残余字节读干净，避免污染下一条命令"""
        t0 = time.monotonic()
        while (time.monotonic() - t0) < wait:
            try:
                n = self.ser.in_waiting
            except Exception:
                return
            if n:
                try:
                    self.ser.read(n)
                except Exception:
                    return
                t0 = time.monotonic()
            else:
                time.sleep(0.005)

    def _txn(self, req, func, timeout=None, retries=None, quiet=False):
        if not self.is_open:
            raise IOError("串口未连接")
        timeout = self.timeout if timeout is None else timeout
        retries = self.retries if retries is None else retries
        last = "无应答"
        for attempt in range(retries + 1):
            try:
                self.ser.reset_input_buffer()
            except Exception:
                pass
            if self.show_raw and not quiet:
                self.log_cb("tx", fmt_hex(req))
            if not quiet:
                self._dump("TX", req)
            try:
                self.ser.write(req)
                self.ser.flush()
            except Exception as e:
                raise IOError("发送失败：%s" % e)
            resp, partial = self._read_frame(func, timeout)
            if resp:
                if self.show_raw and not quiet:
                    self.log_cb("rx", "%s  (%dB)" % (fmt_hex(resp), len(resp)))
                if not quiet:
                    self._dump("RX", resp)
                ok, vals, err = parse_response(resp, func)
                if ok:
                    return vals
                last = err
                if "异常码" in (err or ""):
                    break
            elif partial:
                if self.show_raw and not quiet:
                    self.log_cb("rx", "%s（不完整 %dB）" % (fmt_hex(partial), len(partial)))
                if not quiet:
                    self._dump("RX-", partial)
                if len(partial) >= 2 and partial[1] != func and partial[1] != (func | 0x80):
                    last = "%dms 内收到的是功能码 0x%02X 的帧（在等 0x%02X），%d 字节" % (
                        int(timeout * 1000), partial[1], func, len(partial))
                elif len(partial) >= 3 and func in (3, 4):
                    last = "%dms 内只收到 %d/%d 字节（半包，中途丢字节）" % (
                        int(timeout * 1000), len(partial), 3 + partial[2] + 2)
                else:
                    last = "%dms 内只收到 %d 字节（不完整）" % (int(timeout * 1000), len(partial))
            else:
                last = "%dms 内无应答" % int(timeout * 1000)
            if attempt < retries:
                self.drain(0.05)   # 上一条应答可能还在路上，先排空再重发
        raise IOError(last)

    def exchange(self, write=None, count=0, func=4, timeout=None, retries=None, quiet=False):
        """写命令块 → 读结果，同一把锁内完成"""
        with self.lock:
            if write:
                self._txn(build_write_multi(SLAVE, self.write_addr, write), 0x10,
                          timeout, retries, quiet)
            if count > 0:
                return self._txn(build_read(SLAVE, func, self.read_addr, count), func,
                                 timeout, retries, quiet)
            return None

    def send_raw(self, data, wait=0.6):
        with self.lock:
            if not self.is_open:
                raise IOError("串口未连接")
            try:
                self.ser.reset_input_buffer()
            except Exception:
                pass
            self.log_cb("tx", fmt_hex(data))
            self.ser.write(data)
            self.ser.flush()
            time.sleep(wait)
            try:
                pending = self.ser.in_waiting
            except Exception:
                pending = 0
            if pending:
                self.log_cb("rx", fmt_hex(self.ser.read(pending)))
            else:
                self.log_cb("warn", "%dms 内无任何应答" % int(wait * 1000))


# ============================================================================
# 界面
# ============================================================================

class Card(tk.Frame):
    """白底卡片：标题栏 + 内容区"""

    def __init__(self, parent, title, tools=None, **kw):
        tk.Frame.__init__(self, parent, bg=PANEL, highlightbackground=LINE,
                          highlightthickness=1, **kw)
        head = tk.Frame(self, bg=HEAD_BG)
        head.pack(fill="x")
        tk.Label(head, text=title, bg=HEAD_BG, fg=TEXT, font=FONT_T).pack(side="left", padx=10, pady=5)
        self.head = head
        self.tools = tk.Frame(head, bg=HEAD_BG)
        self.tools.pack(side="right", padx=(0, 8))
        tk.Frame(self, bg=LINE, height=1).pack(fill="x")
        self.body = tk.Frame(self, bg=PANEL)
        self.body.pack(fill="both", expand=True, padx=9, pady=8)


class App(tk.Tk):
    def __init__(self):
        tk.Tk.__init__(self)

        self.title("%s %s" % (APP_TITLE, APP_VERSION))
        sw, sh = self.winfo_screenwidth(), self.winfo_screenheight()
        w, h = min(1340, sw - 60), min(880, sh - 90)
        self.geometry("%dx%d+%d+%d" % (w, h, max(0, (sw - w) // 2), max(0, (sh - h) // 3)))
        self.minsize(1080, 660)
        self.configure(bg=BG)
        self.columnconfigure(0, weight=0)
        self.columnconfigure(1, weight=1)
        self.rowconfigure(0, weight=0)
        self.rowconfigure(1, weight=1)

        self.uiq = queue.Queue()
        self.mb = ModbusMaster(self.log)
        self.mb._dump_path = self._open_raw_log()
        self.channels = []
        self.busy = False
        self.connected = False
        self.port_map = {}
        self.sel_ch = None

        self._init_style()
        self._init_vars()
        self._build_top()
        self._build_left()
        self._build_right()

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self._drain_id = self.after(50, self._drain)
        self.refresh_ports()
        self._load_settings()
        self._apply_addr_mode()
        self.log("info", "工具已就绪：115200 8N1 · 从站 1 · 命令写 PDU 40000 / 结果读 PDU 30000")
        if self.mb._dump_path:
            self.log("info", "原始帧日志：%s" % self.mb._dump_path)

    # ------------------------------------------------------------------ 基础
    def _init_style(self):
        style = ttk.Style(self)
        for name in ("vista", "winnative", "clam"):
            if name in style.theme_names():
                style.theme_use(name)
                break
        style.configure(".", font=FONT)
        style.configure("Treeview", font=FONT, rowheight=24, fieldbackground=PANEL, background=PANEL)
        style.configure("Treeview.Heading", font=FONT_B)
        style.configure("TButton", padding=(8, 2))
        style.configure("TLabelframe.Label", font=FONT_B)
        style.configure("TCheckbutton", background=PANEL)
        style.configure("TCombobox", padding=2)

    def _init_vars(self):
        self.var_port = tk.StringVar()
        self.var_baud = tk.StringVar(value="115200")
        self.var_task = tk.StringVar(value=TASKS[0][1])
        self.var_mask = tk.StringVar(value="0xFF")
        self.var_prog = tk.StringVar(value="空闲")
        self.var_prognum = tk.StringVar(value="0/8")
        self.var_info = tk.StringVar(value="尚未读取数据")
        self.var_version = tk.StringVar(value="—")
        self.var_conn = tk.StringVar(value="未连接")
        self.var_addr_mode = tk.StringVar(value="40000 / 30000（本固件）")
        self.var_ch = [tk.BooleanVar(value=True) for _ in range(CHANNELS)]

        self.var_gd = tk.StringVar(value="10000")
        self.var_zd = tk.StringVar(value="16383")
        self.var_g1 = tk.StringVar(value="×4")
        self.var_g3 = tk.StringVar(value="×4")
        self.var_g2 = tk.StringVar(value="×4")
        self.var_vref = tk.StringVar(value="内部2.5V")
        self.var_ref = tk.StringVar(value="×1.6384")
        self.var_vcm = tk.StringVar(value="2.5V")
        self.var_osr = tk.StringVar(value="128")
        self.var_avg = tk.StringVar(value="4")
        self.var_sys = tk.StringVar(value="00")
        self.var_lut = tk.StringVar(value="00")
        self.var_valid = tk.BooleanVar(value=True)
        self.var_ext = tk.BooleanVar(value=False)
        self.var_pv1 = tk.StringVar(value="")
        self.var_pv2 = tk.StringVar(value="")
        self.var_pv3 = tk.StringVar(value="")
        self.var_autoread = tk.BooleanVar(value=True)
        self.var_showraw = tk.BooleanVar(value=True)
        self.var_autoscroll = tk.BooleanVar(value=True)

        self.var_man_func = tk.StringVar(value="04 读输入寄存器")
        self.var_man_addr = tk.StringVar(value="30000")
        self.var_man_count = tk.StringVar(value="8")
        self.var_man_values = tk.StringVar()
        self.var_man_raw = tk.StringVar()

    def _accent_btn(self, parent, text, cmd, **kw):
        opts = dict(bg=ACCENT, fg="white", activebackground=ACCENT_DARK, activeforeground="white",
                    relief="flat", bd=0, padx=10, pady=5, cursor="hand2", font=FONT_B)
        opts.update(kw)
        return tk.Button(parent, text=text, command=cmd, **opts)

    def _btn(self, parent, text, cmd, **kw):
        b = ttk.Button(parent, text=text, command=cmd)
        return b

    def _hint(self, parent, text, **kw):
        return tk.Label(parent, text=text, bg=PANEL, fg=MUTED, font=("Microsoft YaHei UI", 8), **kw)

    # ------------------------------------------------------------------ 顶栏
    def _build_top(self):
        top = tk.Frame(self, bg=PANEL)
        top.grid(row=0, column=0, columnspan=2, sticky="ew")
        row = tk.Frame(top, bg=PANEL)
        row.pack(fill="x", padx=12, pady=8)

        tk.Label(row, text="串口", bg=PANEL, fg=MUTED).pack(side="left")
        self.cmb_port = ttk.Combobox(row, textvariable=self.var_port, state="readonly", width=30, font=FONT)
        self.cmb_port.pack(side="left", padx=(6, 4))
        ttk.Button(row, text="刷新", width=6, command=self.refresh_ports).pack(side="left")
        tk.Label(row, text="波特率", bg=PANEL, fg=MUTED).pack(side="left", padx=(12, 0))
        ttk.Combobox(row, textvariable=self.var_baud, state="readonly", width=8, font=FONT,
                     values=["115200", "57600", "38400", "9600"]).pack(side="left", padx=6)
        self.btn_connect = self._accent_btn(row, "连接串口", self.on_connect)
        self.btn_connect.pack(side="left", padx=(6, 4))
        self.btn_disconnect = ttk.Button(row, text="断开", width=7, command=self.on_disconnect, state="disabled")
        self.btn_disconnect.pack(side="left")

        self.lbl_dot = tk.Label(row, text="●", bg=PANEL, fg="#c3ccd8", font=("Segoe UI", 12))
        self.lbl_dot.pack(side="right", padx=(0, 4))
        tk.Label(row, textvariable=self.var_conn, bg=PANEL, fg=MUTED).pack(side="right")
        tk.Frame(top, bg=LINE, height=1).pack(fill="x")

    # ------------------------------------------------------------------ 左栏
    def _build_left(self):
        outer = tk.Frame(self, bg=BG, width=386)
        outer.grid(row=1, column=0, sticky="nsew", padx=(12, 0), pady=10)
        outer.grid_propagate(False)
        outer.rowconfigure(0, weight=1)
        outer.columnconfigure(0, weight=1)

        self.left_outer = outer
        canvas = tk.Canvas(outer, bg=BG, highlightthickness=0)
        canvas.grid(row=0, column=0, sticky="nsew")
        vsb = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
        vsb.grid(row=0, column=1, sticky="ns")
        canvas.configure(yscrollcommand=vsb.set)
        self.left_canvas = canvas

        inner = tk.Frame(canvas, bg=BG)
        canvas.create_window((0, 0), window=inner, anchor="nw", width=362)
        inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        self.bind_all("<MouseWheel>", self._on_wheel)

        self._build_system(inner)
        self._build_task(inner)

        card = Card(inner, "更多工具")
        card.pack(fill="x", pady=(0, 8))
        b = card.body
        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x")
        self._btn(r, "芯片配置 0x780…", self.open_config_dialog).pack(side="left", fill="x", expand=True)
        self._btn(r, "手动指令 / 调试…", self.open_manual_dialog).pack(side="left", fill="x", expand=True, padx=(6, 0))

    def _dialog(self, attr, title, builder):
        dlg = getattr(self, attr, None)
        if dlg is not None:
            try:
                if dlg.winfo_exists():
                    dlg.deiconify()
                    dlg.lift()
                    dlg.focus_force()
                    return dlg
            except Exception:
                pass
        dlg = tk.Toplevel(self)
        dlg.title(title)
        dlg.configure(bg=BG)
        dlg.minsize(400, 260)
        dlg.transient(self)
        builder(dlg)
        setattr(self, attr, dlg)
        dlg.lift()
        dlg.focus_force()
        return dlg

    def open_config_dialog(self):
        self._dialog("dlg_cfg", "芯片配置 0x780", self._build_config)

    def open_manual_dialog(self):
        self._dialog("dlg_man", "手动指令 / 调试", self._build_manual)

    def _on_wheel(self, event):
        try:
            w = self.winfo_containing(event.x_root, event.y_root)
        except Exception:
            return
        while w is not None:
            if w is self.left_outer:
                self.left_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
                return "break"
            w = getattr(w, "master", None)

    def _build_system(self, parent):
        card = Card(parent, "系统")
        card.pack(fill="x", pady=(0, 8))
        b = card.body
        row = tk.Frame(b, bg=PANEL)
        row.pack(fill="x")
        self.btn_version = self._btn(row, "读版本 0x104", self.op_version)
        self.btn_version.pack(side="left")
        self.btn_vdd_on = self._btn(row, "总电源开", lambda: self.op_vdd(True))
        self.btn_vdd_on.pack(side="left", padx=6)
        self.btn_vdd_off = self._btn(row, "总电源关", lambda: self.op_vdd(False))
        self.btn_vdd_off.pack(side="left")
        r2 = tk.Frame(b, bg=PANEL)
        r2.pack(fill="x", pady=(6, 0))
        tk.Label(r2, text="版本回读：", bg=PANEL, fg=MUTED).pack(side="left")
        tk.Label(r2, textvariable=self.var_version, bg=PANEL, fg=TEXT, font=MONO).pack(side="left")

    def _build_task(self, parent):
        card = Card(parent, "任务")
        card.pack(fill="x", pady=(0, 8))
        tk.Label(card.tools, text="40001=5 / 40002=0x77", bg=HEAD_BG, fg=MUTED,
                 font=("Consolas", 8)).pack()
        b = card.body

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x")
        tk.Label(r, text="任务号", bg=PANEL, fg=MUTED, width=6, anchor="w").pack(side="left")
        ttk.Combobox(r, textvariable=self.var_task, state="readonly", font=FONT,
                     values=[t[1] for t in TASKS]).pack(side="left", fill="x", expand=True)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(7, 0))
        tk.Label(r, text="通道", bg=PANEL, fg=MUTED, width=6, anchor="w").pack(side="left")
        self.chk_ch = []
        for i in range(CHANNELS):
            cb = tk.Checkbutton(r, text=str(i), variable=self.var_ch[i], bg=PANEL,
                                activebackground=PANEL, font=MONO, command=self._sync_mask,
                                highlightthickness=0, bd=0, padx=3)
            cb.pack(side="left")
            self.chk_ch.append(cb)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(7, 0))
        tk.Label(r, text="掩码", bg=PANEL, fg=MUTED, width=6, anchor="w").pack(side="left")
        e = ttk.Entry(r, textvariable=self.var_mask, width=7, font=MONO)
        e.pack(side="left")
        e.bind("<Return>", lambda ev: self._apply_mask_text())
        e.bind("<FocusOut>", lambda ev: self._apply_mask_text())
        ttk.Button(r, text="全选", width=5, command=lambda: self._set_mask(0xFF)).pack(side="left", padx=(6, 2))
        ttk.Button(r, text="清空", width=5, command=lambda: self._set_mask(0x00)).pack(side="left", padx=2)
        self.lbl_mask = tk.Label(r, text="全部", bg=PANEL, fg=MUTED, font=("Microsoft YaHei UI", 8))
        self.lbl_mask.pack(side="left", padx=(6, 0))

        self.btn_run = self._accent_btn(b, "一键测试并读取", self.on_run_task)
        self.btn_run.pack(fill="x", pady=(9, 0), ipady=3)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(6, 0))
        self.btn_send_task = self._btn(r, "仅下发任务", self.on_send_task)
        self.btn_send_task.pack(side="left", fill="x", expand=True)
        self.btn_query = self._btn(r, "查询状态", self.on_query)
        self.btn_query.pack(side="left", fill="x", expand=True, padx=(6, 0))

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(9, 0))
        self.prog = ttk.Progressbar(r, mode="determinate", maximum=CHANNELS, value=0)
        self.prog.pack(fill="x")
        r2 = tk.Frame(b, bg=PANEL)
        r2.pack(fill="x", pady=(3, 0))
        tk.Label(r2, textvariable=self.var_prog, bg=PANEL, fg=MUTED,
                 font=("Microsoft YaHei UI", 8), anchor="w").pack(side="left")
        tk.Label(r2, textvariable=self.var_prognum, bg=PANEL, fg=MUTED,
                 font=("Consolas", 8)).pack(side="right")

        tk.Frame(b, bg=LINE, height=1).pack(fill="x", pady=(9, 7))
        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x")
        self.btn_read = self._accent_btn(r, "读取 0x610", self.on_read_data, padx=8, pady=3)
        self.btn_read.pack(side="left", fill="x", expand=True)
        self._btn(r, "导出 CSV", self.on_export_csv).pack(side="left", fill="x", expand=True, padx=(6, 0))

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(6, 0))
        self.btn_probe_main = self._accent_btn(r, "OWI 探测", self.on_owi_probe, padx=8, pady=3)
        self.btn_probe_main.pack(side="left", fill="x", expand=True)
        self.btn_qual = self._btn(r, "通信质量", self.on_mb_quality)
        self.btn_qual.pack(side="left", fill="x", expand=True, padx=(6, 0))

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(6, 0))
        self.btn_link = self._btn(r, "链路自检（连发 20 条，统计成功率）", self.on_link_test)
        self.btn_link.pack(side="left", fill="x", expand=True)
        tk.Label(b, textvariable=self.var_info, bg=PANEL, fg=MUTED,
                 font=("Microsoft YaHei UI", 8), anchor="w").pack(fill="x", pady=(5, 0))

    def _build_config(self, parent):
        b = tk.Frame(parent, bg=PANEL)
        b.pack(fill="both", expand=True, padx=9, pady=8)

        def row(label, widget_builder):
            r = tk.Frame(b, bg=PANEL)
            r.pack(fill="x", pady=2)
            tk.Label(r, text=label, bg=PANEL, fg=MUTED, width=9, anchor="w").pack(side="left")
            widget_builder(r)
            return r

        def gd(r):
            ttk.Entry(r, textvariable=self.var_gd, width=8, font=MONO).pack(side="left")
            self._hint(r, "0x08/09 · 0~16383").pack(side="left", padx=6)

        def zd(r):
            ttk.Entry(r, textvariable=self.var_zd, width=8, font=MONO).pack(side="left")
            self._hint(r, "0x0A/0B · 0~16383").pack(side="left", padx=6)

        def gains(r):
            ttk.Combobox(r, textvariable=self.var_g1, state="readonly", width=7, font=FONT,
                         values=["×%g" % g for g in PGA_G1]).pack(side="left")
            ttk.Combobox(r, textvariable=self.var_g3, state="readonly", width=7, font=FONT,
                         values=["×%g" % g for g in PGA_G3]).pack(side="left", padx=4)
            ttk.Combobox(r, textvariable=self.var_g2, state="readonly", width=7, font=FONT,
                         values=["×%g" % g for g in PGA_G2]).pack(side="left")

        def refs(r):
            ttk.Combobox(r, textvariable=self.var_vref, state="readonly", width=9, font=FONT,
                         values=["内部2.5V", "VDD/2.0"]).pack(side="left")
            ttk.Combobox(r, textvariable=self.var_ref, state="readonly", width=8, font=FONT,
                         values=["×1", "×1.6384"]).pack(side="left", padx=4)
            ttk.Combobox(r, textvariable=self.var_vcm, state="readonly", width=7, font=FONT,
                         values=VCM_LIST).pack(side="left")

        def samp(r):
            ttk.Combobox(r, textvariable=self.var_osr, state="readonly", width=8, font=FONT,
                         values=[str(v) for v in OSR_LIST]).pack(side="left")
            ttk.Combobox(r, textvariable=self.var_avg, state="readonly", width=8, font=FONT,
                         values=[str(v) for v in AVG_LIST]).pack(side="left", padx=4)
            self._hint(r, "OSR / 平均次数").pack(side="left", padx=4)

        def sysrow(r):
            e = ttk.Entry(r, textvariable=self.var_sys, width=5, font=MONO)
            e.pack(side="left")
            self.lbl_sys = self._hint(r, "")
            self.lbl_sys.pack(side="left", padx=6)
            self.var_sys.trace_add("write", lambda *a: self._update_preview())
            e.bind("<FocusOut>", lambda ev: self._update_preview())

        def lutrow(r):
            e = ttk.Entry(r, textvariable=self.var_lut, width=5, font=MONO)
            e.pack(side="left")
            self.lbl_lut = self._hint(r, "")
            self.lbl_lut.pack(side="left", padx=6)
            self.var_lut.trace_add("write", lambda *a: self._update_preview())
            e.bind("<FocusOut>", lambda ev: self._update_preview())

        def flags(r):
            tk.Checkbutton(r, text="配置生效 (valid)", variable=self.var_valid, bg=PANEL,
                           activebackground=PANEL, font=FONT, command=self._update_preview,
                           highlightthickness=0, bd=0).pack(side="left")
            tk.Checkbutton(r, text="额外写 0x44/0x47 (ext_en)", variable=self.var_ext, bg=PANEL,
                           activebackground=PANEL, font=FONT, command=self._update_preview,
                           highlightthickness=0, bd=0).pack(side="left", padx=(8, 0))

        row("增益 DAC", gd)
        row("零点 DAC", zd)
        row("PGA 增益", gains)
        row("基准/VCM", refs)
        row("采样", samp)
        row("0x44", sysrow)
        row("0x47", lutrow)
        row("生效", flags)

        for v in (self.var_gd, self.var_zd, self.var_g1, self.var_g3, self.var_g2,
                  self.var_vref, self.var_ref, self.var_vcm, self.var_osr, self.var_avg):
            v.trace_add("write", lambda *a: self._update_preview())

        pv = tk.Frame(b, bg="#f7f9fc", highlightbackground=LINE, highlightthickness=1)
        pv.pack(fill="x", pady=(6, 0))
        for i, var in enumerate((self.var_pv1, self.var_pv2, self.var_pv3)):
            tk.Label(pv, textvariable=var, bg="#f7f9fc", fg="#3b4d63", font=("Consolas", 8),
                     anchor="w", justify="left").pack(fill="x", padx=7,
                                                      pady=(5 if i == 0 else 0, 5 if i == 2 else 0))

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(7, 0))
        self.btn_cfg = self._accent_btn(r, "下发配置", self.on_write_config, padx=8, pady=3)
        self.btn_cfg.pack(side="left", fill="x", expand=True)
        self.btn_cfg_default = self._btn(r, "官方默认值", self.on_cfg_default)
        self.btn_cfg_default.pack(side="left", fill="x", expand=True, padx=(6, 0))
        self._update_preview()

    def _build_manual(self, parent):
        b = tk.Frame(parent, bg=PANEL)
        b.pack(fill="both", expand=True, padx=9, pady=8)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=2)
        tk.Label(r, text="地址基准", bg=PANEL, fg=MUTED, width=9, anchor="w").pack(side="left")
        cb = ttk.Combobox(r, textvariable=self.var_addr_mode, state="readonly", font=FONT,
                          values=["40000 / 30000（本固件）", "0 / 0（标准 0 基从机）"])
        cb.pack(side="left", fill="x", expand=True)
        cb.bind("<<ComboboxSelected>>", lambda ev: self._apply_addr_mode())

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=2)
        tk.Label(r, text="功能码", bg=PANEL, fg=MUTED, width=9, anchor="w").pack(side="left")
        ttk.Combobox(r, textvariable=self.var_man_func, state="readonly", width=15, font=FONT,
                     values=["04 读输入寄存器", "03 读保持寄存器", "10 写多寄存器"]).pack(side="left")
        ttk.Entry(r, textvariable=self.var_man_addr, width=7, font=MONO).pack(side="left", padx=4)
        ttk.Entry(r, textvariable=self.var_man_count, width=5, font=MONO).pack(side="left")

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=2)
        tk.Label(r, text="数据", bg=PANEL, fg=MUTED, width=9, anchor="w").pack(side="left")
        ttk.Entry(r, textvariable=self.var_man_values, font=MONO).pack(side="left", fill="x", expand=True)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(4, 2))
        self.btn_man = self._btn(r, "发送", self.on_manual_send)
        self.btn_man.pack(side="left", fill="x", expand=True)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=2)
        tk.Label(r, text="原始帧", bg=PANEL, fg=MUTED, width=9, anchor="w").pack(side="left")
        ttk.Entry(r, textvariable=self.var_man_raw, font=MONO).pack(side="left", fill="x", expand=True)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(4, 2))
        self.btn_raw = self._btn(r, "直接发送（CRC 不对自动重算）", self.on_raw_send)
        self.btn_raw.pack(side="left", fill="x", expand=True)

        tk.Frame(b, bg=LINE, height=1).pack(fill="x", pady=(8, 6))
        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x")
        self.btn_loop = self._btn(r, "回环自测（TX-RX 短接）", self.on_loopback)
        self.btn_loop.pack(side="left", fill="x", expand=True)
        self.btn_listen = self._btn(r, "监听 3 秒", self.on_listen)
        self.btn_listen.pack(side="left", fill="x", expand=True, padx=(6, 0))

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(4, 2))
        self.btn_scan = self._btn(r, "读长度扫描（量从机最长能回多少）", self.on_scan)
        self.btn_scan.pack(side="left", fill="x", expand=True)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(4, 2))
        self.btn_full = self._btn(r, "整段读取一次（64 个寄存器 / 133 字节）", self.on_full_read)
        self.btn_full.pack(side="left", fill="x", expand=True)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(4, 2))
        self.btn_cap = self._btn(r, "抓帧节奏（量每块字节几点到）", self.on_capture)
        self.btn_cap.pack(side="left", fill="x", expand=True)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(4, 2))
        self.btn_txscan = self._btn(r, "写长度扫描（量从机能完整收到多长的命令）", self.on_tx_scan)
        self.btn_txscan.pack(side="left", fill="x", expand=True)

        r = tk.Frame(b, bg=PANEL)
        r.pack(fill="x", pady=(4, 2))
        self.btn_probe = self._accent_btn(r, "OWI 探测（逐通道：有没有电 / 芯片有没有应答）",
                                          self.on_owi_probe, padx=8, pady=3)
        self.btn_probe.pack(side="left", fill="x", expand=True)

        self._hint(b, "固件 FreeModbus 会把 PDU 地址 +1 再判断 == 40001/30001，\n"
                      "所以命令帧地址填 40000 / 30000（手动地址为绝对 PDU 地址）。",
                   justify="left").pack(fill="x", pady=(4, 0))

    # ------------------------------------------------------------------ 右栏
    def _build_right(self):
        right = tk.Frame(self, bg=BG)
        right.grid(row=1, column=1, sticky="nsew", padx=12, pady=10)
        right.rowconfigure(0, weight=5)
        right.rowconfigure(1, weight=3)
        right.rowconfigure(2, weight=4)
        right.columnconfigure(0, weight=1)

        # ---- 结果表
        card = Card(right, "测试结果")
        card.grid(row=0, column=0, sticky="nsew")
        self.lbl_summary = tk.Label(card.tools, text="尚未读取", bg=HEAD_BG, fg=MUTED,
                                    font=("Microsoft YaHei UI", 8))
        self.lbl_summary.pack(side="left", padx=(0, 10))
        tk.Checkbutton(card.tools, text="任务完成后自动读取", variable=self.var_autoread,
                       bg=HEAD_BG, activebackground=HEAD_BG, fg=MUTED,
                       font=("Microsoft YaHei UI", 8), highlightthickness=0, bd=0).pack(side="left")

        cols = [("ch", "通道", 40), ("status", "状态", 72), ("login", "登录", 56),
                ("err", "错误", 50), ("gd", "增益DAC", 66), ("zd", "零点DAC", 66),
                ("pga", "PGA 0x45", 66), ("adc", "ADC 0x4B", 64), ("pctrl", "基准 0x48", 72),
                ("sys", "SYS 0x44", 64), ("lut", "LUT 0x47", 62), ("tcode", "温度码", 62),
                ("temp", "温度 ℃", 56), ("fuse", "烧写", 46)]
        wrap = tk.Frame(card.body, bg=PANEL)
        wrap.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(wrap, columns=[c[0] for c in cols], show="headings", height=8)
        for key, title, width in cols:
            self.tree.heading(key, text=title)
            self.tree.column(key, width=width, minwidth=width, anchor="center", stretch=False)
        vsb = ttk.Scrollbar(wrap, orient="vertical", command=self.tree.yview)
        hsb = ttk.Scrollbar(wrap, orient="horizontal", command=self.tree.xview)
        self.tree.configure(yscrollcommand=vsb.set, xscrollcommand=hsb.set)
        self.tree.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")
        hsb.grid(row=1, column=0, sticky="ew")
        wrap.rowconfigure(0, weight=1)
        wrap.columnconfigure(0, weight=1)
        self.tree.tag_configure("zebra", background="#f8fafc")
        self.tree.tag_configure("ok", foreground=OK_C)
        self.tree.tag_configure("err", foreground=ERR_C)
        self.tree.tag_configure("skip", foreground=SKIP_C)
        self.tree.bind("<<TreeviewSelect>>", self._on_select_row)
        self._show_placeholder()

        # ---- 详情
        card2 = Card(right, "通道详情")
        card2.grid(row=1, column=0, sticky="nsew", pady=(10, 0))
        self.lbl_detail = tk.Label(card2.tools, text="点击表格行查看", bg=HEAD_BG, fg=MUTED,
                                   font=("Microsoft YaHei UI", 8))
        self.lbl_detail.pack()
        self.detail = tk.Text(card2.body, height=7, wrap="word", bd=0, relief="flat",
                              bg=PANEL, fg=TEXT, font=("Microsoft YaHei UI", 9),
                              highlightthickness=0, padx=2, pady=0)
        self.detail.pack(fill="both", expand=True)
        self.detail.tag_configure("h", font=("Microsoft YaHei UI", 10, "bold"), foreground="#12489f")
        self.detail.tag_configure("n", foreground=TEXT)
        self.detail.tag_configure("m", foreground=MUTED)
        self.detail.insert("1.0", "读取数据后，点击上方表格中的通道行，这里会展开该通道的寄存器位域。", "m")
        self.detail.configure(state="disabled")

        # ---- 日志
        card3 = Card(right, "通信日志")
        card3.grid(row=2, column=0, sticky="nsew", pady=(10, 0))
        tk.Checkbutton(card3.tools, text="显示原始帧", variable=self.var_showraw,
                       bg=HEAD_BG, activebackground=HEAD_BG, fg=MUTED,
                       font=("Microsoft YaHei UI", 8), highlightthickness=0, bd=0,
                       command=self._on_showraw).pack(side="left", padx=(0, 8))
        tk.Checkbutton(card3.tools, text="自动滚动", variable=self.var_autoscroll,
                       bg=HEAD_BG, activebackground=HEAD_BG, fg=MUTED,
                       font=("Microsoft YaHei UI", 8), highlightthickness=0, bd=0).pack(side="left", padx=(0, 8))
        ttk.Button(card3.tools, text="清空", width=5,
                   command=lambda: self._clear_log()).pack(side="left", padx=(0, 4))
        ttk.Button(card3.tools, text="演示数据", width=8,
                   command=self.on_demo).pack(side="left")

        wrap3 = tk.Frame(card3.body, bg=PANEL)
        wrap3.pack(fill="both", expand=True)
        self.log_text = tk.Text(wrap3, height=8, wrap="word", bd=0, relief="flat",
                                bg="#fbfcfe", fg="#48586c", font=("Consolas", 9),
                                highlightthickness=1, highlightbackground=LINE, padx=6, pady=4)
        lsb = ttk.Scrollbar(wrap3, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=lsb.set)
        self.log_text.grid(row=0, column=0, sticky="nsew")
        lsb.grid(row=0, column=1, sticky="ns")
        wrap3.rowconfigure(0, weight=1)
        wrap3.columnconfigure(0, weight=1)
        self.log_text.tag_configure("time", foreground="#a6b2c1")
        self.log_text.tag_configure("tx", foreground="#1b62d6")
        self.log_text.tag_configure("rx", foreground="#0b8a52")
        self.log_text.tag_configure("info", foreground="#6b7a8e")
        self.log_text.tag_configure("ok", foreground="#0b7a49")
        self.log_text.tag_configure("warn", foreground="#8a5b00")
        self.log_text.tag_configure("err", foreground="#b8291f")
        self.log_text.configure(state="disabled")

    def _open_raw_log(self):
        """原始帧日志：优先放在 exe 同目录，不可写则放 %APPDATA%"""
        import sys as _sys
        base = os.path.dirname(_sys.executable if getattr(_sys, "frozen", False)
                               else os.path.abspath(__file__))
        cand = [base, os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"),
                                   "Saf8100Tool")]
        for d in cand:
            try:
                if not os.path.isdir(d):
                    os.makedirs(d)
                path = os.path.join(d, "raw_frames.log")
                with open(path, "a", encoding="utf-8") as f:
                    f.write("")
                return path
            except Exception:
                continue
        return None

    # ------------------------------------------------------------------ 线程队列
    def post(self, fn, *args):
        self.uiq.put((fn, args))

    def _drain(self):
        try:
            while True:
                fn, args = self.uiq.get_nowait()
                try:
                    fn(*args)
                except Exception as e:
                    # 不让界面更新异常静默消失，写进日志便于反馈
                    try:
                        self._log_ui("err", "界面更新异常：%r" % (e,))
                    except Exception:
                        pass
        except queue.Empty:
            pass
        self._drain_id = self.after(40, self._drain)

    # ------------------------------------------------------------------ 日志
    def log(self, kind, msg):
        self.post(self._log_ui, kind, msg)

    def _log_ui(self, kind, msg):
        tag = kind if kind in ("tx", "rx", "info", "ok", "warn", "err") else "info"
        label = {"tx": "TX", "rx": "RX", "info": "··", "ok": "OK", "warn": " !", "err": "ERR"}.get(tag, "··")
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        if tag in ("tx", "rx") and not self.var_showraw.get():
            return
        self.log_text.configure(state="normal")
        self.log_text.insert("end", ts + " ", "time")
        self.log_text.insert("end", label.ljust(4) + " ", tag)
        self.log_text.insert("end", msg + "\n", tag)
        lines = int(self.log_text.index("end-1c").split(".")[0])
        if lines > 600:
            self.log_text.delete("1.0", "%d.0" % (lines - 600))
        self.log_text.configure(state="disabled")
        if self.var_autoscroll.get():
            self.log_text.see("end")

    def _clear_log(self):
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    def _on_showraw(self):
        self.mb.show_raw = self.var_showraw.get()

    # ------------------------------------------------------------------ 串口
    def refresh_ports(self):
        ports = sorted(list_ports.comports(), key=lambda p: p.device)
        self.port_map = {}
        values = []
        for p in ports:
            desc = (p.description or "").strip()
            label = "%s — %s" % (p.device, desc) if desc and desc != "n/a" else p.device
            values.append(label)
            self.port_map[label] = p.device
        self.cmb_port["values"] = values
        if values and not self.var_port.get():
            for v in values:
                if "USB" in v.upper() or "CH340" in v.upper() or "CP210" in v.upper():
                    self.var_port.set(v)
                    break
            else:
                self.var_port.set(values[0])
        if not values:
            self.var_port.set("")
            self.log("warn", "未发现串口设备")

    def on_connect(self):
        if self.mb.is_open:
            return
        label = self.var_port.get()
        port = self.port_map.get(label, label)
        if not port:
            messagebox.showwarning("提示", "请先选择串口（若列表为空，点「刷新」并检查驱动）")
            return
        try:
            self.mb.open(port, int(self.var_baud.get()))
        except Exception as e:
            self.log("err", "打开串口失败：%s" % e)
            messagebox.showerror("打开失败", "无法打开 %s\n\n%s" % (port, e))
            return
        self.connected = True
        self._apply_addr_mode(log_it=False)
        self.var_conn.set("已连接 · %s" % self.var_baud.get())
        self.lbl_dot.configure(fg=OK_C)
        self.btn_connect.configure(state="disabled")
        self.btn_disconnect.configure(state="normal")
        self.log("ok", "串口已打开：%s @ %s 8N1，从站 %d" % (port, self.var_baud.get(), SLAVE))

    def on_disconnect(self):
        self.mb.close()
        self.connected = False
        self.var_conn.set("未连接")
        self.lbl_dot.configure(fg="#c3ccd8")
        self.btn_connect.configure(state="normal")
        self.btn_disconnect.configure(state="disabled")
        self.log("info", "串口已关闭")

    def _on_close(self):
        # 先取消队列泵，否则窗口销毁后 Tk 还会调用一次已删除的回调
        try:
            self.after_cancel(self._drain_id)
        except Exception:
            pass
        try:
            self.mb.close()
        except Exception:
            pass
        self.destroy()

    # ------------------------------------------------------------------ 掩码
    def _sync_mask(self):
        m = 0
        for i, v in enumerate(self.var_ch):
            if v.get():
                m |= 1 << i
        self.var_mask.set("0x%02X" % m)
        self.lbl_mask.configure(text=mask_text(m))

    def _apply_mask_text(self):
        raw = self.var_mask.get().strip().replace("0x", "").replace("0X", "")
        try:
            m = int(raw, 16) & 0xFF
        except Exception:
            m = 0xFF
        self._set_mask(m)

    def _set_mask(self, m):
        self.var_mask.set("0x%02X" % m)
        for i in range(CHANNELS):
            self.var_ch[i].set(bool(m & (1 << i)))
        self.lbl_mask.configure(text=mask_text(m))

    def get_mask(self):
        raw = self.var_mask.get().strip().replace("0x", "").replace("0X", "")
        try:
            return int(raw, 16) & 0xFF
        except Exception:
            return 0xFF

    def _apply_addr_mode(self, log_it=True):
        fw = self.var_addr_mode.get().startswith("40000")
        self.mb.write_addr = PDU_HOLD if fw else 0
        self.mb.read_addr = PDU_INPUT if fw else 0
        self.var_man_addr.set(str(self.mb.read_addr))
        self._update_preview()
        if log_it:
            self.log("info", "地址基准：%s" % ("40000 / 30000（本固件）" if fw else "0 / 0（标准 0 基从机）"))

    # ------------------------------------------------------------------ 配置
    def cfg_from_ui(self):
        def num(var, default=0, base=10):
            try:
                s = str(var.get()).strip().replace("0x", "").replace("0X", "")
                return int(s, base) if s else default
            except Exception:
                return default

        def idx(var, values, default=0):
            s = var.get()
            if s.startswith("×"):
                s = s[1:]
            try:
                if values and isinstance(values[0], str):
                    return values.index(s)
                return values.index(float(s))
            except Exception:
                return default

        cfg = {
            "gd": num(self.var_gd), "zd": num(self.var_zd),
            "g1": idx(self.var_g1, ["%g" % g for g in PGA_G1]),
            "g3": idx(self.var_g3, ["%g" % g for g in PGA_G3]),
            "g2": idx(self.var_g2, ["%g" % g for g in PGA_G2]),
            "vref": 1 if self.var_vref.get() == "VDD/2.0" else 0,
            "ref": 1 if "1.6384" in self.var_ref.get() else 0,
            "vcm": idx(self.var_vcm, VCM_LIST, 3),
            "osr": idx(self.var_osr, [str(v) for v in OSR_LIST], 2),
            "avg": idx(self.var_avg, [str(v) for v in AVG_LIST], 5),
            "sys": num(self.var_sys, 0, 16) & 0xFF,
            "lut": num(self.var_lut, 0, 16) & 0xFF,
            "valid": self.var_valid.get(),
            "ext": self.var_ext.get(),
        }
        # 位域拼装结果，cmd_config() 直接用
        cfg["pga"] = make_pga(cfg["g1"], cfg["g3"], cfg["g2"])
        cfg["adc"] = make_adc(cfg["osr"], cfg["avg"])
        cfg["pctrl"] = make_pctrl(cfg["vref"], cfg["ref"], cfg["vcm"])
        return cfg

    def _cfg_bytes(self):
        c = self.cfg_from_ui()
        return c, c["pga"], c["adc"], c["pctrl"]

    def _update_preview(self):
        c, pga, adc, pctrl = self._cfg_bytes()
        dp, da, dc = decode_pga(pga), decode_adc(adc), decode_pctrl(pctrl)
        try:
            self.lbl_sys.configure(text=sys_text(c["sys"]))
            self.lbl_lut.configure(text=lut_text(c["lut"]))
        except Exception:
            pass
        cmd = [5, 0x780, 0, c["gd"], c["zd"], pga, pctrl, adc, c["sys"], c["lut"],
               (1 if c["valid"] else 0) | (2 if c["ext"] else 0)]
        frame = build_write_multi(SLAVE, self.mb.write_addr, cmd)
        fhex = fmt_hex(frame[:10]) + " …" if len(frame) > 10 else fmt_hex(frame)
        self.var_pv1.set("0x45=0x%02X   G1/G3/G2 = %g / %g / %g×" % (pga, dp["g1"], dp["g3"], dp["g2"]))
        self.var_pv2.set("0x48=0x%02X %s/%s×%g  0x4B=0x%02X OSR%d/AVG%d  0x44=0x%02X 0x47=0x%02X"
                         % (pctrl, dc["vcm"], dc["vref"], dc["ref"], adc, da["osr"], da["avg"],
                            c["sys"], c["lut"]))
        self.var_pv3.set("帧 %dB  %s" % (len(frame), fhex))

    def on_cfg_default(self):
        self.var_gd.set("10000")
        self.var_zd.set("16383")
        self.var_g1.set("×4")
        self.var_g3.set("×4")
        self.var_g2.set("×4")
        self.var_vref.set("内部2.5V")
        self.var_ref.set("×1.6384")
        self.var_vcm.set("2.5V")
        self.var_osr.set("128")
        self.var_avg.set("4")
        self.var_sys.set("00")
        self.var_lut.set("00")
        self.var_valid.set(True)
        self.var_ext.set(False)
        self._update_preview()
        self.log("info", "已恢复官方 demo 默认配置（需点「下发配置」才写入芯片）")

    # ------------------------------------------------------------------ 任务流程
    def _require_port(self):
        if not self.mb.is_open:
            self.log("warn", "请先连接串口")
            return False
        return True

    def _set_busy(self, busy):
        self.busy = busy
        state = "disabled" if busy else "normal"
        for name in ("btn_run", "btn_send_task", "btn_query", "btn_read", "btn_cfg",
                     "btn_cfg_default", "btn_version", "btn_vdd_on", "btn_vdd_off",
                     "btn_man", "btn_raw", "btn_loop", "btn_listen", "btn_scan", "btn_full", "btn_cap", "btn_txscan", "btn_probe", "btn_probe_main", "btn_qual", "btn_link"):
            w = getattr(self, name, None)
            if w is not None:
                try:
                    w.configure(state=state)
                except Exception:
                    pass

    def _bg(self, fn, *args, **kw):
        label = kw.pop("label", "")

        def run():
            try:
                fn(*args)
            except Exception as e:
                msg = str(e)
                if "无应答" in msg:
                    msg += " —— 查：串口是否选对 / TX-RX 是否交叉 / 是否共地 / 从机是否上电 / 波特率是否 115200"
                self.log("err", ("%s失败：%s" % (label, msg)) if label else msg)
            finally:
                self.post(self._set_busy, False)

        t = threading.Thread(target=run, daemon=True)
        t.start()

    def _selected_task(self):
        """下拉框文字 → 任务号（5/6/7）"""
        label = self.var_task.get()
        for num, text in TASKS:
            if text == label:
                return num
        return TASKS[0][0]

    def on_run_task(self):
        if self.busy or not self._require_port():
            return
        task = self._selected_task()
        if task == 7:
            if not messagebox.askyesno(
                    "确认烧写",
                    "OTP 烧写不可逆，每颗芯片只能烧一次。\n建议先用空白片验证 T5。\n\n确认执行 T7 吗？"):
                return
        self.post(self._set_busy, True)
        self.post(self._set_progress, 0, "任务已下发，等待状态…")
        auto_read = bool(self.var_autoread.get())
        self._bg(self._task_flow, task, self.get_mask(), auto_read, label="一键测试")

    def _task_flow(self, task, mask, auto_read=True):
        t0 = time.time()
        self.mb.exchange(write=cmd_task(task, mask))
        self.log("ok", "已下发 T%d，通道掩码 0x%02X（%s）" % (task, mask, mask_text(mask)))
        time.sleep(0.12)

        deadline = time.time() + 120
        timeouts = 0
        last_prog = -1
        done = False
        while time.time() < deadline:
            try:
                st = self.mb.exchange(write=cmd_query(), count=7, timeout=0.4, retries=0, quiet=True)
            except Exception as e:
                timeouts += 1
                if timeouts % 6 == 0:
                    self.log("warn", "状态查询无应答（%s）—— OWI 通信期间固件关闭 Modbus 时基，属正常，继续等待" % e)
                time.sleep(0.12)
                continue
            timeouts = 0
            busy, order, prog = st[0], st[1], st[2]
            self.post(self._set_progress, prog, "测试中 · 通道 %d/%d" % (prog, CHANNELS) if busy else "任务收尾…")
            if prog != last_prog:
                last_prog = prog
                self.log("info", "进度 %d/%d（busy=%d order=%d）" % (prog, CHANNELS, busy, order))
            if busy == 0 and order == 0:
                done = True
                break
            time.sleep(0.15)

        if not done:
            raise RuntimeError("等待任务结束超时（120s）")

        self.post(self._set_progress, CHANNELS, "任务完成")
        self.log("ok", "任务完成，耗时 %.1fs" % (time.time() - t0))
        if auto_read:
            time.sleep(0.15)      # 让固件从 OWI 状态恢复 Modbus 时基，避免第一条命令超时
            self._read_data()

    def _read_seg(self, start, n, tries=5):
        """读一段（n 颗芯片），失败后重试；返回寄存器列表或 None"""
        for attempt in range(tries):
            try:
                part = self.mb.exchange(write=cmd_data(start), count=n * REGS_PER_CHIP,
                                        timeout=1.5, retries=0)
                if len(part) >= n * REGS_PER_CHIP:
                    return part[:n * REGS_PER_CHIP]
            except Exception as e:
                if attempt == tries - 1:
                    self.log("warn", "CH%d-%d 读取失败：%s" % (start, start + n - 1, e))
            if attempt < tries - 1:
                self.mb.drain(0.06)
        return None

    def _read_data(self):
        """分段读 0x610。默认 2 颗一段（16 寄存器 / 37 字节应答）；
        某段失败再降级为 1 颗一段（8 寄存器 / 21 字节应答），
        短帧丢字节概率低很多，能把数据尽量捞回来。"""
        n_ch = CHANNELS * REGS_PER_CHIP
        regs = [0] * n_ch
        ok_list, fail_list = [], []
        for start in range(0, CHANNELS, 2):
            part = self._read_seg(start, 2)
            if part:
                regs[start * REGS_PER_CHIP:(start + 2) * REGS_PER_CHIP] = part
                ok_list.extend([start, start + 1])
                continue
            self.log("warn", "CH%d-%d 整段读不到，改为逐颗补读" % (start, start + 1))
            for k in (start, start + 1):
                one = self._read_seg(k, 1, tries=2)
                if one:
                    regs[k * REGS_PER_CHIP:(k + 1) * REGS_PER_CHIP] = one
                    ok_list.append(k)
                else:
                    regs[k * REGS_PER_CHIP] = 0x00F1      # err=0xF1 读取失败
                    fail_list.append(k)
        if fail_list:
            self.log("warn", "读取完成：%d/%d 颗有数据，失败通道 %s" % (
                len(ok_list), CHANNELS, ", ".join("CH%d" % k for k in fail_list)))
        else:
            self.log("ok", "读取完成：%d 颗全部读到（每段 %d 颗）" % (len(ok_list), 2))
        chs = decode_channels(regs, 0)
        self.post(self._show_channels, chs, regs)
        self._auto_diag(chs)

    def _auto_diag(self, chs):
        """读完数据后自动做两件事：
        1) 读一次 0x6A1，把链路质量打进日志（一眼看出线好不好）；
        2) 若所有已测通道都登录失败，自动逐通道做 OWI 探测，省得再点按钮。"""
        try:
            v = self.mb.exchange(write=cmd_quality(), count=8,
                                 timeout=1.5, retries=1, quiet=True)
            ok, bad, gap, rxb, ore = v[0], v[1], v[2], v[3], v[4]
            self.log("info", "链路质量：正确帧 %d / CRC错帧 %d / 收字节 %d / 溢出 %d / 帧内最大间隔 %d tick(≈%dms)"
                     % (ok, bad, rxb, ore, gap, int(gap * 1.75)))
            if ore:
                self.log("warn", "★ 有 %d 次串口溢出 → 从机 CPU 没及时取数据，是 MCU 侧丢字节" % ore)
            elif bad and gap >= 3:
                self.log("warn", "★ 无溢出但 CRC 错帧 %d 条、帧内停顿 %d ms → 线上字节不连续（转接器/线路）"
                         % (bad, int(gap * 1.75)))
            elif bad:
                self.log("warn", "★ CRC 错帧 %d 条但无溢出、无停顿 → 字节被改写，查共地/干扰/是否接错到 TTL 侧" % bad)
        except Exception:
            pass

        tested = [c for c in chs if c.get("err") != 0xFE]
        if tested and all(c.get("login") != 0x55 for c in tested):
            self.log("warn", "所有已测通道都登录失败 → 自动执行 OWI 探测定位原因（不用再点按钮）")
            self._probe_channels([c["ch"] for c in tested])

    def _show_channels(self, chs, regs):
        self.channels = chs
        self.tree.delete(*self.tree.get_children())
        for i, c in enumerate(chs):
            tags = ["zebra"] if i % 2 == 0 else []
            tags.append(c["status"])
            self.tree.insert("", "end", iid=str(c["ch"]), tags=tuple(tags), values=(
                c["ch"], c["status_text"], "0x%02X" % c["login"], "0x%02X" % c["err"],
                str(c["gd"]), str(c["zd"]), "0x%02X" % c["pga"], "0x%02X" % c["adc"],
                "0x%02X" % c["pctrl"], "0x%02X" % c["sys"], "0x%02X" % c["lut"],
                str(c["tcode"]), "—" if c["status"] == "skip" else "%.2f" % c["temp"],
                "是" if c["fuse"] else "—"))
        ok = sum(1 for c in chs if c["status"] == "ok")
        err = sum(1 for c in chs if c["status"] == "err")
        skip = sum(1 for c in chs if c["status"] == "skip")
        self.lbl_summary.configure(text="%d 通道 · 正常 %d / 异常 %d / 未测 %d" % (len(chs), ok, err, skip))
        now = datetime.datetime.now().strftime("%H:%M:%S")
        self.var_info.set("已读取 %d 通道 · %s · %d 个寄存器" % (len(chs), now, len(regs)))
        self.log("ok", "读取 0x610 完成：%d 通道 / %d 寄存器" % (len(chs), len(regs)))
        if chs:
            self.tree.selection_set(str(chs[0]["ch"]))
            self.tree.focus(str(chs[0]["ch"]))
            self._show_detail(chs[0])

    def _set_progress(self, val, text):
        self.prog["value"] = val
        self.var_prog.set(text)
        self.var_prognum.set("%d/%d" % (val, CHANNELS))

    def on_send_task(self):
        if self.busy or not self._require_port():
            return
        task = self._selected_task()
        mask = self.get_mask()
        self.post(self._set_busy, True)
        self._bg(lambda: (self.mb.exchange(write=cmd_task(task, mask)),
                          self.log("ok", "已下发 T%d，掩码 0x%02X（%s）" % (task, mask, mask_text(mask)))),
                 label="下发任务")

    def on_query(self):
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            st = self.mb.exchange(write=cmd_query(), count=7, timeout=0.4, retries=0)
            self.log("ok", "busy=%d order=%d prog=%d/%d paras=%s" %
                     (st[0], st[1], st[2], CHANNELS, ",".join(str(x) for x in st[3:7])))
            self.post(self._set_progress, st[2], "任务进行中" if st[0] else "空闲")

        self._bg(job)

    def on_read_data(self):
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)
        self._bg(self._read_data, label="读取数据")

    def op_version(self):
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            vals = self.mb.exchange(write=cmd_version(), count=16, timeout=0.6)
            s = "".join(chr(v) for v in vals if 32 <= v < 127).strip()
            self.post(self.var_version.set, s or "(空)")
            self.log("ok", "固件版本：%s" % (s or "(空)"))

        self._bg(job)

    def op_vdd(self, on):
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            vals = self.mb.exchange(write=cmd_vdd(on), count=1)
            self.log("ok" if vals and vals[0] else "warn",
                     "总电源 %s → 回读 %s" % ("开" if on else "关", vals[0] if vals else "—"))

        self._bg(job)

    def on_write_config(self):
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)
        cmd = cmd_config(self.cfg_from_ui())

        def job():
            vals = self.mb.exchange(write=cmd, count=1)
            self.log("ok", "配置已写入（valid=%d ext_en=%d）→ 回读 %s" %
                     (cmd[10] & 1, (cmd[10] >> 1) & 1, vals[0] if vals else "—"))

        self._bg(job)

    # ------------------------------------------------------------------ 详情
    def _show_placeholder(self):
        """表格空态：一行灰色提示，避免看起来像没加载"""
        self.tree.delete(*self.tree.get_children())
        self.tree.insert("", "end", iid="hint", tags=("skip",), values=(
            "—", "尚未读取数据", "", "", "", "", "", "", "", "", "", "", "", ""))

    def _on_select_row(self, event=None):
        sel = self.tree.selection()
        if not sel or sel[0] == "hint":
            return
        try:
            ch = int(sel[0])
        except ValueError:
            return
        for c in self.channels:
            if c["ch"] == ch:
                self._show_detail(c)
                break

    def _show_detail(self, c):
        dp, da, dc = decode_pga(c["pga"]), decode_adc(c["adc"]), decode_pctrl(c["pctrl"])
        self.lbl_detail.configure(text="CH%d · %s" % (c["ch"], c["status_text"]))
        t = self.detail
        t.configure(state="normal")
        t.delete("1.0", "end")

        def put(tag, text):
            t.insert("end", text + "\n", tag)

        put("h", "CH%d · %s" % (c["ch"], c["status_text"]))
        put("n", "登录 0x%02X %s   错误 0x%02X %s   温度 %.2f ℃（code %d）" %
            (c["login"], "ACK" if c["login_ok"] else "NO ACK", c["err"], c["err_text"],
             c["temp"], c["tcode"]))
        put("n", "增益 DAC %d (0x%04X)   零点 DAC %d (0x%04X)   OTP %s" %
            (c["gd"], c["gd"], c["zd"], c["zd"], "已烧写" if c["fuse"] else "未执行"))
        put("n", "PGA_GAIN 0x%02X → G1 ×%g / G3 ×%g / G2 ×%g（总 ×%g）" %
            (c["pga"], dp["g1"], dp["g3"], dp["g2"], dp["total"]))
        put("n", "ADC_CTRL 0x%02X → OSR %d / 平均 %d 次 / SOT %d" %
            (c["adc"], da["osr"], da["avg"], 1 if da["sot"] else 0))
        put("n", "PGA_CTRL 0x%02X → VREF %s / 基准 ×%g / VCM %s / SPIN %s / 斩波 %d-%d" %
            (c["pctrl"], dc["vref"], dc["ref"], dc["vcm"], dc["spin"],
             1 if dc["chop1"] else 0, 1 if dc["chop2"] else 0))
        on = bits_on(c["sys"], SYS_BITS)
        put("n", "SYS_CTRL 0x%02X → %s；置位：%s" % (c["sys"], sys_text(c["sys"]), "、".join(on) or "无"))
        on = bits_on(c["lut"], LUT_BITS)
        put("n", "LUT_SEL 0x%02X → %s；置位：%s" % (c["lut"], lut_text(c["lut"]), "、".join(on) or "无"))
        t.configure(state="disabled")

    # ------------------------------------------------------------------ 手动
    def on_manual_send(self):
        if self.busy or not self._require_port():
            return
        try:
            func_label = self.var_man_func.get()
            addr = int(self.var_man_addr.get().strip() or "0")
            if func_label.startswith("10"):
                values = [int(x, 0) for x in self.var_man_values.get().replace("，", ",").split(",") if x.strip()]
                if not values:
                    raise ValueError("写入数据为空")
                frame = build_write_multi(SLAVE, addr, values)
                func = 0x10
            else:
                count = int(self.var_man_count.get().strip() or "1")
                func = 3 if func_label.startswith("03") else 4
                frame = build_read(SLAVE, func, addr, count)
        except Exception as e:
            self.log("err", "参数错误：%s" % e)
            return

        self.post(self._set_busy, True)

        def job():
            with self.mb.lock:
                vals = self.mb._txn(frame, func)
            if func in (3, 4):
                self.log("ok", "FC%d 读 %d 个：%s" % (func, len(vals), " ".join("0x%04X" % v for v in vals)))
            else:
                self.log("ok", "写入确认")

        self._bg(job)

    def on_raw_send(self):
        if self.busy or not self._require_port():
            return
        try:
            data = parse_hex(self.var_man_raw.get())
            body, tail = data[:-2], data[-2] | (data[-1] << 8)
            if crc16(body) != tail:
                data = build_frame(data[0], list(data[1:]))
                self.log("info", "已自动重算 CRC")
        except Exception as e:
            self.log("err", "十六进制解析失败：%s" % e)
            return

        self.post(self._set_busy, True)
        self._bg(lambda: self.mb.send_raw(data, 0.6))

    def on_loopback(self):
        """把适配器 TX 与 RX 短接后点这里：收到相同字节说明串口/适配器没问题"""
        if self.busy or not self._require_port():
            return
        payload = build_read(SLAVE, 4, self.mb.read_addr, 6)
        self.post(self._set_busy, True)

        def job():
            with self.mb.lock:
                try:
                    self.mb.ser.reset_input_buffer()
                except Exception:
                    pass
                self.log("tx", fmt_hex(payload))
                self.mb.ser.write(payload)
                self.mb.ser.flush()
                time.sleep(0.35)
                try:
                    n = self.mb.ser.in_waiting
                except Exception:
                    n = 0
                data = self.mb.ser.read(n) if n else b""
            if not data:
                self.log("err", "回环无回传 → 适配器 TX/RX 没短接，或串口选错、适配器坏")
            elif data == payload:
                self.log("ok", "回环通过（%s）：串口、适配器、波特率都正常，问题在往下的接线或从机" % fmt_hex(data))
            else:
                self.log("warn", "回环收到不同数据（%d 字节）：%s" % (len(data), fmt_hex(data)))

        self._bg(job)

    def on_link_test(self):
        """连发 20 条短读命令，统计成功率和失败类型——用来判断是"线不行"还是"命令不行"。"""
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            total = 20
            ok = 0
            kinds = {}
            self.log("info", "链路自检：连发 %d 条 8 字节读命令（FC04 读 8 个寄存器），不改变设备状态" % total)
            for i in range(total):
                try:
                    self.mb._txn(build_read(SLAVE, 4, self.mb.read_addr, 8), 4,
                                 timeout=0.7, retries=0, quiet=True)
                    ok += 1
                except Exception as e:
                    m = str(e)
                    if "半包" in m:
                        k = "半包（收到字节不全）"
                    elif "功能码" in m:
                        k = "收到的帧头被打乱"
                    elif "无应答" in m:
                        k = "完全无应答"
                    elif "异常码" in m:
                        k = "从机回异常码（通信本身是通的）"
                    else:
                        k = m[:24]
                    kinds[k] = kinds.get(k, 0) + 1
                time.sleep(0.05)
            rate = ok * 100.0 / total
            self.log("ok" if rate >= 95 else "warn",
                     "链路自检结果：%d/%d 成功（%.0f%%）" % (ok, total, rate))
            for k, v in sorted(kinds.items(), key=lambda x: -x[1]):
                self.log("warn", "    失败 %2d 次：%s" % (v, k))
            if rate >= 95:
                self.log("ok", "链路质量好，通信不是瓶颈")
            elif rate >= 70:
                self.log("warn", "链路一般：偶发丢字节。若固件「通信质量」里溢出=0，就是线/转接器的问题")
            else:
                self.log("err", "链路很差（<70%）：先查 RS485 接线 —— 本板通信是 RS485（U12 TD301M485），"
                                "A/B 必须接 A/B，且要和转接器共地；不要接到 PB4/PB5 的 TTL 侧")

        self._bg(job, label="链路自检")

    def on_mb_quality(self):
        """0x6A1：读从机的通信质量计数，判断命令为什么没应答"""
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            try:
                v = self.mb.exchange(write=cmd_quality(), count=8,
                                     timeout=1.5, retries=1)
            except Exception as e:
                self.log("err", "读通信质量失败：%s" % e)
                return
            ok, bad, gap, rxb, ore = v[0], v[1], v[2], v[3], v[4]
            self.log("info", "通信质量：正确帧 %d / CRC错帧 %d / 收字节 %d / 溢出 %d / 帧内最大间隔 %d tick"
                     % (ok, bad, rxb, ore, gap))
            self.log("info", "最大字节间隔 ≈ %d ms（1 tick = 1.75ms，>1 说明线上有停顿）" % int(gap * 1.75))
            if ore:
                self.log("warn", "有 %d 次串口溢出 → 从机 CPU 取数据不及时，硬件丢字节（与上位机无关）" % ore)
            elif bad > 0 and gap >= 3:
                self.log("warn", "无溢出但 CRC 错帧 %d 条、帧内停顿达 %d ms → USB 转串口把一帧拆成多个包"
                                 % (bad, int(gap * 1.75)))
            elif bad == 0:
                self.log("ok", "没有 CRC 错帧，通信链路干净")
            else:
                self.log("warn", "CRC 错帧 %d 条但无溢出、无停顿 → 字节被改写，查线长/终端电阻/干扰" % bad)

        self._bg(job, label="通信质量")

    def on_owi_probe(self):
        """0x6A0：逐通道断电100ms→上电20ms→发登录，回读原始相位。"""
        if self.busy or not self._require_port():
            return
        mask = self.get_mask()
        if mask == 0:
            self.log("warn", "通道掩码是 0x00，没有可探测的通道")
            return
        chs = [c for c in range(CHANNELS) if (mask >> c) & 1]
        self._bg(lambda: self._probe_channels(chs), label="OWI 探测")

    def _probe_channels(self, chs):
        """在工作线程里逐通道探测（不自己管 busy 状态）"""
        self.log("info", "OWI 探测开始（通道 %s）：每颗断电 100ms → 上电 20ms → 发 F3 A0"
                 % ", ".join("CH%d" % c for c in chs))
        bad = 0
        fail_cmd = 0
        for ch in chs:
            try:
                v = self.mb.exchange(write=cmd_probe(ch), count=8,
                                     timeout=1.5, retries=2)
            except Exception as e:
                bad += 1
                fail_cmd += 1
                self.log("err", "CH%d 探测命令失败：%s" % (ch, e))
                continue
            login = (v[0] >> 8) & 0xFF
            phases = v[0] & 0xFF
            vdd = (v[1] >> 8) & 0xFF
            idle = v[1] & 0xFF
            lo0, hi0 = v[2], v[3]
            lor = "VDD脚=%d 总线=%d" % (vdd, idle)
            if login == 0x55:
                self.log("ok", "CH%d 登录成功 0x55（相位 %d，%s）" % (ch, phases, lor))
            elif phases == 0:
                bad += 1
                if vdd == 0:
                    self.log("err", "CH%d 无应答且 VDD 脚是低（%s）→ 该列电源没打开：查 0x201 总电源 / U9 / 列开关"
                             % (ch, lor))
                elif idle == 0:
                    self.log("err", "CH%d 无应答、VDD 有电但 OWI 线被拉低（%s）→ 线被拉死：查短路或坏片"
                             % (ch, lor))
                else:
                    self.log("err", "CH%d 无应答、VDD 有电、总线空闲高（%s）→ 芯片没反应：查芯片是否插好/方向、OWI 线是否通"
                             % (ch, lor))
            else:
                bad += 1
                self.log("warn", "CH%d 有 %d 个相位但登录失败（%s），首相位 低%d/高%d us"
                                 " → 芯片应答了但解码不过：查上拉电阻 / 位时序" % (ch, phases, lor, lo0, hi0))
            time.sleep(0.05)
        if bad == 0:
            self.log("ok", "探测结束：所选通道全部登录成功")
        elif fail_cmd == len(chs):
            self.log("err", "★ 探测命令全部无应答 —— 从机可能已卡死（v1.6 的 0x6A0 有死锁 bug）。"
                            "请给板子断电重启，并烧 v1.7 后再试")
        else:
            self.log("warn", "探测结束：%d 个通道未通过" % bad)

    def on_tx_scan(self):
        """PC→从机方向：发 13~39 字节的 FC10 命令（内容都是"查状态"，无副作用），
        看从机最长能完整收到多少字节。用来定位"命令被截断"的阈值。"""
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            self.log("info", "写长度扫描：FC10 写 2~15 个寄存器（帧长 13~39 字节），"
                             "内容都是查状态 0x66，不改变设备状态")
            ok_max, fail_min = 0, 0
            for n in (2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15):
                regs = [0x0005, 0x0066] + [0x0000] * (n - 2)
                flen = 9 + 2 * n
                try:
                    self.mb.exchange(write=regs, count=0, timeout=0.6,
                                     retries=0, quiet=True)
                    ok_max = flen
                    self.log("ok", "帧长 %2d 字节（%2d 寄存器）→ 从机收到并回应" % (flen, n))
                except Exception as e:
                    if not fail_min:
                        fail_min = flen
                    self.log("err", "帧长 %2d 字节（%2d 寄存器）→ %s" % (flen, n, e))
                time.sleep(0.05)
            if fail_min and ok_max and fail_min > ok_max:
                self.log("warn", "结论：%d 字节以内能完整收到，%d 字节开始被截断"
                                 % (ok_max, fail_min))
                self.log("warn", "这是典型的「USB 转串口把一帧拆成多个包 + 从机判帧太短」："
                                 "把固件 MB_T35_TICKS 放大即可解决")
            elif ok_max:
                self.log("ok", "结论：最长 %d 字节的命令都能被完整收到，PC→从机方向正常" % ok_max)

        self._bg(job)

    def on_capture(self):
        """发一次整段读，记录每个到达块的时刻，判断从机是"发得慢"还是"帧被改坏" """
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            req = build_read(SLAVE, 4, self.mb.read_addr, CHANNELS * REGS_PER_CHIP)
            with self.mb.lock:
                try:
                    self.mb.ser.reset_input_buffer()
                except Exception:
                    pass
                self.log("tx", fmt_hex(req))
                self.mb.ser.write(req)
                self.mb.ser.flush()
                t0 = time.monotonic()
                chunks = []
                total = 0
                while (time.monotonic() - t0) < 3.0:
                    try:
                        self.mb.ser.timeout = 0.05
                        data = self.mb.ser.read(256)
                    except Exception:
                        break
                    if data:
                        chunks.append(((time.monotonic() - t0) * 1000.0, len(data)))
                        total += len(data)
                        if total >= 133:
                            break
                dt = (time.monotonic() - t0) * 1000.0
            self.log("info", "抓帧结果：%.0f ms 内共 %d 字节（期望 133）" % (dt, total))
            for t, n in chunks[:40]:
                self.log("info", "    +%7.1f ms  %3d 字节" % (t, n))
            if total >= 133:
                self.log("ok" if dt < 200 else "warn",
                         "收满 133 字节，用时 %.0f ms（115200 下正常约 12 ms）" % dt)
            else:
                self.log("warn", "3 秒内没收满，从机发送被卡住了")

        self._bg(job)

    def on_full_read(self):
        """只发一次整段读（64 个寄存器），用于复现长帧问题"""
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            try:
                regs = self.mb.exchange(write=cmd_data(0), count=CHANNELS * REGS_PER_CHIP,
                                        timeout=1.5, retries=0)
                self.log("ok", "整段读取成功：%d 个寄存器" % len(regs))
                self.post(self._show_channels, decode_channels(regs, 0), regs)
            except Exception as e:
                self.log("err", "整段读取失败：%s" % e)

        self._bg(job)

    def on_scan(self):
        """逐个长度读 30001，找出从机能正常回包的最大寄存器数"""
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            self.log("info", "开始扫描：读 30001 起 8/16/24/32/40/48/56/64/80/96/120 个寄存器")
            last_ok = 0
            for cnt in (8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 120):
                try:
                    self.mb.exchange(write=cmd_data(0), count=cnt, timeout=0.8,
                                     retries=0, quiet=True)
                    last_ok = cnt
                    self.log("ok", "读 %3d 个寄存器 → 成功（回包 %d 字节）" % (cnt, 5 + cnt * 2))
                except Exception as e:
                    self.log("err", "读 %3d 个寄存器 → %s" % (cnt, e))
                    break
            self.log("info", "扫描结束：最大可用 %d 个寄存器（%d 颗芯片）" % (last_ok, last_ok // 8))

        self._bg(job)

    def on_listen(self):
        """被动监听 3 秒：看线上有没有任何字节"""
        if self.busy or not self._require_port():
            return
        self.post(self._set_busy, True)

        def job():
            buf = bytearray()
            with self.mb.lock:
                try:
                    self.mb.ser.reset_input_buffer()
                except Exception:
                    pass
                t0 = time.time()
                while time.time() - t0 < 3:
                    try:
                        n = self.mb.ser.in_waiting
                    except Exception:
                        break
                    if n:
                        buf.extend(self.mb.ser.read(n))
                    else:
                        time.sleep(0.02)
            if buf:
                self.log("rx", fmt_hex(bytes(buf)))
                self.log("ok", "3 秒内收到 %d 字节" % len(buf))
            else:
                self.log("warn", "3 秒内没有任何字节（从机被点名才回包，静止属正常；上电瞬间也没有就要查接线）")

        self._bg(job)

    # ------------------------------------------------------------------ 导出 / 演示
    def on_export_csv(self):
        if not self.channels:
            messagebox.showinfo("提示", "还没有数据，请先读取 0x610")
            return
        name = "saf8100_%s.csv" % datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        path = filedialog.asksaveasfilename(defaultextension=".csv", initialfile=name,
                                            filetypes=[("CSV 文件", "*.csv")])
        if not path:
            return
        head = ["通道", "状态", "登录", "错误码", "错误说明", "增益DAC", "零点DAC",
                "PGA_GAIN", "ADC_CTRL", "PGA_CTRL", "SYS_CTRL", "LUT_SEL", "温度码", "温度(℃)", "烧写"]
        try:
            with open(path, "w", newline="", encoding="utf-8-sig") as f:
                w = csv.writer(f)
                w.writerow(head)
                for c in self.channels:
                    w.writerow([c["ch"], "正常" if c["status"] == "ok" else
                                ("未测试" if c["status"] == "skip" else "异常"),
                                "0x%02X" % c["login"], "0x%02X" % c["err"], c["err_text"],
                                c["gd"], c["zd"], "0x%02X" % c["pga"], "0x%02X" % c["adc"],
                                "0x%02X" % c["pctrl"], "0x%02X" % c["sys"], "0x%02X" % c["lut"],
                                c["tcode"], "" if c["status"] == "skip" else "%.2f" % c["temp"],
                                "是" if c["fuse"] else ""])
            self.log("ok", "已导出：%s" % path)
        except Exception as e:
            messagebox.showerror("导出失败", str(e))

    def on_demo(self):
        regs = []
        for ch in range(CHANNELS):
            skip = (ch == 7)
            err = 0x02 if ch == 5 else 0x00
            code = 0 if skip else 36080 + ch * 9
            tx100 = 0 if skip else int(round(temp_from_code(code) * 100))
            regs += [((0x55 if (err == 0 and not skip) else 0x00) << 8) | (0xFE if skip else err),
                     0 if skip else 10000 + ch * 3,
                     0 if skip else 16383,
                     (0x52 << 8) | 0x15,
                     (0x43 << 8) | 0x00,
                     (0x00 << 8) | 0x00,
                     code,
                     tx100 & 0xFFFF]
        chs = decode_channels(regs, 0)
        self._show_channels(chs, regs)
        ok = sum(1 for c in chs if c["status"] == "ok")
        err = sum(1 for c in chs if c["status"] == "err")
        skip = sum(1 for c in chs if c["status"] == "skip")
        self.lbl_summary.configure(text="%d 通道 · 正常 %d / 异常 %d / 未测 %d · 演示数据"
                                        % (len(chs), ok, err, skip))
        self.var_info.set("演示数据（非真实测量）")
        self.log("warn", "已填充演示数据，仅用于预览界面，未与任何硬件通信")

    # ------------------------------------------------------------------ 设置
    def _cfg_path(self):
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
        d = os.path.join(base, "Saf8100Tool")
        try:
            if not os.path.isdir(d):
                os.makedirs(d)
        except Exception:
            return None
        return os.path.join(d, "settings.json")

    def _save_settings(self):
        p = self._cfg_path()
        if not p:
            return
        try:
            data = {
                "baud": self.var_baud.get(),
                "task": self.var_task.get(),
                "mask": self.var_mask.get(),
                "addr_mode": self.var_addr_mode.get(),
                "cfg": {},
            }
            for key, var in (("gd", self.var_gd), ("zd", self.var_zd), ("g1", self.var_g1),
                             ("g3", self.var_g3), ("g2", self.var_g2), ("vref", self.var_vref),
                             ("ref", self.var_ref), ("vcm", self.var_vcm), ("osr", self.var_osr),
                             ("avg", self.var_avg), ("sys", self.var_sys), ("lut", self.var_lut)):
                data["cfg"][key] = var.get()
            data["cfg"]["valid"] = self.var_valid.get()
            data["cfg"]["ext"] = self.var_ext.get()
            with open(p, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=1)
        except Exception:
            pass

    def _load_settings(self):
        p = self._cfg_path()
        if not p or not os.path.isfile(p):
            return
        try:
            with open(p, "r", encoding="utf-8") as f:
                d = json.load(f)
            if d.get("baud"):
                self.var_baud.set(d["baud"])
            if d.get("task"):
                self.var_task.set(d["task"])
            if d.get("mask"):
                self._set_mask(int(str(d["mask"]).replace("0x", ""), 16))
            if d.get("addr_mode"):
                self.var_addr_mode.set(d["addr_mode"])
            cfg = d.get("cfg") or {}
            for key, var in (("gd", self.var_gd), ("zd", self.var_zd), ("g1", self.var_g1),
                             ("g3", self.var_g3), ("g2", self.var_g2), ("vref", self.var_vref),
                             ("ref", self.var_ref), ("vcm", self.var_vcm), ("osr", self.var_osr),
                             ("avg", self.var_avg), ("sys", self.var_sys), ("lut", self.var_lut)):
                if cfg.get(key) is not None:
                    var.set(cfg[key])
            if cfg.get("valid") is not None:
                self.var_valid.set(bool(cfg["valid"]))
            if cfg.get("ext") is not None:
                self.var_ext.set(bool(cfg["ext"]))
        except Exception:
            pass

    def destroy(self):
        try:
            self._save_settings()
        except Exception:
            pass
        tk.Tk.destroy(self)


def main():
    app = App()
    app.mainloop()


if __name__ == "__main__":
    main()
