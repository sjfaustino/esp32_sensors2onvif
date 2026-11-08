# esp32_sensors2onvif

A stand-alone ESP32 hub that receives alerts from slave ESP devices and presents itself as a normal ONVIF
camera. Add it to an NVR on any free channel. Each alert becomes a searchable motion record, and the picture
names the sensor that fired. The firmware knows nothing about NVR channels.

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.h` and fill in the Wi-Fi settings. Set `WEB_PASSWORD`
   to protect the file manager; left empty, anyone on the network can change the pictures.
2. Flash with `pio run -t upload`, which targets the Adafruit Feather ESP32-S2. Add `-e esp32dev` or `-e esp32s3` for
   those boards. Build from VS Code or PowerShell, because the pioarduino toolchain setup refuses to run under Git Bash.

The serial console prints the address once Wi-Fi is up. Open `http://<address>:8899/` for the file manager.

## Pictures and the file manager

Pictures are stored on LittleFS, and the folders are also the list of known sensors:

```
clear.h264                         optional replacement for the built-in "all clear"
AA-BB-CC-DD-EE-01/                 one folder per slave ESP, named after its MAC address
AA-BB-CC-DD-EE-01/1/               one subfolder per sensor on that slave
AA-BB-CC-DD-EE-01/1/active.h264    one file per state
```

The state files are `active`, `approaching`, `away`, `ltr` and `rtl`, each with a `-delayed` version such as
`approaching-delayed.h264`. A missing picture falls back to the nearest one that exists, and finally to a built-in
generic alert, so a sensor with no pictures still makes motion records.

The page can add and delete slaves and sensors, and upload, download and delete pictures. Changes apply at once,
without a reboot. Files must be a single H.264 picture of at most 8 KB in the hub's video format, and are stored with the hub's own header. Others are
refused, because the NVR cannot follow a format change mid-stream.

To change the words, open **Change the text** under a sensor, or beside `clear.h264`, and save. Your browser makes the
pictures and uploads them, so it needs internet access: it downloads the ffmpeg.wasm encoder from jsDelivr, about 30 MB
the first time. The hub remembers the words in a `text.json` file in the same folder.

Without internet, list the sensors in `sensors.json` and run `python tools/make_frames.py` (needs ffmpeg). The files
appear in `data/`, ready to upload one by one, or all together with `pio run -t uploadfs`, which replaces everything
on LittleFS.

```json
{ "sensors": [ { "mac": "AA-BB-CC-DD-EE-01", "input": 1, "label": "FRONT DOOR" } ] }
```

## Sensor input over ESP-NOW

Each device sends this message unicast to the MAC address the serial console prints. It is little-endian and 13 bytes,
plus 3 for each target.

| Bytes | Field |
|---|---|
| 1 | Version, `1` |
| 1 | Input number on the device, starting at `1` |
| 1 | Active: `1` active, `0` inactive |
| 1 | Battery percent, `0xFF` when unknown or mains powered |
| 4 | Event counter. A resend of the same event repeats it, and the hub ignores the repeat. |
| 4 | Age in seconds, `0` when live, `0xFFFFFFFF` when unknown |
| 1 | Number of targets, `0` to `3` |
| 2 per target | Distance in cm, `0xFFFF` when unknown |
| 1 per target | Direction: `0` unknown, `1` approaching, `2` moving away, `3` left to right, `4` right to left |

The hub only accepts a device and input that have a folder on LittleFS.

ESP-NOW has to use the same radio channel as the Wi-Fi network, so the channel is set by your router.
Sensors should try channels 1 to 13 until a send reports delivery, keep that channel, and search again if
sends start failing, because a router on automatic channel selection can move.

Devices that cache alerts should send them oldest first and keep resending until ESP-NOW reports delivery.
They should also resend their current state about hourly, because `/status` marks a sensor silent after 3 hours
without a message.

Alerts older than 30 seconds, or of unknown age, show the delayed picture and play back one at a time so each gets
its own NVR record. The NVR files them at arrival time. `GET /events` gives the true times.

ESP-NOW messages are not authenticated, so anything within radio range that knows a listed MAC address could send a
false alert.

## Sensor input over HTTP

Port 8899 also takes plain HTTP requests, which are useful for testing without a sensor:

| Request | Effect |
|---|---|
| `GET /sensor?mac=AA-BB-CC-DD-EE-01&input=1` | That sensor is active for 10 s. Another alert restarts the 10 s. `input` defaults to 1. |
| `GET /sensor?mac=AA-BB-CC-DD-EE-01&state=clear` | That sensor ends now. |
| `GET /sensor?mac=AA-BB-CC-DD-EE-01&direction=1&distance=250&age=100&battery=80` | Same as an ESP-NOW report. |
| `GET /status` | Active sensors, subscribers, streams and each sensor's last contact, as JSON. |
| `GET /events` | The last 64 state changes, newest first, with true event times. Cleared by a reboot. |

Add `&token=<value>` if `SENSOR_TOKEN` is set. Unknown sensors get a 404.
When several sensors are active, the picture alternates between them every 2 s.

## Adding it to the NVR

Search for it, or add it by address, with protocol ONVIF and port 8899. The firmware does not check the
camera user name or password. For motion records, turn on motion detection for that channel in the NVR,
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
