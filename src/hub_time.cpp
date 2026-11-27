#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_time.h"
#include <esp_sntp.h>
#include <sys/time.h>
#include <atomic>
#include "cam_board_rules.h"
#include "cam_web_checks.h"
#include "hub_settings.h"

// The names the time library is given: it keeps pointers to them, not copies, so they live here, and change only while
// it is stopped.
static std::string servers[3];
// Set on the network task, where readings arrive, and read in the loop, which logs a refusal: on the other core, so the
// refusal and its step are read and written together, under the lock.
static std::atomic<bool> trustNext{false};
static portMUX_TYPE refusalLock = portMUX_INITIALIZER_UNLOCKED;
static bool refused = false;
static int64_t refusedStep = 0;

// Each NTP reading comes here, in place of the time library's own, which would take any time at all. See camNtpTaken.
extern "C" void sntp_sync_time(struct timeval* tv) {
  time_t now = time(nullptr);
  if (!camNtpTaken((int64_t)now, (int64_t)tv->tv_sec, trustNext)) {
    portENTER_CRITICAL(&refusalLock);
    refusedStep = (int64_t)tv->tv_sec - (int64_t)now;
    refused = true;
    portEXIT_CRITICAL(&refusalLock);
    return;
  }
  trustNext = false;
  settimeofday(tv, nullptr);
  sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}

bool takeTimeRefused(int64_t& stepS) {
  portENTER_CRITICAL(&refusalLock);
  bool was = refused;
  stepS = refusedStep;
  refused = false;
  portEXIT_CRITICAL(&refusalLock);
  return was;
}

bool clockSet() { return time(nullptr) > CAM_CLOCK_VALID_AFTER; }

// A manual time does not survive a power cut, so until it is set again NTP is asked, and stopped by keepTime once it answers.
void applyTime() {
  String tz = camPosixTz(settings.tzOffset, settings.tzDst).c_str();
  if (settings.timeNtp || !clockSet()) {
    esp_sntp_stop();  // before the names change under it
    for (std::string& s : servers) s.clear();
    if (!camNtpServers(settings.ntpServer.c_str(), servers)) servers[0] = settings.ntpServer.c_str();
    auto name = [](int i) { return servers[i].empty() ? nullptr : servers[i].c_str(); };
    configTzTime(tz.c_str(), name(0), name(1), name(2));
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
  if (t.ntp) trustNext = true;  // the person chose NTP just now, so its next reading is taken, however far off the clock is
  applyTime();
  return true;
}
