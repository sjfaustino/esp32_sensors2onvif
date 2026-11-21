#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_time.h"
#include <esp_sntp.h>
#include <sys/time.h>
#include "cam_board_rules.h"
#include "hub_settings.h"

bool clockSet() { return time(nullptr) > CAM_CLOCK_VALID_AFTER; }

// A manual time does not survive a power cut, so until it is set again NTP is asked, and stopped by keepTime once it answers.
void applyTime() {
  String tz = camPosixTz(settings.tzOffset, settings.tzDst).c_str();
  if (settings.timeNtp || !clockSet()) {
    configTzTime(tz.c_str(), settings.ntpServer.c_str());
  } else {
    esp_sntp_stop();  // otherwise NTP would overwrite the time that was set by hand
    setenv("TZ", tz.c_str(), 1);
    tzset();
  }
}

void keepTime() {
  if (!settings.timeNtp && esp_sntp_enabled() && clockSet()) applyTime();  // the one-off NTP reading after a power cut has arrived
}

static bool localNow(struct tm& local) {
  time_t now = time(nullptr);
  if (now < CAM_CLOCK_VALID_AFTER) return false;
  localtime_r(&now, &local);
  return true;
}

int localMinute() {
  struct tm local;
  return localNow(local) ? local.tm_hour * 60 + local.tm_min : -1;
}

int localWeekday() {
  struct tm local;
  return localNow(local) ? local.tm_wday : -1;
}

bool scheduledMinute() { return camScheduledMinute(settings.schedule, localWeekday(), localMinute()); }

void timeInfo(CamJson& j) {
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  char shown[24];
  strftime(shown, sizeof(shown), "%Y-%m-%d %H:%M:%S", &local);
  j.flag("ntp", settings.timeNtp).str("server", settings.ntpServer.c_str()).num("offset", settings.tzOffset).num("dst", settings.tzDst);
  j.flag("synced", clockSet()).str("local", shown);
}

bool saveTime(const CamTime& t) {
  if (!storeTime(t)) return false;
  if (!t.ntp && t.epoch) {  // before applyTime, so NTP is not started for a clock that is about to be set
    timeval now = {(time_t)t.epoch, 0};
    settimeofday(&now, nullptr);
  }
  applyTime();
  return true;
}
