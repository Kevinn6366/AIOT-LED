"""Serve the ZYBO control page and forward LED commands to the device FIFO."""

import argparse
import os
import stat

import bottle


WEB_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'web')


def create_app(command_fifo):
    app = bottle.Bottle()

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
    create_app(args.fifo).run(host=args.host, port=args.port)
