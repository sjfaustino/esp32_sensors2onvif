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
  devices, so the hub password can be read from the traffic. Keep the hub on a network only trusted devices join.
- **The MQTT broker.** Anything that can publish to it can arm and disarm the hub. Give the broker a login.
- **Nodes on old firmware.** A node running firmware from before node signing takes any firmware its hub offers; once it
  runs a signed one, it checks every update after.
- **The signing key.** Whoever has it, and the hub password, can put any firmware on any hub. If it is lost, each hub
  takes a firmware with a new key only over its USB cable.
- **A node with no screen pairing.** It cannot check the hub it pairs with, and takes the first that approves it. Pair
  nodes where no other hub may be in range.
