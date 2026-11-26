// The board's own decisions, kept here, away from the radio and the chip, so the unit tests can drive them: which Wi-Fi
// network to try and when the setup network opens, what that network's password is, what is kept through a restart and
// why the hub last started, and the clock's time zone and restart minute. src/ carries out what they decide. Every time
// is in cam_timing.h.
#pragma once
#include <stdint.h>
#include <string>
#include "cam_config.h"

// ---- Wi-Fi ----

// Off the network, the hub tries the main network, then the backup, each for CAM_WIFI_TRY_MS, with the setup network
// open behind them once neither worked or none is saved; it closes once the hub is back on. Someone on the setup network
// stops the tries, as they move the radio and would drop that person.
class CamWifiPlan {
 public:
  enum Action : int {    // to carry out in this order
    PROMOTE_BACKUP = 1,  // the backup joined: it becomes the main network, and the old main the backup
    CLOSE_SETUP = 2,
    OPEN_SETUP = 4,
    TRY_MAIN = 8,
    TRY_BACKUP = 16,
    STOP_TRYING = 32,  // someone came onto the setup network: leave the network being tried
  };
  struct Pass {
    uint32_t now = 0;
    bool connected = false;
    bool justLost = false;    // the connection went since the last pass
    bool turnedAway = false;  // the network being tried said to come back in a moment, since that try
    bool haveMain = false, haveBackup = false;
    bool setupOpen = false;  // as the board has it: opening it can fail
    int setupStations = 0;   // devices on it
  };
  int step(const Pass& p);                  // the actions, as bits
  void triedFromStart(uint32_t now);        // the board's first try, at the start
  bool guest() const { return hadGuest_; }  // someone is on the setup network, whom a restart would throw off
  int trying() const { return trying_; }    // 0 the main network, 1 the backup

 private:
  int trying_ = 0;
  int sameTries_ = 0;  // the network being tried, tried again after turning the hub away
  uint32_t tryAt_ = 0;
  bool hadGuest_ = false;
};

// ---- the setup network's password ----

enum CamSetupKey {
  CAM_SETUP_OPEN,    // a blank hub, with no password yet: its setup page asks for one first
  CAM_SETUP_LOCKED,  // with the password in out, under WPA3
  CAM_SETUP_OFF,     // a hub password too short for Wi-Fi, from an old firmware: no setup network rather than an open one
};
// The one from secrets.h when it has 8 characters or more, else the hub password.
CamSetupKey camSetupKey(const std::string& compiled, const std::string& hubPassword, std::string& out);

// A blank hub's setup network has no password, so anyone in range could set the hub up as their own before its owner
// does. It opens only after someone presses the hub's button, and stays open CAM_OPEN_SETUP_MS after the last press,
// and while someone is on it. A setup network with a password opens as ever. A board with no button opens it as ever.
const uint32_t CAM_OPEN_SETUP_MS = 15 * 60 * 1000;
struct CamSetupButton {
  bool fitted = true;    // the board has a button for it
  bool pressed = false;  // since the start
  uint32_t pressedAt = 0;
};
bool camSetupMayOpen(CamSetupKey key, const CamSetupButton& b, uint32_t now, int stations);

// ---- what is kept through a restart ----

// Why the hub restarted itself. New causes go at the end, so a cause kept by the firmware before means the same.
enum RestartCause : uint32_t {
  RESTART_UNKNOWN,
  RESTART_WIFI_LOST,
  RESTART_LOW_MEMORY,
  RESTART_STALLED,
  RESTART_ROUTER_SILENT,
  RESTART_NEW_WIFI,
  RESTART_NEW_SETTINGS,
  RESTART_BAD_FIXED_ADDRESS,
  RESTART_SCHEDULED,
  RESTART_SETTINGS_RESET,
  RESTART_BUTTON,
  RESTART_FIRMWARE,
  RESTART_RESTORED,
  RESTART_NEW_KEYS,
  RESTART_CAUSES
};
const char* camRestartText(uint32_t cause);  // as Device Info shows it
// Asked for by someone, not a crash, a watchdog, a power cut or one of the hub's own restarts for a fault.
bool camStartPlanned(bool softwareReset, uint32_t cause);

// Kept in the memory a restart leaves alone: why the hub restarted itself, and, set as an update starts, whether the NVR
// was listening then, so the new firmware must get it listening again before it is kept. Written by one firmware and
// read by the next, so its layout is fixed: changing it fails the build until CAM_KEPT_MARK changes too.
struct CamKept {
  uint32_t mark;
  uint32_t cause;  // a RestartCause
  uint32_t updateMark;
  uint32_t needNvr;
};
const uint32_t CAM_KEPT_MARK = 0x4B505431;    // "KPT1": the layout above
const uint32_t CAM_UPDATE_MARK = 0x50524F46;  // "PROF"
// At the start: reads why the hub last started, and leaves kept ready for this firmware. What this firmware's layout
// did not write, after a power cut or from a firmware that kept these elsewhere, reads as an unknown cause and no
// update to prove.
uint32_t camReadKept(CamKept& kept, bool softwareReset);
void camKeepCause(CamKept& kept, uint32_t cause);
void camKeepUpdate(CamKept& kept, bool nvrListening);
bool camUpdateNeedsNvr(const CamKept& kept);
void camForgetUpdate(CamKept& kept);

// ---- the clock ----

// The POSIX time zone for an offset from UTC in minutes and a summer time rule: 0 none, 1 Europe's, 2 North America's.
std::string camPosixTz(int offsetMinutes, int dst);
// Whether this local minute is the one the restart schedule names. weekday 0 is Sunday; -1 for a clock not set.
bool camScheduledMinute(const CamSchedule& s, int weekday, int minuteOfDay);
