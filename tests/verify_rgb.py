"""On the live board, check six PWM outputs and restore both lamp settings."""

import argparse
import json
import mmap
import os
import struct
from urllib import request


def verify(url, uio):
    def send(lamp, **payload):
        req = request.Request(url + '/api/rgb/' + str(lamp),
                              data=json.dumps(payload).encode(),
                              headers={'Content-Type': 'application/json'})
        with request.urlopen(req, timeout=4) as response:
            return json.loads(response.read().decode('utf-8'))['rgb']

    with request.urlopen(url + '/api/panel', timeout=4) as response:
        original = json.loads(response.read().decode('utf-8'))['rgb']
    fd = os.open(uio, os.O_RDONLY)
    try:
        signals = mmap.mmap(fd, 4096, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ)
    finally:
        os.close(fd)
    with signals:
        if struct.unpack_from('<I', signals, 8)[0] >> 14 != 21063:
            raise RuntimeError('Wrong design or UIO map')

        def check(mask):
            seen_high = seen_low = 0
            for _ in range(15000):
                value = (struct.unpack_from('<I', signals, 8)[0] >> 8) & 63
                seen_high |= value
                seen_low |= ~value & 63
            if seen_high != mask or seen_low != 63:
                raise RuntimeError('PWM mismatch: expected %02x, high %02x, low %02x'
                                   % (mask, seen_high, seen_low))
            print('PWM verified: 0x%02x' % mask)

        try:
            for lamp in (5, 6):
                send(lamp, on=False)
            check(0)
            for i, color in enumerate(('#ff0000', '#00ff00', '#0000ff')):
                for lamp in (5, 6):
                    send(lamp, color=color)
                    send(lamp, on=True)
                    check(1 << (i + 3 * (lamp - 5)))
                    state = send(lamp, on=False)[lamp - 5]
                    if state['on'] or state['color'] != color:
                        raise RuntimeError('Off did not preserve the selected color')
            send(5, color='#804000')
            send(6, color='#004080')
            send(5, on=True)
            send(6, on=True)
            check(0x33)
        finally:
            for lamp, state in enumerate(original, 5):
                send(lamp, color=state['color'])
                send(lamp, on=state['on'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('url')
    parser.add_argument('uio')
    args = parser.parse_args()
    verify(args.url.rstrip('/'), args.uio)
