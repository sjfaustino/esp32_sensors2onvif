// What a good value is: every saved setting in CAM_SETTINGS (cam_config.h), and the checks of addresses, names and
// tokens the API's handlers share (cam_web_checks.h).
#include "cam_text.h"
#include "cam_web_server.h"
#include "cam_number.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_web.h"
#include "cam_server_internal.h"
#include "cam_web_checks.h"

bool parseIpv4(const std::string& s, uint32_t& out) {
  out = 0;
  size_t i = 0;
  for (int part = 0; part < 4; part++) {
    if (part) {
      if (i >= s.size() || s[i] != '.') return false;
      i++;
    }
    int value = 0, digits = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) {
      value = value * 10 + (s[i++] - '0');
      if (++digits > 3 || value > 255) return false;
    }
    if (!digits) return false;
    out = (out << 8) | (uint32_t)value;
  }
  return i == s.size();
}

// Refuses anything that could never work, because a bad fixed address takes the hub off the network.
bool validAddress(const CamAddress& a) {
  uint32_t ip, mask, gateway, dns;
  if (!parseIpv4(a.ip, ip) || !parseIpv4(a.mask, mask) || !parseIpv4(a.gateway, gateway)) return false;
  if (!a.dns.empty() && !parseIpv4(a.dns, dns)) return false;
  uint32_t hosts = ~mask;
  if (mask == 0 || (hosts & (hosts + 1)) != 0 || hosts < 3) return false;  // the mask must be contiguous, with room for hosts
  if ((ip & hosts) == 0 || (ip & hosts) == hosts) return false;            // not the network or broadcast address
  return (gateway & mask) == (ip & mask) && gateway != ip;
}

// A plain whole number and nothing else.
bool wholeNumber(const std::string& s, long long& out) { return camNumber(s, out); }

int camNtpServers(const std::string& s, std::string out[3]) {
  if (s.empty() || s.size() > 200) return 0;
  int n = 0;
  size_t at = 0;
  while (at <= s.size()) {
    size_t comma = s.find(',', at);
    if (comma == std::string::npos) comma = s.size();
    std::string name = s.substr(at, comma - at);
    while (!name.empty() && name.front() == ' ') name.erase(0, 1);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty() || name.size() > 63 || n == 3) return 0;
    for (char ch : name)
      if (!isalnum((unsigned char)ch) && ch != '.' && ch != '-') return 0;
    out[n++] = name;
    at = comma + 1;
  }
  return n;
}

bool validNtpServer(const std::string& s) {
  std::string names[3];
  return camNtpServers(s, names) > 0;
}

// Also the hub's network name, so letters, digits and inner hyphens only.
bool validHubName(const std::string& s) {
  bool ok = s.size() <= 63 && (s.empty() || (s.front() != '-' && s.back() != '-'));
  for (char ch : s) ok = ok && (isalnum((unsigned char)ch) || ch == '-');
  return ok;
}

bool validOffset(long long minutes) { return minutes >= -720 && minutes <= 840 && minutes % 15 == 0; }

// Sent unencoded in a URL and in JSON, so it is kept to characters that need no escaping in either.
bool validToken(const std::string& s) {
  bool ok = !s.empty() && s.size() <= 64;
  for (char ch : s) ok = ok && (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~');
  return ok;
}

// The same rule as an NVR's event address: http:// with an IPv4 host, because the hub has no name lookup or TLS for
// these. Nothing that could end the request line early.
bool validHookUrl(const std::string& url) {
  HttpUrl u;
  bool ok = url.size() <= 200;
  for (char ch : url) ok = ok && (unsigned char)ch > 0x20 && (unsigned char)ch < 0x7f;
  return url.empty() || (ok && parseHttpUrl(url, u) && inet_addr(u.host.c_str()) != INADDR_NONE);
}

// An MQTT user name or password: sent as it is, so anything but control characters, up to 64 bytes.
bool validMqttText(const std::string& s) {
  bool ok = s.size() <= 64;
  for (char ch : s) ok = ok && (unsigned char)ch >= 0x20 && ch != 0x7f;
  return ok;
}

static bool anyIpv4(const std::string& v) {
  uint32_t ip;
  return parseIpv4(v, ip);
}
static bool noneOrIpv4(const std::string& v) { return v.empty() || anyIpv4(v); }
static bool zeroOrOne(const std::string& v) { return v == "0" || v == "1"; }
static bool ssid(const std::string& v) { return v.size() <= 32; }
static bool wifiPassword(const std::string& v) { return v.empty() || (v.size() >= 8 && v.size() <= 63); }

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): built before anything runs; no memory then is no hub at all
const CamSetting CAM_SETTINGS[] = {
    {"wifi", "ssid", 's', 0, 0, ssid},
    {"wifi", "password", 's', 0, 0, wifiPassword},
    {"wifi", "ssid2", 's', 0, 0, ssid},
    {"wifi", "password2", 's', 0, 0, wifiPassword},
    {"net", "onvif", 'u', 1, 65535, nullptr},
    {"net", "web", 'u', 1, 65535, nullptr},
    {"net", "rtsp", 'u', 1, 65535, nullptr},
    {"net", "name", 's', 0, 0, [](const std::string& v) { return !v.empty() && validHubName(v); }},
    // As the page requires; empty in a backup from before a password was required, which camBackupValueKept leaves out.
    {"net", "webpw", 's', 0, 0, [](const std::string& v) { return v.empty() || (v.size() >= 8 && v.size() <= 63); }},
    {"net", "token", 's', 0, 0, validToken},
    {"net", "ipfor", 's', 0, 0, ssid},  // the network a fixed address is for
    {"net", "ip", 's', 0, 0, anyIpv4},
    {"net", "mask", 's', 0, 0, anyIpv4},
    {"net", "gw", 's', 0, 0, anyIpv4},
    {"net", "dns", 's', 0, 0, noneOrIpv4},
    {"net", "nvrip", 's', 0, 0, noneOrIpv4},
    {"net", "ntp", 'b', 0, 1, nullptr},
    {"net", "ntpsrv", 's', 0, 0, validNtpServer},
    {"net", "tzoff", 'i', -720, 840,
     [](const std::string& v) {
       long long n = 0;
       return camNumber(v, n) && validOffset(n);  // whole quarter hours
     }},
    {"net", "tzdst", 'i', 0, 2, nullptr},
    {"net", "rben", 'b', 0, 1, nullptr},
    {"net", "rbday", 'i', -1, 6, nullptr},  // the scheduled restart's day, 0 for Sunday; -1 every day
    {"net", "rbmin", 'i', 0, 1439, nullptr},
    {"net", "hlth", 'b', 0, 1, nullptr},
    {"net", "enkey", 's', 0, 0, [](const std::string& v) { return v.size() >= 16 && validToken(v); }},
    {"net", "hook", 's', 0, 0, validHookUrl},
    {"net", "hooktx", 'b', 0, 1, nullptr},  // plain text, from before the format; still saved beside it
    {"net", "hookal", 'b', 0, 1, nullptr},
    {"net", "hookpr", 'b', 0, 1, nullptr},
    {"net", "hookdy", 'i', -1, 1439, nullptr},  // the daily check's minute; -1 for none
    {"net", "hosts", 's', 0, 0, camHostListValid},
    {"net", "pairkey", 's', 0, 0,
     [](const std::string& v) {
       uint8_t k[CAM_X25519_LEN];
       return camParseHex(v, k, sizeof(k));
     }},
    {"net", "rearm", 'u', 0, CAM_MAX_DISARM_MINUTES, nullptr},
    {"net", "mqhost", 's', 0, 0, noneOrIpv4},
    {"net", "mqport", 'u', 1, 65535, nullptr},
    {"net", "mquser", 's', 0, 0, validMqttText},
    {"net", "mqpw", 's', 0, 0, validMqttText},
    {"net", "hookfm", 'i', CAM_HOOK_JSON, CAM_HOOK_GOTIFY, nullptr},
    {"net", "hooktk", 's', 0, 0, [](const std::string& v) { return v.empty() || validToken(v); }},
    {"net", "hookwd", 'i', -1, 6, nullptr},  // the weekly summary's day, 0 for Sunday; -1 for none
    {"net", "hookwm", 'i', 0, 1439, nullptr},
    {"net", "hooklg", 's', 0, 0, camTextLanguageKnown},  // the messages' language, as "pt"; "en" for English
    // The status LEDs' pins, -1 for none; the hub checks them against its own board as it starts.
    {"net", "ledg", 'i', -1, 48, nullptr},
    {"net", "ledr", 'i', -1, 48, nullptr},
    {"net", "ledrgb", 'i', -1, 48, nullptr},
    {"net", "ledlo", 'b', 0, 1, nullptr},
};
const size_t CAM_SETTING_COUNT = sizeof(CAM_SETTINGS) / sizeof(CAM_SETTINGS[0]);

const CamSetting* camSetting(const std::string& key) {
  for (const CamSetting& s : CAM_SETTINGS)
    if (key == std::string(s.space) + "." + s.key) return &s;
  return nullptr;
}

bool camSettingNumber(const std::string& key, const std::string& value, long long& out) {
  const CamSetting* s = camSetting(key);
  long long n = 0;
  if (!s || s->kind == 's' || !wholeNumber(value, n) || n < s->min || n > s->max || (s->check && !s->check(value))) return false;
  out = n;
  return true;
}

bool camSettingValid(const std::string& key, const std::string& value) {
  const CamSetting* s = camSetting(key);
  if (!s) return false;
  if (s->kind == 'b') return zeroOrOne(value);
  long long n;
  if (s->kind != 's') return camSettingNumber(key, value, n);
  return s->check && s->check(value);
}

bool camBackupValueValid(const std::string& key, const std::string& value) { return camSettingValid(key, value); }

// An empty hub password, from a backup made before one was required, would leave the hub open: the hub keeps the one
// it has instead.
bool camBackupValueKept(const std::string& key, const std::string& value) { return !(key == "net.webpw" && value.empty()); }

// A setting's number, for one kept as an int.
bool settingInt(const char* key, const std::string& value, int& out) {
  long long n = 0;
  if (!camSettingNumber(key, value, n)) return false;
  out = (int)n;
  return true;
}
bool camBackupKeyKnown(const std::string& key) { return camSetting(key) != nullptr; }
