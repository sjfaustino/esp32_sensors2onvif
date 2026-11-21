# Changelog

What changed in each release, newest first. Versions follow [semantic versioning](https://semver.org) from 1.0.0;
before that, a new minor version may change settings or the messages.

## [Unreleased]

### The hub

- Notifications go to ntfy or Gotify as those servers take them, with a title and a priority for the phone (alerts
  urgent, problems high, news low), and an access token. The Notifications page explains what each needs.
- A weekly summary, on the day and time chosen: the week's alerts and whether they reached the NVR, the sensor that
  fired most, the problems, the batteries running low and the last test alert. It can also be sent at once.
- Each sensor can name the NVR camera that covers it. Its alert notifications and the Events page say which camera to
  look at.
- **Getting started** on the Overview takes a new hub through its setup a step at a time, saying what to do and why.
- The pages follow the device's dark setting, and fit a phone: forms wrap and wide tables scroll.
- The setup network uses WPA3, so its password, usually the hub password, cannot be guessed offline from a recording of
  someone joining it.
- A blank hub's first password guards it as soon as it is chosen, not only after the restart that follows.
- The weekly summary reads the history a little at a time, so the hub stays responsive meanwhile.
- A new hub password, or other names for the hub, apply at once. Only a new port or hub name restarts the hub.
- Someone on the network can no longer keep the NVR out by holding the hub's few connections: a refused request's body
  is read for at most a minute, and the NVR always gets a connection, in place of the oldest that is not its own.
  Kept out for 10 minutes, its subscription lapsed and alerts stopped reaching it.
- Sealed messages are numbered on from where the last start left off, reserved in flash, so no number seals two
  messages after a power cut; on the hub and on the nodes.
- The hub shows fingerprints 16 characters long; a node's 12-character label is their start, and still matches.
- ONVIF logins that come again with the same nonce are counted on the ONVIF data the page reads, to learn whether the
  NVR ever repeats one before such replays are refused.
- The video's login is checked in the same time, right or wrong.
- Approving a node approves the key whose fingerprint was checked. A hello from the node's address with another key,
  as anyone in radio range could send while pairing is open, replaced the one waiting, and approval by address alone
  paired that key instead.
- The hub's pages can no longer be shown in a frame by another site, which with the browser's saved login could lead
  someone to click Disarm unseen; the settings backup, with its passwords, is never cached.
- The NVR's video can no longer be kept out: a video connection that is not playing is closed two minutes after it
  opened, however much it sends, and the NVR always gets one, in place of the oldest that is not its own.

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

[Unreleased]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/sjfaustino/esp32_sensors2onvif/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/sjfaustino/esp32_sensors2onvif/releases/tag/v0.1.0
