# Changelog

What changed in each release, newest first. Versions follow [semantic versioning](https://semver.org) from 1.0.0;
before that, a new minor version may change settings or the messages.

## [Unreleased]

### Security

- Through an MQTT broker the hub does not log in to, anyone on the network could move it from armed away to armed home,
  which stops the interior sensors' alerts as a disarm would; only the disarm itself was refused. Now that is refused
  too. Arming home from disarmed is still taken.
- Guessing the hub password from many addresses no longer gets ten tries for each. The hub has room to count eight
  addresses, and a new one used to push out the one quiet longest, which then started again; now an address that has
  used up its tries keeps its place for the day, and once all eight are such, the others share one count. The NVR's
  address always keeps a count of its own: in the shared one, others' wrong passwords could have shut it out, and
  its alerts would have stopped.
- The Maintenance page follows a newer release's links only when they are GitHub's own.
- An ONVIF discovery probe's message number goes back in the answer only when it is plain, so a probe cannot put its
  own XML in the hub's answer.
- An NVR's event address follows the webhook's rule, so a line break written into it cannot add a line to the
  request the hub sends there.
- A video client that sends requests without reading the answers no longer piles them up in memory; it needed no
  login for that.
- Once its clock is set, the hub refuses an NTP reading that would move it by more than 5 minutes, and logs it as
  **time refused**, sent with node problems: NTP is not signed, and a faked time could move quiet hours over an alert.
  Saving the Time page takes the next reading, however far off.

### Fixes

- A reply to an event push or a notification is read from its whole status line, even when that comes in pieces; it
  used to be judged by one character of the first piece.
- Base64 with anything in it that is not base64, as in a login, is refused rather than read with the odd characters
  left out. Comparing a secret no longer takes longer for a longer guess.
- A refused time reading is passed from the network task to the loop under a lock, and the count of Wi-Fi drops is
  kept as an atomic, as each is written on one core and read on the other.
- A pin for an LED larger than the pins go is refused, rather than read as a smaller number that may be allowed.
- An NVR whose first event push did not get through was sent the next as Changed, never as Initialized, which ONVIF
  says comes first. A subscription's first push now counts only once it gets there. A Subscribe that takes the place of
  another while a push to it is under way is no longer marked by that push's result.
- tools/update_hub.py refused to update a hub while alerts waited for the NVR, so an NVR out of reach blocked every
  update. The hub keeps those alerts in flash and replays them after a restart, so now only an active alert stops it.
- A hub on a network without a fixed address was never given one: it asked for 255.255.255.255 as a fixed address in
  place of DHCP. It joined, so it opened no setup network and tried no backup, yet could reach nothing, the NVR
  included. Hubs with a fixed address, as most are set up for the NVR, were not affected on that network.
- A hub that joins a network but has no address counts as off it, so it tries its backup network and then opens the
  setup network, as when a network cannot be joined.
- A paired node's key is forgotten only when its pairing file is deleted. One that could not be read for a moment, as
  while memory was short, or was damaged, used to unpair the node until the next rescan, so its signed alerts were
  refused meanwhile.
- The Events page explains a refused time, and lists it among the problems; it had no words for it.
- An event's explanation on the Events page stays open until tapped again; the refresh every few seconds closed it.
- The Sensors page reads the nodes again each time it is opened, so one paired from another device shows without
  reloading the page.
- Device info's smallest largest free block no longer shows a made-up size in the hub's first second.
- A language name kept on the device that is not one of the pages' own, such as one changed by hand, falls back to
  the browser's language instead of leaving the pages without words.
- A notification its server did not take, as while Home Assistant restarts, is sent again 10 seconds later, three
  tries in all, before it is dropped. It used to be sent once, whatever happened to it.
- An event push the listener answers with an HTTP error now counts as not delivered, so the hub sends it again and,
  until one is taken, treats the NVR as not listening, keeping its alerts to replay. It used to count any push it had
  sent. A listener that closes without a reply, as NVRs do, is unchanged.
- A router that still holds the hub's connection from before a restart, and so turns it away for a moment, gets the
  hub back two seconds later, up to three times, before the hub moves on to its backup network and the setup network.
  Back in a few seconds after a restart, not a minute.

### Changes

- Alerts waiting for the NVR, such as test alerts it was away for, can be cleared from the Overview, which shows them while they wait, or with `POST /api/waiting/clear`.
  It is refused while an alert is on, and logged as waiting alerts cleared, so a cleared queue cannot pass unseen.
- A settings backup writes letters, digits and `.-_` as they are, and only other characters as `%XX`, so it is about a
  third the size and can be read. Backups written before, with every character as `%XX`, still restore.
- Up to three NTP servers, tried in turn. A new hub uses Portugal's legal time from the ObservatÃ³rio AstronÃ³mico de
  Lisboa, `ntp04.oal.ul.pt` and `ntp02.oal.ul.pt`, then `pt.pool.ntp.org`.
- Device info and the week of health take the least free memory as the memory itself keeps it, at every allocation,
  so a fall that lasts a moment, as a web request's, is seen. The row that said "today" says since the daily check,
  or, on a hub without one, since the start.
- The week of health is asked for on its own, once a minute, not with Device info every few seconds.
- The ESP32-S3 build is for boards with 16 MB of flash and 8 MB of octal PSRAM (N16R8): the same layout as the 16 MB
  ESP32, and the PSRAM in use. On one, the hub starts with some 245 KB of free memory, against some 130 KB on an
  ESP32, and the PSRAM takes allocations of 4 KB and more. An S3 with less flash no longer fits, and one without PSRAM is
  untested. Device info's largest free block and memory pieces are of the chip's own memory, as the PSRAM's 8 MB in
  one piece would hide how cut up it is; the PSRAM row says how much of it is free.

- Two status LEDs, green and red, can say whether the hub is armed and protecting, and when a sensor is firing, to
  someone standing by it. They are a choice: their pins are set under Device info > Status lights, from a list of the
  pins the board can spare, with a test for each. See Status lights in the user guide. An RGB LED, as many ESP32-S3
  boards have on GPIO 48 or 38, can show the same in one light. It shows red, green and blue as the hub starts, 0.4
  seconds each, as a test of itself.

- Device info draws the last week of the hub's health, an hour to a bar: the least free memory, the weakest Wi-Fi
  signal, and how much of each hour the NVR was listening. Kept across restarts.
- The Maintenance page says when a newer release is out, and links its file for this hub's board. The browser asks
  GitHub at most once a day; the hub does not reach the internet.

## [0.7.0] - 2026-11-23

Update from 0.6.0. The pages and the notifications can be in Portuguese, from translation files any gettext tool edits;
Home Assistant can disarm the hub only through a broker it logs in to; and the hub keeps an eye on itself: a monthly
test reminder, a backup reminder, and its least free memory each day.

### Security

- The hub takes a disarm from Home Assistant only when it logs in to the MQTT broker with a user. A broker that lets
  anyone in, as Mosquitto can, let anyone on the network disarm the hub; arming and test alerts are still taken. The
  Home Assistant page says which applies.

### Languages

- The pages are in Portuguese (European) on a device whose browser prefers it, with a switch at the foot of the menu
  for each device; the hub's messages are shown in the same language.
- Notifications can be in Portuguese: **Language of the messages**, under Notifications, words every alert, problem,
  daily check and weekly summary, and Home Assistant's message. The event names stay English, for automations.
- Each language is a gettext file, `i18n/<code>.po`, for translators' tools such as Poedit or Weblate: one file for the
  pages and the hub's messages, with contexts for a word said two ways and each language's own plural rule. The build
  makes from it what the pages fetch and the firmware holds, and offers a language once its file is marked complete.
  CONTRIBUTING.md says how to add one.
- The pages fetch only the language they show, once, instead of carrying every language: 78 KB gzipped in English,
  from 103. The setup page fetches only its own few phrases, so it stays at 5 KB over the hub's open setup network.
- The hub gives a refusal that holds values as a phrase and its values, so the pages translate it whole.
- `tools/check_translations.py`, one of the quick checks, keeps every phrase translated: the pages', the messages the
  hub sends them, and the hub's own notifications.

### The pages

- The Overview asks for a sealed backup when there is none from the last month, or a node was paired since.
- The daily check asks for a test alert once the last is over a month old, by the time the hub keeps.
- The daily check and Device info give the least free memory since the last daily check, when it was, and how many
  web and video connections were open then; the daily check says so in words when it fell below 40 KB. A fall that
  recovered left nothing to see before.
- Device info prints a label for the hub: a QR code of its page, its setup network, the fingerprint nodes are paired
  against, and its serial number. The QR code is made in the page, and checked against segno module by module.
- The pages carry an ETag, so a browser opening them again is told in a few bytes that it has them, instead of being
  sent the whole page from the hub's flash each time.
- The Sensors page no longer shows for a moment before the page asked for, as it was the one page not hidden at first.
- A full backup keeps each node's battery readings and its first hour's checks; the hub takes a battery log of up to
  8 KB back.

### Tools

- `tools/update_hub.py` updates a hub over the network: it builds, signs and sends the firmware, and watches until the
  hub keeps it or says it went back, by the hub's own uptime, so a PC that sleeps meanwhile does not misjudge it. With
  `--release v0.7.0` it sends a published release, as CI built it. It refuses a commit CI has not passed, changes not
  committed, and a hub with an alert active.
- `tools/precheck.py` runs CI's quick checks in seconds, and the pre-push hook in `tools/hooks` runs it before each
  push; CI runs the same script. The sensor node project runs it too, from its `hub/` folder.
- `tools/check_platform.py` checks, in CI and before a release is built, that the PlatformIO platform fetched by URL
  is byte for byte the one recorded, and with it the versions and addresses of the compiler and framework it installs.

## [0.6.0] - 2026-11-22

Update from 0.5.0 before updating any node to its 0.2.0, which seals its messages in a way only this hub reads; this hub
still reads nodes on 0.1.0. A backup is now sealed with a passphrase, and a blank hub's setup network waits for its BOOT
button.

### Security

- Backups are sealed with a passphrase you choose (600,000 rounds of PBKDF2-HMAC-SHA256, worked out by the browser, then XChaCha20-Poly1305 on the hub), so the file holds no readable
  password or key. The nodes' keys go only in the sealed settings, not in the full backup's files. A backup from before
  still restores.
- A restored hub password must be 8 to 63 characters, as on the page; a backup without one keeps the hub's.
- A blank hub's setup network, which has no password, opens only for 15 minutes after the board's BOOT button is
  pressed, so nobody in range can set the hub up as theirs first. `SETUP_BUTTON_PIN` -1 for a board without one.
- A node's info carries the number from the hub's last confirmation, and once a node has sent it the hub ignores its
  info without it. Played back, a recording of a node saying it reports only every few hours could otherwise have
  given a thief that long before the node was missed. Needs node firmware newer than 0.1.0.
- A sealed message is never sealed with a number used before. The node sealed an alert it was still resending with
  the alert's own number, while what it said changed, and the hub went on sealing when it could not save its
  reserved numbers; it now sends signed confirmations until it can. Fresh numbers come from the chip's random source.
- The threat model says what someone watching the home network can read, and what to do about it.

- Sealed messages between the hub and the nodes are XChaCha20-Poly1305 (Monocypher), with a 16-byte tag that also binds
  the node's address and the direction, in place of a key stream and an 8-byte signature built from HMAC-SHA1. The hub
  still reads the old form from nodes on older firmware, and answers each in the form it used. X25519, for pairing, is
  Monocypher's too, in place of a port of TweetNaCl; keys made before still work.

### The pages

- The switch that makes node problems raise the alarm, and a name given while pairing, say so when the hub refuses
  them; the switch goes back to what the hub has.
- The pages ask the hub far less: nothing while the tab is hidden, the Overview its summary every 3 s and the rest only
  when it changes, and the status line not twice. Open on the Overview, a browser asks 12 times in 30 s, from 33; on Nodes
  and pairing 29, from 47.

### Storage

- Each node keeps one `node.txt`, its settings and what it last said, and each sensor one `sensor.txt`, its name and
  settings, in place of four files; every file of lines is read by one reader. A backup or a folder with the old
  `settings.txt`, `about.txt` or `name.txt` is taken in and the old files removed.
- Saving a node's times on the Sensors page no longer loses the hardware set for it, and a full backup keeps the
  camera of a sensor that has only that setting.

### Tests

- Each end-to-end test stands on its own: it starts from a quiet hub, sets up what it needs, and puts back what it
  changed, so any of them runs alone (`python tools/e2e_test.py pairing`) and one that fails does not take the next
  with it. A test that needs a paired node pairs its own, which numbers its own reports. The browser tests each get a
  fresh page.
- Running them alone found a fault in the Sensors page: a picture slot showing the Seen picture in its place faded its
  words too far to read. They keep their contrast now.

### The nodes

- **Add a node**, on the Nodes and pairing page, takes a node through its setting up a step at a time, each ticked as
  the hub sees it done.
- Each node has a page of its own: its board, firmware and hardware, its battery over time, its signal and restarts,
  each sensor's week, and its events.
- A node firmware can be tried on one node first; the others get it a day after that node runs it, and never if it
  had it undone. Each file says how many nodes of its chip run it.
- A node's page checks its first hour: heard at a good signal, a test alert that reached the NVR, and a walk past each
  of its sensors, each ticked as the hub sees it, and kept as `checked.txt` once all three are.
- The floor plan can colour each sensor by how well the hub hears its node, with a key, to show where the signal is weak.

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

[Unreleased]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/sjfaustino/esp32_sensors2onvif/releases/tag/v0.1.0
