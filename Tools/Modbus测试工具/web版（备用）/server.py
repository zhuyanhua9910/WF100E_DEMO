#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
本地静态服务（仅回环地址）。

Web Serial API 只在"安全上下文"中可用：https:// 或 http://127.0.0.1 / localhost。
直接双击 index.html（file://）浏览器会禁用串口，所以用这个小服务把目录挂到
http://127.0.0.1:8765 上，再自动用 Edge / Chrome 打开。

用法：双击「启动工具.bat」，或在本目录执行 python server.py
停止：在本窗口按 Ctrl+C
"""

import os
import socket
import sys
import threading
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.abspath(__file__))
PORT_START = 8765
PORT_TRIES = 10
PAGE = "index.html"


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        # 工具是本地使用，避免浏览器缓存旧版本
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass  # 静默访问日志，窗口只保留必要信息


def pick_server():
    for port in range(PORT_START, PORT_START + PORT_TRIES):
        try:
            return ThreadingHTTPServer(("127.0.0.1", port), Handler), port
        except OSError:
            continue
    return None, 0


def open_browser(url):
    """优先用 Edge / Chrome，其次是系统默认浏览器（Web Serial 需要 Chromium 内核）"""
    candidates = [
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                import subprocess

                subprocess.Popen([path, url])
                return
            except Exception:
                break
    try:
        webbrowser.open(url)
    except Exception:
        pass


def main():
    httpd, port = pick_server()
    if httpd is None:
        print(f"[ERROR] 端口 {PORT_START}-{PORT_START + PORT_TRIES - 1} 都被占用，请关闭占用程序后重试。")
        return 1

    url = f"http://127.0.0.1:{port}/{PAGE}"
    print("=" * 62)
    print("  SAF8100 Modbus 测试工具")
    print("=" * 62)
    print(f"  地址：{url}")
    print("  提示：浏览器弹出的串口选择框里选接治具的 COM 口")
    print("        使用期间请不要关闭本窗口（关闭即停止服务）")
    print("-" * 62)
    print("  按 Ctrl+C 退出")
    print("=" * 62)

    threading.Timer(0.8, lambda: open_browser(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止。")
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
