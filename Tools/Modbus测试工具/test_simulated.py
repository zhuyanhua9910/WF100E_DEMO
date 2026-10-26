# -*- coding: utf-8 -*-
"""临时脚本：用虚拟从机回放整条链路（下发任务 → 轮询 → 读数据 → 渲染）"""
import sys
import time

sys.path.insert(0, '.')
import saf8100_tool as T


class FakeSer(object):
    """模拟固件行为的假串口"""

    def __init__(self):
        self.is_open = True
        self.timeout = 0.05
        self.buf = bytearray()
        self.order = 0
        self.busy = 0
        self.prog = 0
        self.task_start = 0.0
        self.task_len = 1.2
        self.mask = 0xFF
        self.max_regs = 16      # 超过这个寄存器数就不应答，模拟"长帧发不出来"

    # --- pyserial 接口 ---
    def reset_input_buffer(self):
        self.buf.clear()

    def reset_output_buffer(self):
        pass

    def flush(self):
        pass

    def close(self):
        self.is_open = False

    @property
    def in_waiting(self):
        return len(self.buf)

    def read(self, n):
        if self.buf:
            out = bytes(self.buf[:n])
            del self.buf[:n]
            return out
        time.sleep(min(self.timeout, 0.01))
        return b""

    def write(self, data):
        self._handle(bytes(data))
        return len(data)

    # --- 模拟固件 ---
    def _q(self, frame):
        self.buf.extend(frame)

    def _handle(self, d):
        if len(d) < 8 or d[0] != T.SLAVE:
            return
        func = d[1]
        addr = (d[2] << 8) | d[3]
        cnt = (d[4] << 8) | d[5]
        if func == 0x10:
            if addr != 40000 or (d[6] & 0xFF) != cnt * 2 or cnt < 2:
                self._q(T.build_frame(d[0], [0x90, 0x02]))
                return
            self._q(T.build_frame(d[0], [0x10, d[2], d[3], d[4], d[5]]))
            regs = [(d[7 + i * 2] << 8) | d[8 + i * 2] for i in range(cnt)]
            self._on_write(regs)
        elif func == 0x04:
            if cnt > self.max_regs:      # 模拟：长应答被截断/发不出
                return
            if addr != 30000:
                self._q(T.build_frame(d[0], [0x84, 0x02]))
                return
            vals = self._read_regs(cnt)
            pdu = [0x04, len(vals) * 2]
            for v in vals:
                pdu += [(v >> 8) & 0xFF, v & 0xFF]
            self._q(T.build_frame(d[0], pdu))

    def _on_write(self, regs):
        head = regs[1]
        self.last_head = head
        if head == 0x77:
            self.order = regs[2]
            self.mask = regs[3]
            self.busy = 1
            self.prog = 0
            self.task_start = time.time()
        elif head == 0x201:
            self.vdd = regs[2]
        elif head == 0x610:
            self.start_ch = regs[2]
        elif head == 0x780:
            self.cfg = regs[3:11]
        if self.busy and time.time() - self.task_start > self.task_len:
            self.busy = 0
            self.prog = 8
            self.order = 0

    def _tick(self):
        if self.busy:
            el = time.time() - self.task_start
            self.prog = min(8, int(el / self.task_len * 8))
            if el > self.task_len:
                self.busy = 0
                self.order = 0
                self.prog = 8

    def _read_regs(self, cnt):
        self._tick()
        # 版本
        if getattr(self, "last_head", None) == 0x104:
            s = "8100 v1.0"
            return [ord(c) for c in s] + [0] * (cnt - len(s))
        if getattr(self, "last_head", None) == 0x201:
            return [1]
        if getattr(self, "last_head", None) == 0x66:
            return [self.busy, self.order, self.prog, 0, 0, 0, 0]
        if getattr(self, "last_head", None) == 0x6A1:
            return [12, 3, 4, 500, 0, 0, 0, 0]
        if getattr(self, "last_head", None) == 0x6A0:
            # 探测：默认模拟"有电、芯片应答"（登录成功 0x55 / 9 个相位）
            return [0x5509, 0x0101, 16, 15, 8, 23, 8, 23]
        if getattr(self, "last_head", None) == 0x610:
            regs = []
            for ch in range(8):
                skip = (ch == 7)
                err = 0x02 if ch == 5 else 0x00
                code = 0 if skip else 36080 + ch * 9
                x100 = 0 if skip else int(round(T.temp_from_code(code) * 100))
                regs += [((0x55 if (err == 0 and not skip) else 0) << 8) | (0xFE if skip else err),
                         0 if skip else 10000 + ch * 3,
                         0 if skip else 16383,
                         (0x52 << 8) | 0x15,
                         (0x43 << 8) | 0x00,
                         0,
                         code,
                         x100 & 0xFFFF]
            st = getattr(self, "start_ch", 0)
            return regs[st * 8: st * 8 + cnt]
        return [0] * cnt


def main():
    app = T.App()
    app.geometry("1320x830+20+20")
    app.update()

    fake = FakeSer()
    app.mb.ser = fake
    app.connected = True

    # 记录写命令，供 _read_regs 判断上下文
    orig_handle = fake._on_write

    def on_write(regs):
        fake.last_head = regs[1]
        orig_handle(regs)

    fake._on_write = on_write

    print("任务号选择:", app._selected_task(), "命令块:", T.cmd_task(app._selected_task(), 0xFF))

    def pump(sec):
        t0 = time.time()
        while time.time() - t0 < sec:
            app.update()
            time.sleep(0.02)

    def step(name, fn, sec=3.0):
        before = app.log_text.get("1.0", "end").count("ERR")
        fn()
        pump(sec)
        after = app.log_text.get("1.0", "end").count("ERR")
        print("  %-10s 新增错误 %d" % (name, after - before))

    print("[1] 一键测试")
    app.on_run_task()
    pump(4.0)

    print("[2] 其它按钮")
    step("读版本", app.op_version, 2)
    step("总电源", lambda: app.op_vdd(True), 2)
    step("查询状态", app.on_query, 2)
    step("读取数据", app.on_read_data, 3)
    step("下发配置", app.on_write_config, 2)
    step("手动FC10", lambda: (app.var_man_func.set("10 写多寄存器"),
                            app.var_man_addr.set("40000"),
                            app.var_man_values.set("5, 0x66"), app.on_manual_send()), 2)
    step("原始帧", lambda: (app.var_man_raw.set("01 04 75 30 00 01"), app.on_raw_send()), 2)

    errs = [l for l in app.log_text.get("1.0", "end").splitlines() if " ERR " in l]
    print("通道数:", len(app.channels))
    print("汇总:", app.lbl_summary.cget("text"))
    print("错误行:", errs)
    if app.channels:
        c = app.channels[0]
        print("CH0:", c["status"], c["login"], c["gd"], c["zd"], c["tcode"], round(c["temp"], 2))
    print("详情首行:", app.detail.get("1.0", "2.0").strip())
    app.destroy()


main()
