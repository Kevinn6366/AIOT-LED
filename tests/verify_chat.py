"""Exercise two short Chinese Qwen prompts through the Windows web console."""
import json
import time
from urllib.request import ProxyHandler, Request, build_opener

url = 'http://127.0.0.1:8089'
client = build_opener(ProxyHandler({}))
for question, expected in [('中国的首都是哪里？请只回答城市名。', ('北京',)),
                           ('一盏红灯加两盏绿灯，一共有几盏灯？', ('三', '3'))]:
    req = Request(url + '/api/chat', json.dumps({'question': question}).encode(),
                  {'Content-Type': 'application/json', 'Origin': url})
    start = time.monotonic()
    first = None
    answer = ''
    final = {}
    with client.open(req, timeout=180) as response:
        for line in response:
            frame = json.loads(line)
            if frame.get('text'):
                if first is None:
                    first = time.monotonic() - start
                answer += frame['text']
            if frame.get('done'):
                final = frame
    print(question, answer, 'first_s', first, final, flush=True)
    assert final and any(word in answer for word in expected)
