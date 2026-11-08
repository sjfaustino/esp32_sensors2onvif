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

#define ONVIF_PORT 8899
#define RTSP_PORT 8554

// How long one sensor alert stays active. A new alert from the same sensor restarts it.
#define SENSOR_HOLD_MS 10000
