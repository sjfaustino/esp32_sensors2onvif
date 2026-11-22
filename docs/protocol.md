# The ESP-NOW messages

Every message between the hub and its nodes, byte by byte, for whoever builds a node or another sensor. They are written
once, in `lib/s2o_proto`, which the hub and the node firmware ([esp32_sensor_node](https://github.com/sjfaustino/esp32_sensor_node))
both build from; the two projects' tests check that code against the values worked out from this page alone. What the
hub then does with an alert, such as how long it lasts and when it counts as a past event, is in the
[user guide](user-guide.md#sensor-input-over-esp-now).

## Reports and confirmations

Each device sends this message unicast to the MAC address the serial console prints. It is little-endian and 17 bytes,
plus 3 for each target, plus the 8-byte signature. The messages are written once, in `lib/s2o_proto`, which the hub
and the node firmware (esp32_sensor_node) both build from.

| Bytes | Field |
|---|---|
| 1 | `2`, a report |
| 1 | Input number on the device, starting at `1` |
| 1 | Active: `1` active, `0` inactive |
| 1 | Battery percent, `0xFF` when unknown or mains powered. Other values above 100 also count as unknown. |
| 4 | Event counter. A resend of the same event repeats it, and the hub ignores a repeat within a minute. It need not survive the sensor's restarts. |
| 4 | Age in seconds, `0` when live, `0xFFFFFFFF` when unknown |
| 4 | Fresh: the number from the hub's last confirmation, `0` when the device has none (see below) |
| 1 | Number of targets, `0` to `3` |
| 2 per target | Distance in cm, `0xFFFF` when unknown |
| 1 per target | Direction: `0` unknown, `1` approaching, `2` moving away, `3` left to right, `4` right to left |
| 8 | The first 8 bytes of HMAC-SHA1, keyed with the ESP-NOW key as text, over the sender's 6-byte MAC address followed by every byte above |

The hub only accepts a device and input that have a folder on LittleFS.

ESP-NOW has to use the same radio channel as the Wi-Fi network, so the channel is set by your router.
Sensors should try channels 1 to 13 until a send reports delivery, keep that channel, and search again if
sends start failing, because a router on automatic channel selection can move.

Once it has accepted a report and saved it, the hub sends a **confirmation** back to that MAC address, usually within
100 ms; wait at least 300 ms before resending. A sensor should resend a report, with the same number, until the
confirmation arrives: delivery by ESP-NOW only means the radio got it. A resend is confirmed again. Messages that are
refused, malformed or from a device without a folder get no confirmation. The confirmation is 34 bytes. With it comes
what the hub has for the node; a sensor that needs none of it can read just the first 7 bytes and check the signature.

| Bytes | Field |
|---|---|
| 1 | `3`, a confirmation |
| 1 | The report's input number |
| 1 | The report's active value |
| 4 | The report's event counter |
| 4 | The hub's time, in seconds since 1970 UTC, `0` while its clock is not set |
| 4 | How often the node should report with nothing to report, in seconds, as set on the hub; `0` for the node's own |
| 2 | How often it should report while a sensor stays on, in seconds, as set on the hub; `0` for its own |
| 4 | The end of a walk test started on the hub, on the hub's time; `0` for none |
| 1 | Flags: `1` when a firmware update is ready for the node; `2` asking the node to prove it is there (see below); `4` when the hub has other hardware settings for it (see Hardware settings) |
| 4 | The fresh number for the node's next report |
| 8 | The first 8 bytes of HMAC-SHA1, keyed with the ESP-NOW key, over the **sensor's own** 6-byte MAC address followed by the 26 bytes above |

The repeat time is kept below the shortest alert time of the node's sensors, so the alert does not lapse between repeats.
A node's clock can run some percent fast or slow while it sleeps. A node that keeps alerts for hours can measure its rate
against the hub's time over successive confirmations and scale the ages it sends by it, so a past event's time is right.

A sensor should check the signature, so that nothing else in radio range can stop it resending. The confirmation means
the hub has the alert, not that the NVR got it or recorded it; the hub reports delivery separately (see [Events and alerts](user-guide.md#events-and-alerts) in the user guide).

Devices that cache alerts should send them oldest first and keep resending each until it is confirmed.
They should also report their current state regularly, a little more often than their node's **Report this node silent
after** time, so that a sensor that stops reporting is noticed.

## Node info

A node can also tell the hub about itself, with each regular report, so the hub learns of any change. It is signed like a
report, gets no confirmation, and is sent once per node, not per input:

| Bytes | Field |
|---|---|
| 1 | `6`, node info |
| 4 | How often the node reports with nothing to report, in seconds; `0` when it does not say |
| 1 | `1` while the node is saving its battery, so reporting less often; else `0` |
| 1 | Why it last restarted: `0` unknown, `1` power-up, `2` reset button, `3` software, such as an update, `4` crash, `5` watchdog, `6` brownout |
| 2 | How many times it has restarted since it was set up |
| 2 | How long its last alert took to be confirmed, in milliseconds; `0xFFFF` for none |
| 2 | Its chip, as in its firmware's header: `0` ESP32, `9` ESP32-S3, `13` ESP32-C6 and so on; `0xFFFF` unknown |
| 4 | The firmware update it refused, because it was undone (see Firmware updates, below); `0` for none |
| 1 + n | The length, 0 to 24, then the text of its sensor's kind, such as `RCWL-0516` |
| 1 + n | The same for its firmware version |
| 9 | Optional: the hardware settings it runs, as in Hardware settings below. A node from before them leaves these out |
| 8 | The signature, as a report's |

Texts are printable ASCII. The Nodes and pairing page shows them, and the hub keeps them in the node's folder as `about.txt`, all
but the alert time, which changes with every alert. A restart counted for a crash, the watchdog or a brownout is logged.
**Report this node silent after** is then a quarter longer than the node's report time, and at least 10 minutes longer,
and follows the node when that changes. Set on the Sensors page, it comes first. A node that never says uses 3 hours.
Its report time and repeat time can also be set there, under **Node settings**; they reach the node with its next
confirmation, and the node then says its new report time, so the silent time follows.

The ESP-NOW key is made the first time the hub starts and is shown on the Nodes and pairing page. It is kept by a settings reset and
carried in backups. The hub refuses a message with a wrong signature or none; the Nodes and pairing page counts those it can read. The report
number is never a reason to refuse a correctly signed message, so a sensor that restarts its count, or loses it with a
battery change, is never ignored. It is used only to ignore a resend: the same number and state again within a minute.
Someone who records a sensor's message can play it back later. That makes a false alert, never a false all-clear,
because a clear report never ends an alert; a missed intruder is worse than a false alert. A paired node goes further,
so that a recording cannot keep a stolen node looking alive: each confirmation gives it a new fresh number, which it
puts in its next report. A report from a paired node that carries neither of the last two numbers the hub gave it
still raises its alert, but does not count as the node being there, nor is its battery reading taken; its
confirmation asks for proof (flag `2`), and the node reports again at once with the new number. That happens once
after the node or the hub lost power. Nodes on the shared key are taken at their word, as before.

## Sealed messages

A report says which sensor fired, whether it is on and the battery level, which anyone listening
could read. Sealed, only the hub can. A node may seal any report or node info, and the hub then seals its confirmation:

| Bytes | Field |
|---|---|
| 1 | `7`, sealed |
| 4 | A number the sender never uses twice with the same key; for a report, its event counter will do, as a resend is the same message again |
| n | The message inside, without its signature, XORed with the key stream |
| 8 | The signature over the **node's** MAC address followed by every byte above, keyed as the message inside would be |

The seal key is HMAC-SHA1, keyed with the signing key (the node's own, or the ESP-NOW key as text), over the text
`sensors2onvif seal`. Block i of the key stream, 20 bytes, is HMAC-SHA1 keyed with the seal key over the direction (`0`
to the hub, `1` from it), the number (4 bytes) and i (1 byte). The hub opens it, checks the message inside as if it had
come plain, and answers in kind; its own numbers start at random at each start. Sealing hides what a message says, not
that it was sent: someone listening still sees a node send at other times than its regular reports.

ESP-NOW has encryption of its own, but only for a few peers at once (7 by default, 17 at most), and a hub
has more nodes than that. So sealing is built from the same HMAC-SHA1 that signs, as a counter-mode key stream, and the
signature covers the sealed bytes: encrypt, then sign.

## Hardware settings

What a node's sensor is, and where it, a tamper switch and a battery are wired, is set for each node on the Sensors page,
so one firmware serves every node of a chip. The node says what it runs at the end of its info; when the hub has other
settings, its confirmations carry flag `4`, and the node, with nothing else to report, asks for them:

| Ask, node to hub, 9 bytes | |
|---|---|
| 1 | `10` |
| 8 | The signature, as a report's |

The hub answers at once:

| Settings, hub to node, 18 bytes | |
|---|---|
| 1 | `11` |
| 4 | Their number: one more each time they are changed on the hub. The node takes only a higher one than it runs, so a recording played back cannot set it back |
| 1 | The sensor: `1` any whose output goes high while it detects, `2` one whose output goes low, `3` RCWL-0516, `4` HC-SR501, `5` LD2410 |
| 1 | The sensor's GPIO |
| 1 | The tamper switch's GPIO, `255` for none. It is the node's input 2, the sensor its input 1 |
| 1 | The battery's GPIO, read through a divider of two equal resistors, `255` for none |
| 1 | The battery: `0` none, `1` a lithium-ion or lithium-polymer cell, `2` lithium iron phosphate |
| 8 | The signature, as a confirmation's |

The node checks that the settings can work on its chip (`s2oHardwareValid` in `lib/s2o_proto`: pins that can wake it
for the sensor and tamper switch, one that can read a voltage for the battery, none used twice), keeps them in flash and
restarts into them; its next info says so. The hub's page checks them by the same rules first.

## Firmware updates

When a confirmation says an update is ready, a node can ask for it, signed like a report:

| Ask, node to hub, 17 bytes | |
|---|---|
| 1 | `8` |
| 4 | The update's number, `0` when it does not know it yet |
| 4 | The offset it wants, or `0xFFFFFFFF` for a description |
| 8 | The signature, as a report's |

The hub answers at once with up to 4 parts from that offset, or with a description when asked for one or for an update it
no longer has:

| Part, hub to node | |
|---|---|
| 1 | `9` |
| 4 | The update's number, which changes with its file |
| 4 | Its size in bytes |
| 4 | The offset, or `0xFFFFFFFF` for a description |
| up to 200 | That part of the file, or for a description its version |
| 8 | The signature, as a confirmation's |

The node asks for the next offset until it has the whole file, and asks again for anything that did not arrive.

The file is the node's firmware as the node project signs it (`sign_firmware.py --node`; the format is in
`lib/s2o_proto/s2o_fw_sig.h`): the image, its release number, and an Ed25519 signature with the project's key over a
context of its own, `s2o nodefirm v1\n`, so the hub's firmware cannot pass for a node's. The hub refuses an unsigned
file when it is put on it, and keeps the rest whole, signature and all. The node writes the image as it comes and holds
the last 72 bytes back; it starts the update only when the signature is good and the release number is not lower than
its own, and otherwise does not try that file again. A node from before signing writes those bytes after the image,
where nothing reads them, so a signed file updates it too.

## Pairing a node

A node can instead pair with the hub: it finds the hub by itself and gets a key of its own, which no one listening to the
radio can learn, and nothing secret is typed in or printed. Its reports and confirmations are then exactly as above, but
signed with its own 20-byte key instead of the ESP-NOW key, and the hub accepts only that key from it, never the shared
one. A stolen node then gives away its own key only.

The node makes an X25519 key pair (RFC 7748) when it is first set up, keeps the private key, and has the fingerprint of
its public key on a label: the first 6 bytes of the public key's SHA-1, in upper-case hex, as `4F2A-91C7-03BD`. To pair:

1. On the Nodes and pairing page, press **Allow new nodes to pair for 2 minutes**, or **Add a sensor** and choose a
   sensor node, which does the same.
2. The node sends a **hello** to the broadcast address `FF:FF:FF:FF:FF:FF`, on channel 1, then 2 and so on, waiting about
   100 ms on each for an answer, and repeats every few seconds until it is paired.
3. The hub lists the node with its fingerprint. Approve it once the fingerprint matches the label: that check is what stops
   anything else from pairing in its place. The hub makes a folder for each of its inputs, and the page asks where each
   sensor is, such as "Back door", which alerts and notifications then use.
4. The node's next hello is answered as paired, signed with its key. It keeps the hub's MAC address, the channel and the
   hub's public key, and starts reporting.

| Hello, node to hub, 34 bytes | |
|---|---|
| 1 | `4` |
| 1 | The number of inputs, `1` to `16` |
| 32 | The node's public key |

| Answer, hub to node, 43 bytes | |
|---|---|
| 1 | `5` |
| 1 | `0` waiting for approval, `1` paired |
| 1 | The hub's Wi-Fi channel |
| 32 | The hub's public key |
| 8 | When paired, the first 8 bytes of HMAC-SHA1, keyed with the node's key, over the node's own 6-byte MAC address followed by the 35 bytes above; while waiting, zeros |

The node's key is HMAC-SHA1, keyed with the 32-byte X25519 secret the two share, over the text
`sensors2onvif node key` followed by the hub's public key and then the node's: 20 bytes. The node works it out from its
private key and the hub's public key in the answer, and checks the paired answer's signature with it.

A paired hello is answered at any time, not only while pairing is open, so a node that stops getting confirmations can
search the channels again with it and learn where the hub has moved. Any other hello is answered only while pairing is
open. A node that keeps a hub should trust only that hub's public key afterwards, and pair again only when someone asks it
to: something that answered its first hello in the hub's place could otherwise take its alerts. After approval, check
that the node starts reporting: one that paired with something else stays "Not heard yet" on the hub.

The hub's key pair is made once and kept by a settings reset and in backups; the page shows its fingerprint. The node's
key is in `pairing.txt` in its folder, which a full backup includes, and deleting the node's folder unpairs it. Nodes set
up with the shared ESP-NOW key keep working as before.
