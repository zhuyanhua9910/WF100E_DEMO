#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
重新打包 exe（改了 saf8100_tool.py 之后运行本脚本）：

    python build_exe.py

依赖：Python 3.9+（需带 tkinter）、pyserial、pyinstaller
    pip install pyserial pyinstaller
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "SAF8100 Modbus测试工具"


def main():
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--noconfirm", "--clean", "--onefile", "--noconsole",
        "--name", NAME,
        "--icon", os.path.join(HERE, "icon.ico"),
        "--exclude-module", "PIL",
        "--exclude-module", "numpy",
        os.path.join(HERE, "saf8100_tool.py"),
    ]
    print("执行：", " ".join(cmd))
    subprocess.run(cmd, cwd=HERE, check=True)

    src = os.path.join(HERE, "dist", NAME + ".exe")
    dst = os.path.join(HERE, NAME + ".exe")
    try:
        shutil.copy2(src, dst)
    except PermissionError:
        # exe 正在运行：改名挪开再放新的，提示用户重开
        old = dst + ".old.exe"
        try:
            if os.path.exists(old):
                os.remove(old)
        except Exception:
            pass
        os.replace(dst, old)
        shutil.copy2(src, dst)
        print("旧版仍在运行，已改名为 %s，请关闭工具窗口后删掉它" % os.path.basename(old))
    print("\n已生成：%s（%.1f MB）" % (dst, os.path.getsize(dst) / 1024.0 / 1024.0))

    for d in ("build", "dist", "__pycache__"):
        p = os.path.join(HERE, d)
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)
    for f in os.listdir(HERE):
        if f.endswith(".spec"):
            try:
                os.remove(os.path.join(HERE, f))
            except OSError:
                pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
