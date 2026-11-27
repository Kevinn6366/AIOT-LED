"""Local, read-only COM3 and Ethernet boot status page."""

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.request import ProxyHandler, build_opener

import serial


BOARD = 'http://169.254.155.221:8080'
WEB = Path(__file__).with_name('boot.html')
LOCK = threading.Lock()
state = {'port': '等待 COM3', 'event': '尚未观察到启动消息', 'time': ''}


def listen():
    markers = [('U-Boot ', 'U-Boot 启动'), ('Starting kernel', '内核开始启动'),
               ('Linux version ', 'Linux 内核已加载'),
               ('[AIOT] PetaLinux started', 'PetaLinux 已进入运行级别 5'),
               ('[AIOT] ready http', '板端灯控服务已就绪')]
    while True:
        try:
            with serial.Serial('COM3', 115200, timeout=.5) as port:
                with LOCK:
                    state['port'] = 'COM3 已连接'
                while True:
                    line = port.read_until(b'\n', 2048).decode('utf-8', 'replace')
                    for marker, message in markers:
                        if marker in line:
                            with LOCK:
                                state.update(event=message, time=time.strftime('%H:%M:%S'))
        except (OSError, serial.SerialException):
            with LOCK:
                state['port'] = 'COM3 未连接或被占用'
            time.sleep(2)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/':
            data, mime = WEB.read_bytes(), 'text/html; charset=utf-8'
        elif self.path == '/status':
            with LOCK:
                result = dict(state)
            try:
                with build_opener(ProxyHandler({})).open(BOARD + '/api/system', timeout=1.5) as response:
                    result['board'] = json.load(response)
            except (OSError, ValueError):
                result['board'] = None
            data, mime = json.dumps(result).encode(), 'application/json'
        else:
            return self.send_error(404)
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format, *args):
        pass


if __name__ == '__main__':
    httpd = ThreadingHTTPServer(('127.0.0.1', 8089), Handler)
    threading.Thread(target=listen, daemon=True).start()
    httpd.serve_forever()
