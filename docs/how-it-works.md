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
a false replay, never a missed one.

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
first minutes after an update would undo it.

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

## Past events

Late alerts wait in `CamDelayedQueue` (`lib/sensorcam/cam_sensors.h`) and play one at a time, a few seconds apart, so
each gets its own NVR record. A live alert never shares a record with one: a past event that is playing stops when a live
alert starts, and goes back in the queue to play in full afterwards. Late alerts from one sensor within its hold of each other share a record, as they would
have live. Up to 16 records wait; beyond that a late alert joins a waiting one from the same sensor, or else the newest
waiting record, which then stands for several sensors and shows the built-in "PAST EVENT, CHECK LOGS".

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
