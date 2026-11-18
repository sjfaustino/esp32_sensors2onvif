// The rules that decide when the hub restarts itself, and whether a new firmware is kept or undone. Each restart stops
// the hub hearing its sensors for a while, so the rules live here, away from the board, where the unit tests can drive
// them; src/main.cpp gathers what they look at once a pass and carries out what they decide. Every time is in
// cam_timing.h.
#pragma once
#include <stdint.h>
#include <functional>

enum CamRestartRule {
  CAM_RULE_NONE,
  CAM_RULE_WIFI_LOST,          // off the network too long
  CAM_RULE_LOW_MEMORY,         // memory short too long
  CAM_RULE_STALLED,            // the loop runs but the server serves nothing
  CAM_RULE_SCHEDULED,          // the restart set on the page
  CAM_RULE_BAD_FIXED_ADDRESS,  // a fixed address that cannot reach the router: forget it first
  CAM_RULE_ROUTER_SILENT,      // connected, but the router stopped answering
};

enum CamFirmwareVerdict { CAM_FIRMWARE_WAIT, CAM_FIRMWARE_KEEP, CAM_FIRMWARE_UNDO };

class CamRestartRules {
 public:
  // What the board sees, once a pass.
  struct Pass {
    uint32_t now = 0;          // millis(); read after lastPingReply, which the ping task may change at any moment
    bool haveNetwork = false;  // a Wi-Fi network is saved
    bool connected = false;    // on it
    bool guest = false;        // someone is on the setup network, whom a restart would throw off
    bool usingStatic = false;  // with a fixed address
    uint32_t freeHeap = 0;
    uint32_t stalledMs = 0;    // since the server last waited normally
    uint32_t pingReplies = 0;  // on this connection
    uint32_t pingLost = 0;
    uint32_t lastPingReply = 0;
    bool uploading = false;        // a firmware upload is under way, whose data arriving shows the network works
    bool scheduledMinute = false;  // the restart schedule is on, the clock is set and this is its minute
    bool alarmBusy = false;        // an alert is on, live or a past event playing
  };

  // routerAnswersArp is asked only when a fixed address looks wrong: a router may ignore ping, but not ARP.
  explicit CamRestartRules(std::function<bool()> routerAnswersArp) : routerAnswersArp_(routerAnswersArp) {}

  CamRestartRule check(const Pass& p);                         // which rule says to restart now, or none
  uint32_t lostAt() const { return lostAt_; }                  // when the network went, while it is gone; 0 while on it
  bool scheduledWaiting() const { return scheduledAt_ != 0; }  // the scheduled restart is waiting for alerts to end

  // A new firmware is kept once it has done the hub's work: on the network for a minute, ESP-NOW running, and the NVR
  // listening again if it was when the update began. Not managing that within 10 minutes undoes it. Call after check()
  // in the same pass, with that pass.
  CamFirmwareVerdict firmware(const Pass& p, bool espNowStarted, bool needNvr, bool nvrListening) const;

 private:
  static uint32_t nonzero(uint32_t t) { return t ? t : 1; }  // 0 means "not started" below
  std::function<bool()> routerAnswersArp_;
  bool wasConnected_ = false;
  uint32_t connectedAt_ = 0;
  bool staticChecked_ = false;
  uint32_t lostAt_ = 0;
  uint32_t lowSince_ = 0;
  uint32_t scheduledAt_ = 0;   // when the scheduled minute came, while its restart waits
  bool scheduleSeen_ = false;  // that minute is still the one already acted on
};
