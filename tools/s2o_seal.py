"""Sealed messages, as docs/protocol.md describes them, for the tests: made and opened here independently of the
firmware's Monocypher, so a test that passes shows both follow the description.

The present form: 12, the number (u32), the message XChaCha20-Poly1305-encrypted (as libsodium's
crypto_aead_xchacha20poly1305_ietf and Monocypher's crypto_aead_lock), then the 16-byte tag. The key is BLAKE2b, keyed
with the signing key, of "sensors2onvif aead v1"; the nonce the direction and the number, then zeros; the associated
data the node's MAC address and the 5 bytes of type and number. The form from before, 7, is HMAC-SHA1 throughout.
"""
import hashlib
import hmac
import struct

from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

SEALED, SEALED_OLD = 12, 7


def _hchacha20(key, nonce16):
    """The subkey XChaCha20 runs ChaCha20 with: 20 rounds of the ChaCha block on the key and the nonce's first 16 bytes."""
    s = list(struct.unpack('<4I', b'expand 32-byte k') + struct.unpack('<8I', key) + struct.unpack('<4I', nonce16))

    def rotl(v, c):
        return ((v << c) & 0xFFFFFFFF) | (v >> (32 - c))

    def quarter(a, b, c, d):
        s[a] = (s[a] + s[b]) & 0xFFFFFFFF; s[d] = rotl(s[d] ^ s[a], 16)
        s[c] = (s[c] + s[d]) & 0xFFFFFFFF; s[b] = rotl(s[b] ^ s[c], 12)
        s[a] = (s[a] + s[b]) & 0xFFFFFFFF; s[d] = rotl(s[d] ^ s[a], 8)
        s[c] = (s[c] + s[d]) & 0xFFFFFFFF; s[b] = rotl(s[b] ^ s[c], 7)

    for _ in range(10):
        quarter(0, 4, 8, 12); quarter(1, 5, 9, 13); quarter(2, 6, 10, 14); quarter(3, 7, 11, 15)
        quarter(0, 5, 10, 15); quarter(1, 6, 11, 12); quarter(2, 7, 8, 13); quarter(3, 4, 9, 14)
    return struct.pack('<8I', *(s[0:4] + s[12:16]))


def _xchacha(key, nonce24):
    return ChaCha20Poly1305(_hchacha20(key, nonce24[:16])), b'\0\0\0\0' + nonce24[16:]


def _inputs(key, mac_hex, head, from_hub):
    aead_key = hashlib.blake2b(b'sensors2onvif aead v1', key=key, digest_size=32).digest()
    nonce = bytes([1 if from_hub else 0]) + head[1:5] + bytes(19)
    return aead_key, nonce, bytes.fromhex(mac_hex) + head


def seal(inner, key, mac_hex, number, from_hub=False):
    head = bytes([SEALED]) + number.to_bytes(4, 'little')
    aead_key, nonce, ad = _inputs(key, mac_hex, head, from_hub)
    cipher, n12 = _xchacha(aead_key, nonce)
    return head + cipher.encrypt(n12, inner, ad)  # the encrypted message, then its tag


def seal_old(inner, key, mac_hex, number, from_hub=False):
    seal_key = hmac.new(key, b'sensors2onvif seal', hashlib.sha1).digest()
    stream = b''
    for block in range((len(inner) + 19) // 20):
        stream += hmac.new(seal_key, bytes([1 if from_hub else 0]) + number.to_bytes(4, 'little') + bytes([block]), hashlib.sha1).digest()
    body = bytes([SEALED_OLD]) + number.to_bytes(4, 'little') + bytes(a ^ b for a, b in zip(inner, stream))
    return body + hmac.new(key, bytes.fromhex(mac_hex) + body, hashlib.sha1).digest()[:8]


def open_sealed(data, key, mac_hex, from_hub):
    """The message inside either form, or None for one that is not genuine."""
    if data[:1] == bytes([SEALED]):
        aead_key, nonce, ad = _inputs(key, mac_hex, data[:5], from_hub)
        cipher, n12 = _xchacha(aead_key, nonce)
        try:
            return cipher.decrypt(n12, data[5:], ad)
        except Exception:
            return None
    if data[:1] != bytes([SEALED_OLD]) or hmac.new(key, bytes.fromhex(mac_hex) + data[:-8], hashlib.sha1).digest()[:8] != data[-8:]:
        return None
    seal_key = hmac.new(key, b'sensors2onvif seal', hashlib.sha1).digest()
    inner, stream = data[5:-8], b''
    for block in range((len(inner) + 19) // 20):
        stream += hmac.new(seal_key, bytes([1 if from_hub else 0]) + data[1:5] + bytes([block]), hashlib.sha1).digest()
    return bytes(a ^ b for a, b in zip(inner, stream))


if __name__ == '__main__':  # the unit test's vector: a report sealed with a key of twenty 0x22 bytes, number 0x11223344
    report = bytes([2, 1, 1, 80, 7]) + bytes(12)
    print(seal(report, b'\x22' * 20, '020000000055', 0x11223344).hex())
