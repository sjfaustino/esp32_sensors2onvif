// Whole numbers read from text that came from outside: a sensor, a node, a browser, an NVR, the MQTT broker or a file.
// Only digits, with a leading '-' allowed when T is signed; no spaces, no '+', nothing after, and within T's range.
// So "257" is refused for a uint8_t rather than read as 1, as a cast after atoi would.
#pragma once
#include <charconv>
#include <string>
#include <system_error>

template <typename T>
bool camNumber(const char* begin, const char* end, T& out, int base = 10) {
  if (begin == end) return false;
  T v{};
  std::from_chars_result r = std::from_chars(begin, end, v, base);
  if (r.ec != std::errc() || r.ptr != end) return false;
  out = v;
  return true;
}

template <typename T>
bool camNumber(const std::string& s, T& out, int base = 10) {
  return camNumber(s.data(), s.data() + s.size(), out, base);
}

// The same, also refused outside lowest..highest.
template <typename T>
bool camNumberIn(const std::string& s, T lowest, T highest, T& out) {
  T v{};
  if (!camNumber(s, v) || v < lowest || v > highest) return false;
  out = v;
  return true;
}
