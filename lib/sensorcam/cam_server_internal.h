// Shared by the files that make up SensorCamServer. Not part of the library's interface.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <string>
#include <utility>
#include "cam_frames.h"
#include "cam_json.h"
#include "cam_timing.h"

static const size_t MAX_REQUEST = 16 * 1024;
static const size_t MAX_HEADERS = 4096;  // larger would let a client with no login use up the memory the hub needs
static const size_t STREAM_BUFFER = CAM_MAX_FRAME + 1024;  // the picture plus 18 bytes of headers per 1400-byte packet
static const int MAX_WAITING_POLLS = 2;         // the rest of the connections stay free for the web page and the NVR
static const size_t MAX_FIRMWARE = 3 * 1024 * 1024;
static const uint8_t LOW_BATTERY_PERCENT = 20;
static const char WAITING_FILE[] = "waiting.bin";  // alerts that still need an NVR record, replayed after a restart
static const char RTSP_REALM[] = "SensorCam";
static const char HUB_USER[] = "admin";
static const char JSON_TYPE[] = "application/json";
static const char HTML_TYPE[] = "text/html; charset=utf-8";
static const char JSON_OK[] = "{\"ok\":true}\n";
static const char GZIPPED[] = "Content-Encoding: gzip\r\n";  // the web pages are stored that way
static const char UNAUTHORIZED[] = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"SensorCam\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
static const size_t HISTORY_ROOM = 64 * 1024;  // beyond the event logs: the waiting alerts, settings files and room to rewrite them
static const int WEAK_SIGNAL_DBM = -80;     // smoothed ESP-NOW signal below this is reported as weak
static const char ALIVE_FILE[] = "alive.bin";
static const char MULTICAST_GROUP[] = "239.255.255.250";
static const uint16_t MULTICAST_PORT = 3702;

// Calls a callback the board may leave unset. An unset one does nothing and gives false, an empty text or nothing,
// so a missing feature is refused, not a crash.
template <typename R, typename... P, typename... A>
inline R camCall(const std::function<R(P...)>& f, A&&... a) {
  if (!f) return R();
  return f(std::forward<A>(a)...);
}

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
  return "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\nContent-Type: " + type + "\r\n" + extraHeaders + "Content-Length: " + std::to_string(length) + "\r\nConnection: close\r\n\r\n";
}

inline std::string httpResponse(int status, const char* type, const std::string& body) { return httpHead(status, type, body.size()) + body; }
inline std::string jsonOk(const std::string& body) { return httpResponse(200, JSON_TYPE, body); }
inline std::string jsonFail(int status, const std::string& message) { return httpResponse(status, JSON_TYPE, camJsonError(message)); }
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
