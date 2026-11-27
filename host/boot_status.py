"""Local operator console. Serve the UI on Windows and contact the board via Ethernet."""

import json
import http.client
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.request import ProxyHandler, build_opener
from urllib.parse import urlsplit

import serial


BOARD = 'http://169.254.155.221:8080'
WEB = Path(__file__).with_name('boot.html')
ASSETS = WEB.parent.parent / 'web'
LOCAL_HOSTS = {'localhost:8089', '127.0.0.1:8089'}
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
    def reply(self, code, data, mime):
        self.send_response(code)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def board_request(self, path, size=0):
        conn = http.client.HTTPConnection('169.254.155.221', 8080,
                                          timeout=90 if path == '/api/firmware' else 3)
        try:
            conn.putrequest(self.command, path)
            if self.command == 'POST':
                conn.putheader('Content-Type', self.headers.get_content_type())
                conn.putheader('Content-Length', str(size))
            conn.endheaders()
            while size:
                chunk = self.rfile.read(min(size, 131072))
                if not chunk:
                    raise OSError('client disconnected')
                conn.send(chunk)
                size -= len(chunk)
            response = conn.getresponse()
            self.reply(response.status, response.read(), 'application/json')
        except (OSError, http.client.HTTPException):
            self.reply(503, json.dumps({'error': '开发板未响应，请查看启动监视'}).encode(),
                       'application/json')
        finally:
            conn.close()

    def restart_board(self, size):
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import padding

        try:
            boot_id = json.loads(self.rfile.read(size))['boot_id']
            if not isinstance(boot_id, str) or not re.fullmatch('[0-9a-f-]{36}', boot_id):
                raise ValueError()
        except (ValueError, TypeError, KeyError):
            return self.send_error(400)
        conn = http.client.HTTPConnection('169.254.155.221', 8080, timeout=4)
        try:
            path = ASSETS.parent / 'build/sd/signing-key.pem'
            key = serialization.load_pem_private_key(path.read_bytes(), password=None)
            signature = key.sign(('AIOT-REBOOT1/' + boot_id).encode('ascii'),
                                 padding.PKCS1v15(), hashes.SHA256())
            conn.request('POST', '/api/reboot', signature,
                         {'Content-Type': 'application/octet-stream'})
            response = conn.getresponse()
            self.reply(response.status, response.read(), 'application/json')
        except (OSError, ValueError, http.client.HTTPException):
            self.send_error(503)
        finally:
            conn.close()

    def do_POST(self):
        host = self.headers.get('Host', '')
        if host not in LOCAL_HOSTS or self.headers.get('Origin') != 'http://' + host:
            return self.send_error(403)
        path = urlsplit(self.path).path
        if not re.fullmatch(r'/api/(led|panel/[0-3]|rgb/[56]|firmware|reboot)', path):
            return self.send_error(404)
        firmware = path == '/api/firmware'
        if self.headers.get_content_type() != ('application/octet-stream' if firmware else 'application/json'):
            return self.send_error(415)
        try:
            size = int(self.headers.get('Content-Length', '-1'))
        except ValueError:
            return self.send_error(400)
        if not 0 < size <= (80 * 1024 * 1024 if firmware else 4096):
            return self.send_error(413)
        if path == '/api/reboot':
            return self.restart_board(size)
        self.board_request(path, size)

    def do_GET(self):
        if self.headers.get('Host') not in LOCAL_HOSTS:
            return self.send_error(403)
        path = urlsplit(self.path).path
        if path == '/':
            data = (ASSETS / 'index.html').read_bytes()
            mime = 'text/html; charset=utf-8'
        elif path in ('/main.js', '/panel.css'):
            data = (ASSETS / path[1:]).read_bytes()
            mime = ('text/javascript' if path.endswith('.js') else 'text/css') + '; charset=utf-8'
        elif path == '/boot':
            data, mime = WEB.read_bytes(), 'text/html; charset=utf-8'
        elif path in ('/api/system', '/api/panel'):
            return self.board_request(path)
        elif path == '/status':
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
        self.reply(200, data, mime)

    def log_message(self, format, *args):
        pass


if __name__ == '__main__':
    httpd = ThreadingHTTPServer(('127.0.0.1', 8089), Handler)
    threading.Thread(target=listen, daemon=True).start()
    httpd.serve_forever()
