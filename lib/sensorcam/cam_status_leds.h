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
// Green is off while red is steady, so the hub never looks fine while it is not. An RGB LED shows the same in one
// light: red while red is lit, otherwise green while green is.
//
// The LEDs are a choice: none is lit until the page is told which pins they are on, from a list of pins the board can
// spare (camLedPinChoices).
#pragma once
#include <stdint.h>
#include <string>
#include "cam_config.h"

// The pins the page offers for an LED, on the board named as its release files are (CamConfig::board). Left out: the
// pins the chip reads as it starts, the flash's and the PSRAM's, USB's, the serial port's, and those that only read.
// On an ESP32: not 0, 2, 5, 12 and 15 (read at start), 6 to 11 (flash), 16 and 17 (PSRAM on some boards), 1 and 3
// (serial), 34 to 39 (input only). On an ESP32-S3: not 0, 3, 45 and 46 (read at start), 19 and 20 (USB), 26 to 37
// (flash and octal PSRAM), 43 and 44 (serial).
// The RGB LED may also be on the last two of the ESP32-S3's, 47 and 48, as boards put theirs on 48 or 38.
struct CamPinList {
  const int* pins = nullptr;
  int count = 0;  // 0 for a board this does not know: no LEDs, rather than a guess
  bool has(int pin) const {
    for (int i = 0; i < count; i++)
      if (pins[i] == pin) return true;
    return false;
  }
};

inline CamPinList camPinList(const std::string& board, bool rgb) {
  static const int ESP32[] = {4, 13, 14, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
  static const int ESP32S3[] = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47, 48};
  const int S3_RGB_ONLY = 2;  // 47 and 48, at the end
  CamPinList l;
  if (board == "esp32s3") {
    l.pins = ESP32S3;
    l.count = (int)(sizeof(ESP32S3) / sizeof(ESP32S3[0])) - (rgb ? 0 : S3_RGB_ONLY);
  } else if (board == "esp32_16mb" || board == "esp32dev") {
    l.pins = ESP32;
    l.count = (int)(sizeof(ESP32) / sizeof(ESP32[0]));
  }
  return l;
}
inline CamPinList camLedPinChoices(const std::string& board) { return camPinList(board, false); }
inline CamPinList camRgbPinChoices(const std::string& board) { return camPinList(board, true); }
inline bool camLedPinAllowed(const std::string& board, int pin) { return camLedPinChoices(board).has(pin); }
inline bool camRgbPinAllowed(const std::string& board, int pin) { return camRgbPinChoices(board).has(pin); }

// The pins as saved, with any the board cannot spare, and a red on the green's pin, or an RGB on either's, left out.
inline CamLedPins camLedPinsChecked(const std::string& board, CamLedPins p) {
  if (!camLedPinAllowed(board, p.green)) p.green = -1;
  if (!camLedPinAllowed(board, p.red) || p.red == p.green) p.red = -1;
  if (!camRgbPinAllowed(board, p.rgb) || p.rgb == p.green || p.rgb == p.red) p.rgb = -1;
  return p;
}

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
  bool blue = false;  // only in the RGB LED's test, to show its colours are in the right order
};

// The RGB LED's colour for those, dim, as these LEDs are bright enough to light a room at full.
struct CamRgb {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const CamRgb& o) const { return r == o.r && g == o.g && b == o.b; }
};
inline CamRgb camRgbFor(const CamLeds& l) {
  const uint8_t ON = 40;
  CamRgb c;
  if (l.red)
    c.r = ON;
  else if (l.green)
    c.g = ON;
  else if (l.blue)
    c.b = ON;
  return c;
}

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
  if (notProtecting) return l;
  if (s.mode == CAM_ARMED_AWAY)
    l.green = true;
  else if (s.mode == CAM_ARMED_HOME)
    l.green = ms % 2000 < 1000;
  else
    l.green = ms % 3000 < 100;
  return l;
}
