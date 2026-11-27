"""Compare streaming output from the live local console to INT8 reference vectors."""
import json
from pathlib import Path
from urllib.request import ProxyHandler, Request, build_opener

url = 'http://127.0.0.1:8089'
client = build_opener(ProxyHandler({}))
vectors = Path(__file__).resolve().parents[1] / 'build/zybogpt/export/console-vectors.json'
for case in json.loads(vectors.read_text()):
    prompt, seed, expected = case['prompt'], case['seed'], case['text']
    payload = json.dumps(dict(prompt=prompt, seed=seed, length=len(expected))).encode()
    request = Request(url + '/api/generate', data=payload,
                      headers={'Content-Type': 'application/json', 'Origin': url})
    with client.open(request, timeout=10) as response:
        events = [json.loads(line) for line in response]
    assert events[-1] == {'done': True}
    assert ''.join(frame['char'] for frame in events[:-1]) == expected
    cycles = [frame['cycles'] for frame in events[:-1]]
    assert all(a < b for a, b in zip([0] + cycles, cycles))
    print('PASS', repr(prompt), repr(expected), cycles[-1], 'PL cycles')
