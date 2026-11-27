"""Stream ZyboGPT hardware results from the SDK worker."""
import json
import os
import subprocess
import tempfile
import threading

import bottle


def register(app, runtime):
    active = threading.Lock()

    @app.post('/api/generate')
    def generate():
        if not 0 < bottle.request.content_length <= 1024:
            bottle.abort(400)
        data = bottle.request.json
        if not isinstance(data, dict):
            bottle.abort(400)
        prompt, length, seed = (data.get(key) for key in ('prompt', 'length', 'seed'))
        if (not isinstance(prompt, str) or not prompt or
                any(ord(c) < 32 or ord(c) > 126 for c in prompt) or
                type(length) is not int or not 1 <= length <= 128 - len(prompt) or
                type(seed) is not int or not 1 <= seed <= 0xffffffff):
            bottle.response.status = 400
            return {'error': '请输入英文开头，输入与续写合计最多 128 个字符'}
        device = os.path.join(runtime, 'gpt-uio')
        if not os.path.exists(device):
            bottle.response.status = 503
            return {'error': 'PL 推理核未就绪'}
        if not active.acquire(False):
            bottle.response.status = 409
            return {'error': '开发板正在生成文本'}

        def frames():
            try:
                with tempfile.TemporaryDirectory(dir=runtime) as directory:
                    fifo = directory + '/request'
                    os.mkfifo(fifo, 0o600)
                    fd = os.open(fifo, os.O_RDWR)
                    try:
                        os.write(fd, ('%d %d %d\n%s' % (seed, length, len(prompt), prompt)).encode('ascii'))
                        worker = subprocess.Popen([os.path.join(runtime, 'build/artifacts/gpt.elf'),
                                                   device, fifo], stdout=subprocess.PIPE)
                        try:
                            for line in worker.stdout:
                                token, cycles = map(int, line.split())
                                yield json.dumps(dict(char=chr(token), cycles=cycles)) + '\n'
                            if worker.wait() != 0:
                                raise ValueError('PL inference failed')
                            yield '{"done":true}\n'
                        finally:
                            if worker.poll() is None:
                                worker.kill()
                            worker.wait()
                            worker.stdout.close()
                    finally:
                        os.close(fd)
            except (OSError, ValueError):
                yield '{"error":"PL inference failed"}\n'
            finally:
                active.release()
        bottle.response.content_type = 'application/x-ndjson'
        bottle.response.set_header('Cache-Control', 'no-store')
        return frames()
