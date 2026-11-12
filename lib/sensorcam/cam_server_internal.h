// Shared by the files that make up SensorCamServer. Not part of the library's interface.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include "cam_frames.h"
#include "cam_json.h"

static const size_t MAX_REQUEST = 16 * 1024;
static const size_t MAX_HEADERS = 4096;  // larger would let a client with no login use up the memory the hub needs
static const size_t STREAM_BUFFER = CAM_MAX_FRAME + 1024;  // the picture plus 18 bytes of headers per 1400-byte packet
static const uint32_t HTTP_TIMEOUT_MS = 10000;
static const uint32_t MAX_POLL_MS = 10000;      // the longest a PullMessages is held, so it cannot occupy a connection for long
static const int MAX_WAITING_POLLS = 2;         // the rest of the connections stay free for the web page and the NVR
static const size_t MAX_FIRMWARE = 3 * 1024 * 1024;
static const uint8_t LOW_BATTERY_PERCENT = 20;
static const char EVENTS_FILE[] = "events.bin";
static const char RTSP_REALM[] = "SensorCam";
static const char HUB_USER[] = "admin";
static const char JSON_TYPE[] = "application/json";
static const char HTML_TYPE[] = "text/html; charset=utf-8";
static const char JSON_OK[] = "{\"ok\":true}\n";
static const char GZIPPED[] = "Content-Encoding: gzip\r\n";  // the web pages are stored that way
static const char UNAUTHORIZED[] = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"SensorCam\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
static const uint32_t RTSP_IDLE_MS = 120000;
static const uint32_t RTSP_STALL_MS = 30000;
static const uint32_t NOTIFY_SEND_MS = 2000;
static const uint32_t NOTIFY_CLOSE_MS = 1000;
static const uint32_t REPEAT_TRUE_MS = 1000;
static const uint32_t DELAYED_AFTER_S = 30;       // older reports are past events, unless the node's settings say otherwise
static const uint32_t DELAYED_GAP_MS = 4000;      // lets the NVR close one record before the next
static const uint32_t RESEND_WINDOW_MS = 60000;   // the same ESP-NOW report again within this is a resend
static const uint32_t EVENT_SAVE_MS = 5000;  // a burst of events is written to storage once, not once per event
static const char EVENTS_CSV[] = "events.csv";
static const char EVENTS_CSV_OLD[] = "events-old.csv";
static const size_t EVENTS_CSV_MAX = 256 * 1024;  // then it becomes the older file, so about 5000 events are kept
static const char CSV_HEADER[] = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\n";
static const int WEAK_SIGNAL_DBM = -80;     // smoothed ESP-NOW signal below this is reported as weak
static const uint32_t HOOK_WAIT_MS = 5000;  // for the webhook to connect, and again for its reply
static const uint32_t RECORD_CHECK_MS = 5000;     // after an alert starts, time for its event push to reach the NVR
static const uint32_t ALIVE_EVERY_MS = 10UL * 60UL * 1000UL;  // how often the time is saved, so a restart can tell how long the hub was off
static const uint32_t START_CLOCK_WAIT_MS = 120000;  // "hub started" waits this long for the clock, to say how long it was off
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
