// Whether each alert reached the NVR, and the alerts it missed, kept until it listens again.
//
// An alert that starts a new NVR record is checked a few seconds later, when its event push has had time to arrive, or
// when it ends if that is sooner. If the NVR was not listening then, the alert is kept as missed; once the NVR listens
// again the hub plays it as a past event. A replay the NVR misses too is kept again, but is not logged a second time.
#pragma once
#include <stdint.h>
#include <functional>
#include <vector>
#include "cam_sensors.h"

struct CamPendingCheck {
  bool on = false;
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  uint8_t input = 0;
  bool delayed = false;  // a past event
  bool several = false;  // a record standing for several sensors
  bool replay = false;   // a replay of one the NVR missed, already logged then
  uint32_t at = 0;
  uint32_t holdMs = 0;
};

class CamDeliveryWatch {
 public:
  static const uint32_t CHECK_AFTER_MS = 5000;
  struct Missed {  // an alert the NVR did not get
    int id;
    bool delayed;
    bool log;  // false for a replay, logged the first time
  };

  void started(int id, const uint8_t mac[6], uint8_t input, bool delayed, bool several, bool replay, uint32_t holdMs, uint32_t now);
  // Checks every alert that is due: after CHECK_AFTER_MS, or as soon as active(id) is false. Those the NVR missed are
  // kept and added to missed.
  void check(uint32_t now, bool listening, const std::function<bool(int)>& active, std::vector<Missed>& missed);
  bool takeMissed(CamDelayed& out) { return missed_.take(out); }  // the oldest, to replay while the NVR listens
  void keepMissed(const uint8_t mac[6], uint8_t input, bool several, uint32_t holdMs, uint32_t now) {
    missed_.add(mac, input, 0, false, now, holdMs, several, true);
  }
  void forget(int id) { pending_[id - 1] = CamPendingCheck(); }  // the sensor is gone
  void forgetChecks() {  // alerts from before now are not checked
    for (CamPendingCheck& p : pending_) p = CamPendingCheck();
  }
  const CamPendingCheck& pending(int id) const { return pending_[id - 1]; }
  const CamDelayedQueue& missed() const { return missed_; }

 private:
  CamPendingCheck pending_[CAM_MAX_SENSORS];  // by sensor id
  CamDelayedQueue missed_;
};
