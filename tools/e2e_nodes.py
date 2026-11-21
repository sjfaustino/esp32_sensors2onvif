"""Pairing a node, and what a paired node gets from the hub: sealed messages, settings, the time, a walk test, proof that it is
there, restart and battery reports, and firmware updates."""
from e2e_common import *  # noqa: F401,F403  (the helpers every part shares)
import e2e_common as common


HUB_PAIRING_KEY = 'a8abababababababababababababababababababababababababababababab6b'  # the PC build's, in host_main.cpp


def pairing_tests():
    """A node pairs with public keys: this test plays the node, with real X25519 keys."""
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey

    def raw(key):
        return key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)

    def fingerprint(public):
        h = hashlib.sha1(public).hexdigest().upper()
        return f'{h[0:4]}-{h[4:8]}-{h[8:12]}-{h[12:16]}'

    mac, folder = '020000000055', '02-00-00-00-00-55'
    node = X25519PrivateKey.generate()
    node_public = raw(node.public_key())
    hello = bytes([4, 2]) + node_public

    def say(data, sender=mac):
        """Sends a message as the node, and returns the hub's immediate answer as (to, bytes), or None."""
        control('last-sent')
        control(f'radio-raw {sender} {data.hex()}')
        sent = control('last-sent')
        return (sent.split(' ')[0], bytes.fromhex(sent.split(' ')[1])) if sent else None

    def signed(key, data):
        return hmac.new(key, bytes.fromhex(mac) + data, hashlib.sha1).digest()[:8]

    def report(input_number, counter, key, fresh=0, battery=80):
        body = bytes([2, input_number, 1, battery]) + counter.to_bytes(4, 'little') + bytes(4) + fresh.to_bytes(4, 'little') + bytes([0])
        return body + signed(key, body)

    def node_row(input_number):
        return next((n for n in get_json('/api/espnow')['nodes'] if n['node'] == folder and n['input'] == input_number), {})

    check('a hello while pairing is closed gets no answer', say(hello) is None)
    check('opening pairing needs the web password', api('POST', '/api/pairing/open', 'x=1', auth=False)[0] == 401)
    check('pairing can be opened', api('POST', '/api/pairing/open', 'x=1')[0] == 200)
    answer = say(hello)
    to, data = answer or ('', b'')
    hub_public = raw(X25519PrivateKey.from_private_bytes(bytes.fromhex(HUB_PAIRING_KEY)).public_key())
    check("a hello while pairing is open is answered: waiting, with the channel and the hub's public key",
          to == mac and len(data) == 43 and data[:3] == bytes([5, 0, 6]) and data[3:35] == hub_public and data[35:] == bytes(8), answer)
    page = get_json('/api/pairing')
    waiting = [w for w in page['waiting'] if w['mac'] == folder]
    check("the node waits for approval, shown with its key's fingerprint", waiting and waiting[0]['fingerprint'] == fingerprint(node_public) and waiting[0]['inputs'] == 2, page)
    check("the page shows the hub's fingerprint", page['hub'] == fingerprint(hub_public) and page['open'] > 100, page)
    check('a report before approval is ignored', say(report(1, 1, b'anything')) is None and not node_row(1))
    check('approval needs the web password', api('POST', '/api/pairing/approve', f'mac={folder}', auth=False)[0] == 401)
    # The key approved is the one whose fingerprint was checked: another sent from the node's address meanwhile, as
    # anyone in range could, takes the waiting one's place, and approving the fingerprint that was shown is refused.
    shown = waiting[0]['fingerprint'] if waiting else ''
    stranger = raw(X25519PrivateKey.generate().public_key())
    say(bytes([4, 2]) + stranger)
    check("a stranger's key sent from the node's address is not approved with the fingerprint that was checked",
          api('POST', '/api/pairing/approve', f'mac={folder}&fingerprint={shown}')[0] == 409)
    check('nor without a fingerprint', api('POST', '/api/pairing/approve', f'mac={folder}')[0] == 409)
    say(hello)  # the node's own hello again
    check('the node can be approved, by the fingerprint that was checked', api('POST', '/api/pairing/approve', f'mac={folder}&fingerprint={shown}')[0] == 200)
    tree = get_json('/api/tree')
    added = next((n for n in tree['nodes'] if n['mac'] == folder), {})
    check('approval adds the node and its sensors, paired', added.get('paired') is True and [s['input'] for s in added.get('sensors', [])] == [1, 2], added)
    check('and it leaves the waiting list', not [w for w in get_json('/api/pairing')['waiting'] if w['mac'] == folder])

    key = hmac.new(node.exchange(X25519PublicKey.from_public_bytes(hub_public)), b'sensors2onvif node key' + hub_public + node_public, hashlib.sha1).digest()
    to, data = say(hello) or ('', b'')
    check("once approved, the hello is answered as paired, signed with the node's own key", len(data) == 43 and data[1] == 1 and data[35:] == signed(key, data[:35]), data.hex())

    control('last-sent')
    control(f'radio-raw {mac} {report(1, 7, key).hex()}')
    check("a report signed with the node's own key is accepted", f'{folder}/1' in status()['active'], status())
    sent = wait_until(lambda: control('last-sent'))
    confirmation = bytes.fromhex(sent.split(' ')[1]) if ' ' in sent else b''
    check("and confirmed, signed with the node's own key", sent.startswith(mac + ' ') and len(confirmation) == 34 and confirmation[:7] == bytes([3, 1, 1]) + (7).to_bytes(4, 'little')
          and confirmation[26:] == signed(key, confirmation[:26]), sent)
    hub_time = int.from_bytes(confirmation[7:11], 'little')
    check("the confirmation carries the hub's time, for the node's clock", abs(hub_time - time.time()) < 5, hub_time)
    check('and nothing set for the node yet', confirmation[11:21] == bytes(10), confirmation.hex())
    check("a paired node's first report, without the hub's number, is asked to prove the node is there", confirmation[21] == 2
          and int.from_bytes(confirmation[22:26], 'little') != 0, confirmation.hex())
    control('end-alerts')
    control(f'radio-raw {mac} {report(2, 8, b"test-espnow-key-0123456789").hex()}')
    check("a paired node's report signed with the shared key is refused", node_row(2).get('refusal') == 'wrong signature', node_row(2))
    check('the Nodes and pairing page shows the node has its own key', node_row(1).get('paired') is True)

    def info(every, saving, sensor, version, key=key, restart=1, restarts=0, response=0xFFFF, chip=13, refused=0):
        body = (bytes([6]) + every.to_bytes(4, 'little') + bytes([saving, restart]) + restarts.to_bytes(2, 'little') + response.to_bytes(2, 'little')
                + chip.to_bytes(2, 'little') + refused.to_bytes(4, 'little') + bytes([len(sensor)]) + sensor + bytes([len(version)]) + version)
        return body + signed(key, body)

    def tree_node():
        return next((n for n in get_json('/api/tree')['nodes'] if n['mac'] == folder), {})

    check('before it says how often it reports, the node is silent after the default', node_row(1).get('silentAfter') == 3 * 3600, node_row(1))
    say(info(7200, 0, b'RCWL-0516', b'3f2a1b0'))
    row = node_row(1)
    check("the node's info shows on the Nodes and pairing page, for each of its sensors",
          (row.get('every'), row.get('sensor'), row.get('version'), row.get('saving')) == (7200, 'RCWL-0516', '3f2a1b0', False) and node_row(2).get('sensor') == 'RCWL-0516', row)
    check('its silent time follows from how often it reports', row.get('silentAfter') == 7200 + 1800, row)
    check('the Sensors page shows both', (tree_node().get('every'), tree_node().get('autoSilent')) == (7200, 9000), tree_node())
    check('it is kept in the node folder', api('GET', f'/api/file?path={folder}/about.txt')
          == (200, b'every=7200\nsaving=0\nrestart=1\nrestarts=0\nchip=13\nrefused=0\nsensor=RCWL-0516\nversion=3f2a1b0\n'))
    check('with its chip and why it last started', (row.get('chip'), row.get('restart'), row.get('restarts'), row.get('restartFault')) == ('ESP32-C6', 'power on', 0, False), row)
    say(info(6 * 3600, 1, b'RCWL-0516', b'3f2a1b0'))
    check('a node saving its battery reports less often, and the silent time follows', (node_row(1).get('saving'), node_row(1).get('silentAfter')) == (True, 6 * 3600 + 5400), node_row(1))
    api('POST', f'/api/node-settings?path={folder}', 'silent=600')
    check("the node's own setting comes first", node_row(1).get('silentAfter') == 600 and tree_node().get('autoSilent') == 6 * 3600 + 5400, node_row(1))
    api('POST', f'/api/node-settings?path={folder}', 'silent=')
    check('and emptied, the node is followed again', node_row(1).get('silentAfter') == 6 * 3600 + 5400, node_row(1))
    say(info(60, 0, b'forged', b'x', b'test-espnow-key-0123456789'))
    check('info signed with the wrong key is refused', node_row(1).get('sensor') == 'RCWL-0516' and node_row(1).get('refusal') == 'wrong signature', node_row(1))
    say(info(60, 0, b'bad\nline', b'x'))
    check('info with a line break in its text is ignored', node_row(1).get('sensor') == 'RCWL-0516', node_row(1))
    say(info(7200, 0, b'RCWL-0516', b'3f2a1b0'))
    node_tests(mac, folder, key, say, signed, report, info, node_row, tree_node)

    other = X25519PrivateKey.generate()
    api('POST', '/api/pairing/close', 'x=1')
    check('a hello with a different key outside pairing gets no answer', say(bytes([4, 2]) + raw(other.public_key())) is None)
    api('POST', '/api/pairing/open', 'x=1')
    say(bytes([4, 1]) + raw(other.public_key()), '020000000056')
    check('another node can be rejected', api('POST', '/api/pairing/reject', 'mac=02-00-00-00-00-56')[0] == 200
          and not get_json('/api/pairing')['waiting'])
    check('approving a node that is not waiting is refused', api('POST', '/api/pairing/approve', 'mac=02-00-00-00-00-56')[0] == 409)
    api('POST', '/api/pairing/close', 'x=1')
    api('DELETE', f'/api/file?path={folder}')


def open_sealed(data, key, mac, from_hub):
    """A sealed message, as docs/protocol.md describes it, opened: the message inside, or None for a wrong signature."""
    if data[0] != 7 or hmac.new(key, bytes.fromhex(mac) + data[:-8], hashlib.sha1).digest()[:8] != data[-8:]:
        return None
    seal_key = hmac.new(key, b'sensors2onvif seal', hashlib.sha1).digest()
    inner, stream = data[5:-8], b''
    for block in range((len(inner) + 19) // 20):
        stream += hmac.new(seal_key, bytes([1 if from_hub else 0]) + data[1:5] + bytes([block]), hashlib.sha1).digest()
    return bytes(a ^ b for a, b in zip(inner, stream))


def seal(inner, key, mac, number, from_hub=False):
    seal_key = hmac.new(key, b'sensors2onvif seal', hashlib.sha1).digest()
    stream = b''
    for block in range((len(inner) + 19) // 20):
        stream += hmac.new(seal_key, bytes([1 if from_hub else 0]) + number.to_bytes(4, 'little') + bytes([block]), hashlib.sha1).digest()
    body = bytes([7]) + number.to_bytes(4, 'little') + bytes(a ^ b for a, b in zip(inner, stream))
    return body + hmac.new(key, bytes.fromhex(mac) + body, hashlib.sha1).digest()[:8]


def node_tests(mac, folder, key, say, signed, report, info, node_row, tree_node):
    """What a paired node gets from the hub besides confirmations: sealed messages, settings, a walk test, the time,
    restart and battery reports, and firmware updates."""
    def confirmation_of(answer, sealed=False):
        data = answer[1] if answer else b''
        if sealed:
            data = open_sealed(data, key, mac, True) or b''
        elif data[26:] != signed(key, data[:26]):
            return b''
        return data

    fresh = [0]

    def confirmed(counter, battery=80):
        """Sends a report as the node does, with the number from the hub's last confirmation, and keeps the next one."""
        control('end-alerts')
        body = confirmation_of(say(report(1, counter, key, fresh[0], battery)))
        if len(body) >= 26:
            fresh[0] = int.from_bytes(body[22:26], 'little')
        return body

    # Playing back a recorded report: it still raises an alert, but does not show the node is there.
    first = confirmed(15)
    check('a report without the number is asked for proof', first[21] & 2 == 2, first.hex())
    recorded = report(1, 16, key, fresh[0], 33)
    control('end-alerts')
    body = confirmation_of(say(recorded))
    fresh[0] = int.from_bytes(body[22:26], 'little')
    check('a report with the number is taken as the node being there, and asks for nothing more', body[21] & 2 == 0 and node_row(1).get('battery') == 33, (body.hex(), node_row(1)))
    confirmed(17)
    confirmed(18)
    control('end-alerts')
    body = confirmation_of(say(recorded))
    check('the same report played back later raises its alert, as a false alert is better than a missed one', f'{folder}/1' in status()['active'], status())
    check('but it is asked for proof, and its battery reading is not taken', body[21] & 2 == 2 and node_row(1).get('battery') == 80, (body.hex(), node_row(1)))
    confirmed(19)

    # Sealed: what a report says is hidden, and so is the confirmation, which repeats it.
    plain = report(1, 21, key, fresh[0])[:-8]
    sealed = seal(plain, key, mac, 0xABCD0001)
    check('a sealed report hides the input, state and battery', plain[1:4] not in sealed[5:-8])
    control('end-alerts')
    answer = say(sealed)
    body = confirmation_of(answer, True)
    fresh[0] = int.from_bytes(body[22:26], 'little') if len(body) == 26 else 0
    check('a sealed report is accepted, and confirmed sealed', f'{folder}/1' in status()['active'] and body[:7] == bytes([3, 1, 1]) + (21).to_bytes(4, 'little'), (answer, body))
    check('the Nodes and pairing page shows it was sealed', node_row(1).get('sealed') is True, node_row(1))
    forged = bytearray(seal(plain, key, mac, 0xABCD0002))
    forged[7] ^= 1
    control('end-alerts')
    check('a sealed report that was changed on the way is refused', say(bytes(forged)) is None and node_row(1).get('refusal') == 'wrong signature', node_row(1))
    check('a sealed report signed with the shared key is refused', say(seal(plain[:4] + (22).to_bytes(4, 'little') + plain[8:], b'test-espnow-key-0123456789', mac, 3)) is None)
    hello = bytes([4, 1]) + bytes(32)
    check('only a report or node info is taken sealed', say(seal(hello, key, mac, 4)) is None)
    say(seal(info(7200, 0, b'RCWL-0516', b'3f2a1b0', response=240)[:-8], key, mac, 5))
    check('sealed node info is read too, with how long the last alert took', node_row(1).get('responseMs') == 240, node_row(1))

    # Settings for the node, which reach it with each confirmation.
    check('the report and repeat times can be set for the node', api('POST', f'/api/node-settings?path={folder}', 'every=3600&repeat=30')[0] == 200)
    check('a report time below a minute is refused', api('POST', f'/api/node-settings?path={folder}', 'every=30')[0] == 400)
    node = tree_node()
    check('the Sensors page shows them', (node.get('reportEvery'), node.get('repeat')) == (3600, 30), node)
    control('end-alerts')
    body = confirmed(23)
    check('they reach the node with its next confirmation', int.from_bytes(body[11:15], 'little') == 3600, body.hex())
    check("the repeat is kept below the sensor's alert time, so the alert does not lapse between repeats", int.from_bytes(body[15:17], 'little') == HOLD_MS // 1000 - 1, body.hex())
    api('POST', f'/api/node-settings?path={folder}', '')

    # A walk test, which the node learns of with the confirmation of its next alert.
    check('a walk test can be started for a node', api('POST', f'/api/walk-test?path={folder}', 'go=1')[0] == 200)
    check('a walk test for a node that does not exist is refused', api('POST', '/api/walk-test?path=02-00-00-00-00-77', 'go=1')[0] == 404)
    control('end-alerts')
    body = confirmed(24)
    until, now_hub = int.from_bytes(body[17:21], 'little'), int.from_bytes(body[7:11], 'little')
    check('the confirmation says when the walk test ends, on the hub\'s time', 590 <= until - now_hub <= 600, (until, now_hub))
    check('the Sensors page shows it running', 0 < tree_node().get('walkLeft', 0) <= 600, tree_node())
    api('POST', f'/api/walk-test?path={folder}', 'stop=1')
    control('end-alerts')
    check('and it can be stopped', confirmed(25)[17:21] == bytes(4))

    # A restart for a fault shows a node failing before it goes silent.
    say(info(7200, 0, b'RCWL-0516', b'3f2a1b0', restart=6, restarts=1))
    row = node_row(1)
    check('a node that restarted by a brownout is shown so', (row.get('restart'), row.get('restarts'), row.get('restartFault')) == ('brownout', 1, True), row)
    check('and logged', any(e['event'] == 'node restarted by a fault' and e['sensor'] == f'{folder}/1' for e in event_list()), event_list()[:3])
    say(info(7200, 0, b'RCWL-0516', b'3f2a1b0', restart=6, restarts=1))
    check('the same restart is logged once', sum(e['event'] == 'node restarted by a fault' for e in event_list()) == 1)

    # The battery: a reading is kept every few hours, for the forecast.
    battery_file = api('GET', f'/api/file?path={folder}/battery.txt')
    check('a battery reading is kept in the node folder, for the forecast', battery_file[0] == 200 and battery_file[1].decode().split()[-1] == '33', battery_file)  # the first, from the report with the number above
    check('one reading is not enough for a forecast', node_row(1).get('daysLeft') is None, node_row(1))

    # Firmware for the node: put on the hub, offered to nodes of that chip on another version, fetched in parts.
    image = bytearray(os.urandom(3000))
    image[0], image[12], image[13] = 0xE9, 13, 0
    mark = b'sensors2onvif-node-version=4b5c6d7\0'
    image[1500:1500 + len(mark)] = mark
    image = bytes(image)
    check('a file that is not a node firmware is refused', api('PUT', '/api/node-firmware', b'\xe9' + bytes(2999))[0] == 400)
    check('node firmware needs the web password', api('PUT', '/api/node-firmware', image, auth=False)[0] == 401)
    status_code, reply = api('PUT', '/api/node-firmware', image)
    check('a node firmware can be put on the hub', status_code == 200 and json.loads(reply) == {'ok': True, 'chip': 'ESP32-C6', 'version': '4b5c6d7'}, reply)
    listed = get_json('/api/espnow')['nodeFirmware']
    check('the Nodes and pairing page lists it', listed == [{'chip': 13, 'name': 'ESP32-C6', 'version': '4b5c6d7', 'size': 3000}], listed)
    control('end-alerts')
    body = confirmed(26)
    check('a node of that chip on another version is told an update is ready', body[21:22] == b'\x01', body.hex())
    check('and the Nodes and pairing page says so', node_row(1).get('update') == 'update ready', node_row(1))

    def ask(number, offset):
        data = bytes([8]) + number.to_bytes(4, 'little') + offset.to_bytes(4, 'little')
        control('all-sent')
        control(f'radio-raw {mac} {(data + signed(key, data)).hex()}')
        sent = control('all-sent')
        parts = [bytes.fromhex(m.split(' ')[1]) for m in sent.split(';')] if sent else []
        return [p for p in parts if p[0] == 9 and p[-8:] == signed(key, p[:-8])]

    described = ask(0, 0xFFFFFFFF)
    number = int.from_bytes(described[0][1:5], 'little') if described else 0
    check('asked, the hub describes the update: its number, size and version', len(described) == 1 and described[0][5:13] == (3000).to_bytes(4, 'little') + bytes([0xFF] * 4)
          and described[0][13:-8] == b'4b5c6d7', described)
    fetched = b''
    while len(fetched) < len(image):
        parts = ask(number, len(fetched))
        if not parts:
            break
        for p in parts:
            if int.from_bytes(p[9:13], 'little') == len(fetched):
                fetched += p[13:-8]
    check('the node fetches it in signed parts, four at a time', fetched == image, (len(fetched), len(image)))
    check('the Nodes and pairing page shows the update going', node_row(1).get('update', '').startswith('updating'), node_row(1))
    check('asking for another image gets this one described', len(ask(number + 1, 0)) == 1)
    forged_ask = bytes([8]) + number.to_bytes(4, 'little') + bytes(4)
    control('all-sent')
    control(f'radio-raw {mac} {(forged_ask + bytes(8)).hex()}')
    check('an unsigned ask gets nothing', control('all-sent') == '')
    say(info(7200, 0, b'RCWL-0516', b'4b5c6d7'))
    check('once the node runs it, the update is over', node_row(1).get('update') == '', node_row(1))
    control('end-alerts')
    check('and no longer offered', confirmed(27)[21:22] == b'\x00')
    say(info(7200, 0, b'RCWL-0516', b'3f2a1b0', refused=number))
    control('end-alerts')
    check('a node that had the update undone is not offered it again', confirmed(28)[21:22] == b'\x00')
    check('and the page says it was undone', node_row(1).get('update') == 'update undone', node_row(1))
    say(info(7200, 1, b'RCWL-0516', b'3f2a1b0'))
    control('end-alerts')
    check('a node saving its battery is not offered an update', confirmed(29)[21:22] == b'\x00')
    say(info(7200, 0, b'RCWL-0516', b'3f2a1b0', chip=0))
    control('end-alerts')
    check('nor is a node of another chip', confirmed(30)[21:22] == b'\x00')
    check('a node firmware can be deleted', api('POST', '/api/node-firmware/delete', 'chip=13')[0] == 200 and get_json('/api/espnow')['nodeFirmware'] == [])
    control('end-alerts')
