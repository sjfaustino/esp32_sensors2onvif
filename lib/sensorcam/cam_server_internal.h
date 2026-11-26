// Shared by the files that make up the hub's parts (SensorCamServer and what it owns). Not part of the library's interface.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <string>
#include <utility>
#include "cam_frames.h"
#include "cam_json.h"
#include "cam_timing.h"

// An ONVIF request, held whole before its login can be checked. An NVR's are about 1 KB, so this leaves room for any
// client while six connections at the limit still cannot use up the hub's memory.
static const size_t MAX_REQUEST = 8 * 1024;
static const size_t MAX_HEADERS = 4096;         // larger would let a client with no login use up the memory the hub needs
static const size_t MAX_UNREAD_REPLIES = 4096;  // RTSP replies a client has not read yet, before its next request waits
static const int MAX_WAITING_POLLS = 2;         // the rest of the connections stay free for the web page and the NVR
static const size_t MAX_FIRMWARE = 3 * 1024 * 1024;
static const uint8_t LOW_BATTERY_PERCENT = 20;
static const char WAITING_FILE[] = "waiting.bin";  // alerts that still need an NVR record, replayed after a restart
static const char RTSP_REALM[] = "SensorCam";
static const char HUB_USER[] = "admin";
static const char JSON_TYPE[] = "application/json";
static const char HTML_TYPE[] = "text/html; charset=utf-8";
static const char JSON_OK[] = "{\"ok\":true}\n";
// The web pages are stored gzipped. no-cache: a browser must not show an old page after the firmware changes. Shown in
// a frame only by the hub's own pages, as the Wi-Fi page shows the setup page: framed by another site, the login the
// browser keeps for the hub would go with it, and that site could lead someone to click Disarm unseen.
static const char PAGE_HEADERS[] = "Content-Encoding: gzip\r\nCache-Control: no-cache\r\nContent-Security-Policy: frame-ancestors 'self'\r\n";
static const char UNAUTHORIZED[] =
    "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"SensorCam\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
static const size_t HISTORY_ROOM = 64 * 1024;  // beyond the event logs: the waiting alerts, settings files and room to rewrite them
static const int WEAK_SIGNAL_DBM = -80;        // smoothed ESP-NOW signal below this is reported as weak
static const char ALIVE_FILE[] = "alive.bin";
static const char MULTICAST_GROUP[] = "239.255.255.250";
static const uint16_t MULTICAST_PORT = 3702;

// ---- HTTP replies ----

inline std::string httpHead(int status, const char* type, size_t length, const std::string& extraHeaders = std::string()) {
  const char* text = status == 200   ? "OK"
                     : status == 400 ? "Bad Request"
                     : status == 403 ? "Forbidden"
                     : status == 409 ? "Conflict"
                     : status == 413 ? "Payload Too Large"
                     : status == 421 ? "Misdirected Request"
                     : status == 422 ? "Unprocessable Entity"
                     : status == 507 ? "Insufficient Storage"
                                     : "Not Found";
  // Every reply: never framed by another site (see PAGE_HEADERS), and taken only as the type it says.
  return "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\nContent-Type: " + type +
         "\r\nX-Frame-Options: SAMEORIGIN\r\nX-Content-Type-Options: nosniff\r\n" + extraHeaders + "Content-Length: " + std::to_string(length) +
         "\r\nConnection: close\r\n\r\n";
}

inline std::string httpResponse(int status, const char* type, const std::string& body) { return httpHead(status, type, body.size()) + body; }
inline std::string jsonOk(const std::string& body) { return httpResponse(200, JSON_TYPE, body); }
inline std::string jsonFail(int status, const std::string& message) { return httpResponse(status, JSON_TYPE, camJsonError(message)); }
// A refusal whose message holds values: "error" has them filled in, in English, and "say" and "values" give the phrase and
// them apart, so a page translates the phrase whole and puts the values in, in its own language's order.
inline std::string jsonFail(int status, const std::string& say, std::initializer_list<std::pair<const char*, std::string>> values) {
  std::string filled = say;
  for (const auto& v : values) {
    std::string mark = std::string("{") + v.first + "}";
    for (size_t at = filled.find(mark); at != std::string::npos; at = filled.find(mark, at + v.second.size()))
      filled.replace(at, mark.size(), v.second);
  }
  std::string body;
  CamJson j(body);
  j.object().flag("ok", false).str("error", filled).str("say", say).object("values");
  for (const auto& v : values) j.str(v.first, v.second);
  j.finish();
  return httpResponse(status, JSON_TYPE, body);
}
const uint32_t CAM_WALK_TEST_S = 600;
inline std::string saved(bool ok, const char* failure) { return ok ? jsonOk(JSON_OK) : jsonFail(507, failure); }

// A reply of one object, whose members fill writes.
template <typename Fill>
inline std::string jsonObject(Fill fill) {
  std::string out;
  CamJson j(out);
  j.object();
  fill(j);
  j.finish();
  return out;
}
