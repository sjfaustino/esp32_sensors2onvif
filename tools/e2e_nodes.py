"""Pairing a node, and what a paired node gets from the hub: sealed messages, settings, the time, a walk test, proof that it is
there, restart and battery reports, firmware updates and its hardware. Each test pairs a node of its own."""
from e2e_common import *  # noqa: F401,F403  (the helpers every part shares)
import e2e_common as common
from s2o_seal import open_sealed, seal, seal_old  # made here independently of the firmware

HUB_PAIRING_KEY = 'a8abababababababababababababababababababababababababababababab6b'  # the PC build's, in host_main.cpp
SHARED_KEY = b'test-espnow-key-0123456789'  # the PC build's key for nodes that are not paired


def raw(key):
    from cryptography.hazmat.primitives import serialization
    return key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def fingerprint(public):
    h = hashlib.sha1(public).hexdigest().upper()
    return f'{h[0:4]}-{h[4:8]}-{h[8:12]}-{h[12:16]}'


def hub_public():
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
    return raw(X25519PrivateKey.from_private_bytes(bytes.fromhex(HUB_PAIRING_KEY)).public_key())


def hardware(number, sensor, sensor_pin, tamper_pin, battery_pin, cell):
    return number.to_bytes(4, 'little') + bytes([sensor, sensor_pin, tamper_pin, battery_pin, cell])


_next_node = [0x60]


class PairedNode:
    """A node of the test's own, played here with real X25519 keys: paired, approved, and saying what it is, as a node
    does when it first starts. It numbers its own reports and keeps the number the hub last asked it to send back, so a
    test never counts for it. close() deletes its folder."""

    def __init__(self, inputs=2):
        from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
        n = _next_node[0]
        _next_node[0] += 1
        self.mac, self.folder = f'0200000000{n:02x}', f'02-00-00-00-00-{n:02X}'
        private = X25519PrivateKey.generate()
        self.public = raw(private.public_key())
        self.hello = bytes([4, inputs]) + self.public
        api('POST', '/api/pairing/open', 'x=1')
        self.say(self.hello)
        shown = next((w['fingerprint'] for w in get_json('/api/pairing')['waiting'] if w['mac'] == self.folder), '')
        approved = api('POST', '/api/pairing/approve', f'mac={self.folder}&fingerprint={shown}')[0]
        api('POST', '/api/pairing/close', 'x=1')
        if approved != 200:
            raise ValueError(f'the test node {self.folder} could not be paired: HTTP {approved}')
        theirs = hub_public()
        self.key = hmac.new(private.exchange(X25519PublicKey.from_public_bytes(theirs)), b'sensors2onvif node key' + theirs + self.public,
                            hashlib.sha1).digest()
        self.counter = 0
        self.fresh = 0  # the number from the hub's last confirmation, which proves the next report is not played back
        self.say(self.info())

    def close(self):
        api('DELETE', f'/api/file?path={self.folder}')

    def say(self, data, sender=None):
        """Sends a message as the node, and returns the hub's immediate answer as (to, bytes), or None."""
        control('last-sent')
        control(f'radio-raw {sender or self.mac} {data.hex()}')
        sent = control('last-sent')
        return (sent.split(' ')[0], bytes.fromhex(sent.split(' ')[1])) if sent else None

    def said_all(self, data):
        """Sends a message, and returns every message the hub sent for it."""
        control('all-sent')
        control(f'radio-raw {self.mac} {data.hex()}')
        sent = control('all-sent')
        return [bytes.fromhex(m.split(' ')[1]) for m in sent.split(';')] if sent else []

    def sign(self, data, key=None):
        return hmac.new(key or self.key, bytes.fromhex(self.mac) + data, hashlib.sha1).digest()[:8]

    def report(self, input_number=1, battery=80, key=None, fresh=None):
        """A report, numbered one past the last, carrying the hub's number unless told otherwise; signed."""
        self.counter += 1
        body = (bytes([2, input_number, 1, battery]) + self.counter.to_bytes(4, 'little') + bytes(4)
                + (self.fresh if fresh is None else fresh).to_bytes(4, 'little') + bytes([0]))
        return body + self.sign(body, key)

    def info(self, every=7200, saving=0, sensor=b'RCWL-0516', version=b'3f2a1b0', key=None, restart=1, restarts=0, response=0xFFFF,
             chip=13, refused=0, hw=b''):
        body = (bytes([6]) + every.to_bytes(4, 'little') + bytes([saving, restart]) + restarts.to_bytes(2, 'little') + response.to_bytes(2, 'little')
                + chip.to_bytes(2, 'little') + refused.to_bytes(4, 'little') + bytes([len(sensor)]) + sensor + bytes([len(version)]) + version + hw)
        return body + self.sign(body, key)

    def confirmation_of(self, answer, sealed=False):
        """The hub's confirmation in an answer, checked as the node checks it; empty if it is not one."""
        data = answer[1] if answer else b''
        if sealed:
            return open_sealed(data, self.key, self.mac, True) or b''
        return data if len(data) == 34 and data[26:] == self.sign(data[:26]) else b''

    def keep_fresh(self, body):
        if len(body) >= 26:
            self.fresh = int.from_bytes(body[22:26], 'little')

    def confirmed(self, battery=80):
        """Sends a report as the node does, and returns the confirmation, keeping its number for the next."""
        control('end-alerts')
        body = self.confirmation_of(self.say(self.report(1, battery)))
        self.keep_fresh(body)
        control('end-alerts')
        return body

    def row(self, input_number=1):
        return next((n for n in get_json('/api/espnow')['nodes'] if n['node'] == self.folder and n['input'] == input_number), {})

    def tree(self):
        return next((n for n in get_json('/api/tree')['nodes'] if n['mac'] == self.folder), {})


def with_node(test):
    """The test, given a node of its own, which is deleted afterwards whatever happens."""
    def run(s):
        node = PairedNode()
        try:
            test(s, node)
        finally:
            node.close()
    run.__name__ = test.__name__
    return run


@case(pc=True)
def pairing(s):
    """A node pairs with public keys: this test plays the node, with real X25519 keys."""
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
    mac, folder = '020000000055', '02-00-00-00-00-55'
    private = X25519PrivateKey.generate()
    node_public = raw(private.public_key())
    hello = bytes([4, 2]) + node_public

    def say(data, sender=mac):
        control('last-sent')
        control(f'radio-raw {sender} {data.hex()}')
        sent = control('last-sent')
        return (sent.split(' ')[0], bytes.fromhex(sent.split(' ')[1])) if sent else None

    def signed(key, data):
        return hmac.new(key, bytes.fromhex(mac) + data, hashlib.sha1).digest()[:8]

    def report(input_number, counter, key):
        body = bytes([2, input_number, 1, 80]) + counter.to_bytes(4, 'little') + bytes(9)
        return body + signed(key, body)

    def row(input_number):
        return next((n for n in get_json('/api/espnow')['nodes'] if n['node'] == folder and n['input'] == input_number), {})

    try:
        check('a hello while pairing is closed gets no answer', say(hello) is None)
        check('opening pairing needs the web password', api('POST', '/api/pairing/open', 'x=1', auth=False)[0] == 401)
        check('pairing can be opened', api('POST', '/api/pairing/open', 'x=1')[0] == 200)
        answer = say(hello)
        to, data = answer or ('', b'')
        theirs = hub_public()
        check("a hello while pairing is open is answered: waiting, with the channel and the hub's public key",
              to == mac and len(data) == 43 and data[:3] == bytes([5, 0, 6]) and data[3:35] == theirs and data[35:] == bytes(8), answer)
        page = get_json('/api/pairing')
        waiting = [w for w in page['waiting'] if w['mac'] == folder]
        check("the node waits for approval, shown with its key's fingerprint", waiting and waiting[0]['fingerprint'] == fingerprint(node_public) and waiting[0]['inputs'] == 2, page)
        check("the page shows the hub's fingerprint", page['hub'] == fingerprint(theirs) and page['open'] > 100, page)
        check('a report before approval is ignored', say(report(1, 1, b'anything')) is None and not row(1))
        check('approval needs the web password', api('POST', '/api/pairing/approve', f'mac={folder}', auth=False)[0] == 401)
        # The key approved is the one whose fingerprint was checked: another sent from the node's address meanwhile, as
        # anyone in range could, takes the waiting one's place, and approving the fingerprint that was shown is refused.
        shown = waiting[0]['fingerprint'] if waiting else ''
        say(bytes([4, 2]) + raw(X25519PrivateKey.generate().public_key()))
        check("a stranger's key sent from the node's address is not approved with the fingerprint that was checked",
              api('POST', '/api/pairing/approve', f'mac={folder}&fingerprint={shown}')[0] == 409)
        check('nor without a fingerprint', api('POST', '/api/pairing/approve', f'mac={folder}')[0] == 409)
        say(hello)  # the node's own hello again
        check('the node can be approved, by the fingerprint that was checked', api('POST', '/api/pairing/approve', f'mac={folder}&fingerprint={shown}')[0] == 200)
        added = next((n for n in get_json('/api/tree')['nodes'] if n['mac'] == folder), {})
        check('approval adds the node and its sensors, paired', added.get('paired') is True and [x['input'] for x in added.get('sensors', [])] == [1, 2], added)
        check('and it leaves the waiting list', not [w for w in get_json('/api/pairing')['waiting'] if w['mac'] == folder])

        key = hmac.new(private.exchange(X25519PublicKey.from_public_bytes(theirs)), b'sensors2onvif node key' + theirs + node_public, hashlib.sha1).digest()
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
        control(f'radio-raw {mac} {report(2, 8, SHARED_KEY).hex()}')
        check("a paired node's report signed with the shared key is refused", row(2).get('refusal') == 'wrong signature', row(2))
        check('the Nodes and pairing page shows the node has its own key', row(1).get('paired') is True)

        other = raw(X25519PrivateKey.generate().public_key())
        api('POST', '/api/pairing/close', 'x=1')
        check('a hello with a different key outside pairing gets no answer', say(bytes([4, 2]) + other) is None)
        api('POST', '/api/pairing/open', 'x=1')
        say(bytes([4, 1]) + other, '020000000056')
        check('another node can be rejected', api('POST', '/api/pairing/reject', 'mac=02-00-00-00-00-56')[0] == 200
              and not get_json('/api/pairing')['waiting'])
        check('approving a node that is not waiting is refused', api('POST', '/api/pairing/approve', 'mac=02-00-00-00-00-56')[0] == 409)
    finally:
        api('POST', '/api/pairing/close', 'x=1')
        api('DELETE', f'/api/file?path={folder}')


@case(pc=True)
@with_node
def node_info(s, node):
    """What a node says of itself: how often it reports, its sensor, firmware, chip and why it last started."""
    row = node.row()  # what it said when it started
    check("the node's info shows on the Nodes and pairing page, for each of its sensors",
          (row.get('every'), row.get('sensor'), row.get('version'), row.get('saving')) == (7200, 'RCWL-0516', '3f2a1b0', False) and node.row(2).get('sensor') == 'RCWL-0516', row)
    check('its silent time follows from how often it reports', row.get('silentAfter') == 7200 + 1800, row)
    check('the Sensors page shows both', (node.tree().get('every'), node.tree().get('autoSilent')) == (7200, 9000), node.tree())
    check('it is kept in the node folder', api('GET', f'/api/file?path={node.folder}/node.txt')
          == (200, b'said.every=7200\nsaid.saving=0\nsaid.restart=1\nsaid.restarts=0\nsaid.chip=13\nsaid.refused=0\nsaid.sensor=RCWL-0516\nsaid.version=3f2a1b0\n'))
    check('with its chip and why it last started', (row.get('chip'), row.get('restart'), row.get('restarts'), row.get('restartFault')) == ('ESP32-C6', 'power on', 0, False), row)
    node.say(node.info(6 * 3600, 1))
    check('a node saving its battery reports less often, and the silent time follows', (node.row().get('saving'), node.row().get('silentAfter')) == (True, 6 * 3600 + 5400), node.row())
    api('POST', f'/api/node-settings?path={node.folder}', 'silent=600')
    check("the node's own setting comes first", node.row().get('silentAfter') == 600 and node.tree().get('autoSilent') == 6 * 3600 + 5400, node.row())
    api('POST', f'/api/node-settings?path={node.folder}', 'silent=')
    check('and emptied, the node is followed again', node.row().get('silentAfter') == 6 * 3600 + 5400, node.row())
    node.say(node.info(60, 0, b'forged', b'x', SHARED_KEY))
    check('info signed with the wrong key is refused', node.row().get('sensor') == 'RCWL-0516' and node.row().get('refusal') == 'wrong signature', node.row())
    node.say(node.info(60, 0, b'bad\nline', b'x'))
    check('info with a line break in its text is ignored', node.row().get('sensor') == 'RCWL-0516', node.row())


@case(pc=True)
def a_node_that_has_said_nothing(s):
    """Before a node says how often it reports, the hub takes it as silent after its default of 3 hours."""
    node = PairedNode.__new__(PairedNode)
    # Paired by hand, without the info a node sends when it starts.
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
    n = _next_node[0]
    _next_node[0] += 1
    node.mac, node.folder = f'0200000000{n:02x}', f'02-00-00-00-00-{n:02X}'
    node.public = raw(X25519PrivateKey.generate().public_key())
    try:
        api('POST', '/api/pairing/open', 'x=1')
        node.say(bytes([4, 1]) + node.public)
        shown = next((w['fingerprint'] for w in get_json('/api/pairing')['waiting'] if w['mac'] == node.folder), '')
        api('POST', '/api/pairing/approve', f'mac={node.folder}&fingerprint={shown}')
        check('before it says how often it reports, the node is silent after the default', node.row().get('silentAfter') == 3 * 3600, node.row())
    finally:
        api('POST', '/api/pairing/close', 'x=1')
        node.close()


@case(pc=True)
@with_node
def a_report_played_back(s, node):
    """A recorded report played back still raises its alert, as a false alert is better than a missed one, but does not
    show the node is there, and its battery reading is not taken."""
    first = node.confirmed()
    check('a report without the number is asked for proof', first[21] & 2 == 2, first.hex())
    recorded = node.report(1, 33)
    control('end-alerts')
    body = node.confirmation_of(node.say(recorded))
    node.keep_fresh(body)
    check('a report with the number is taken as the node being there, and asks for nothing more', body[21] & 2 == 0 and node.row().get('battery') == 33, (body.hex(), node.row()))
    node.confirmed()
    node.confirmed()
    control('end-alerts')
    body = node.confirmation_of(node.say(recorded))
    check('the same report played back later raises its alert', f'{node.folder}/1' in status()['active'], status())
    check('but it is asked for proof, and its battery reading is not taken', body[21] & 2 == 2 and node.row().get('battery') == 80, (body.hex(), node.row()))
    battery = api('GET', f'/api/file?path={node.folder}/battery.txt')
    check('the reading that was taken is kept in the node folder, for the forecast', battery[0] == 200 and battery[1].decode().split()[-1] == '33', battery)
    check('one reading is not enough for a forecast', node.row().get('daysLeft') is None, node.row())


@case(pc=True)
@with_node
def sealed_messages(s, node):
    """Sealed, what a report says is hidden, and so is the confirmation, which repeats it."""
    plain = node.report()[:-8]
    sealed = seal(plain, node.key, node.mac, 0xABCD0001)
    check('a sealed report hides the input, state and battery', plain[1:4] not in sealed[5:-8])
    control('end-alerts')
    answer = node.say(sealed)
    body = node.confirmation_of(answer, True)
    node.keep_fresh(body)
    check('a sealed report is accepted, and confirmed sealed', f'{node.folder}/1' in status()['active'] and body[:7] == bytes([3, 1, 1]) + plain[4:8], (answer, body))
    check('the Nodes and pairing page shows it was sealed', node.row().get('sealed') is True, node.row())
    check('a confirmation to a node sealing in the present form is in that form', answer and answer[1][:1] == bytes([12]), answer)
    forged = bytearray(seal(plain, node.key, node.mac, 0xABCD0002))
    forged[7] ^= 1
    control('end-alerts')
    check('a sealed report that was changed on the way is refused', node.say(bytes(forged)) is None and node.row().get('refusal') == 'wrong signature', node.row())
    check('a sealed report signed with the shared key is refused', node.say(seal(node.report()[:-8], SHARED_KEY, node.mac, 3)) is None)
    check('only a report or node info is taken sealed', node.say(seal(bytes([4, 1]) + bytes(32), node.key, node.mac, 4)) is None)
    node.say(seal(node.info(response=240)[:-8], node.key, node.mac, 5))
    check('sealed node info is read too, with how long the last alert took', node.row().get('responseMs') == 240, node.row())
    # A node on firmware from before the present form seals the old way: still heard, and answered the same way, so it
    # can read the answer.
    control('end-alerts')
    plain = node.report()[:-8]
    answer = node.say(seal_old(plain, node.key, node.mac, 0xABCD0010))
    body = (open_sealed(answer[1], node.key, node.mac, True) or b'') if answer else b''
    check('a node sealing the old way is still heard, and answered the old way', answer and answer[1][:1] == bytes([7])
          and body[:7] == bytes([3, 1, 1]) + plain[4:8], (answer, body))


@case(pc=True)
@with_node
def node_settings_reach_the_node(s, node):
    """The report and repeat times set for a node reach it with each confirmation."""
    check('the report and repeat times can be set for the node', api('POST', f'/api/node-settings?path={node.folder}', 'every=3600&repeat=30')[0] == 200)
    check('a report time below a minute is refused', api('POST', f'/api/node-settings?path={node.folder}', 'every=30')[0] == 400)
    check('the Sensors page shows them', (node.tree().get('reportEvery'), node.tree().get('repeat')) == (3600, 30), node.tree())
    body = node.confirmed()
    check('they reach the node with its next confirmation', int.from_bytes(body[11:15], 'little') == 3600, body.hex())
    check("the repeat is kept below the sensor's alert time, so the alert does not lapse between repeats", int.from_bytes(body[15:17], 'little') == HOLD_MS // 1000 - 1, body.hex())


@case(pc=True)
@with_node
def node_walk_test(s, node):
    """A walk test, which the node learns of with the confirmation of its next alert."""
    check('a walk test can be started for a node', api('POST', f'/api/walk-test?path={node.folder}', 'go=1')[0] == 200)
    check('a walk test for a node that does not exist is refused', api('POST', '/api/walk-test?path=02-00-00-00-00-77', 'go=1')[0] == 404)
    body = node.confirmed()
    until, now_hub = int.from_bytes(body[17:21], 'little'), int.from_bytes(body[7:11], 'little')
    check("the confirmation says when the walk test ends, on the hub's time", 590 <= until - now_hub <= 600, (until, now_hub))
    check('the Sensors page shows it running', 0 < node.tree().get('walkLeft', 0) <= 600, node.tree())
    api('POST', f'/api/walk-test?path={node.folder}', 'stop=1')
    check('and it can be stopped', node.confirmed()[17:21] == bytes(4))


@case(pc=True)
@with_node
def node_restarts(s, node):
    """A restart for a fault shows a node failing before it goes silent."""
    node.say(node.info(restart=6, restarts=1))
    row = node.row()
    check('a node that restarted by a brownout is shown so', (row.get('restart'), row.get('restarts'), row.get('restartFault')) == ('brownout', 1, True), row)

    def logged():
        return sum(e['event'] == 'node restarted by a fault' and e['sensor'] == f'{node.folder}/1' for e in event_list())
    check('and logged', logged() == 1, event_list()[:3])
    node.say(node.info(restart=6, restarts=1))
    check('the same restart is logged once', logged() == 1)


def node_firmware_image():
    image = bytearray(os.urandom(3000))
    image[0], image[12], image[13] = 0xE9, 13, 0
    mark = b'sensors2onvif-node-version=4b5c6d7\0'
    image[1500:1500 + len(mark)] = mark
    return bytes(image)


@case(pc=True)
@with_node
def node_firmware(s, node):
    """Firmware for the node: put on the hub, offered to nodes of that chip on another version, fetched in parts."""
    raw_image = node_firmware_image()
    image = signed_node(raw_image)  # as the node project signs it: the hub keeps it whole, signature and all, for the nodes
    try:
        check('a file that is not a node firmware is refused', api('PUT', '/api/node-firmware', signed_node(b'\xe9' + bytes(2999)))[0] == 400)
        code, reply = api('PUT', '/api/node-firmware', raw_image)
        check('a node firmware without a signature is refused', code == 400 and b'not signed' in reply, (code, reply[:80]))
        code, reply = api('PUT', '/api/node-firmware', common.signed(raw_image))
        check("one signed as the hub's own firmware is refused too", code == 400 and b'not signed' in reply, (code, reply[:80]))
        changed = bytearray(image)
        changed[1600] ^= 1
        check('and one changed after signing', api('PUT', '/api/node-firmware', bytes(changed))[0] == 400)
        check('node firmware needs the web password', api('PUT', '/api/node-firmware', image, auth=False)[0] == 401)
        code, reply = api('PUT', '/api/node-firmware', image)
        check('a node firmware can be put on the hub', code == 200 and json.loads(reply) == {'ok': True, 'chip': 'ESP32-C6', 'version': '4b5c6d7'}, reply)
        listed = get_json('/api/espnow')['nodeFirmware']
        check('the Nodes and pairing page lists it', [{k: f[k] for k in ('chip', 'name', 'version', 'size', 'pilot')} for f in listed]
              == [{'chip': 13, 'name': 'ESP32-C6', 'version': '4b5c6d7', 'size': len(image), 'pilot': ''}], listed)
        body = node.confirmed()
        check('a node of that chip on another version is told an update is ready', body[21] & 1 == 1, body.hex())
        check('and the Nodes and pairing page says so', node.row().get('update') == 'update ready', node.row())

        def ask(number, offset, signed=True):
            data = bytes([8]) + number.to_bytes(4, 'little') + offset.to_bytes(4, 'little')
            parts = node.said_all(data + (node.sign(data) if signed else bytes(8)))
            return [p for p in parts if p[0] == 9 and p[-8:] == node.sign(p[:-8])]

        described = ask(0, 0xFFFFFFFF)
        number = int.from_bytes(described[0][1:5], 'little') if described else 0
        check('asked, the hub describes the update: its number, size and version', len(described) == 1 and described[0][5:13] == len(image).to_bytes(4, 'little') + bytes([0xFF] * 4)
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
        check('the Nodes and pairing page shows the update going', node.row().get('update', '').startswith('updating'), node.row())
        check('asking for another image gets this one described', len(ask(number + 1, 0)) == 1)
        check('an unsigned ask gets nothing', node.said_all(bytes([8]) + number.to_bytes(4, 'little') + bytes(4) + bytes(8)) == [])
        # Tried on one node first: this one, the pilot, still gets it; a node not of that chip cannot be the pilot.
        check('a node firmware can be tried on one node first', api('POST', '/api/node-firmware/pilot', f'chip=13&mac={node.folder}')[0] == 200)
        check('only on a node of its chip', api('POST', '/api/node-firmware/pilot', f'chip=13&mac={A}')[0] == 400)
        firmware = get_json('/api/espnow')['nodeFirmware'][0]
        check('the page shows which, and that it has not run it yet', firmware['pilot'] == node.folder and not firmware['pilotRuns'], firmware)
        node.say(node.info(version=b'4b5c6d7'))
        check('once the node runs it, the update is over', node.row().get('update') == '', node.row())
        firmware = get_json('/api/espnow')['nodeFirmware'][0]
        check("the pilot runs it, so the others' day starts", firmware['pilotRuns'] and 86000 < firmware.get('pilotLeft', 0) <= 86400, firmware)
        check('every node at once again', api('POST', '/api/node-firmware/pilot', 'chip=13&mac=')[0] == 200
              and get_json('/api/espnow')['nodeFirmware'][0]['pilot'] == '')
        check('and no longer offered', node.confirmed()[21] & 1 == 0)
        node.say(node.info(refused=number))
        check('a node that had the update undone is not offered it again', node.confirmed()[21] & 1 == 0)
        check('and the page says it was undone', node.row().get('update') == 'update undone', node.row())
        node.say(node.info(saving=1))
        check('a node saving its battery is not offered an update', node.confirmed()[21] & 1 == 0)
        node.say(node.info(chip=0))
        check('nor is a node of another chip', node.confirmed()[21] & 1 == 0)
        check('a node firmware can be deleted', api('POST', '/api/node-firmware/delete', 'chip=13')[0] == 200 and get_json('/api/espnow')['nodeFirmware'] == [])
    finally:
        api('POST', '/api/node-firmware/delete', 'chip=13')


@case(pc=True)
@with_node
def node_hardware(s, node):
    """Hardware set on the Sensors page: the node fetches it once a confirmation says the hub has other settings for it."""
    node.say(node.info(hw=hardware(0, 3, 2, 255, 255, 0)))
    check('the node says which hardware it runs', node.tree().get('hwRuns') == '0,3,2,255,255,0' and node.tree().get('chip') == 13, node.tree())
    check('with nothing set on the hub, the confirmation asks nothing of it', node.confirmed()[21] & 4 == 0)
    for bad, why in (('sensor=4&sensorPin=9&tamperPin=&batteryPin=&cell=0', b'cannot wake'), ('sensor=4&sensorPin=3&tamperPin=&batteryPin=1&cell=0', b'both'),
                     ('sensor=4&sensorPin=3&tamperPin=3&batteryPin=&cell=0', b'twice'), ('sensor=9&sensorPin=3&tamperPin=&batteryPin=&cell=0', b'no such')):
        code, reply = api('POST', f'/api/node-hardware?path={node.folder}', bad)
        check(f'hardware that cannot work on this chip is refused, saying why: {bad}', code == 400 and why in reply, (code, reply[:90]))
    check('saved hardware for the node', api('POST', f'/api/node-hardware?path={node.folder}', 'sensor=4&sensorPin=3&tamperPin=4&batteryPin=1&cell=1')[0] == 200)
    check('the page shows it, numbered one past what the node runs', node.tree().get('hwSet') == '1,4,3,4,1,1', node.tree())
    check('a tamper switch gets its input 2, so its alerts are taken', any(n['input'] == 2 for n in node.tree().get('sensors', [])), node.tree())
    check('the next confirmation says the hub has other hardware for it', node.confirmed()[21] & 4 == 4)
    asked = bytes([10])
    answer = (node.said_all(asked + node.sign(asked)) or [b''])[0]
    check('asked, the hub answers with it, signed for the node', answer[:1] == bytes([11]) and answer[1:10] == hardware(1, 4, 3, 4, 1, 1)
          and answer[10:] == node.sign(answer[:10]), answer.hex())
    check('an unsigned ask gets nothing', node.said_all(asked + bytes(8)) == [])
    node.say(node.info(hw=hardware(1, 4, 3, 4, 1, 1)))
    check('once the node runs it, the page says so and the confirmation asks nothing more',
          node.tree().get('hwRuns') == '1,4,3,4,1,1' and node.confirmed()[21] & 4 == 0, node.tree())
    kept = api('GET', f'/api/file?path={node.folder}/node.txt')[1]
    check("it is kept in the node's file, beside what the page set", kept.startswith(b'hw=1,4,3,4,1,1\n') and b'\nsaid.hw=1,4,3,4,1,1\n' in kept, kept)
    api('POST', f'/api/node-settings?path={node.folder}', 'silent=600')
    check('saving the node\'s other settings keeps its hardware', node.tree().get('hwSet') == '1,4,3,4,1,1', node.tree())
    node.say(node.info())
    check('a node from before hardware settings is never asked to fetch them', node.confirmed()[21] & 4 == 0 and node.tree().get('hwRuns') == '', node.tree())
