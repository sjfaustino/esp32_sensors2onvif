// Copy to include/secrets.h (ignored by git) and fill in. Empty SSID stops the firmware at boot.
#pragma once

#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define NTP_SERVER "pool.ntp.org"

// Optional. When set, sensors must add &token=<value> to their requests.
#define SENSOR_TOKEN ""
