"""Exercise the Bottle service and native C worker over a real Linux FIFO."""

import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from wsgiref.simple_server import make_server

from server.app import create_app


class LedTest(unittest.TestCase):

    def test_http_to_device(self):
        with tempfile.TemporaryDirectory() as runtime:
            fifo = Path(runtime, 'commands')
            value = Path(runtime, 'value')
            value.write_text('0')
            httpd = make_server('127.0.0.1', 0, create_app(str(fifo)))
            url = 'http://127.0.0.1:%d' % httpd.server_port
            thread = threading.Thread(target=httpd.serve_forever)
            thread.start()
            self.addCleanup(httpd.server_close)
            self.addCleanup(thread.join)
            self.addCleanup(httpd.shutdown)

            def post(data):
                request = urllib.request.Request(
                    url + '/api/led', data=json.dumps(data).encode(),
                    headers={'Content-Type': 'application/json'})
                return urllib.request.urlopen(request, timeout=3)

            for path in ('/', '/main.js'):
                with urllib.request.urlopen(url + path, timeout=3) as result:
                    self.assertEqual(result.status, 200)
            for payload, status in (({'on': 1}, 400), ({'on': True}, 503)):
                with self.assertRaises(urllib.error.HTTPError) as caught:
                    post(payload)
                self.assertEqual(caught.exception.code, status)

            worker = subprocess.Popen(['build/test/led-control', fifo, value])
            try:
                for on in (True, False):
                    for _ in range(100):
                        try:
                            with post({'on': on}) as result:
                                self.assertEqual(result.status, 202)
                            break
                        except urllib.error.HTTPError as error:
                            if error.code != 503 or worker.poll() is not None:
                                raise
                            time.sleep(.01)
                    else:
                        self.fail('worker did not open FIFO')
                    expected = str(int(on))
                    for _ in range(100):
                        if value.read_text() == expected:
                            break
                        time.sleep(.01)
                    self.assertEqual(value.read_text(), expected)
            finally:
                worker.terminate()
                worker.wait(timeout=3)


if __name__ == '__main__':
    unittest.main()
