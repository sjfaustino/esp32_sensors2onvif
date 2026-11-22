#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// The hub password (user name "admin") for the web page, ONVIF and RTSP. Empty falls back to the setup network's password.
#ifndef WEB_PASSWORD
#define WEB_PASSWORD ""
#endif

// The public half of the key that signs firmware updates; the hub keeps no update sent over the network without its
// signature (lib/sensorcam/cam_firmware_sig.h, tools/sign_firmware.py). A build signed with another key can name that
// key's public half in secrets.h; a hub then takes updates signed only with that key, until it is flashed over its cable.
#ifndef FIRMWARE_PUBLIC_KEY
#define FIRMWARE_PUBLIC_KEY "6916f0b42782640b9938de592599bb604a38815633937c87529c425846687e68"
#endif

// tools/version.py writes version.h with the git commit or tag before each firmware build.
#if __has_include("version.h")
#include "version.h"
#endif
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef FIRMWARE_RELEASE
#define FIRMWARE_RELEASE 0  // an update older than this release is refused
#endif

#define ONVIF_PORT 8899  // ONVIF only. Its usual default is 80, which the web interface uses here.
#define WEB_PORT 80      // the file manager, sensor input, status and events
#define RTSP_PORT 8554

// The file manager is at http://<MDNS_NAME>.local on the same network.
#define MDNS_NAME "esp-alarmhub"

// The hub's own network, opened when it has no Wi-Fi settings or cannot join its network.
#define SETUP_AP_NAME "esp-alarmhub-setup"
#ifndef SETUP_AP_PASSWORD
#define SETUP_AP_PASSWORD ""  // at least 8 characters; empty keeps the setup network off
#endif

// How long one sensor alert stays active. A new alert from the same sensor restarts it.
#define SENSOR_HOLD_MS 10000
