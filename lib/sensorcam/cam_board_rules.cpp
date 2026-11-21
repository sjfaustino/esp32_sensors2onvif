#include "cam_board_rules.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "cam_timing.h"

// ---- Wi-Fi ----

void CamWifiPlan::triedFromStart(uint32_t now) {
  trying_ = 0;
  tryAt_ = now;
}

int CamWifiPlan::step(const Pass& p) {
  int act = 0;
  if (p.connected) {
    hadGuest_ = false;
    if (trying_ == 1) {
      act |= PROMOTE_BACKUP;
      trying_ = 0;
    }
    if (p.setupOpen) act |= CLOSE_SETUP;
    return act;
  }
  auto tryNetwork = [&](int i) {
    act |= i ? TRY_BACKUP : TRY_MAIN;
    trying_ = i;
    tryAt_ = p.now;
  };
  if (!p.haveMain && !p.setupOpen) act |= OPEN_SETUP;
  if (p.justLost && p.haveMain) tryNetwork(0);
  bool guest = p.setupOpen && p.setupStations > 0;
  if (guest && !hadGuest_) act |= STOP_TRYING;
  hadGuest_ = guest;
  if (!guest && p.haveMain && p.now - tryAt_ > CAM_WIFI_TRY_MS) {
    if (trying_ == 0 && p.haveBackup) {
      tryNetwork(1);
    } else {  // neither network worked: open the setup network and keep trying behind it
      if (!p.setupOpen) act |= OPEN_SETUP;
      tryNetwork(0);
    }
  }
  return act;
}

// ---- the setup network's password ----

CamSetupKey camSetupKey(const std::string& compiled, const std::string& hubPassword, std::string& out) {
  out = compiled.size() >= 8 ? compiled : hubPassword;
  if (out.empty()) return CAM_SETUP_OPEN;
  return out.size() >= 8 ? CAM_SETUP_LOCKED : CAM_SETUP_OFF;
}

// ---- what is kept through a restart ----

static_assert(sizeof(CamKept) == 16 && offsetof(CamKept, cause) == 4 && offsetof(CamKept, updateMark) == 8 && offsetof(CamKept, needNvr) == 12,
              "what is kept through a restart changed: change CAM_KEPT_MARK, then these checks");
static_assert(RESTART_CAUSES == 14, "a new restart cause: add it at the end, so a cause kept by the firmware before still means the same");

static const char* const RESTART_TEXT[RESTART_CAUSES] = {"Software restart",
                                                         "WiFi lost for 5 minutes",
                                                         "Low memory",
                                                         "Server stalled",
                                                         "Router stopped answering",
                                                         "New WiFi settings",
                                                         "New web, port or address settings",
                                                         "Fixed address could not reach the router",
                                                         "Scheduled restart",
                                                         "Settings reset",
                                                         "Restart button",
                                                         "Firmware update",
                                                         "Settings restored",
                                                         "New keys"};

const char* camRestartText(uint32_t cause) { return RESTART_TEXT[cause < RESTART_CAUSES ? cause : RESTART_UNKNOWN]; }

bool camStartPlanned(bool softwareReset, uint32_t cause) {
  if (!softwareReset) return false;
  switch (cause) {
    case RESTART_NEW_WIFI:
    case RESTART_NEW_SETTINGS:
    case RESTART_SCHEDULED:
    case RESTART_SETTINGS_RESET:
    case RESTART_BUTTON:
    case RESTART_FIRMWARE:
    case RESTART_RESTORED:
    case RESTART_NEW_KEYS: return true;
    default: return false;
  }
}

uint32_t camReadKept(CamKept& kept, bool softwareReset) {
  bool ours = kept.mark == CAM_KEPT_MARK;
  uint32_t cause = softwareReset && ours && kept.cause < RESTART_CAUSES ? kept.cause : (uint32_t)RESTART_UNKNOWN;
  if (!ours) kept.updateMark = 0;  // not this layout's, so not an update to prove either
  kept.mark = CAM_KEPT_MARK;
  kept.cause = RESTART_UNKNOWN;
  return cause;
}

void camKeepCause(CamKept& kept, uint32_t cause) {
  kept.mark = CAM_KEPT_MARK;
  kept.cause = cause;
}

void camKeepUpdate(CamKept& kept, bool nvrListening) {
  kept.updateMark = CAM_UPDATE_MARK;
  kept.needNvr = nvrListening;
}

bool camUpdateNeedsNvr(const CamKept& kept) { return kept.updateMark == CAM_UPDATE_MARK && kept.needNvr; }

void camForgetUpdate(CamKept& kept) { kept.updateMark = 0; }

// ---- the clock ----

static std::string tzName(int minutes) {
  char b[12];
  snprintf(b, sizeof(b), "<%c%02d%02d>", minutes < 0 ? '-' : '+', abs(minutes) / 60, abs(minutes) % 60);
  return b;
}

// Its sign is the opposite of the usual one: UTC+1 is written as -1.
std::string camPosixTz(int offsetMinutes, int dst) {
  char offset[10];
  snprintf(offset, sizeof(offset), "%c%d:%02d", offsetMinutes < 0 ? '+' : '-', abs(offsetMinutes) / 60, abs(offsetMinutes) % 60);
  std::string tz = tzName(offsetMinutes) + offset;
  char rule[32];
  if (dst == 1) {  // Europe changes at 01:00 UTC everywhere
    int hour = offsetMinutes / 60;
    snprintf(rule, sizeof(rule), ",M3.5.0/%d,M10.5.0/%d", 1 + hour, 2 + hour);
    tz += tzName(offsetMinutes + 60) + rule;
  } else if (dst == 2) {
    tz += tzName(offsetMinutes + 60) + ",M3.2.0/2,M11.1.0/2";
  }
  return tz;
}

bool camScheduledMinute(const CamSchedule& s, int weekday, int minuteOfDay) {
  if (!s.on || weekday < 0 || minuteOfDay < 0) return false;
  return (s.day < 0 || s.day == weekday) && minuteOfDay == s.minute;
}
