// SensorCamServer: the alarm's side of the network. What reaches CamAlarm from ESP-NOW and HTTP, and what it asks of the
// rest of the hub.
#include <string.h>
#include <new>
#include "cam_server.h"
#include "cam_number.h"
#include "cam_net.h"
#include "cam_server_internal.h"

// Checking, starting and confirming an alert use no heap, so they work even when memory has run out. Anything else that
// runs out of memory drops only itself: the hub keeps running, and a report left unconfirmed is sent again.
void SensorCamServer::espNowPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
  try {
    if (len >= 1 && data[0] == CAM_FIRMWARE_ASK)
      nodes_.firmwareAsk(mac, data, len);
    else if (len >= 1 && data[0] == CAM_HELLO)
      nodes_.hello(mac, data, len, rssi);
    else
      alarm_.espNowReport(mac, data, len, rssi);
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
  }
}

std::string SensorCamServer::handleSensor(const std::string& target, int& status) {
  if (!cfg_.sensorToken.empty() && !camSameText(queryParam(target, "token"), cfg_.sensorToken)) {
    status = 403;
    return camJsonError("bad token");
  }
  uint8_t mac[6], input = 1;
  std::string inputText = queryParam(target, "input");
  if (!inputText.empty() && !camNumberIn(inputText, (uint8_t)1, (uint8_t)255, input)) {  // never a different input's alert
    status = 400;
    return camJsonError("input must be a number from 1 to 255");
  }
  bool macOk = camParseMac(queryParam(target, "mac"), mac);
  std::string state = queryParam(target, "state");
  SensorReport r;
  r.active = !(state == "clear" || state == "off");
  std::string age = queryParam(target, "age");
  uint32_t ageS = 0;
  r.age = age.empty() ? 0 : camNumber(age, ageS) ? ageS : CAM_AGE_UNKNOWN;  // "unknown", or anything else unreadable
  int percent = 0;
  if (camNumber(queryParam(target, "battery"), percent)) r.battery = camBatteryPercent(percent);
  std::string distance = queryParam(target, "distance"), direction = queryParam(target, "direction");
  if (!distance.empty() || !direction.empty()) {
    int d = 0;  // stays 0 when missing or unreadable, and the distance stays unknown
    camNumber(direction, d);
    r.targetCount = 1;
    camNumber(distance, r.targets[0].distanceCm);
    r.targets[0].direction = (uint8_t)(d >= 0 && d < CAM_DIRECTIONS ? d : 0);
  }
  // The reply is the confirmation, so it waits for storage as an ESP-NOW confirmation does.
  status = macOk ? alarm_.webReport(mac, input, r) : 404;
  if (status == 404) return camJsonError("unknown sensor");
  if (status == 507) return camJsonError("the alert was received but could not be saved; send it again");
  return JSON_OK;
}

bool SensorCamServer::downloadingHistory() const {
  for (const HttpConn& c : http_)
    if (c.fd >= 0 && c.stream == &csvRead_) return true;
  return false;
}

// The NVR is listening when it holds a subscription and either the last event pushed to it arrived, or, for one that
// pulls events, it asked for events within the last minute. With no NVR address set, any subscriber counts.
bool SensorCamServer::nvrListening(uint32_t now) const {
  uint32_t nvr = cfg_.nvrIp.empty() ? 0 : inet_addr(cfg_.nvrIp.c_str());
  for (int i = 0; i < SubTable::SIZE; i++) {
    const CamSub& s = subs_.at(i);
    if (!s.used || (nvr && s.peer != nvr)) continue;
    if (s.push ? !s.pushFailed : now - s.pulledAt < CAM_PULL_ALIVE_MS) return true;  // other requests from it do not count
  }
  return false;
}

void SensorCamServer::sensorReset(int id) { mqtt_.sensorReset(id); }

void SensorCamServer::sensorsRescanned(bool listChanged) {
  video_.changed();
  if (listChanged) mqtt_.sensorsChanged();
}

void SensorCamServer::notifyHook(int id, CamEventKind kind, bool delayed) { webhook_.notify(id, kind, delayed); }

void SensorCamServer::notifyHubHook(CamEventKind kind, const std::string& detail, const std::string& message) {
  webhook_.notifyHub(kind, detail, message);
}
