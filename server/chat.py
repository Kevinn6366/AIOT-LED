"""Forward a bounded Chinese question to the board-local llama.cpp worker."""
import http.client
import json
import threading
import time

import bottle


PREFIX = ('<|im_start|>system\n你是一个中文助手。请简短、准确地回答。<|im_end|>\n'
          '<|im_start|>user\n')


def warm_prefix():
    # Prime only the shared instruction, before accepting interactive questions.
    for _ in range(120):
        conn = http.client.HTTPConnection('127.0.0.1', 8091, timeout=480)
        try:
            body = json.dumps(dict(prompt=PREFIX, n_predict=1, cache_prompt=True,
                                   temperature=0))
            conn.request('POST', '/completion', body, {'Content-Type': 'application/json'})
            response = conn.getresponse()
            data = response.read()
            if response.status == 200 and json.loads(data.decode('utf-8')).get('stop'):
                return True
        except (OSError, ValueError, http.client.HTTPException):
            pass
        finally:
            conn.close()
        time.sleep(1)
    return False


def projection_counts():
    try:
        with open('/tmp/aiot-led/qproj.stats') as stats:
            values = tuple(map(int, stats.read().split()))
            if len(values) == 36:
                return values + (0,) * 20
            if len(values) == 56:
                return values
    except (ValueError, OSError):
        pass
    return (0,) * 56


def register(app):
    active = threading.Lock()

    @app.post('/api/chat')
    def chat():
        if not 0 < bottle.request.content_length <= 4096:
            bottle.abort(413)
        data = bottle.request.json
        question = data.get('question') if isinstance(data, dict) else None
        if not isinstance(question, str) or not 1 <= len(question.strip()) <= 96 or '<|' in question:
            bottle.response.status = 400
            return {'error': '请输入 1 至 96 字的问题'}
        backend = data.get('backend', 'pl' if projection_counts()[0] else 'ps')
        if backend not in ('ps', 'pl') or (backend == 'pl' and not projection_counts()[0]):
            bottle.response.status = 400
            return {'error': '所选推理后端不可用'}
        if not active.acquire(False):
            bottle.response.status = 409
            return {'error': '开发板正在回答另一个问题'}
        baseline = projection_counts()
        conn = http.client.HTTPConnection('127.0.0.1', 8091, timeout=480)
        try:
            prompt = PREFIX + question.strip() + '<|im_end|>\n<|im_start|>assistant\n'
            body = json.dumps(dict(prompt=prompt, n_predict=80, temperature=0,
                                   stream=True, cache_prompt=True, aiot_ps=backend == 'ps', stop=['<|im_end|>']))
            conn.request('POST', '/completion', body, {'Content-Type': 'application/json'})
            upstream = conn.getresponse()
            if upstream.status != 200:
                raise ValueError()
        except (OSError, ValueError, http.client.HTTPException):
            conn.close()
            active.release()
            bottle.response.status = 503
            return {'error': '板端 Qwen 尚未就绪，请稍后再试'}

        def frames():
            try:
                for line in upstream:
                    if not line.startswith(b'data:'):
                        continue
                    item = json.loads(line[5:].decode('utf-8'))
                    if item.get('stop'):
                        final = projection_counts()
                        delta = [v-b for v, b in zip(final, baseline)]
                        yield json.dumps(dict(done=True, backend=backend, pl_calls=delta[0],
                                              pl_layers=delta[2:26], pl_ops=delta[26:36],
                                               pl_op_cycles=delta[36:46],
                                               pl_last_graph_cycles=final[46:] if delta[0] else [0]*10,
                                              pl_cycles=delta[1], timings=item['timings'],
                                              limited=item.get('stopped_limit', False))) + '\n'
                        return
                    yield json.dumps(dict(text=item['content'])) + '\n'
                raise ValueError()
            except (OSError, ValueError, KeyError, http.client.HTTPException):
                yield '{"error":"Qwen stream interrupted"}\n'
            finally:
                conn.close()
                active.release()
        bottle.response.content_type = 'application/x-ndjson'
        return frames()


if __name__ == '__main__':
    raise SystemExit(0 if warm_prefix() else 1)
