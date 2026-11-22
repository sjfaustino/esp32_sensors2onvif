"""Writes the fuzzer's starting inputs into a folder: one well-formed example for each parser in
test/fuzz/fuzz_parsers.cpp, its first byte the parser's number, for the fuzzer to change from."""

import base64, hashlib, hmac, os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
MAC = bytes([2, 0, 0, 0, 0, 1])
KEY = b'0123456789abcdef0123456789abcdef'


def sig(body):
    return hmac.new(KEY, MAC + body, hashlib.sha1).digest()[:8]


report = bytes([2, 1, 1, 80]) + struct.pack('<III', 7, 0, 0) + bytes([1]) + struct.pack('<H', 250) + bytes([1])
info = bytes([3]) + struct.pack('<I', 3600) + bytes([0, 1]) + struct.pack('<HHHI', 2, 120, 13, 0) + bytes([6]) + b'RCWL01' + bytes([5]) + b'1.0.0'
hello = bytes([4, 2]) + bytes(range(32))
answer = bytes([5, 1, 6]) + bytes(range(32)) + bytes(8)
ask = bytes([8]) + struct.pack('<II', 1, 0)
part = bytes([9]) + struct.pack('<III', 1, 100, 0) + bytes(20)
confirmation = bytes([6, 1, 1]) + struct.pack('<IIIHI', 7, 1800000000, 7200, 5, 0) + bytes([0]) + struct.pack('<I', 0)
wsse = ('<s:Envelope><s:Header><Security><UsernameToken><Username>admin</Username><Password>abc=</Password>'
        '<Nonce>' + base64.b64encode(b'0123456789abcdef').decode() + '</Nonce><Created>2026-01-01T00:00:00Z</Created>'
        '</UsernameToken></Security></s:Header><s:Body><Subscribe><ConsumerReference><Address>http://192.168.1.5:80/x</Address>'
        '</ConsumerReference></Subscribe></s:Body></s:Envelope>').encode()
mqtt = bytes([0x30, 22, 0, 18]) + b'hub/armed/set/' + b'1234' + b'ON'
event = struct.pack('<q', 1800000000) + bytes(88)

seeds = {
    0: report, 1: info, 2: hello, 3: answer, 4: ask + sig(ask), 5: bytes([7]) + struct.pack('<I', 9) + report + sig(bytes([7]) + struct.pack('<I', 9) + report),
    6: confirmation, 7: report + sig(report), 8: hello,
    9: b'hold=30\nquiet=1320-420\nzone=interior\ncamera=Camera 3\n', 10: b'late=300\nsilent=600\nevery=3600\nrepeat=5\n',
    11: b'chip=13\nversion=1.0.0\nsensor=RCWL01\nevery=3600\nrestart=1\nrestarts=2\n',
    12: b'1800000000 80\n1800086400 79\n1800172800 78\n', 13: b'public=' + b'ab' * 32 + b'\nkey=' + b'cd' * 32 + b'\n',
    14: event, 15: bytes([0, 0, 0, 1, 0x67, 1, 2, 0, 0, 0, 1, 0x68, 3, 0, 0, 0, 1, 0x65, 4, 5]),
    16: b'AA-BB-CC-DD-EE-01/1/active.h264',
    17: b'GET /api/file?path=AA-BB-CC-DD-EE-01/1&token=abc HTTP/1.1\r\nHost: 192.168.1.20\r\nContent-Length: 12\r\nAuthorization: Basic YWRtaW46cGFzc3dvcmQx',
    18: b'Digest username="admin", realm="SensorCam", nonce="abcdef", uri="rtsp://h/stream", response="0123"',
    19: b'http://192.168.1.10:8123/api/webhook/hub', 20: b'YWRtaW46cGFzc3dvcmQx', 21: b'hub.lan, alarm.lan',
    22: wsse, 23: wsse, 24: mqtt, 25: b'net.hookwd=3', 26: b'AA-BB-CC-DD-EE-01',
    27: struct.pack('<IIII', 0x4B505431, 11, 0x50524F46, 1),
    29: b'late=300\nhw=1,4,3,4,1,1\nsaid.every=3600\nsaid.chip=13\nsaid.version=1.0.0\nsaid.hw=1,4,3,4,1,1\n',
    30: b'name=Front gate\nhold=30\nzone=interior\n',
    31: b'sensorcam-backup sealed 1 32 48\nAAAA\n',
}
# Also the present sealed form for parser 5: 12, the number, the report encrypted, and a 16-byte tag.
extra = {'05b': (5, bytes([12]) + struct.pack('<I', 9) + report + bytes(16)),
         # Node info with its hardware and the hub's number after it.
         '01b': (1, info + bytes([1, 0, 0, 0, 4, 3, 4, 1, 1]) + struct.pack('<I', 0xA1B2C3D4))}
for name, (n, body) in extra.items():
    with open(os.path.join(out, f'seed{name}'), 'wb') as f:
        f.write(bytes([n]) + body)
for n, body in seeds.items():
    with open(os.path.join(out, f'seed{n:02d}'), 'wb') as f:
        f.write(bytes([n]) + body)
print(f'{len(seeds)} seeds in {out}')
