"""Signs a hub firmware for the hub to take as an update (lib/sensorcam/cam_firmware_sig.h has the format).

  python tools/sign_firmware.py <firmware.bin> <signed.bin>   signs, with the key named below
  python tools/sign_firmware.py --new-key                     makes the key, if there is none, and prints its public half
  python tools/sign_firmware.py --public                      prints the public half of the key

The private key is read from the FIRMWARE_SIGNING_KEY environment variable (the PEM text, as in CI), or else from
the file in FIRMWARE_SIGNING_KEY_FILE, or else from ~/.sensors2onvif/firmware-signing-key.pem. It never belongs in
the repository. Whoever has it can update any hub that has its password; a hub takes a new key only over its cable.
Needs the cryptography package.
"""
import hashlib
import os
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

MAGIC = b'S2OSIG1\n'
CONTEXT = b's2o firmware v1\n'
DEFAULT_FILE = Path.home() / '.sensors2onvif' / 'firmware-signing-key.pem'


def key_file():
    return Path(os.environ.get('FIRMWARE_SIGNING_KEY_FILE') or DEFAULT_FILE)


def load_key():
    pem = os.environ.get('FIRMWARE_SIGNING_KEY')
    data = pem.encode() if pem else key_file().read_bytes()
    return serialization.load_pem_private_key(data, password=None)


def public_hex(key):
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()


def sign(image, key):
    """The image followed by its trailer."""
    if image.endswith(MAGIC):
        raise ValueError('already signed')
    signature = key.sign(CONTEXT + hashlib.sha512(image).digest())
    return image + signature + MAGIC


def main(args):
    if args == ['--new-key']:
        path = key_file()
        if path.exists():
            print('a key is already there, and was kept:', path)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            key = Ed25519PrivateKey.generate()
            path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                               serialization.NoEncryption()))
            print('made', path)
        print('public key', public_hex(load_key()))
        return 0
    if args == ['--public']:
        print(public_hex(load_key()))
        return 0
    if len(args) != 2:
        print(__doc__)
        return 2
    image = Path(args[0]).read_bytes()
    if not image or image[0] != 0xE9:
        print('not an ESP32 firmware image:', args[0])
        return 1
    Path(args[1]).write_bytes(sign(image, load_key()))
    print('signed', args[1])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
