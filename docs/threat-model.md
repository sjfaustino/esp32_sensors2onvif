# Threat model

The hub exists to record intruders. Its first promise is that an alert reaches the NVR; a false alert is the lesser
failure, and a missed one the worse. This page lists who might try to defeat it, what the hub does about each, and what
it cannot do. [SECURITY.md](../SECURITY.md) says how to report a weakness, and
[how-it-works.md](how-it-works.md) where each defence lives in the code.

## What is protected

- **That alerts are recorded:** a node's report reaches the hub, and the hub's motion event reaches the NVR.
- **The alarm's state:** only the owner arms and disarms.
- **The hub itself:** its firmware, settings, passwords and keys, and the nodes' firmware it hands out.

## Who might attack, and from where

| Where they are | Examples |
| --- | --- |
| In radio range of a node or the hub, outside the house | An intruder approaching with a laptop, a radio or a jammer |
| On the home network | A compromised device on the Wi-Fi, a guest, a malicious web page open in the owner's browser |
| On the MQTT broker | Anything allowed to publish to it |
| Holding a node or the hub | An intruder who reaches a sensor, or takes the hub away |

Someone on the internet reaches the hub only through the owner's browser or a device on the network: the hub is not
meant to be reachable from outside, and nothing in it is built for that.

## What the hub defends against

| Threat | Defence |
| --- | --- |
| Faking a node's all-clear, or keeping a stolen node looking alive | Each paired node has its own key, from an X25519 exchange checked by fingerprint. Messages are signed and sealed; a replay can only make a false alert, never an all-clear, and cannot stand in for a node's reports. |
| Playing back a node's info, to make the hub wait hours before missing a stolen node | A paired node's info carries the number from the hub's last confirmation, which changes with each report. Once a node has sent it, the hub ignores that node's info without it or with an old one, and remembers this through a restart. |
| Reading sealed messages, or forging them, from two sealed with one number | Each sealed message has a number of its own, never used twice under a key: the node and the hub reserve numbers in flash before using them. A hub that cannot save its reservation sends its confirmations signed instead of sealed until it can. |
| Pairing a rogue node in a real one's place | Approval names the fingerprint that was checked; a hello with another key while pairing is open does not replace it. |
| Silencing a node | A node quiet for longer than its time is reported silent, and several going quiet together as one event. |
| Keeping the NVR out, to stop its recording | The NVR always gets a connection, in place of the oldest that is not its own; a refused body is read for at most a minute; a video connection that is not playing is closed. |
| Guessing the hub password | Ten wrong guesses from one address lock it out for a minute, then longer, up to 15 minutes; logins take the same time right or wrong. |
| A web page using the owner's logged-in browser | Requests from another site, another host name (DNS rebinding) or in a frame from another site are refused. |
| Putting other firmware on the hub over the network | The hub keeps only firmware signed with the project's Ed25519 key; the signature is checked before the update is kept. The key is kept off the hub and out of the repository. |
| Putting other firmware on the nodes, through the hub or a hub of one's own | A node's update is signed with the project's key, in its own context so the hub's firmware cannot pass for it. The hub refuses an unsigned one when it is uploaded, and each node checks the signature again, and its release number, before it runs the update. |
| Putting back an older firmware, with faults since fixed | Each signed update carries its release number, signed with it, and the hub refuses one older than its own. |
| A login left open in a browser | **Log out**, at the top of every page, makes the browser forget the hub password. |
| A firmware that breaks the hub | A new firmware is kept only once it has stayed on the network, started ESP-NOW and got the NVR listening again; otherwise the hub goes back to the one before. |
| Setting a new hub up as one's own before its owner does | A blank hub's setup network has no password, so it opens only for 15 minutes after the board's BOOT button is pressed, and only while someone holding the hub has pressed it. |
| Reading the passwords and keys from a backup file | Backups are sealed with a passphrase the owner chooses: 600,000 rounds of PBKDF2-HMAC-SHA256, worked out by the browser so the hub never spends the time, then XChaCha20-Poly1305 on the hub. The nodes' keys are only in the sealed part. A restored hub password must meet the page's rules, and a backup without one leaves the hub's in place. |
| Capturing the setup network's password | The setup network uses WPA3, so it cannot be guessed offline from a recording of someone joining. |
| A crash from a malformed message | Every parser of what arrives from outside is fuzzed, with AddressSanitizer and UndefinedBehaviorSanitizer, on each push. |

## What it cannot defend against

These are limits of the design, or of the hardware, and the owner can lessen each one.

- **Radio jamming.** A jammer can keep the nodes from reaching the hub. The hub reports them silent, but it cannot stop
  the jamming or tell what is happening meanwhile. The NVR's own cameras still record.
- **A power cut.** With no power the hub records nothing. A battery or UPS for the hub, the router and the NVR keeps
  them up; the hub logs how long it was off.
- **Wi-Fi deauthentication.** Without protected management frames (PMF), anyone in range can knock the hub off the
  Wi-Fi, and the NVR then sees nothing from it. Turn on PMF on the router where the router offers it.
- **Someone holding the hub.** Passwords and keys are on its flash unencrypted, and its USB port takes any firmware.
  Put the hub where an intruder will not reach it before being recorded.
- **Someone watching the home network.** HTTP, ONVIF, RTSP and MQTT are not encrypted, as is usual between local
  devices. The pages send the hub password with every request, readable by anyone who sees the traffic, and that one
  password is also the ONVIF and RTSP login and can disarm the hub. On a WPA2 network with one shared password, any
  device that knows it can read the others' traffic once it sees them join. TLS does not fit in the hub's memory
  beside the video, so this is accepted, and the owner lessens it:
  - Put the hub and the NVR on a network of their own, such as a separate Wi-Fi name or VLAN that guests and other
    devices do not join, or at least use WPA3, where one device cannot read another's traffic.
  - Do not open the hub's pages from a device you do not trust, or over a network you do not control.
  - If the hub password may have been seen, change it on the HTTP page; it applies at once.
- **A weak backup passphrase.** Whoever has a backup file can try passphrases on their own computer. 600,000 rounds of PBKDF2 make each
  try slow, but a short or common passphrase still falls. Choose a few words nobody would guess.
- **A board built without the setup button** (`SETUP_BUTTON_PIN` -1). Its setup network opens with no password as soon
  as it cannot join a network, until a hub password is chosen. Set such a hub up as soon as it first starts.
- **The MQTT broker.** Anything that can publish to it can arm and disarm the hub. Give the broker a login.
- **Nodes on old firmware.** A node running firmware from before node signing takes any firmware its hub offers; once it
  runs a signed one, it checks every update after. One from before its info carried the hub's number can still have
  its info played back, until it runs firmware that sends the number.
- **The signing key.** Whoever has it, and the hub password, can put any firmware on any hub. If it is lost, each hub
  takes a firmware with a new key only over its USB cable.
- **A node with no screen pairing.** It cannot check the hub it pairs with, and takes the first that approves it. Pair
  nodes where no other hub may be in range.
