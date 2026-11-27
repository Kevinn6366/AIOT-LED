"""Exercise live lamp commands via the Windows console and restore panel settings."""

import json
import time
from urllib.request import ProxyHandler, Request, build_opener

URL = 'http://127.0.0.1:8089'
client = build_opener(ProxyHandler({}))


def call(path, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = Request(URL + path, data=data, headers={
        'Origin': URL, 'Content-Type': 'application/json'})
    with client.open(req, timeout=6) as response:
        return json.load(response)


def verify():
    was_synced = call('/api/panel')['rgb_sync']
    original = call('/api/rgb/sync', {'on': False})
    try:
        verify_manual(original)
        call('/api/rgb/sync', {'on': True})
        colors = set()
        for _ in range(12):
            time.sleep(.1)
            sample = call('/api/panel')
            first, second = sample['rgb']
            assert sample['rgb_sync'] and first == second and first['on']
            colors.add(first['color'])
        assert len(colors) >= 6, 'rainbow did not advance'
        restored = call('/api/rgb/sync', {'on': False})
        assert not restored['rgb_sync'] and restored['rgb'] == original['rgb']
        print('PASS: synchronized rainbow, %d distinct colors, manual settings restored'
              % len(colors))
    finally:
        call('/api/rgb/sync', {'on': was_synced})


def verify_manual(original):
    for channel in range(4):
        bit = 1 << channel
        path = '/api/panel/' + str(channel)
        mode = ('on' if original['leds'] & bit else 'off') if original['remote'] & bit else 'switch'
        try:
            for choice in ('off', 'on', 'switch'):
                call(path, {'mode': choice})
                state = call('/api/panel')
                assert bool(state['remote'] & bit) == (choice != 'switch')
                expected = bool(state['switches'] & bit) if choice == 'switch' else choice == 'on'
                assert bool(state['leds'] & bit) == expected
        finally:
            call(path, {'mode': mode})
    for index, color in enumerate(('#ff0000', '#0000ff')):
        path = '/api/rgb/' + str(index + 5)
        before = original['rgb'][index]
        try:
            call(path, {'color': color})
            for on in (True, False):
                call(path, {'on': on})
                state = call('/api/panel')['rgb'][index]
                assert state['on'] == on and state['color'] == color
        finally:
            call(path, {'color': before['color']})
            call(path, {'on': before['on']})
    print('PASS: four digital outputs, switch modes and both RGB setpoints')


if __name__ == '__main__':
    verify()
