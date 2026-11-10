// Copy to include/secrets.h (ignored by git) and fill in.
#pragma once

// The first network to try. A network chosen on the setup page replaces it. May be left empty.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Password of the hub's own setup network, at least 8 characters. Empty keeps the setup network off.
#define SETUP_AP_PASSWORD ""

#define NTP_SERVER "pool.ntp.org"

// Sensors must add &token=<value> to HTTP reports. Left empty, the hub makes one and shows it on the HTTP page.
#define SENSOR_TOKEN ""

// The hub password (user name "admin") for the web page, ONVIF and RTSP. Left empty, the setup network's password is used.
#define WEB_PASSWORD ""
