# -*- coding: utf-8 -*-
"""回归脚本：注入"应答中途丢字节"故障，验证工具能否靠重试/降级把数据捞回来

用法：python test_byte_loss.py
不需要硬件，直接跑。退出码 0 表示 8 通道数据完整。
"""
import os
import sys
import time

sys.path.insert(0, '.')
import test_simulated as SIM
import saf8100_tool as T


class LossSer(SIM.FakeSer):
    """在应答里随机丢掉中间若干字节，模拟实测到的"帧中间丢字节" """

    def __init__(self):
        SIM.FakeSer.__init__(self)
        self.loss_left = 3          # 前 6 条应答都坏掉

    def _q(self, frame):
        if len(frame) == 37 and self.loss_left > 0:
            self.loss_left -= 1
            # 抠掉数据区中间 7 个字节，帧尾 CRC 保留（和实测现象一致）
            bad = frame[:10] + frame[17:]
            self.buf.extend(bad)
            return
        self.buf.extend(frame)


def main():
    SIM.FakeSer_orig = SIM.FakeSer
    ser = LossSer()
    app = T.App()
    app.geometry("1320x830+20+20")
    app.update()
    app.mb.ser = ser
    app.connected = True

    def pump(sec):
        t0 = time.time()
        while time.time() - t0 < sec:
            app.update()
            time.sleep(0.02)

    app.var_mask.set("0xFF")
    print("[1] 一键测试（CH0-1 的 3 次应答被注入丢字节）")
    app.on_run_task()
    pump(25.0)

    errs = [l for l in app.log_text.get("1.0", "end").splitlines() if " ERR " in l]
    warns = [l for l in app.log_text.get("1.0", "end").splitlines() if " ! " in l]
    print("通道数:", len(app.channels))
    print("汇总:", app.lbl_summary.cget("text"))
    print("ERR 行:", errs)
    print("WARN 行:")
    for w in warns:
        print("   ", w.strip())
    ok = len(app.channels) == 8 and not errs
    print("\n结论:", "通过 —— 丢字节被重试/降级吸收，8 通道数据完整" if ok
          else "失败 —— 丢字节导致数据缺失")
    app.destroy()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
