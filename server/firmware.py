"""Stage a developer-signed FIT on the board's SD card for the next boot."""

import hashlib
import mmap
import os
import shutil
import subprocess
import struct
import tempfile
import threading

MAGIC = b'AIOTFIT1'
LIMIT = 80 * 1024 * 1024
CARD = '/run/media/mmcblk0p1'
UPDATE_LOCK = threading.Lock()


def read_text(path, default=''):
    try:
        with open(path) as stream:
            return stream.read().strip()
    except OSError:
        return default


def boot_env(slot):
    return ('uenvcmd=if fatload mmc ${sdbootdev} ${netstart} aiot-%s.ub; '
            'then if iminfo ${netstart}; then bootm ${netstart}; fi; fi\n' % slot)


def ld4_output():
    # Zynq-7000 GPIO bank 0 DATA drives LD4 on MIO7; DATA_RO samples inputs.
    try:
        with open('/dev/mem', 'rb', buffering=0) as memory:
            with mmap.mmap(memory.fileno(), 4096, flags=mmap.MAP_SHARED,
                           prot=mmap.PROT_READ, offset=0xe000a000) as bank:
                return bool(struct.unpack_from('<I', bank, 0x40)[0] & (1 << 7))
    except (OSError, ValueError, OverflowError):
        return None


def system_state():
    def running(name):
        pid = read_text('/tmp/aiot-led/' + name + '.pid')
        return pid.isdigit() and os.path.isdir('/proc/' + pid)
    env = read_text(CARD + '/uEnv.txt')
    return dict(ld4=ld4_output() if running('ps') else None, kernel=os.uname().release,
                boot_id=read_text('/proc/sys/kernel/random/boot_id'),
                uptime=int(float(read_text('/proc/uptime', '0').split()[0])),
                version=read_text('/etc/aiot-version', '临时部署'),
                stage=read_text('/tmp/aiot-led/boot-stage', 'unknown'),
                qwen=running('qwen') and os.path.isfile('/tmp/aiot-led/qwen-ready'),
                gpt=os.path.exists('/tmp/aiot-led/gpt-uio'),
                ps=running('ps'), pl=running('pl'), uio=os.path.exists('/dev/uio1'),
                next_slot=next((s for s in ('a', 'b') if env == boot_env(s).strip()), ''),
                card=os.path.ismount(CARD))


def verify_signature(signature, digest):
    # Strict RSA-2048 / PKCS#1 v1.5 SHA-256, using the paired public modulus.
    modulus = int(read_text('/etc/aiot-public.hex'), 16)
    value = int.from_bytes(signature, 'big')
    prefix = bytes.fromhex('3031300d060960864801650304020105000420')
    tail = prefix + digest
    expected = b'\x00\x01' + b'\xff' * (253 - len(tail)) + b'\x00' + tail
    if value >= modulus or pow(value, 65537, modulus).to_bytes(256, 'big') != expected:
        raise ValueError('签名不匹配')


def restart(signature):
    boot_id = read_text('/proc/sys/kernel/random/boot_id')
    digest = hashlib.sha256(('AIOT-REBOOT1/' + boot_id).encode('ascii')).digest()
    verify_signature(signature, digest)
    if not UPDATE_LOCK.acquire(False):
        raise ValueError('写卡或重启正在进行')

    def reboot():
        try:
            os.sync()
            subprocess.check_call(['/sbin/reboot'])
        except OSError:
            UPDATE_LOCK.release()
        except subprocess.CalledProcessError:
            UPDATE_LOCK.release()

    threading.Timer(1, reboot).start()
    return dict(boot_id=boot_id)


def install(stream, length):
    if not 268 < length <= LIMIT or stream.read(8) != MAGIC:
        raise ValueError('无效的固件包')
    if not os.path.ismount(CARD):
        raise ValueError('SD 卡未挂载')
    env_path = CARD + '/uEnv.txt'
    existing = read_text(env_path)
    slots = {boot_env(s).strip(): s for s in ('a', 'b')}
    if existing not in slots:
        raise ValueError('当前引导配置不属于本项目')
    signature = stream.read(256)
    digest = hashlib.sha256()
    with tempfile.TemporaryFile() as image:
        remaining = length - 264
        while remaining:
            block = stream.read(min(remaining, 131072))
            if not block:
                raise ValueError('上传中断')
            image.write(block)
            digest.update(block)
            remaining -= len(block)
        image.seek(0)
        magic, size = struct.unpack('>II', image.read(8))
        if magic != 0xd00dfeed or size != length - 264:
            raise ValueError('FIT 长度或标识不正确')
        verify_signature(signature, digest.digest())
        active = slots.get(read_text('/tmp/aiot-led/startup-env'))
        if active is None:
            raise ValueError('无法确认本次启动槽')
        target = 'b' if active == 'a' else 'a'
        image.seek(0)
        path = CARD + '/aiot-' + target + '.ub'
        with open(path + '.new', 'wb') as out:
            shutil.copyfileobj(image, out, 131072)
            out.flush()
            os.fsync(out.fileno())
        check = hashlib.sha256()
        with open(path + '.new', 'rb') as verify:
            for block in iter(lambda: verify.read(131072), b''):
                check.update(block)
        if check.digest() != digest.digest():
            raise IOError('SD 卡回读校验失败')
        os.replace(path + '.new', path)
        os.sync()
        with open(env_path + '.new', 'w') as out:
            out.write(boot_env(target))
            out.flush()
            os.fsync(out.fileno())
        os.replace(env_path + '.new', env_path)
        os.sync()
        return dict(slot=target, sha256=digest.hexdigest())
