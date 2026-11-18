#include "cam_restart_rules.h"
#include "cam_timing.h"

CamRestartRule CamRestartRules::check(const Pass& p) {
  if (p.connected && !wasConnected_) {  // a new connection: its own fixed-address check, and its own proof
    connectedAt_ = nonzero(p.now);
    staticChecked_ = false;
  }
  wasConnected_ = p.connected;

  // An unattended hub must not stay off the network: after a while, start clean. Not while someone uses the setup page.
  if (p.connected)
    lostAt_ = 0;
  else if (lostAt_ == 0 || p.guest)
    lostAt_ = nonzero(p.now);
  if (p.haveNetwork && !p.connected && p.now - lostAt_ > CAM_WIFI_LOST_RESTART_MS) return CAM_RULE_WIFI_LOST;

  // Out of memory the network stack stops working while the loop carries on, so the loop watchdog cannot see it.
  if (p.freeHeap >= CAM_LOW_HEAP_BYTES)
    lowSince_ = 0;
  else if (lowSince_ == 0)
    lowSince_ = nonzero(p.now);
  if (lowSince_ && p.now - lowSince_ > CAM_LOW_HEAP_RESTART_MS) return CAM_RULE_LOW_MEMORY;
  if (p.stalledMs > CAM_STALLED_RESTART_MS) return CAM_RULE_STALLED;

  // The scheduled restart waits for alerts to end, so it never cuts an NVR record short.
  // If they have not ended in 30 minutes it gives up until the next time. Not soon after a start, which would be the
  // same minute again.
  if (!p.scheduledMinute)
    scheduleSeen_ = false;
  else if (!scheduleSeen_ && p.now > CAM_SCHEDULE_AFTER_START_MS) {
    scheduleSeen_ = true;
    scheduledAt_ = nonzero(p.now);
  }
  if (scheduledAt_) {
    if (!p.alarmBusy) return CAM_RULE_SCHEDULED;
    if (p.now - scheduledAt_ > CAM_SCHEDULE_WAIT_MS) scheduledAt_ = 0;
  }

  // A wrong fixed address leaves Wi-Fi connected but the hub unreachable, with no other way back to it.
  if (p.connected && p.usingStatic && !staticChecked_ && p.now - connectedAt_ > CAM_STATIC_CHECK_MS) {
    staticChecked_ = true;
    if (p.pingReplies == 0 && p.pingLost > 0 && !routerAnswersArp_()) return CAM_RULE_BAD_FIXED_ADDRESS;
  }
  // Only after the router has answered at least once, so a router that ignores ping cannot cause restarts.
  if (p.connected && p.pingReplies > 0 && p.now - p.lastPingReply > CAM_ROUTER_SILENT_RESTART_MS && !p.uploading) return CAM_RULE_ROUTER_SILENT;
  return CAM_RULE_NONE;
}

CamFirmwareVerdict CamRestartRules::firmware(const Pass& p, bool espNowStarted, bool needNvr, bool nvrListening) const {
  if (!p.haveNetwork)  // still being set up: nothing to prove it on
    return p.now > CAM_FIRMWARE_PROOF_MS ? CAM_FIRMWARE_KEEP : CAM_FIRMWARE_WAIT;
  if (p.connected && p.now - connectedAt_ > CAM_FIRMWARE_PROOF_MS && espNowStarted && (!needNvr || nvrListening)) return CAM_FIRMWARE_KEEP;
  return p.now > CAM_FIRMWARE_PROOF_DEADLINE_MS ? CAM_FIRMWARE_UNDO : CAM_FIRMWARE_WAIT;
}
