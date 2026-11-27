"""Serve the ZYBO control page and forward LED commands to the device FIFO."""

import argparse
import os
import re
import socketserver
import stat
from wsgiref.simple_server import make_server, WSGIRequestHandler, WSGIServer

import bottle

if __package__:
    from .panel import Panel
    from . import chat, firmware, gpt
else:
    from panel import Panel
    import chat
    import firmware
    import gpt


WEB_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'web')


class LedServer(socketserver.ThreadingMixIn, WSGIServer):
    """Handle browser preconnections independently of active requests."""

    daemon_threads = True


class LedHandler(WSGIRequestHandler):
    timeout = 10


def create_server(host, port, fifo):
    return make_server(host, port, create_app(fifo), LedServer, LedHandler)


def create_app(command_fifo):
    app = bottle.Bottle()
    chat.register(app)
    gpt.register(app, os.path.dirname(command_fifo))
    panel = Panel(os.path.dirname(command_fifo))

    @app.get('/api/system')
    def system_status():
        bottle.response.set_header('Cache-Control', 'no-store')
        return firmware.system_state()

    @app.post('/api/reboot')
    def reboot():
        if bottle.request.content_length != 256:
            bottle.response.status = 400
            return {'error': '需要本机签名'}
        try:
            return firmware.restart(bottle.request.environ['wsgi.input'].read(256))
        except ValueError as error:
            bottle.response.status = 403
            return {'error': str(error)}

    @app.post('/api/firmware')
    def update_firmware():
        size = bottle.request.content_length
        if bottle.request.content_type != 'application/octet-stream' or not 268 < size <= firmware.LIMIT:
            bottle.response.status = 400
            return {'error': '请选择项目生成的 .aiot 固件包'}
        if not firmware.UPDATE_LOCK.acquire(False):
            bottle.response.status = 409
            return {'error': '正在写入固件'}
        try:
            return firmware.install(bottle.request.environ['wsgi.input'], size)
        except (ValueError, OSError) as error:
            bottle.response.status = 400
            return {'error': str(error)}
        finally:
            firmware.UPDATE_LOCK.release()

    def read_panel(channel=-1, mode=-1):
        bottle.response.set_header('Cache-Control', 'no-store')
        try:
            return panel.sample(channel, mode)
        except (OSError, ValueError):
            bottle.response.status = 503
            return {'error': 'PL 状态不可用'}

    @app.get('/api/panel')
    def panel_status():
        return read_panel()

    @app.post('/api/panel/<channel:int>')
    def panel_command(channel):
        data = bottle.request.json
        modes = {'switch': -1, 'off': 0, 'on': 1}
        mode = data.get('mode') if isinstance(data, dict) else None
        if channel not in range(4) or not isinstance(mode, str) or mode not in modes:
            bottle.response.status = 400
            return {'error': '无效的通道或操作'}
        return read_panel(channel, modes[mode])

    @app.post('/api/rgb/sync')
    def sync_rgb():
        data = bottle.request.json
        if isinstance(data, dict) and type(data.get('on')) is bool:
            return read_panel(7, int(data['on']))
        bottle.response.status = 400
        return {'error': '请指定同步效果开关'}

    @app.post('/api/rgb/<lamp:int>')
    def rgb_command(lamp):
        data = bottle.request.json
        if lamp in (5, 6) and isinstance(data, dict):
            if set(data) == {'on'} and type(data['on']) is bool:
                return read_panel(lamp, -3 if data['on'] else -2)
            color = data.get('color')
            if set(data) == {'color'} and isinstance(color, str) and re.fullmatch(
                    r'#[0-9A-Fa-f]{6}', color):
                return read_panel(lamp, int(color[1:], 16))
        bottle.response.status = 400
        return {'error': '请指定 LD5 或 LD6，并提交开关值或六位 RGB 色值'}

    @app.get('/panel.css')
    def stylesheet():
        return bottle.static_file('panel.css', root=WEB_ROOT)

    @app.get('/')
    def index():
        return bottle.static_file('index.html', root=WEB_ROOT)

    @app.get('/main.js')
    def javascript():
        return bottle.static_file('main.js', root=WEB_ROOT)

    @app.post('/api/led')
    def set_led():
        try:
            data = bottle.request.json
        except bottle.HTTPError:
            data = None
        if not isinstance(data, dict) or type(data.get('on')) is not bool:
            bottle.response.status = 400
            return {'error': 'on 字段必须为布尔值'}
        try:
            fd = os.open(command_fifo, os.O_WRONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
            try:
                if not stat.S_ISFIFO(os.fstat(fd).st_mode):
                    raise OSError('Expected FIFO')
                os.write(fd, b'1\n' if data['on'] else b'0\n')
            finally:
                os.close(fd)
        except OSError:
            bottle.response.status = 503
            return {'error': '控制管道不可用，请检查板端程序'}
        bottle.response.status = 202
        return {'queued': True}

    return app


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--fifo', required=True)
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    httpd = create_server(args.host, args.port, args.fifo)
    try:
        httpd.serve_forever()
    finally:
        httpd.server_close()
