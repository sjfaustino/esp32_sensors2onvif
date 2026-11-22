# Changelog

What changed in each release, newest first. Versions follow [semantic versioning](https://semver.org) from 1.0.0;
before that, a new minor version may change settings or the messages.

## [Unreleased]

### Security

- Sealed messages between the hub and the nodes are XChaCha20-Poly1305 (Monocypher), with a 16-byte tag that also binds
  the node's address and the direction, in place of a key stream and an 8-byte signature built from HMAC-SHA1. The hub
  still reads the old form from nodes on older firmware, and answers each in the form it used. X25519, for pairing, is
  Monocypher's too, in place of a port of TweetNaCl; keys made before still work.

### The pages

- The pages ask the hub far less: nothing while the tab is hidden, the Overview its summary every 3 s and the rest only
  when it changes, and the status line not twice. Open on the Overview, a browser asks 12 times in 30 s, from 33; on Nodes
  and pairing 29, from 47.

### The nodes

- **Add a node**, on the Nodes and pairing page, takes a node through its setting up a step at a time, each ticked as
  the hub sees it done.
- Each node has a page of its own: its board, firmware and hardware, its battery over time, its signal and restarts,
  each sensor's week, and its events.
- A node firmware can be tried on one node first; the others get it a day after that node runs it, and never if it
  had it undone. Each file says how many nodes of its chip run it.

## [0.5.0] - 2026-11-22

Update from 0.4.0: the hub then refuses older firmware, and node firmware that is not signed. Nodes take their sensor
and pins from the Sensors page once they run the sensor node's 0.1.0; earlier nodes keep what they were built with.

### Security

- The hub refuses an update older than its own release, which may have faults since fixed. Each signed update carries
  its release number, signed with it; an older one goes on only over the board's cable.
- Node firmware is signed too, with the project's key in a context of its own. The hub refuses an unsigned node file
  when it is put on it, and a node with this signing checks every update again, and its release number, before it
  runs it. The signature code is in `lib/s2o_proto`, which the node project shares.
- **Log out**, at the top of every page, makes the browser forget the hub password. The hub refuses its logout route
  always, and never counts it as a wrong password.

### The nodes

- Each node's hardware, its sensor and the pins of its sensor, a tamper switch and its battery, is set on the Sensors
  page and fetched by the node, so one firmware serves every node of a chip. The hub and the node check the pins against
  the node's chip. A node says what it runs in its info, and fetches other settings when a confirmation says the hub has
  them (protocol: confirmation flag 4, messages 10 and 11, and the settings at the end of the node info).

### The pages

- Each menu group has an icon, the tab shows the hub's shield, and a phone's "Add to home screen" gives the hub's name
  and the shield.
- Every page works from the keyboard: the menu is made of buttons and links, a skip link leads past it, and the
  keyboard's place is ringed. The browser tests check every page, light and dark, with axe-core.

### The project

- CI compares the pictures of the pages it takes with those in `docs/images`, and fails when a page changed without
  them (`tools/compare_pictures.py`).

## [0.4.0] - 2026-11-22

Update from 0.3.0: from this version the hub takes only signed firmware over the network, and its pages are redesigned.
Settings, sensors and history are kept.

### Security

- The hub takes only firmware signed with the project's key. Before, anyone with the hub password could put any firmware
  on it over the network. Each release's update files are signed; a hub on 0.3.0 or earlier takes them too, and from
  then on takes no other. A cable to the board still takes any firmware.
- Each release lists the SHA-256 of its files in `SHA256SUMS`, carries a software bill of materials (`.spdx.json`), and
  has GitHub's build attestation, so anyone can check a file was built from the tagged commit.
- `docs/threat-model.md` lists who might attack the hub, from where, and what it does and cannot do about each.

### The pages

- The Overview opens with the alarm's state in large type and its colour, red for armed away, amber for armed home and
  green for disarmed, with an icon and large buttons; the bar across the top of every page shows the hub's name and the
  same state.
- The pages ask in the page, never in the browser's own boxes, with a red button for what cannot be undone, and confirm
  a change in a notice at the foot of the page. A change of mode can be undone from it.
- The settings pages group their fields in cards, with the help under each field and the main button filled. A bar
  at the foot says when changes are not saved, with Save and Discard, and leaving the page asks first.
- A file is dropped on the page or chosen, and shows its name and size; the firmware upload shows its progress.
  Restarting, new keys and a reset are together under Danger zone on Maintenance.
- The Sensors page names each picture by what it is shown for, with a small preview of it or of what is shown in its
  place, and each node by its sensors' names; the file names show only when asked for.
- The Events page labels each event in the colour of what it is, shows 25 at a time, and leaves empty cells blank.
- Lists show grey bars while they load, and an empty one says what to do, with a button that does it. The status line
  leads to what it is about. The Overview no longer shows work dropped for lack of memory, which Device info has.
- The menu's groups are Alarm, Sensors, Network, Recording and System, and the Overview groups what is happening now
  and what the hub is connected to in two cards.

### Fixed

- A stored file whose reading failed part way was taken as whole, cut short.

### The project

- CI also runs CodeQL, clang-tidy's bug-finding checks and OpenSSF Scorecard, and measures how much of the library the
  tests run: 93% of its lines. The GitHub Actions it uses are pinned to commits, and Dependabot proposes their updates.
- The parser fuzzer also runs on Windows, with LLVM's libFuzzer: `tools/fuzz.ps1`.

## [0.3.0] - 2026-11-21

Update from 0.2.0: it closes ways to defeat the hub from nearby, below. None needs the hub password.

### Security

- Someone on the network could keep the NVR out by holding the hub's few connections, trickling the body of a refused
  request. Kept out for 10 minutes, the NVR's subscription lapsed and alerts stopped reaching it. A refused body is now
  read for at most a minute, and the NVR always gets a connection, in place of the oldest that is not its own.
- Likewise its video: a video connection kept itself open with any request, even without the password, and the NVR,
  without its video, records nothing at an alert. One that is not playing is now closed two minutes after it opened,
  and the NVR always gets one.
- While pairing was open, a hello from a node's address with another key replaced the key waiting, and approving the
  node paired that key: anyone in radio range could pair their own in the node's place. Approval now names the
  fingerprint that was checked.
- Another site could show the hub's pages in a hidden frame, with the browser's saved login, and lead someone to
  click Disarm unseen. Pages may now be framed only by the hub itself.
- The setup network uses WPA3: under WPA2 its password, usually the hub password, could be guessed offline from a
  recording of someone joining it.
- A blank hub's first password guards it as soon as it is chosen, not only after the restart that follows.
- Sealed messages are numbered on from where the last start left off, so after a power cut no number seals two
  messages, on the hub or, with their next firmware, on the nodes.
- The hub shows 16-character fingerprints, beyond what a made-up key can match; a node's 12-character label is their
  start, and still matches.
- The video's login is checked in the same time, right or wrong; the settings backup, with its passwords, is never
  cached.
- ONVIF logins that repeat a nonce are counted (onvifRepeats in /api/info), to learn whether the NVR ever repeats one
  before such replays are refused.

### The hub

- Notifications go to ntfy or Gotify as those servers take them, with a title and a priority for the phone (alerts
  urgent, problems high, news low), and an access token. The Notifications page explains what each needs.
- A weekly summary, on the day and time chosen: the week's alerts and whether they reached the NVR, the sensor that
  fired most, the problems, the batteries running low and the last test alert. It can also be sent at once. It reads
  the history a little at a time, so the hub stays responsive meanwhile.
- Each sensor can name the NVR camera that covers it. Its alert notifications and the Events page say which camera to
  look at.
- **Getting started** on the Overview takes a new hub through its setup a step at a time, saying what to do and why.
- The pages follow the device's dark setting, and fit a phone: forms wrap and wide tables scroll.
- A new hub password, or other names for the hub, apply at once. Only a new port or hub name restarts the hub.

### The project

- Every saved setting, API route and page colour is described once, in a table, and CI checks the rest follows it.
- The board's decisions (which network to try, the setup network's password, what is kept through a restart) are in
  the library with unit tests, and src/main.cpp is split into the parts of the board.

## [0.2.0] - 2026-11-21

### The hub

- A hub can be set up from a release, with no PC: each release has a `-full.bin` for every board to write to a blank
  one. A new hub opens its setup network, and its setup page asks for the hub password along with the Wi-Fi network.
  The setup network then uses the hub password, unless the firmware was built with one of its own.
- A folder listing that could not look at one of its entries, as when memory runs out, no longer counts as complete.
  The sensor table rebuilt from it forgot those sensors, and refused their alerts as from unknown senders.

### The project

- A user guide, `docs/user-guide.md`, with pictures of the pages, takes the details out of the README.
- Third-party notices, with the licence texts, in the repository and with every release.

## [0.1.0] - 2026-11-18

The first numbered release.

### The hub

- Presents itself to any ONVIF NVR as a camera, on a free channel. Each alert becomes a motion record, and the picture
  names the sensor that fired, live or as a past event.
- Takes sensor reports over ESP-NOW from nodes, and over HTTP. A report is confirmed only once it is saved, so a node
  keeps sending until the hub has it, through power cuts and full memory.
- Checks that the NVR got each alert, and plays the ones it missed as past events once it listens again.
- Arms away or home, or disarms until armed again or for a time. Interior sensors, quiet hours, alert times and
  past-event times are set per sensor or node.
- Pairs nodes with public keys and a fingerprint check, so each node has its own key; sealed messages hide what a node
  says. Replays make a false alert, never a false all-clear, and cannot keep a stolen node looking alive.
- Notices silent nodes, low batteries, weak signals, nodes going quiet together and sensors firing far too often, and can
  raise an NVR alarm for them. Forecasts each battery's life.
- Notifications by webhook, with a daily check. Home Assistant over MQTT, with discovery and an alarm panel.
- Updates its own firmware from its page, and keeps it only once it has proved itself; updates the nodes' firmware over
  ESP-NOW.
- A page for every part: the overview and its setup checklist, sensors and their pictures, a floor plan, a walk test,
  the event history with a CSV download, nodes and pairing, the NVR, Wi-Fi with a backup network, and maintenance.
- Restarts itself only when it must, and never on its schedule while an alert is on.
- Always has a password, which can be changed but not turned off.

### Tests

- 111 unit tests, an end-to-end test of 617 checks playing the NVR's part, and the pages in a real browser, all run on
  every push.

[Unreleased]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/sjfaustino/esp32_sensors2onvif/releases/tag/v0.1.0
