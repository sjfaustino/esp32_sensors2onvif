#include "cam_delivery.h"
#include <string.h>

void CamDeliveryWatch::started(int id, const uint8_t mac[6], uint8_t input, bool delayed, bool several, bool replay, uint32_t holdMs, uint32_t now) {
  CamPendingCheck& p = pending_[id - 1];
  p.on = true;
  memcpy(p.mac, mac, 6);
  p.input = input;
  p.delayed = delayed;
  p.several = several;  // noted now, because the check may come after the alert has ended
  p.replay = replay;
  p.at = now;
  p.holdMs = holdMs;
}

void CamDeliveryWatch::check(uint32_t now, bool listening, const std::function<bool(int)>& active, std::vector<Missed>& missed) {
  for (int id = 1; id <= CAM_MAX_SENSORS; id++) {
    CamPendingCheck& p = pending_[id - 1];
    if (!p.on || (now - p.at < CHECK_AFTER_MS && active(id))) continue;
    p.on = false;
    if (listening) continue;
    missed_.add(p.mac, p.input, 0, false, now, p.holdMs, p.several, true);
    missed.push_back({id, p.delayed, !p.replay});
  }
}
