// SensorCamServer: arming away and home, timed disarms, and test alerts.
#include "cam_server.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <new>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_server_internal.h"

const char* camArmModeName(CamArmMode mode) { return mode == CAM_DISARMED ? "disarmed" : mode == CAM_ARMED_HOME ? "home" : "away"; }

std::string camMinutesText(uint32_t m) {
  if (m && m % 1440 == 0) return std::to_string(m / 1440) + (m == 1440 ? " day" : " days");
  if (m && m % 60 == 0) return std::to_string(m / 60) + " h";
  return std::to_string(m) + " min";
}

bool camTimedDisarmOver(int64_t until, time_t wallNow, uint32_t msSinceStart, uint32_t startWaitMs) {
  if (until <= 0) return false;  // no end
  if (until == 1) return true;   // began without a clock, so how long is left is not known
  if (wallNow >= CAM_CLOCK_VALID_AFTER) return (int64_t)wallNow >= until;
  return msSinceStart >= startWaitMs;
}

// Disarmed, no sensor counts; armed home, interior ones do not. A report of something that happened while its sensor
// still counted counts, and so does one whose time is not known: a false alert is better than a missed one. A mode
// kept from before the start counts from the start, as nothing earlier is known.
CamEventKind SensorCamServer::heldAs(int id, const SensorReport& r, uint32_t now) const {
  bool interior = slots_[id - 1].settings.interior;
  bool held = armMode() == CAM_DISARMED || (armMode() == CAM_ARMED_HOME && interior);
  if (!held || r.age == CAM_AGE_UNKNOWN) return CAM_EVENT_REPORT;
  uint32_t offAt = interior ? interiorOffAt_ : perimeterOffAt_;
  if ((uint64_t)r.age * 1000 > (uint64_t)(now - offAt)) return CAM_EVENT_REPORT;
  return armed() ? CAM_EVENT_HOME_ALERT : CAM_EVENT_DISARMED_ALERT;
}

// A disarm can be given an end, or have its end changed, while disarmed; arming always ends it.
bool SensorCamServer::setArmMode(CamArmMode mode, const std::string& from, uint32_t minutes) {
  CamArmMode was = armMode();
  if (mode != CAM_DISARMED) minutes = 0;
  if (mode == was && mode != CAM_DISARMED) return true;
  time_t t = time(nullptr);
  int64_t until = !minutes ? 0 : t >= CAM_CLOCK_VALID_AFTER ? (int64_t)t + (int64_t)minutes * 60 : 1;
  if (mode == CAM_DISARMED && was == CAM_DISARMED && !minutes && !cfg_.disarmedUntil) return true;  // nothing changes
  if (!camCall(cfg_.armedSave, mode, until)) return false;
  uint32_t now = millis_();
  if (was == CAM_ARMED_AWAY) interiorOffAt_ = now;  // a change of end keeps when a disarm began
  if (was != CAM_DISARMED && mode == CAM_DISARMED) perimeterOffAt_ = now;
  cfg_.armMode = mode;
  cfg_.disarmedUntil = until;
  disarmEndKnown_ = minutes != 0;
  disarmEndsAt_ = now + minutes * minuteMs_;
  std::string detail = minutes ? from + ", for " + camMinutesText(minutes) : from;
  CamEventKind kind = mode == CAM_ARMED_AWAY ? CAM_EVENT_ARMED : mode == CAM_ARMED_HOME ? CAM_EVENT_ARMED_HOME : CAM_EVENT_DISARMED;
  hubEvent(kind, detail, CAM_AGE_UNKNOWN, camEventMessage(kind, cfg_.hostName, detail, CAM_AGE_UNKNOWN, false));
  return true;
}

bool SensorCamServer::testAlert(int id) {
  if (id < 1 || id > CAM_MAX_SENSORS || !slots_[id - 1].used) return false;
  uint32_t now = millis_();
  interruptPastEvent(now);
  bool wasActive = sensors_.active(id);
  sensors_.trigger(id, now, 0, false, holdFor(id));
  if (!wasActive) startAlert(id, false, now);  // checked as delivered, as a real one is: that is part of what is tested
  SensorReport r;
  r.active = true;
  r.battery = health_[id - 1].battery;
  logEvent(id, r, false, CAM_EVENT_TEST_ALERT);
  notifyHook(id, CAM_EVENT_TEST_ALERT, false);
  return true;
}

void SensorCamServer::rearmWhenDue(uint32_t now) {
  if (armed() || !cfg_.disarmedUntil) return;
  bool due = disarmEndKnown_ ? (int32_t)(now - disarmEndsAt_) >= 0
                             : camTimedDisarmOver(cfg_.disarmedUntil, time(nullptr), now - beganAt_, CAM_START_CLOCK_WAIT_MS);
  if (due) setArmMode(CAM_ARMED_AWAY, "the re-arm timer");  // away, the safer; if it cannot be saved, it is tried again on the next pass
}

long long SensorCamServer::disarmSecondsLeft(uint32_t now) const {
  if (armed() || !cfg_.disarmedUntil) return -1;
  if (disarmEndKnown_) return (int32_t)(disarmEndsAt_ - now) > 0 ? (disarmEndsAt_ - now + 999) / 1000 : 0;
  time_t t = time(nullptr);
  if (cfg_.disarmedUntil > 1 && t >= CAM_CLOCK_VALID_AFTER) return cfg_.disarmedUntil > t ? cfg_.disarmedUntil - t : 0;
  return -1;
}
