// SensorCamServer: the loop that runs the hub's parts, and what the parts ask of one another.
#include <string.h>
#include <new>
#include "cam_server.h"
#include "cam_number.h"
#include "cam_net.h"
#include "cam_server_internal.h"

// A part the board leaves out: every feature it serves refused or left out.
static CamChip noChip;
static CamRadio noRadio;
static CamStore noStore;

SensorCamServer::SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn)
    : cfg_(cfg),
      chip_(cfg.chip ? cfg.chip : &noChip),
      radio_(cfg.radio ? cfg.radio : &noRadio),
      store_(cfg.store ? cfg.store : &noStore),
      localIp_(localIp),
      millis_(millisFn),
      storage_(cfg.fsRoot),
      alarm_(cfg_, *chip_, *radio_, *store_, storage_, table_, images_, *this, failures_, millisFn),
      nodes_(cfg_, *radio_, storage_, alarm_, table_, images_, millisFn),
      events_(cfg_, alarm_, *chip_, localIp),
      mqtt_(cfg_, alarm_, table_, *chip_, events_, failures_, localIp),
      video_(cfg_, *chip_, storage_, alarm_, table_, logins_, events_, failures_, millisFn),
      webhook_(cfg_, alarm_, table_, *chip_, events_, failures_),
      web_(CamWebParts{cfg_, *chip_, *radio_, *store_, storage_, logins_, nodes_, alarm_, table_, mqtt_, events_, video_, webhook_, failures_,
                       localIp, millisFn}) {}

bool SensorCamServer::begin() {
  netInit();
  bool webOk = web_.begin();
  bool videoOk = video_.begin(netListen(cfg_.rtspPort, cfg_.loopbackOnly));
  events_.begin();
  lastGoodWait_ = millis_();
  nodes_.begin();
  alarm_.begin();
  mqtt_.begin();
  return webOk && videoOk;
}

std::string SensorCamServer::debugLine() {
  int web = web_.openCount(), video = video_.openCount(), push = events_.pushCount();
  return "wait errors " + std::to_string(waitErrors_) + " (errno " + std::to_string(lastWaitErrno_) + "), accepted " +
         std::to_string(web_.accepted()) + ", refused " + std::to_string(web_.refused()) + ", open web " + std::to_string(web) + " video " +
         std::to_string(video) + " push " + std::to_string(push) + ", short of memory " + std::to_string(memoryFailures());
}

void SensorCamServer::networkUp() {
  alarm_.networkUp();
  events_.networkUp();
}

// Running out of memory drops only the piece of work that needed it, never the hub: a restart would miss alerts while it
// lasted. Whatever was dropped is tried again on a later pass, or by the client that asked.
// The two halves apart, so work that keeps running out of memory cannot stop the connections being served.
void SensorCamServer::poll(int maxWaitMs) {
  try {
    housekeeping(millis_());
  } catch (const std::bad_alloc&) {
    failures_.note();
  }
  try {
    pollOnce(maxWaitMs);
  } catch (const std::bad_alloc&) {
    failures_.note();
  }
}

void SensorCamServer::housekeeping(uint32_t now) {
  alarm_.tick(now);
  webhook_.dailyCheck(now);
  events_.subs().expire(now);
  web_.servicePolls(now);
  events_.tick(now, web_.sharedSockets() - events_.pushCount(), CamWeb::MAX_HTTP);
  webhook_.tick(now, web_.sharedSockets() < CamWeb::MAX_HTTP);  // leaves one for an event push
  try {                                                         // on its own, so running out of memory there still leaves the picture to be sent
    mqtt_.tick(now, web_.sharedSockets() < CamWeb::MAX_HTTP && !web_.updating());
  } catch (const std::bad_alloc&) {
    failures_.note();
  }
  video_.tick(now);
}

void SensorCamServer::pollOnce(int maxWaitMs) {
  uint32_t now = millis_();
  fd_set rd, wr;
  FD_ZERO(&rd);
  FD_ZERO(&wr);
  int maxFd = -1;
  web_.watch(rd, wr, maxFd);
  video_.watch(rd, wr, maxFd, !web_.updating());
  events_.watch(rd, wr, maxFd);
  webhook_.watch(rd, wr, maxFd);
  if (mqtt_.wantsRead()) netWatch(mqtt_.fd(), rd, maxFd);
  if (mqtt_.wantsWrite()) netWatch(mqtt_.fd(), wr, maxFd);

  int32_t untilFrame = video_.untilFrame(now);
  int wait = untilFrame < maxWaitMs ? (untilFrame < 0 ? 0 : untilFrame) : maxWaitMs;
  timeval tv{wait / 1000, (wait % 1000) * 1000};
  if (maxFd >= 0 && select(maxFd + 1, &rd, &wr, nullptr, &tv) < 0) {
    waitErrors_++;
    lastWaitErrno_ = errno;
    return;
  }
  now = millis_();
  lastGoodWait_ = now;

  web_.serve(rd, wr, now);
  video_.serve(rd, wr, now);
  events_.serve(rd, wr, now);
  webhook_.serve(rd, wr, now);
  if (mqtt_.fd() >= 0) mqtt_.service(FD_ISSET(mqtt_.fd(), &rd), FD_ISSET(mqtt_.fd(), &wr), now);
}

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
    failures_.note();
  }
}

bool SensorCamServer::nvrListening(uint32_t now) const { return events_.nvrListening(now); }

bool SensorCamServer::downloadingHistory() const { return web_.downloadingHistory(); }

void SensorCamServer::sensorReset(int id) { mqtt_.sensorReset(id); }

void SensorCamServer::sensorsRescanned(bool listChanged) {
  video_.changed();
  if (listChanged) mqtt_.sensorsChanged();
}

void SensorCamServer::notifyHook(int id, CamEventKind kind, bool delayed) { webhook_.notify(id, kind, delayed); }

void SensorCamServer::notifyHubHook(CamEventKind kind, const std::string& detail, const std::string& message) {
  webhook_.notifyHub(kind, detail, message);
}
