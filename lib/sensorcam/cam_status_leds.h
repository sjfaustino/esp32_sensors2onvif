// The hub's two status LEDs, green and red, for someone standing by it without a phone: whether it is protecting,
// whether it is armed, and whether a sensor is firing now.
//
//   red, flashing fast     a sensor is firing
//   red, steady            not protecting: off Wi-Fi, or the NVR not set or not listening, so alerts are not recorded
//   red, blinking slowly   the setup network is open
//   red, a short flash     protecting, but a sensor has a problem: silent, a low battery, a weak signal
//   green, steady          armed away
//   green, blinking slowly armed home
//   green, a short flash   disarmed: running, but not raising alerts
// Green is off while red is steady, so the hub never looks fine while it is not.
#pragma once
#include <stdint.h>
#include "cam_config.h"

struct CamLedState {
  bool online = false;        // on its Wi-Fi network
  bool setupOpen = false;     // the setup network is open
  bool nvrSet = false;        // an NVR has subscribed to the hub
  bool nvrListening = false;  // and it is listening now
  CamArmMode mode = CAM_DISARMED;
  bool firing = false;    // a sensor is active now
  bool problems = false;  // a sensor is silent, low on battery, weak or refused
};

struct CamLeds {
  bool green = false, red = false;
};

// Which LEDs are lit at this moment; ms is any running count of milliseconds, which sets the blinking.
inline CamLeds camStatusLeds(const CamLedState& s, uint32_t ms) {
  CamLeds l;
  bool notProtecting = !s.online || !s.nvrSet || !s.nvrListening;
  if (s.firing)
    l.red = ms % 250 < 125;  // 4 times a second
  else if (s.setupOpen)
    l.red = ms % 1000 < 500;
  else if (notProtecting)
    l.red = true;
  else if (s.problems)
    l.red = ms % 2000 < 100;
  if (!notProtecting) l.green = s.mode == CAM_ARMED_AWAY ? true : s.mode == CAM_ARMED_HOME ? ms % 2000 < 1000 : ms % 3000 < 100;
  return l;
}
