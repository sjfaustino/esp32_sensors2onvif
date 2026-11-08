# esp32_sensors2onvif

A stand-alone ESP32 that receives alerts from movement sensors and presents itself as a normal ONVIF
camera. Add it to an NVR on any free channel. Each sensor alert becomes a searchable motion record, and
the picture names the sensor that fired. The firmware knows nothing about NVR channels.

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.h` and fill in the Wi-Fi settings.
2. Edit `sensors.json` if you want your own sensor names, then run `python tools/make_frames.py` (needs ffmpeg).
3. Flash with `pio run -e esp32dev -t upload`, or `-e esp32s3` for an ESP32-S3.

The serial console prints the address once Wi-Fi is up.

## Sensor input over ESP-NOW

Each sensor sends a 5-byte message: `'S'`, `'C'`, `1` (version), the sensor id, and the state (`1` alert, `0` clear).
Send it unicast to the MAC address the serial console prints at boot.

ESP-NOW has to use the same radio channel as the Wi-Fi network, so the channel is set by your router.
Sensors should try channels 1 to 13 until a send reports delivery, keep that channel, and search again if
sends start failing, because a router on automatic channel selection can move.

ESP-NOW messages are not authenticated, so anything within radio range could send a false alert.

## Sensor input over HTTP

Anything on the network can also make a plain HTTP request to port 8899:

| Request | Effect |
|---|---|
| `GET /sensor?id=3` | Sensor 3 is active for 10 s. Another alert restarts the 10 s. |
| `GET /sensor?id=3&state=clear` | Sensor 3 ends now. |
| `GET /status` | Active sensors, subscribers and streams, as JSON. |

Add `&token=<value>` if `SENSOR_TOKEN` is set. Unknown ids get a 404.
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
