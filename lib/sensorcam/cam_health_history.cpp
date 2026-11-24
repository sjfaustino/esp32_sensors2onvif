#include "cam_health_history.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "cam_events.h"

static const uint32_t LOOK_MAX_MS = 60000;  // a longer gap between looks, the loop held up, stands for no more than this

bool CamHealthHistory::sample(time_t now, uint32_t freeBytes, int rssi, bool nvrListening, uint32_t spanMs) {
  if (now < CAM_CLOCK_VALID_AFTER) return false;
  uint32_t hour = (uint32_t)(now / 3600);
  bool ended = false;
  if (!started_ || hour != current_.hour) {
    // A new hour. One that went back, as when the clock is set right, drops the hour under way rather than mixing two.
    if (started_ && hour > current_.hour) {
      push(current_);
      ended = true;
    }
    current_ = CamHealthHour();
    current_.hour = hour;
    current_.lowKb = 0xFFFF;
    started_ = true;
    upMs_ = nvrMs_ = 0;
    spanMs = 0;  // the time before the first look of an hour belongs to the hour before
  }
  uint32_t kb = freeBytes / 1024;
  if (kb < current_.lowKb) current_.lowKb = (uint16_t)kb;
  if (rssi < 0 && (current_.rssi == 0 || rssi < current_.rssi)) current_.rssi = (int8_t)(rssi < -127 ? -127 : rssi);
  if (spanMs > LOOK_MAX_MS) spanMs = LOOK_MAX_MS;
  upMs_ += spanMs;
  if (nvrListening) nvrMs_ += spanMs;
  current_.upMinutes = (uint8_t)(upMs_ / 60000 > 60 ? 60 : upMs_ / 60000);
  current_.nvrMinutes = (uint8_t)(nvrMs_ / 60000 > 60 ? 60 : nvrMs_ / 60000);
  return ended;
}

void CamHealthHistory::push(const CamHealthHour& h) {
  // Kept in order: hours from a clock since set back are dropped first.
  while (count_ && at(count_ - 1).hour >= h.hour) count_--;
  if (count_ == HOURS) {
    first_ = (first_ + 1) % HOURS;
    count_--;
  }
  ring_[(first_ + count_) % HOURS] = h;
  count_++;
}

void CamHealthHistory::hours(CamJson& j) const {
  uint32_t last = started_ ? current_.hour : count_ ? at(count_ - 1).hour : 0;
  auto one = [&](const CamHealthHour& h) {
    if (h.hour + HOURS <= last) return;  // older than a week
    j.array().num((int64_t)h.hour * 3600).num(h.lowKb).num(h.rssi).num(h.upMinutes).num(h.nvrMinutes).end();
  };
  for (int i = 0; i < count_; i++) one(at(i));
  if (started_) one(current_);
}

std::string CamHealthHistory::text() const {
  std::string out;
  out.reserve(count_ * 24);
  char line[48];
  for (int i = 0; i < count_; i++) {
    const CamHealthHour& h = at(i);
    snprintf(line, sizeof(line), "%lu %u %d %u %u\n", (unsigned long)h.hour, h.lowKb, h.rssi, h.upMinutes, h.nvrMinutes);
    out += line;
  }
  return out;
}

void CamHealthHistory::read(const std::string& text) {
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    // Five whole numbers, each after one space, and nothing after them.
    long long v[5] = {};
    const char* p = line.c_str();
    bool ok = true;
    for (int i = 0; i < 5 && ok; i++) {
      char* stop;
      errno = 0;
      v[i] = strtoll(p, &stop, 10);
      ok = stop != p && errno == 0 && (*stop == (i < 4 ? ' ' : '\0'));
      p = stop + (i < 4 ? 1 : 0);
    }
    long long hour = v[0], low = v[1], rssi = v[2], up = v[3], nvr = v[4];
    if (!ok || low < 0 || low > 0xFFFF || rssi > 0 || rssi < -127 || up < 0 || up > 60 || nvr < 0 || nvr > up ||
        hour < CAM_CLOCK_VALID_AFTER / 3600 || hour > 0xFFFFFFFFLL)
      continue;
    CamHealthHour h;
    h.hour = (uint32_t)hour;
    h.lowKb = (uint16_t)low;
    h.rssi = (int8_t)rssi;
    h.upMinutes = (uint8_t)up;
    h.nvrMinutes = (uint8_t)nvr;
    push(h);
  }
}
