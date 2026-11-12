# esp32_sensors2onvif

A stand-alone ESP32 hub that receives alerts from ESP sensor nodes and presents itself as a normal ONVIF
camera. Add it to an NVR on any free channel. Each alert becomes a searchable motion record, and the picture
names the sensor that fired. The firmware knows nothing about NVR channels.

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.h`. Set `SETUP_AP_PASSWORD` (at least 8 characters) to enable
   the setup network, and optionally fill in the first Wi-Fi network to try. Set `WEB_PASSWORD` to choose the hub
   password; left empty, the setup network's password is used.
2. Flash with `pio run -t upload`, which targets a regular ESP32 with 16 MB of flash. Add `-e esp32dev` for one with 4 MB,
   or `-e esp32s3` for an ESP32-S3.
   Build from VS Code or PowerShell, because the pioarduino toolchain setup refuses to run under Git Bash.

Open `http://esp-alarmhub.local` for the file manager, from a device on the same network as the hub. The name does not
work through a VPN, and some devices cannot resolve `.local` names. In those cases use the address printed on the serial
console once Wi-Fi is up, as `http://<address>/`. Add the hub to the NVR by address, not by name.

### IP address

**Network Manager > Network** switches between DHCP and a fixed address. A fixed address is used only on the WiFi network the
hub was connected to when it was saved, and other networks use DHCP. If the router cannot be found from a fixed address, the
hub forgets it about two minutes after joining and goes back to DHCP.

### Changing the Wi-Fi network

The hub keeps a main and a backup network. It tries the main network for 30 seconds, then the backup for 30 seconds. If
the backup joins, it becomes the main network and the old main becomes the backup.

If neither works, or none is saved, the hub opens its own network, `esp-alarmhub-setup`, protected by `SETUP_AP_PASSWORD`.
Join it from a phone or laptop and the setup page opens by itself, or browse to `http://192.168.4.1`. Choose a network, and
whether it is the main or the backup. A new main network moves the previous main to the backup. Saved settings replace the
one in `secrets.h` and survive reflashing.

The setup network closes as soon as the hub rejoins Wi-Fi, and the hub keeps trying both networks while nobody is connected
to the setup network. Networks can also be changed at any time from **Network Manager > WiFi** on the web page.

## Events and alerts

**Sensors Manager > Events** lists the last 64 events and keeps them across restarts. They are saved within 5 seconds, so a crash or
power cut can lose the last few. **Download the full history (CSV)** gives every event the hub has kept, about the last 5000, in
UTC. A sensor can be given a name on the Sensors page, which then appears on the ESP-NOW and Events pages.

A node that stays silent for 3 hours, counted from the last restart if it has not reported since, reports 20% battery or
less, or whose ESP-NOW signal stays below -80 dBm, is logged as an event. The ESP-NOW page can also raise an NVR alarm for
silence and low battery. A sensor that is unplugged, smashed or taken is only noticed once it has been silent that long,
so set **Report this node silent after** under its Node settings to a little over the time between its regular reports,
from 1 minute to 7 days, and have the node report that often.

The hub cannot report its own death: while it has no power or no Wi-Fi, nothing reaches the NVR, and the hub logs how long
it was off only once it is back. Turn on the NVR's video-loss alarm for the hub's channel so the NVR notices, and power the
hub from a small UPS.

The hub also checks that each alert reached the NVR. Five seconds after an alert starts, or when it ends if that is sooner,
the NVR must hold an event subscription and the last event pushed to it must have arrived, or, for an NVR that pulls
events, it must have asked within the last minute. If not, the history gets **Alert not recorded** for that sensor, and
Notifications sends it. With the NVR's address set on the NVR page only that NVR counts; without it, any subscriber does,
and the page warns about that at the top. When the NVR has not been listening for 3 minutes,
the hub logs **NVR stopped listening** once, and **NVR listening again** when it is back.

Each time the hub starts it logs **Hub started** with the reason, such as a power cut or a firmware update, and how long
it was off. It saves the time every 10 minutes and before a planned restart, so after a power cut the time off is an upper
bound, at most 10 minutes too long. It is unknown until the clock has been set once.

**Alert settings** under each sensor on the Sensors page set how long its alert lasts, instead of the hub's 10 seconds, and
quiet hours in local time. During quiet hours that sensor's alerts are logged as "alert in quiet hours" but are not sent to the
NVR or to Notifications. Quiet hours need the clock to be set; until it is, alerts always go through. The settings are kept in
`settings.txt` in the sensor's folder.

## Notifications

**Sensors Manager > Notifications** sends each alert, and optionally each node problem, to a web address on your network, such as
a Home Assistant webhook, Node-RED or a self-hosted ntfy server. The hub posts JSON like
`{"event":"alert","sensor":"AA-BB-CC-DD-EE-01/1","name":"Gate","battery":80,"time":"2026-11-11T18:00:12Z","hub":"esp-alarmhub.local","message":"Gate (AA-BB-CC-DD-EE-01/1): alert"}`,
or just the message as plain text. Events are `alert`, `delayed alert`, `went silent`, `low battery`, `weak signal`,
`alert not recorded`, `delayed alert not recorded`, `NVR not listening`, `NVR listening again`, `hub started` and `test`.
The hub's own events have `"sensor":"hub"` and an empty name, and give the reason a hub started as `"detail"`. An alert not recorded is sent
whenever alerts or node problems are; the hub's own events are sent with node problems.
The address must be `http://` with an IP address, because the hub has no name lookup or TLS for this. Messages go one at a
time; up to 8 wait, and the page shows how the last one went.

## ONVIF thumbnails

ONVIF clients ask the hub for a JPEG of the current picture. The hub stores only H.264 and cannot convert it, so the browser
saves a small JPEG beside each picture it uploads. **Create missing thumbnails** on the Sensors page makes them for pictures
that have none. A picture without a thumbnail shows the built-in All Clear, Alert or Past Event image instead.

## Security

One hub password, with the user name `admin`, protects the web page, ONVIF and RTSP. It comes from `WEB_PASSWORD`, or from
`SETUP_AP_PASSWORD` when that is empty, and can be changed or turned off under **Network Manager > HTTP**. An NVR needs the same
user and password on its channel. ONVIF clients may ask for the clock and capabilities before logging in, as ONVIF requires.
With no password, from `secrets.h` or after turning it off, anyone on the network can change the hub and watch its video;
the web page then says so at the top.

HTTP sensor reports need `&token=<value>`. The hub makes a token the first time it starts and shows it on the HTTP page.
ESP-NOW reports are signed with a separate key instead; see below.

Ten wrong passwords in a row from one address, on the web page, ONVIF or RTSP, shut that address out for a minute, even
with the right password. Asking without a password, as a browser or NVR first does, does not count. The web port answers
only to the hub's own IP address, its name alone or with `.local`, `.lan`, `.home`, `.home.arpa`, `.localdomain` or
`.internal`, and the setup address, so a website cannot point its own name at the hub and use your browser to reach it.
The ONVIF snapshot address carries a token made from the password and a secret that never leaves the hub, so the address
cannot be used to guess the password. The picture encoder the page downloads is checked against the published files
before it runs, because it runs with your login.

Passwords travel over the network unencrypted, as HTTP, ONVIF and RTSP between local devices usually do, and are kept on
the hub's flash unencrypted, so someone who takes the hub can read the Wi-Fi password, the hub password and the ESP-NOW key.
Keep the hub out of reach. ONVIF logins are not checked for their time or reused nonces, because NVRs differ in how they
send them and refusing your NVR's login would stop recording; every ONVIF request only reads, so a replayed one can at most
subscribe, and a subscription from the NVR's address is never pushed out by other clients.

## System Manager

**Maintenance** schedules a restart, restarts the hub now, updates the firmware from a `firmware.bin` file, downloads and
restores a settings backup (which holds the passwords in plain text), makes and restores a full backup, and resets the settings.
A new firmware must stay on the network for a minute; if it crashes, stalls or loses the network first, or the power fails
during that minute, the hub goes back to the previous firmware and Device Info says so. A reset erases the saved WiFi
networks, ports, name, web password, fixed address, time and notification settings, and keeps the pictures, the sensor nodes,
and the ESP-NOW key and token that sensors use.

A **full backup** is one `.tar` file with the settings, every picture and thumbnail, each sensor's name and alert settings, and each node's settings.
Your browser builds it one file at a time, so it works however many pictures there are. Restoring adds the nodes, sensors and
pictures in it, replacing any with the same name and keeping others, then replaces the settings and restarts the hub. Like the
settings backup, it holds the passwords in plain text.

**Time** sets the timezone, daylight saving, and NTP or manual time, and can read the time from the PC. The NVR always
receives UTC, so these only change the time shown on the page and the scheduled restart. A manual time does not survive
a power cut, so the hub then reads the time once from the NTP server if it can reach it; otherwise set it again.

**Device Info** shows firmware and hardware details, uptime, why the hub last restarted, memory, storage and WiFi status.
The firmware version is the git commit it was built from, such as `c336d03`, or the tag on that commit; `-modified`
means it was built with changes that were not committed.

After a crash, Device Info names the task and address where it happened and offers the crash report the chip saved,
until it is deleted or the next crash replaces it. The report holds a copy of the hub's memory, which can include
passwords. Reading it needs the `firmware.elf` of the exact build that crashed, because two builds of the same commit
differ. Each build keeps a copy in `builds/`, named after the board and version, such as
`builds/esp32_16mb-0adf209.elf`, and the newest 10 per board are kept. Then:

```
python -m esp_coredump --chip esp32 info_corefile -t raw -c hub-crash.bin firmware.elf
```

using the Python that comes with PlatformIO, with `tool-xtensa-esp-elf-gdb\bin` from the PlatformIO packages on the
PATH. Use `--chip esp32s3` for an S3 board.

## ESP-NOW page

**Network Manager > ESP-NOW** shows the hub's MAC address, WiFi channel and ESP-NOW key, which sensor nodes need, and lists every
sensor node with its state, last contact, battery, signal strength, last report number and refused messages. A signal marked
"weak" has averaged below -80 dBm over the last few messages; it is cleared above -75 dBm. Only nodes added under
**Sensors Manager > Sensors** are listed as nodes, and the page refreshes itself every few seconds. Valid reports from anything
else are ignored, and the eight most recent such senders appear under **Unknown senders** until the hub restarts or they are added.

## NVR page and live video

**NVR Manager > NVR** stores the NVR's IP address and shows whether that NVR has the video and motion events open. The address
is used only for that status: the NVR connects to the hub, and the hub never contacts the NVR.

**NVR Manager > Live Video** shows the picture the hub is sending to the NVR and names the file it came from. The page asks
once a second which picture is being sent, and your browser decodes a new one only when it changes, so no extra video stream
runs. The first use on a device needs internet access to load the decoder, about 30 MB.

## Pictures and the file manager

Pictures are stored on LittleFS, and the folders are also the list of known sensors:

```
clear.h264                         optional replacement for the built-in "all clear"
AA-BB-CC-DD-EE-01/                 one folder per sensor node, named after its MAC address
AA-BB-CC-DD-EE-01/settings.txt     optional past-event and silent times for the node, set on the Sensors page
AA-BB-CC-DD-EE-01/1/               one subfolder per sensor on that node
AA-BB-CC-DD-EE-01/1/active.h264    one file per state
AA-BB-CC-DD-EE-01/1/name.txt       optional name, set on the Sensors page
AA-BB-CC-DD-EE-01/1/settings.txt   optional alert time and quiet hours, set on the Sensors page
```

The state files are `active`, `approaching`, `away`, `ltr` and `rtl`, each with a `-delayed` version such as
`approaching-delayed.h264` for past events. A missing picture falls back to `active.h264` or `active-delayed.h264`, and
finally to the built-in red "ALERT" or amber "PAST EVENT, CHECK LOGS", so a sensor with no pictures still makes motion
records. A past event never falls back to a live picture.

The page can add and delete nodes and sensors, and upload, download and delete pictures. Changes apply at once,
without a reboot. Pictures cannot fill the storage: the room the event history may still need, up to about 600 KB, is
kept free, and the Sensors page shows how much that is. Without it the hub could not save reports, and so could not
confirm them to the sensors. Files must be a single H.264 picture of at most 32 KB in the hub's video format, and are stored with the hub's own header. Others are
refused, because the NVR cannot follow a format change mid-stream.

To change the words, open **Change the text** under a sensor, or beside `clear.h264`, and save. Your browser makes the
pictures and uploads them, so it needs internet access: it downloads the ffmpeg.wasm encoder from jsDelivr, about 30 MB
the first time. The hub remembers the words in a `text.json` file in the same folder.

To use a photo or drawing instead, click **Use an image** on any picture. The browser opens the image, fits it to
640x360 with black bars if the shape differs, adds any text you type, and encodes it. Detailed images are made softer
step by step until they fit the 32 KB limit, and a preview shows the result before you save. Only the finished picture
is stored on the hub, not the original. Saving **Change the text** replaces all 10 of that sensor's pictures,
including any images.

Without internet, list the sensors in `sensors.json` and run `python tools/make_frames.py` (needs ffmpeg). The files
appear in `data/`, ready to upload one by one, or all together with `pio run -t uploadfs`, which replaces everything
on LittleFS.

```json
{ "sensors": [ { "mac": "AA-BB-CC-DD-EE-01", "input": 1, "label": "FRONT DOOR" } ] }
```

## Sensor input over ESP-NOW

Each device sends this message unicast to the MAC address the serial console prints. It is little-endian and 13 bytes,
plus 3 for each target, plus the 8-byte signature in version 2.

| Bytes | Field |
|---|---|
| 1 | Version: `2` signed, `1` unsigned |
| 1 | Input number on the device, starting at `1` |
| 1 | Active: `1` active, `0` inactive |
| 1 | Battery percent, `0xFF` when unknown or mains powered. Other values above 100 also count as unknown. |
| 4 | Event counter. A resend of the same event repeats it, and the hub ignores a repeat within a minute. It need not survive the sensor's restarts. |
| 4 | Age in seconds, `0` when live, `0xFFFFFFFF` when unknown |
| 1 | Number of targets, `0` to `3` |
| 2 per target | Distance in cm, `0xFFFF` when unknown |
| 1 per target | Direction: `0` unknown, `1` approaching, `2` moving away, `3` left to right, `4` right to left |
| 8 | Version 2 only: the first 8 bytes of HMAC-SHA1, keyed with the ESP-NOW key as text, over the sender's 6-byte MAC address followed by every byte above |

The hub only accepts a device and input that have a folder on LittleFS.

ESP-NOW has to use the same radio channel as the Wi-Fi network, so the channel is set by your router.
Sensors should try channels 1 to 13 until a send reports delivery, keep that channel, and search again if
sends start failing, because a router on automatic channel selection can move.

Once it has accepted a report and saved it to storage, the hub sends a **confirmation** back to that MAC address, usually
within 100 ms; wait at least 300 ms before resending. The save comes first, so a power cut cannot lose an alert its
sensor has stopped sending: the event history, the past events waiting for their NVR record, and the alerts whose
recording has not yet been checked are all on storage, and after a restart those last two play as past events. If the
save fails, no confirmation is sent and the next resend tries again. A sensor should resend a report, with the same number, until the confirmation arrives: delivery by ESP-NOW only
means the radio got it. A resend is confirmed again. Messages that are refused, malformed or from a device without a
folder get no confirmation. The confirmation is 15 bytes:

| Bytes | Field |
|---|---|
| 1 | `3`, a confirmation |
| 1 | The report's input number |
| 1 | The report's active value |
| 4 | The report's event counter |
| 8 | The first 8 bytes of HMAC-SHA1, keyed with the ESP-NOW key, over the **sensor's own** 6-byte MAC address followed by the 7 bytes above |

A sensor should check the signature, so that nothing else in radio range can stop it resending. The confirmation means
the hub has the alert, not that the NVR recorded it; the hub reports that separately (see Events and alerts).

Devices that cache alerts should send them oldest first and keep resending each until it is confirmed.
They should also resend their current state about hourly, because `/status` marks a sensor silent after 3 hours
without a message.

An alert lasts its hold time, 10 seconds or the sensor's own alert time, from the sensor's latest activation. Another
activation before then extends it, so a person moving about makes one NVR record rather than several. A clear report is
logged but does not end the alert early. Every activation that starts an alert, and every clear report, is in the event
history.

A node has no clock, so it sends how many seconds ago the event happened, and the hub works out the time. An alert older
than 30 seconds, or of unknown age, is a **past event**. The time can be set for each node under **Node settings** on the
Sensors page, from 1 to 86400 seconds; set it to how long that node can take to deliver an alert, retries included. A past
event shows the sensor's `-delayed` picture, or else the built-in amber "PAST EVENT, CHECK LOGS", never the live picture,
so whoever watches the recording can tell it did not happen at that moment. Past events play back one at a time so each
gets its own NVR record. Late alerts from one sensor that happened within its hold of each other make one record, as they would
have live. Up to 16 records can wait; beyond that a late alert joins a waiting one from the same sensor, or, if there is
none, the newest waiting record, which then shows the built-in "PAST EVENT, CHECK LOGS" because it stands for several
sensors. So every past event gets an NVR record, and the history says which sensors they were. The NVR files them at arrival time. `GET /events` gives the true times.

The ESP-NOW key is made the first time the hub starts and is shown on the ESP-NOW page. It is kept by a settings reset and
carried in backups. The hub refuses a version 2 message with a wrong signature; the ESP-NOW page counts these. The report
number is never a reason to refuse a correctly signed message, so a sensor that restarts its count, or loses it with a
battery change, is never ignored. It is used only to ignore a resend: the same number and state again within a minute.
The cost is that someone who records a sensor's message could play it back later to make a false alert, though not a
false all-clear, because a clear report never ends an alert. A missed intruder is worse than a false alert.

Version 1 messages carry no signature, so anything within radio range that knows a listed MAC address could send a false
alert. The hub refuses them unless **Also accept unsigned messages** is ticked on the ESP-NOW page.

## Sensor input over HTTP

Port 8899 is for ONVIF only. The web port, 80, also takes plain HTTP requests, which are useful for testing without a sensor.
The ports can be changed on the web page, under **Network Manager > ONVIF**, **HTTP** and **RTSP**. The HTTP page also sets the hub's
`.local` name and turns the web password on or off. These are saved on the board, replace the values in `config.h` and
`secrets.h`, and take effect after the hub restarts by itself. After changing the ONVIF port, set the same port on the NVR
channel. The serial heartbeat always shows all three ports. A forgotten web password can only be cleared by erasing the board's
settings over USB, which also erases its Wi-Fi networks.

| Request | Effect |
|---|---|
| `GET /sensor?mac=AA-BB-CC-DD-EE-01&input=1` | That sensor is active for 10 s, or its own alert time. Another alert restarts it. `input` defaults to 1. |
| `GET /sensor?mac=AA-BB-CC-DD-EE-01&state=clear` | Logged in the event history. The alert still lasts its hold. |
| `GET /sensor?mac=AA-BB-CC-DD-EE-01&direction=1&distance=250&age=100&battery=80` | Same as an ESP-NOW report. |
| `GET /status` | Active sensors, subscribers, streams and each sensor's last contact, as JSON. |
| `GET /events` | The last 64 events, newest first, with true event times. Kept across restarts. |

Sensor reports need `&token=<value>`, using the token shown on the HTTP page. `/status` and `/events` need the hub login.
Unknown sensors get a 404. The 200 reply is the confirmation and comes only once the report is saved; if it could
not be saved the reply is 507, and the sensor should send it again.
When several sensors are active, the picture alternates between them every 2 s.

## Adding it to the NVR

Search for it, or add it by address, with protocol ONVIF and port 8899. Enter the user `admin` and the hub
password on the channel, unless password protection is off on the HTTP page. For motion records, turn on motion detection for that channel in the NVR,
with recording and logging enabled. The event comes from the camera, so the NVR's picture-based detection
has nothing to do.

## Tests

No hardware or NVR needed. Everything runs on loopback.

```
pio test -e native
pio run -e host
python tools/e2e_test.py
```

`tools/e2e_test.py` can also test a flashed board. Its header explains how.

## Changing the web pages

The pages are ordinary files in `web/`: `index` is the main page and `setup` the Wi-Fi setup page, each with its own
`.html`, `.css` and `.js`. Opening `web/index.html` in a browser shows the layout, though nothing loads without a hub.
Every build runs `tools/embed_web.py`, which puts each page's stylesheet and script inside it, gzips it and builds it
into the firmware. The hub sends the pages gzipped, which every browser accepts. `node --check web/index.js` finds
script mistakes before a build. The scripts and styles are laid out by Prettier, and the checks on GitHub refuse them
otherwise; after an edit, run `npx prettier@3.3.3 --print-width 120 --end-of-line lf --write web/*.js web/*.css`.
