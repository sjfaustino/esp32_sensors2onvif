# How it works

The README says what the hub does and what to set. This page is for whoever changes the code: how the hub keeps its
promises, and where.

## Saving before confirming

A report is confirmed, by ESP-NOW or by the HTTP 200, only once it is on storage: the event history, the past events
waiting for their NVR record, and the alerts whose delivery has not been checked yet. Reports that arrive together are
saved together, then confirmed in the same pass of the main loop. If the save fails, nothing is confirmed, the sensors
resend, and each resend tries the save again. Pictures may not use the last of the storage: what the history may still
need, up to about 600 KB, is kept free, because without it no report could be confirmed.

`waiting.bin` holds what still needs an NVR record: "WAIT", version 1, a count, then 9 bytes each, the MAC address,
input, direction and flags (1 for a record standing for several sensors, 2 for an alert the NVR missed, 4 for a replay
already logged). It is written with the report that starts an alert, and when a queue changes; after a restart its
entries play as past events. An alert whose delivery check has passed since may still be in it, so a power cut can cause
a false replay, never a missed one. A planned restart, such as a firmware update, writes the file as it is first, so it
replays nothing the NVR already got.

## The event history

`events.log` holds every event as a 96-byte record: little-endian, the same on every machine, with a checksum, added to
and never rewritten (`lib/sensorcam/cam_events.cpp`). A record cut short by a power cut is found and written over. At
256 KB the log becomes `events-old.log`, so about 5400 events are kept. At start only the newest 64 are read, for the
Events page. The CSV download is made from the log as it is sent, so its length is not known in advance; it goes in
HTTP chunks, so a download cut short is seen as failed rather than taken for the whole history.

While every web connection is busy, or memory is short, the hub does not accept new connections, so they wait in the
network stack's queue and are served once room frees, rather than being accepted and closed. A sensor's report waits
a moment instead of being lost. A large reply is sent from its own string, never copied after its headers, and a request
that still finds no block big enough fails alone with a 507: an out-of-memory abort would restart the hub, and in the
first minutes after an update would undo it. Running out of memory anywhere else also drops only the work that needed
it: an event push, a reply to the NVR's poll or a webhook message waits for a later pass, and the NVR's poll is still
owed the change it was not told of. Checking, starting, logging and confirming an ESP-NOW alert use no heap at all, so
an alert gets through even with no memory left; Device Info counts the work dropped. The Events page's list carries a
tag, so the page's request every 3 seconds is answered "not changed" until an event is added, and a list that has
changed is built at its exact size. Video is sent a packet at a time straight from the picture, and files and snapshots are read from storage as they are
sent, so the only picture-sized buffer is the one the pictures load into; a stream partway through a picture that is
being replaced drops the rest of that frame, and the next one plays. During a firmware upload the hub closes its video streams and takes no new
connections until the upload ends: on a board without PSRAM, an upload beside a running stream could use up the memory
and stop the hub.

Firmware before this log kept its newest events in `events.bin`, which is converted and removed, and possibly
`events.csv` files, which are served first in the download and never written again. If an update is undone, the older
firmware starts a new `events.bin`; the next update adds its events after the log's.

## Checking delivery, and replaying what the NVR missed

`CamDeliveryWatch` (`lib/sensorcam/cam_delivery.h`) checks each alert that starts a new NVR record, a few seconds after it
starts or when it ends if sooner. The NVR counts as listening when it holds a subscription and either the last event
pushed to it arrived, or, for an NVR that pulls, it asked for events within the last minute; other requests from it do
not count. A failed push is tried again every 30 seconds, so one failure cannot leave the NVR counted as gone. An alert
the NVR missed is logged once, kept, and played as a past event once it listens again; a replay missed too waits again
without a second entry. Delivered is all the hub can know: whether the NVR records depends on its own settings.

## MQTT

`lib/sensorcam/cam_mqtt_client.cpp` keeps one connection to the broker and holds one message at a time, built only once
the one before has gone, in this order: the hub's "online", any alert that started or ended, the next event, Home
Assistant's discovery, other state changes, then a ping. Events are not queued anywhere new: the connection reads them
from the event history's newest 64 and remembers how far the broker has acknowledged, so logging an alert costs MQTT
nothing, and an event in flight when the connection drops is sent again. Building a message that runs out of memory
leaves it for a later pass. The connection takes one of the sockets web connections use, so with MQTT on, five web
connections can be open at once instead of six.

## Past events

Late alerts wait in `CamDelayedQueue` (`lib/sensorcam/cam_sensors.h`) and play one at a time, a few seconds apart, so
each gets its own NVR record. A live alert never shares a record with one: a past event that is playing stops when a live
alert starts, and goes back in the queue to play in full afterwards. Late alerts from one sensor within its hold of each other share a record, as they would
have live. Up to 16 records wait; beyond that a late alert joins a waiting one from the same sensor, or else the newest
waiting record, which then stands for several sensors and shows the built-in "PAST EVENT, CHECK LOGS".

## Where things are

`lib/s2o_proto` holds every ESP-NOW message, SHA-1 and X25519, with no heap and no Arduino, and is the one copy the
nodes build too: the node project has this repository as a git submodule. `lib/sensorcam` is the hub. `SensorCamServer`
(`cam_server.h`) owns the hub's parts and runs them in one select loop, each waiting on its own sockets; the PC build
runs the same loop in one process, which is what the end-to-end tests drive. The parts:

- `CamAlarm` (`cam_alarm.h`): the sensor table, reports, node health, arming, the event history, the alerts waiting for an
  NVR record, and the delivery check. It decides; the others carry what it decides. Per-node data is in its `Node`
  entries, one per node folder, which each sensor's `Slot` points to.
- `CamNodes` (`cam_nodes.h`): pairing, and the node firmware files (`CamNodeImages`).
- `CamOnvifEvents` (`cam_onvif_events.h`): the NVR's event subscriptions (`SubTable`), the pushes to them, WS-Discovery,
  and whether the NVR is listening.
- `CamVideo` (`cam_video.h`): the RTSP connections and the picture they send.
- `CamWeb` (`cam_web_server.h`): HTTP, which carries the ONVIF requests, the pages and their API, sensor reports and
  uploads.
- `CamMqttClient` (`cam_mqtt_client.h`) and `CamWebhookSender` (`cam_webhook.h`): Home Assistant, and the webhook.
  The webhook and the event pushes each send through a `CamPost`, one HTTP POST that does not block.

What the board does for the hub reaches them through three interfaces in `cam_config.h`: `CamChip`, `CamRadio` and
`CamStore`. Each part is made after the parts it uses and is handed them directly. The alarm is made first, so what it
needs from the others goes through `CamAlarmOutside`, which the server answers. Running out of memory is counted in one
`CamMemoryFailures`, which every part adds to.

## Pairing

`lib/s2o_proto/s2o_x25519.cpp` is X25519 after TweetNaCl, checked against RFC 7748's vectors. It is slow, tens of
milliseconds on the board, and runs only when the hub starts and when a node is approved; reports never use it. A paired
node's key is read from its `pairing.txt` into its sensors' rows when the sensor table is rebuilt, so checking its reports
picks the key by the sender and needs no memory, as before.

A node's info (message 6) is read the same way, from its `about.txt`, into each of its sensors' rows. It arrives with
every regular report but is written only when it differs, so flash is not worn. The silent check asks `silentMsFor`: the
node's own setting, else a quarter over its report time, else the hub's 3 hours.

A paired node's fresh number lives in its `Node` entry: the newest and the one before, so a confirmation lost on the
air costs nothing. It changes with every report that is not a resend, and is only in memory, so after a restart of the
hub every node proves itself once.

A sealed message (7) is opened with the key of the node it comes from, found by its MAC address alone, and the message
inside gets a signature made with that key appended, so `espNowReport` checks it as if it had come plain; `sealed` then
makes it seal the confirmation. All of it is on the stack, so a sealed alert is still confirmed with no memory left.
What a confirmation carries for the node comes from `forNode`, which reads only the sensor table and the clock.

## Node firmware

An upload to `/api/node-firmware` streams into `nodes/upload.bin`, as the hub's own update streams to flash, and is then
read through once by `camCheckNodeImage`: the ESP32 image's first byte and chip, the version after
`sensors2onvif-node-version=`, and an FNV-1a hash of the whole file, which is the update's number. Its description,
`nodes/<chip>.txt`, is written once it is renamed into place, so a start of the hub reads only the small files. A node's
ask (message 8) is answered at once from storage with up to 4 parts, read in one piece. Nothing is kept per transfer but
the offset last asked for, for the page; a node that stops asking for 2 minutes is no longer shown as updating.

A node's battery readings go to `battery.txt` at most every 6 hours, after its confirmation is queued and outside it, so
a lack of memory skips a reading and never an alert. The forecast is a least-squares line through the last 30 days.

## Passwords and names

`CamLoginGuard` (`lib/sensorcam/cam_auth.h`) counts wrong passwords per address, on the web page, ONVIF and RTSP: ten in
a row lock the address for a minute, each one after that doubles the lock up to 15 minutes, and the count is kept for a
day or until a right password. Asking without a password does not count. `camKnownHost` lets the web port answer only to
the hub's address, its name alone or with `.local`, `.lan`, `.home`, `.home.arpa`, `.localdomain` or `.internal`, the
setup address and the names set on the HTTP page, which stops DNS rebinding; sensor reports are exempt, because their
token guards them. The snapshot token is an HMAC of the password with a secret kept on the hub. The page checks the
downloaded encoder against the SHA-256 of the published files before running it. ONVIF logins are not checked for their
time or reused nonces, because NVRs differ there and refusing the NVR would stop recording.

## Timings

Every time the hub waits for is in `lib/sensorcam/cam_timing.h`, each marked as measured on the NVR or chosen.

## Changing the web pages

The pages are ordinary files in `web/`: `index.html` is the main page and `setup.html` the Wi-Fi setup page. The main
page's scripts are `sensors.js` (the Sensors page, and the helpers the others use), `pictures.js` (making pictures),
`sha256.js` (checking the downloaded encoder), `encoder-worker.js` (the encoder's worker, carried in the page as text) and
`index.js` (the menu and every other page). Opening `web/index.html` in a browser shows the layout, though nothing loads
without a hub. Every build runs `tools/embed_web.py`, which puts each stylesheet and script a page links inside it, gzips
it and builds it into the firmware. `node --check` finds script mistakes before a build. The scripts and styles are laid
out by Prettier, and the checks on GitHub refuse them otherwise; after an edit, run
`npx prettier@3.3.3 --print-width 120 --end-of-line lf --write web/*.js web/*.css`.
