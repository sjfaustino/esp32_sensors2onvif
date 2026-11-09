#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// Optional password for the file manager page (user name "admin"). Empty leaves it open on the network.
#ifndef WEB_PASSWORD
#define WEB_PASSWORD ""
#endif

#define FIRMWARE_VERSION "1.0.0"

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
