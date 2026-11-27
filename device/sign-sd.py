"""Create the local signing key once, or sign the completed PetaLinux FIT."""

import argparse
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['prepare', 'sign'])
    args = parser.parse_args()
    out = Path(__file__).resolve().parents[1] / 'build' / 'sd'
    out.mkdir(parents=True, exist_ok=True)
    path = out / 'signing-key.pem'
    if args.action == 'prepare' and not path.exists():
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        with path.open('xb') as stream:
            stream.write(key.private_bytes(serialization.Encoding.PEM,
                                          serialization.PrivateFormat.PKCS8,
                                          serialization.NoEncryption()))
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if args.action == 'prepare':
        (out / 'public.hex').write_text(format(key.public_key().public_numbers().n, 'x'))
    else:
        image = (out / 'aiot.ub').read_bytes()
        signature = key.sign(image, padding.PKCS1v15(), hashes.SHA256())
        (out / 'AIOT-LED.aiot').write_bytes(b'AIOTFIT1' + signature + image)


if __name__ == '__main__':
    main()
