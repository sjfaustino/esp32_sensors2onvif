#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#define ONVIF_PORT 8899
#define RTSP_PORT 8554

// How long one sensor alert stays active. A new alert from the same sensor restarts it.
#define SENSOR_HOLD_MS 10000
