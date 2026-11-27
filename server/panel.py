"""Request a live sample from the C worker over the two board FIFOs."""

import os
import select
import stat
import threading
import uuid


class Panel:
    def __init__(self, directory):
        self.commands = os.path.join(directory, 'pl-commands')
        self.status = os.path.join(directory, 'pl-status')
        self.lock = threading.Lock()

    def sample(self, channel=-1, mode=-1):
        with self.lock:
            flags = os.O_NOFOLLOW | os.O_NONBLOCK
            status = os.open(self.status, os.O_RDWR | flags)
            try:
                cmd = os.open(self.commands, os.O_WRONLY | flags)
                try:
                    if not all(stat.S_ISFIFO(os.fstat(fd).st_mode)
                               for fd in (cmd, status)):
                        raise OSError('Invalid panel FIFO')
                    token = uuid.uuid4().hex
                    os.write(cmd, ('%s %d %d\n' % (token, channel, mode)).encode())
                finally:
                    os.close(cmd)
                if not select.select([status], [], [], 1)[0]:
                    raise OSError('Panel timeout')
                for line in os.read(status, 4096).decode('ascii').splitlines():
                    fields = line.split()
                    if len(fields) == 4 and fields[0] == token:
                        switches, leds, mask = map(int, fields[1:])
                        return dict(switches=switches, leds=leds, remote=mask)
                raise OSError('Missing sample')
            finally:
                os.close(status)
