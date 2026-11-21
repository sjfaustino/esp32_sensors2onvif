#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_time.h"
#include <esp_sntp.h>
#include <sys/time.h>
#include "hub_settings.h"

static String tzName(int minutes) {
  char b[12];
  snprintf(b, sizeof(b), "<%c%02d%02d>", minutes < 0 ? '-' : '+', abs(minutes) / 60, abs(minutes) % 60);
  return b;
}

// POSIX timezone text. Its sign is the opposite of the usual one: UTC+1 is written as -1.
static String posixTz() {
  char offset[10];
  snprintf(offset, sizeof(offset), "%c%d:%02d", settings.tzOffset < 0 ? '+' : '-', abs(settings.tzOffset) / 60, abs(settings.tzOffset) % 60);
  String tz = tzName(settings.tzOffset) + offset;
  char rule[32];
  if (settings.tzDst == 1) {  // Europe changes at 01:00 UTC everywhere
    int hour = settings.tzOffset / 60;
    snprintf(rule, sizeof(rule), ",M3.5.0/%d,M10.5.0/%d", 1 + hour, 2 + hour);
    tz += tzName(settings.tzOffset + 60) + rule;
  } else if (settings.tzDst == 2) {
    tz += tzName(settings.tzOffset + 60) + ",M3.2.0/2,M11.1.0/2";
  }
  return tz;
}

bool clockSet() { return time(nullptr) > CAM_CLOCK_VALID_AFTER; }

// A manual time does not survive a power cut, so until it is set again NTP is asked, and stopped by keepTime once it answers.
void applyTime() {
  String tz = posixTz();
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

bool scheduledMinute() {
  struct tm local;
  if (!settings.schedule.on || !clockSet() || !localNow(local)) return false;
  return (settings.schedule.day < 0 || settings.schedule.day == local.tm_wday) && local.tm_hour * 60 + local.tm_min == settings.schedule.minute;
}

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
