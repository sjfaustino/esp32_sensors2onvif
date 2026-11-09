// Copy to include/secrets.h (ignored by git) and fill in.
#pragma once

// The first network to try. A network chosen on the setup page replaces it. May be left empty.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Password of the hub's own setup network, at least 8 characters. Empty keeps the setup network off.
#define SETUP_AP_PASSWORD ""

#define NTP_SERVER "pool.ntp.org"

// Optional. When set, sensors must add &token=<value> to their requests.
#define SENSOR_TOKEN ""

// Optional password for the file manager page (user name "admin").
#define WEB_PASSWORD ""
