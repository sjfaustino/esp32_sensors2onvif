"""Signs a firmware for a hub or a sensor node to take as an update (lib/s2o_proto/s2o_fw_sig.h has the format).

  python tools/sign_firmware.py <firmware.bin> <signed.bin> [release]
                         signs the hub's firmware, with the key named below, and the release number of the build, from
                         the hub's include/version.h unless given
  python hub/tools/sign_firmware.py --node <firmware.bin> <signed.bin> [release]
                         signs a node's, from the node project, with the release number of its include/version.h, in the
                         folder it is run from, unless given
  python tools/sign_firmware.py --new-key     makes the key, if there is none, and prints its public half
  python tools/sign_firmware.py --public      prints the public half of the key

The private key is read from the FIRMWARE_SIGNING_KEY environment variable (the PEM text, as in CI), or else from
the file in FIRMWARE_SIGNING_KEY_FILE, or else from ~/.sensors2onvif/firmware-signing-key.pem. It never belongs in
the repository. Whoever has it can update any hub that has its password, and any node through such a hub; a hub or a
node takes a new key only over its cable. Needs the cryptography package.
"""
import hashlib
import os
import re
import struct
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

MAGIC = b'S2OSIG1\n'
CONTEXT = b's2o firmware v1\n'  # the hub's own firmware
NODE_CONTEXT = b's2o nodefirm v1\n'  # a node's, so neither signature passes for the other
RELEASE_MAGIC = b'S2OREL1\n'
VERSION_H = Path(__file__).resolve().parent.parent / 'include' / 'version.h'
DEFAULT_FILE = Path.home() / '.sensors2onvif' / 'firmware-signing-key.pem'


def key_file():
    return Path(os.environ.get('FIRMWARE_SIGNING_KEY_FILE') or DEFAULT_FILE)


def load_key():
    pem = os.environ.get('FIRMWARE_SIGNING_KEY')
    data = pem.encode() if pem else key_file().read_bytes()
    return serialization.load_pem_private_key(data, password=None)


def public_hex(key):
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()


def built_release(node=False):
    """The release number the version script wrote for the last build: the hub's FIRMWARE_RELEASE, or a node's
    NODE_RELEASE in the node project's include/version.h."""
    path, name = (Path('include') / 'version.h', 'NODE_RELEASE') if node else (VERSION_H, 'FIRMWARE_RELEASE')
    m = re.search(rf'#define {name} (\d+)', path.read_text(encoding='utf-8')) if path.exists() else None
    if not m:
        raise ValueError(f'{path} has no {name}: build the firmware first')
    return int(m.group(1))


def sign(image, key, release=None, node=False):
    """The image, its release number, and the trailer with the signature over both. A hub, or a node, refuses a release
    number lower than its own."""
    if image.endswith(MAGIC):
        raise ValueError('already signed')
    signed = image + struct.pack('<I', built_release(node) if release is None else release) + RELEASE_MAGIC
    signature = key.sign((NODE_CONTEXT if node else CONTEXT) + hashlib.sha512(signed).digest())
    return signed + signature + MAGIC


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
    node = args[:1] == ['--node']
    args = args[1:] if node else args
    if len(args) not in (2, 3):
        print(__doc__)
        return 2
    image = Path(args[0]).read_bytes()
    if not image or image[0] != 0xE9:
        print('not an ESP32 firmware image:', args[0])
        return 1
    release = int(args[2]) if len(args) == 3 else built_release(node)
    Path(args[1]).write_bytes(sign(image, load_key(), release, node))
    print('signed', args[1], 'as', 'a node' if node else 'the hub', 'release', release)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
