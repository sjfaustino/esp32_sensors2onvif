// A week of the hub's health, an hour at a time, for Device info: the least free memory in each hour, the weakest Wi-Fi
// signal, and for how many of the minutes the hub ran the NVR was listening. A memory fall that recovered, a Wi-Fi that
// faded at night, and an NVR that was off for hours all show at a glance, where the events say only the last of them.
// Kept by the hub's clock, so nothing is kept until it is set; saved as text as each hour ends, so a restart keeps it.
#pragma once
#include <stdint.h>
#include <time.h>
#include <string>
#include "cam_json.h"

struct CamHealthHour {
  uint32_t hour = 0;       // hours since 1970, by the hub's clock
  uint16_t lowKb = 0;      // the least free memory seen in it, in KB
  int8_t rssi = 0;         // the weakest Wi-Fi signal seen, dBm; 0 when the hub was never on Wi-Fi in it
  uint8_t upMinutes = 0;   // minutes of it the hub was running and looked
  uint8_t nvrMinutes = 0;  // and of those, the minutes the NVR was listening
};

class CamHealthHistory {
 public:
  static const int HOURS = 7 * 24;
  // One look at the hub, every few seconds: the time by its clock, free memory now, the Wi-Fi signal (0 when off it), and
  // whether the NVR is listening; spanMs is the time since the last look, which it stands for. Nothing is kept while the
  // clock is not set. True when an hour has just ended, for the history to be saved.
  bool sample(time_t now, uint32_t freeBytes, int rssi, bool nvrListening, uint32_t spanMs);
  // The hours of the last week, oldest first, the one under way last, as [[start, lowKb, rssi, upMinutes, nvrMinutes], ...]
  // with start in seconds since 1970. An hour the hub was off is not there.
  void hours(CamJson& j) const;
  std::string text() const;            // the hours ended, one line each, to save
  void read(const std::string& text);  // what text() wrote; a line that does not read is left out
  int ended() const { return count_; }

 private:
  void push(const CamHealthHour& h);
  const CamHealthHour& at(int i) const { return ring_[(first_ + i) % HOURS]; }
  CamHealthHour ring_[HOURS];  // the hours ended, oldest at first_
  int first_ = 0, count_ = 0;
  CamHealthHour current_;  // the hour under way, once started_
  bool started_ = false;
  uint32_t upMs_ = 0, nvrMs_ = 0;
};
