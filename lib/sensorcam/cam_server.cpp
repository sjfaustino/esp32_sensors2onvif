#include "cam_server.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_web.h"

static const size_t MAX_REQUEST = 16 * 1024;
static const size_t MAX_UPLOAD = CAM_MAX_FRAME + 2048;
static const size_t STREAM_BUFFER = CAM_MAX_FRAME + 1024;  // the picture plus 18 bytes of headers per 1400-byte packet
static const uint32_t HTTP_TIMEOUT_MS = 10000;
static const uint32_t RTSP_IDLE_MS = 120000;
static const uint32_t RTSP_STALL_MS = 30000;
static const uint32_t NOTIFY_SEND_MS = 2000;
static const uint32_t NOTIFY_CLOSE_MS = 1000;
static const uint32_t REPEAT_TRUE_MS = 1000;
static const uint32_t DELAYED_AFTER_S = 30;       // older reports are shown as delayed
static const uint32_t DELAYED_GAP_MS = 4000;      // lets the NVR close one record before the next
static const uint32_t SILENT_AFTER_MS = 3UL * 3600UL * 1000UL;  // missed heartbeats
static const time_t CLOCK_VALID_AFTER = 1577836800;  // 2020-01-01: earlier means NTP has not synced
static const char MULTICAST_GROUP[] = "239.255.255.250";
static const uint16_t MULTICAST_PORT = 3702;

SensorCamServer::SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn)
    : cfg_(cfg), localIp_(localIp), millis_(millisFn), sensors_(CAM_MAX_SENSORS, cfg.holdMs), signal_(REPEAT_TRUE_MS),
      storage_(cfg.fsRoot), slots_(CAM_MAX_SENSORS), health_(CAM_MAX_SENSORS) {}

int SensorCamServer::listenOn(uint16_t port) {
  int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int on = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = cfg_.loopbackOnly ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
  if (bind(fd, (sockaddr*)&a, sizeof(a)) != 0 || listen(fd, 4) != 0) {
    netClose(fd);
    return -1;
  }
  netNonblock(fd);
  return fd;
}

void SensorCamServer::openDiscovery() {
  if (udp_ >= 0) netClose(udp_);
  udp_ = (int)socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_ < 0) return;
  int on = 1;
  setsockopt(udp_, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(cfg_.discoveryPort);
  a.sin_addr.s_addr = cfg_.loopbackOnly ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
  if (bind(udp_, (sockaddr*)&a, sizeof(a)) != 0) {
    netClose(udp_);
    udp_ = -1;
    return;
  }
  netNonblock(udp_);
  if (!cfg_.loopbackOnly) {
    ip_mreq m{};
    m.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(udp_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&m, sizeof(m));
  }
}

bool SensorCamServer::begin() {
  netInit();
  httpListen_ = listenOn(cfg_.onvifPort);
  webListen_ = listenOn(cfg_.webPort);
  if (cfg_.captivePort && cfg_.captivePort != cfg_.webPort && cfg_.captivePort != cfg_.onvifPort && cfg_.captivePort != cfg_.rtspPort) captiveListen_ = listenOn(cfg_.captivePort);
  rtspListen_ = listenOn(cfg_.rtspPort);
  openDiscovery();
  nextFrameAt_ = millis_();
  lastGoodWait_ = nextFrameAt_;
  frameData_.reserve(CAM_MAX_FRAME);
  for (std::string& spare : spareOut_) spare.reserve(STREAM_BUFFER);
  rescan();
  return httpListen_ >= 0 && webListen_ >= 0 && rtspListen_ >= 0;
}

std::string SensorCamServer::debugLine() {
  int web = 0, video = 0, push = 0;
  for (const HttpConn& c : http_) web += c.fd >= 0;
  for (const RtspConn& c : rtsp_) video += c.fd >= 0;
  for (const NotifyIo& n : notify_) push += n.state != NotifyIo::IDLE;
  return "wait errors " + std::to_string(waitErrors_) + " (errno " + std::to_string(lastWaitErrno_) + "), accepted " + std::to_string(accepted_) +
         ", refused " + std::to_string(refused_) + ", open web " + std::to_string(web) + " video " + std::to_string(video) + " push " + std::to_string(push);
}

void SensorCamServer::networkUp() {
  openDiscovery();
  if (cfg_.loopbackOnly || udp_ < 0) return;
  std::string hello = discoveryHello(localIp_(), cfg_.onvifPort, cfg_.serial, messageNo_++);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(MULTICAST_PORT);
  to.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
  sendto(udp_, hello.data(), (int)hello.size(), 0, (sockaddr*)&to, sizeof(to));
}

void SensorCamServer::closeHttp(HttpConn& c) {
  if (uploading_ == &c) {
    uploading_ = nullptr;
    frameKey_.clear();
  }
  if (c.fd >= 0) netClose(c.fd);
  c = HttpConn();
}

void SensorCamServer::closeRtsp(RtspConn& c) {
  if (c.out.capacity() >= STREAM_BUFFER) {  // keep the reserved buffer for the next viewer
    for (std::string& spare : spareOut_) {
      if (spare.capacity() >= STREAM_BUFFER) continue;
      c.out.clear();
      spare.swap(c.out);
      break;
    }
  }
  if (c.fd >= 0) netClose(c.fd);
  c = RtspConn();
}

void SensorCamServer::acceptHttp(int listenFd, uint32_t now) {
  sockaddr_in a{};
  socklen_t len = sizeof(a);
  int fd = (int)accept(listenFd, (sockaddr*)&a, &len);
  if (fd < 0) return;
  if (listenFd == captiveListen_ && !(cfg_.setupActive && cfg_.setupActive())) {
    netClose(fd);
    return;
  }
  if (cfg_.canAllocate && !cfg_.canAllocate(0)) {  // short of RAM: let existing traffic drain first
    refused_++;
    netClose(fd);
    return;
  }
  accepted_++;
  for (HttpConn& c : http_) {
    if (c.fd >= 0) continue;
    netNonblock(fd);
    c.fd = fd;
    c.web = listenFd != httpListen_;
    c.peer = a.sin_addr.s_addr;
    c.openedAt = now;
    return;
  }
  netClose(fd);  // all slots busy; the client will retry
}

void SensorCamServer::acceptRtsp(uint32_t now) {
  sockaddr_in a{};
  socklen_t len = sizeof(a);
  int fd = (int)accept(rtspListen_, (sockaddr*)&a, &len);
  if (fd < 0) return;
  if (cfg_.canAllocate && !cfg_.canAllocate(0)) {
    netClose(fd);
    return;
  }
  for (RtspConn& c : rtsp_) {
    if (c.fd >= 0) continue;
    netNonblock(fd);
    c.fd = fd;
    c.peer = a.sin_addr.s_addr;
    c.activityAt = now;
    return;
  }
  netClose(fd);
}

// ---- HTTP: ONVIF SOAP and the sensor input share one port ----

static std::string httpResponse(int status, const char* type, const std::string& body) {
  const char* text = status == 200   ? "OK"
                     : status == 400 ? "Bad Request"
                     : status == 403 ? "Forbidden"
                     : status == 413 ? "Payload Too Large"
                     : status == 422 ? "Unprocessable Entity"
                     : status == 507 ? "Insufficient Storage"
                                     : "Not Found";
  return "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\nContent-Type: " + type + "\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

static uint32_t subIdFromPath(const std::string& path) {
  size_t slash = path.rfind('/');
  return slash == std::string::npos ? 0 : (uint32_t)strtoul(path.c_str() + slash + 1, nullptr, 10);
}

std::string SensorCamServer::handleOnvif(HttpConn& c, const std::string& headers, const std::string& body, uint32_t now, int& status) {
  std::string action = soapAction(body);
  std::string path = headers.substr(0, headers.find("\r\n"));
  size_t sp1 = path.find(' '), sp2 = path.rfind(' ');
  path = sp1 != std::string::npos && sp2 > sp1 ? path.substr(sp1 + 1, sp2 - sp1 - 1) : "/";

  SoapContext ctx;
  ctx.host = headerValue(headers, "host");
  if (ctx.host.empty()) ctx.host = localIp_() + ":" + std::to_string(cfg_.onvifPort);
  ctx.rtspPort = cfg_.rtspPort;
  ctx.serial = cfg_.serial;
  ctx.now = time(nullptr);

  // Dispatch on the SOAP action, never on the URL: the NVR sends Subscribe to device_service.
  std::string reply;
  if (action == "Subscribe") {
    std::string consumer = soapConsumerAddress(body);
    HttpUrl url;
    if (consumer.empty() || !parseHttpUrl(consumer, url) || inet_addr(url.host.c_str()) == INADDR_NONE) {
      status = 400;
      return soapFault("Subscribe needs an http consumer address with an IPv4 host");
    }
    reply = soapSubscribeReply(ctx, subs_.addPush(consumer, c.peer, now).id);
  } else if (action == "CreatePullPointSubscription") {
    reply = soapPullPointReply(ctx, subs_.addPull(c.peer, now).id);
  } else if (action == "PullMessages") {
    CamSub* sub = subs_.find(subIdFromPath(path));
    int state = sensors_.anyActive() ? 1 : 0;
    bool changed = sub && sub->lastPull != state;
    bool initial = sub && sub->lastPull < 0;
    if (sub) sub->lastPull = state;
    reply = soapPullMessagesReply(ctx, changed, state == 1, initial);
  } else if (action == "Renew") {
    reply = soapRenewReply(ctx);
  } else if (action == "Unsubscribe") {
    subs_.remove(subIdFromPath(path));
    reply = soapUnsubscribeReply();
  } else {
    reply = soapDeviceReply(action, ctx);
  }
  if (reply.empty()) {
    status = 400;
    return soapFault("Action not supported");
  }
  status = 200;
  return soapEnvelope(reply);
}

void SensorCamServer::logEvent(int id, const SensorReport& r, bool delayed) {
  CamEvent& e = events_[eventNext_];
  e.received = time(nullptr);
  e.age = r.age;
  snprintf(e.sensor, sizeof(e.sensor), "%s", slotName(id).c_str());
  e.active = r.active;
  e.delayed = delayed;
  e.battery = r.battery;
  e.targetCount = r.targetCount;
  for (int i = 0; i < CAM_MAX_TARGETS; i++) e.targets[i] = r.targets[i];
  eventNext_ = (eventNext_ + 1) % EVENT_LOG_SIZE;
  if (eventCount_ < EVENT_LOG_SIZE) eventCount_++;
}

bool SensorCamServer::applyReport(int id, const SensorReport& r, uint32_t now) {
  if (id < 1 || id > sensors_.count()) return false;
  Health& h = health_[id - 1];
  h.heard = true;
  h.heardAt = now;
  h.silent = false;
  h.battery = r.battery;

  bool late = r.age > DELAYED_AFTER_S;  // an unknown age counts as late
  bool wasActive = sensors_.active(id);
  if (r.active && late) {
    if (delayedCount_ < DELAYED_QUEUE_SIZE) {
      DelayedAlert& d = delayed_[(delayedHead_ + delayedCount_) % DELAYED_QUEUE_SIZE];
      d.sensor = (uint8_t)id;
      d.direction = reportDirection(r);
      delayedCount_++;
    }
  } else if (r.active) {
    sensors_.trigger(id, now, reportDirection(r));
  } else if (!late) {
    sensors_.clear(id);
  }
  // Repeats and heartbeats that change nothing would push real events out of the log.
  if (late || r.active != wasActive) logEvent(id, r, late);
  return true;
}

void SensorCamServer::espNowInput(const uint8_t mac[6], const SensorReport& r) {
  int id = findSlot(mac, r.input);
  if (id == 0) return;
  Health& h = health_[id - 1];
  // A resend repeats the counter. Only equality is rejected, so a sensor that restarts its count still works.
  if (h.counterSeen && h.counter == r.counter) return;
  h.counterSeen = true;
  h.counter = r.counter;
  applyReport(id, r, millis_());
}

// Late alerts play back one at a time so each becomes its own NVR record.
void SensorCamServer::serviceDelayed(uint32_t now) {
  // The flag keeps the comparison valid: an old timestamp alone would look like the future after 24.8 days.
  if (delayedGap_ && (int32_t)(now - nextDelayedAt_) >= 0) delayedGap_ = false;
  if (delayedCount_ == 0 || sensors_.anyActive() || delayedGap_) return;
  DelayedAlert d = delayed_[delayedHead_];
  delayedHead_ = (delayedHead_ + 1) % DELAYED_QUEUE_SIZE;
  delayedCount_--;
  if (slots_[d.sensor - 1].used) sensors_.trigger(d.sensor, now, d.direction, true);  // it may have been deleted while waiting
  nextDelayedAt_ = now + cfg_.holdMs + DELAYED_GAP_MS;
  delayedGap_ = true;
}

std::string SensorCamServer::handleSensor(const std::string& target, int& status) {
  if (!cfg_.sensorToken.empty() && queryParam(target, "token") != cfg_.sensorToken) {
    status = 403;
    return "{\"ok\":false,\"error\":\"bad token\"}\n";
  }
  uint8_t mac[6];
  std::string input = queryParam(target, "input");
  int id = camParseMac(queryParam(target, "mac"), mac) ? findSlot(mac, input.empty() ? 1 : (uint8_t)atoi(input.c_str())) : 0;
  std::string state = queryParam(target, "state");
  SensorReport r;
  r.active = !(state == "clear" || state == "off");
  std::string age = queryParam(target, "age");
  r.age = age == "unknown" ? CAM_AGE_UNKNOWN : (uint32_t)strtoul(age.c_str(), nullptr, 10);
  std::string battery = queryParam(target, "battery");
  if (!battery.empty()) r.battery = (uint8_t)atoi(battery.c_str());
  std::string distance = queryParam(target, "distance"), direction = queryParam(target, "direction");
  if (!distance.empty() || !direction.empty()) {
    int d = atoi(direction.c_str());
    r.targetCount = 1;
    if (!distance.empty()) r.targets[0].distanceCm = (uint16_t)atoi(distance.c_str());
    r.targets[0].direction = (uint8_t)(d >= 0 && d < CAM_DIRECTIONS ? d : 0);
  }
  bool ok = applyReport(id, r, millis_());
  status = ok ? 200 : 404;
  return ok ? "{\"ok\":true}\n" : "{\"ok\":false,\"error\":\"unknown sensor\"}\n";
}

std::string SensorCamServer::statusJson(uint32_t now) {
  std::string active;
  int known = 0;
  for (int id = 1; id <= sensors_.count(); id++) {
    known += slots_[id - 1].used;
    if (sensors_.active(id)) active += (active.empty() ? "\"" : ",\"") + slotName(id) + "\"";
  }
  int streams = 0;
  for (RtspConn& c : rtsp_) streams += c.playing;
  std::string health;
  for (int id = 1; id <= sensors_.count(); id++) {
    const Health& h = health_[id - 1];
    if (!h.heard) continue;
    uint32_t quiet = now - h.heardAt;
    health += (health.empty() ? "" : ",") + std::string("{\"sensor\":\"") + slotName(id) + "\",\"last_heard_s\":" + std::to_string(quiet / 1000) +
              ",\"battery\":" + (h.battery == CAM_BATTERY_UNKNOWN ? std::string("null") : std::to_string(h.battery)) +
              ",\"silent\":" + (h.silent ? "true" : "false") + "}";
  }
  return "{\"sensors\":" + std::to_string(known) + ",\"active\":[" + active + "],\"subscribers\":" +
         std::to_string(subs_.count()) + ",\"streams\":" + std::to_string(streams) + ",\"delayed_waiting\":" + std::to_string(delayedCount_) +
         ",\"heard\":[" + health + "],\"uptime_s\":" + std::to_string(now / 1000) + "}\n";
}

std::string SensorCamServer::eventsJson() {
  // Trimmed to what RAM allows, because running out is fatal. About 200 bytes each, doubled while building the reply.
  int count = eventCount_;
  while (count > 0 && cfg_.canAllocate && !cfg_.canAllocate((size_t)count * 400)) count /= 2;
  std::string out = "[";
  out.reserve((size_t)count * 200 + 8);
  for (int n = 0; n < count; n++) {
    const CamEvent& e = events_[(eventNext_ - 1 - n + 2 * EVENT_LOG_SIZE) % EVENT_LOG_SIZE];
    bool clockOk = e.received >= CLOCK_VALID_AFTER;
    bool ageKnown = e.age != CAM_AGE_UNKNOWN;
    if (n) out += ",";
    out += std::string("{\"sensor\":\"") + e.sensor + "\",\"active\":" + (e.active ? "true" : "false") +
           ",\"delayed\":" + (e.delayed ? "true" : "false") +
           ",\"received\":" + (clockOk ? "\"" + iso(e.received) + "\"" : std::string("null")) +
           ",\"happened\":" + (clockOk && ageKnown ? "\"" + iso(e.received - (time_t)e.age) + "\"" : std::string("null")) +
           ",\"age_s\":" + (ageKnown ? std::to_string(e.age) : std::string("null")) +
           ",\"battery\":" + (e.battery == CAM_BATTERY_UNKNOWN ? std::string("null") : std::to_string(e.battery)) + ",\"targets\":[";
    for (int i = 0; i < e.targetCount; i++) {
      out += std::string(i ? "," : "") + "{\"distance_cm\":" +
             (e.targets[i].distanceCm == CAM_DISTANCE_UNKNOWN ? std::string("null") : std::to_string(e.targets[i].distanceCm)) +
             ",\"direction\":" + std::to_string(e.targets[i].direction) + "}";
    }
    out += "]}";
  }
  return out + "]\n";
}

// ---- sensor table and pictures, both driven by the folders on storage ----

int SensorCamServer::findSlot(const uint8_t mac[6], uint8_t input) const {
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (slots_[i].used && slots_[i].input == input && memcmp(slots_[i].mac, mac, 6) == 0) return i + 1;
  return 0;
}

std::string SensorCamServer::slotName(int id) const {
  return camMacText(slots_[id - 1].mac) + "/" + std::to_string(slots_[id - 1].input);
}

void SensorCamServer::rescan() {
  bool seen[CAM_MAX_SENSORS] = {false};
  std::vector<CamFsEntry> slaves;
  storage_.list("", slaves);
  for (const CamFsEntry& s : slaves) {
    uint8_t mac[6];
    if (!s.dir || !camParseMac(s.name, mac)) continue;
    std::vector<CamFsEntry> inputs;
    storage_.list(s.name, inputs);
    for (const CamFsEntry& e : inputs) {
      if (!e.dir || camClassifyPath(s.name + "/" + e.name) != CAM_PATH_SENSOR) continue;
      uint8_t input = (uint8_t)atoi(e.name.c_str());
      int id = findSlot(mac, input);
      for (int i = 0; id == 0 && i < CAM_MAX_SENSORS; i++) {
        if (slots_[i].used) continue;
        slots_[i].used = true;
        memcpy(slots_[i].mac, mac, 6);
        slots_[i].input = input;
        health_[i] = Health();
        id = i + 1;
      }
      if (id) seen[id - 1] = true;
    }
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++) {
    if (!slots_[i].used || seen[i]) continue;
    sensors_.clear(i + 1);
    slots_[i] = Slot();
    health_[i] = Health();
  }
  frameKey_.clear();
}

bool SensorCamServer::loadFrame(const std::string& path) {
  // frameData_ is one fixed buffer reserved at boot. Allocating per picture is what exhausted RAM.
  if (!storage_.readFile(path, frameData_, CAM_MAX_FRAME)) return false;
  return camParseFrame((const uint8_t*)frameData_.data(), frameData_.size(), frameNals_) && camSameNal(frameNals_[0], CAM_FRAMES[0].nals[0]);
}

// A missing picture falls back to the nearest one that exists, then to the built-in pictures.
const CamFrame& SensorCamServer::frameToShow(uint32_t now) {
  int id = 0;
  uint8_t direction = 0;
  bool delayed = false;
  bool on = sensors_.shown(now, id, direction, delayed);
  if (uploading_) {  // an upload is using the picture buffer
    frameKey_.clear();
    frame_ = &CAM_FRAMES[on ? 1 : 0];
    return *frame_;
  }
  std::string key = on ? slotName(id) + "/" + std::to_string(direction) + (delayed ? "d" : "") : "clear";
  if (key == frameKey_) return *frame_;
  frameKey_ = key;

  std::vector<std::string> candidates;
  if (on) {
    std::string base = slotName(id) + "/";
    candidates = {base + camStateFile(direction, delayed), base + camStateFile(0, delayed), base + camStateFile(direction, false), base + camStateFile(0, false)};
  } else {
    candidates = {"clear.h264"};
  }
  frame_ = &CAM_FRAMES[on ? 1 : 0];
  for (const std::string& path : candidates) {
    if (loadFrame(path)) {
      frame_ = &loadedFrame_;
      break;
    }
  }
  return *frame_;
}

// ---- file manager ----

static std::string jsonError(const char* message) { return std::string("{\"ok\":false,\"error\":\"") + message + "\"}\n"; }
static const char JSON_OK[] = "{\"ok\":true}\n";

bool SensorCamServer::authorized(const std::string& headers) const {
  if (cfg_.webPassword.empty()) return true;
  std::string pair = "admin:" + cfg_.webPassword;
  return headerValue(headers, "authorization") == "Basic " + base64((const uint8_t*)pair.data(), pair.size());
}

std::string SensorCamServer::treeJson() {
  uint32_t used = 0, total = 0;
  if (cfg_.fsUsage) cfg_.fsUsage(used, total);
  std::vector<CamFsEntry> root;
  storage_.list("", root);
  std::string clear = "null", slaves;
  for (const CamFsEntry& s : root) {
    if (!s.dir && s.name == "clear.h264") clear = std::to_string(s.size);
    if (!s.dir || camClassifyPath(s.name) != CAM_PATH_SLAVE) continue;
    std::vector<CamFsEntry> inputs;
    storage_.list(s.name, inputs);
    std::string sensors;
    for (const CamFsEntry& n : inputs) {
      std::string base = s.name + "/" + n.name;
      if (!n.dir || camClassifyPath(base) != CAM_PATH_SENSOR) continue;
      std::vector<CamFsEntry> items;
      storage_.list(base, items);
      std::string files;
      for (const CamFsEntry& f : items) {
        if (f.dir || camClassifyPath(base + "/" + f.name) != CAM_PATH_FILE) continue;
        files += (files.empty() ? "" : ",") + std::string("{\"name\":\"") + f.name + "\",\"size\":" + std::to_string(f.size) + "}";
      }
      sensors += (sensors.empty() ? "" : ",") + std::string("{\"input\":") + n.name + ",\"files\":[" + files + "]}";
    }
    slaves += (slaves.empty() ? "" : ",") + std::string("{\"mac\":\"") + s.name + "\",\"sensors\":[" + sensors + "]}";
  }
  return "{\"used\":" + std::to_string(used) + ",\"total\":" + std::to_string(total) + ",\"clear\":" + clear + ",\"slaves\":[" + slaves + "]}\n";
}

// A Wi-Fi name or password can hold any character, so form values arrive percent-encoded.
static std::string percentDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

void SensorCamServer::handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body, size_t bodyLen) {
  std::string route = target.substr(0, target.find('?'));
  // On the setup network the password of that network already guards these, and a captive-portal window may not offer a login.
  bool inSetup = cfg_.setupActive && cfg_.setupActive();
  bool setupRoute = route == "/setup" || route == "/api/wifi" || route == "/api/wifi/scan" || (inSetup && route == "/");
  if (!(inSetup && setupRoute) && !authorized(headers)) {
    c.out = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"SensorCam\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    return;
  }
  const char* json = "application/json";
  if (setupRoute) {
    if (method == "GET" && (route == "/" || route == "/setup")) {
      c.staticBody = CAM_SETUP_PAGE;
      c.staticLen = sizeof(CAM_SETUP_PAGE) - 1;
      c.out = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(c.staticLen) + "\r\nConnection: close\r\n\r\n";
    } else if (method == "GET" && route == "/api/wifi/scan") {
      c.out = httpResponse(200, json, cfg_.wifiNetworks ? cfg_.wifiNetworks() : std::string("{\"scanning\":false,\"networks\":[]}\n"));
    } else if (method == "POST" && route == "/api/wifi") {
      std::string form = "?" + std::string(body, bodyLen);
      std::string ssid = percentDecode(queryParam(form, "ssid")), password = percentDecode(queryParam(form, "password"));
      if (ssid.empty() || ssid.size() > 32 || (!password.empty() && (password.size() < 8 || password.size() > 63))) {
        c.out = httpResponse(400, json, jsonError("the name must be 1 to 32 characters and the password empty or 8 to 63"));
      } else {
        c.out = cfg_.wifiSave && cfg_.wifiSave(ssid, password, queryParam(form, "role") == "backup") ? httpResponse(200, json, JSON_OK) : httpResponse(507, json, jsonError("could not save the Wi-Fi settings"));
      }
    } else {
      c.out = httpResponse(404, json, jsonError("not found"));
    }
    return;
  }
  if (method == "GET" && (route == "/" || route == "/files")) {
    c.staticBody = CAM_WEB_PAGE;
    c.staticLen = sizeof(CAM_WEB_PAGE) - 1;
    c.out = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(c.staticLen) + "\r\nConnection: close\r\n\r\n";
    return;
  }
  if (method == "GET" && route == "/api/tree") {
    c.out = httpResponse(200, json, treeJson());
    return;
  }
  if (method == "POST" && route == "/api/settings") {
    std::string form = "?" + std::string(body, bodyLen);
    auto port = [&](const char* name, long current) {  // absent keeps the current port; anything but a plain number is invalid
      std::string text = queryParam(form, name);
      if (text.empty()) return current;
      char* end = nullptr;
      long value = strtol(text.c_str(), &end, 10);
      return *end ? -1L : value;
    };
    long onvif = port("onvif", cfg_.onvifPort), web = port("web", cfg_.webPort), rtsp = port("rtsp", cfg_.rtspPort);
    std::string name = queryParam(form, "name"), protect = queryParam(form, "protect"), password = percentDecode(queryParam(form, "password"));
    bool nameOk = name.size() <= 63 && (name.empty() || (name.front() != '-' && name.back() != '-'));
    for (char ch : name) nameOk = nameOk && (isalnum((unsigned char)ch) || ch == '-');
    bool passwordOn = protect.empty() ? !cfg_.webPassword.empty() : protect == "on";
    if (onvif < 1 || onvif > 65535 || web < 1 || web > 65535 || rtsp < 1 || rtsp > 65535 || onvif == web || onvif == rtsp || web == rtsp) {
      c.out = httpResponse(400, json, jsonError("ports must be 1 to 65535 and all different"));
    } else if (!nameOk) {
      c.out = httpResponse(400, json, jsonError("the name may use letters, digits and hyphens, up to 63 characters, and cannot start or end with a hyphen"));
    } else if (password.size() > 63 || (passwordOn && password.empty() && cfg_.webPassword.empty())) {
      c.out = httpResponse(400, json, jsonError("enter a password of up to 63 characters"));
    } else {
      CamSettings s;
      s.onvifPort = (uint16_t)onvif;
      s.webPort = (uint16_t)web;
      s.rtspPort = (uint16_t)rtsp;
      s.name = name;
      s.passwordOn = passwordOn;
      s.password = password;
      c.out = cfg_.settingsSave && cfg_.settingsSave(s) ? httpResponse(200, json, JSON_OK) : httpResponse(507, json, jsonError("could not save the settings"));
    }
    return;
  }
  if (method == "GET" && route == "/api/info") {
    int streams = 0;
    for (RtspConn& r : rtsp_) streams += r.playing;
    c.out = httpResponse(200, json, "{\"ip\":\"" + localIp_() + "\",\"name\":\"" + cfg_.hostName + "\",\"onvifPort\":" + std::to_string(cfg_.onvifPort) +
                                        ",\"webPort\":" + std::to_string(cfg_.webPort) + ",\"rtspPort\":" + std::to_string(cfg_.rtspPort) +
                                        ",\"streams\":" + std::to_string(streams) + ",\"subscribers\":" + std::to_string(subs_.count()) +
                                        ",\"serial\":\"" + cfg_.serial + "\",\"password\":" + (cfg_.webPassword.empty() ? "false" : "true") + "}\n");
    return;
  }
  bool fileRoute = route == "/api/file", mkdirRoute = route == "/api/mkdir";
  if (!fileRoute && !mkdirRoute) {
    c.out = httpResponse(404, json, jsonError("not found"));
    return;
  }
  std::string path = queryParam(target, "path");
  CamPathKind kind = camClassifyPath(path);
  bool isFile = kind == CAM_PATH_FILE || kind == CAM_PATH_CLEAR;
  bool isText = kind == CAM_PATH_TEXT;
  if (kind == CAM_PATH_INVALID) {
    c.out = httpResponse(400, json, jsonError("invalid path"));
    return;
  }
  if (mkdirRoute && method == "POST" && !isFile && !isText) {
    bool ok = storage_.makeDirs(path);
    rescan();
    c.out = ok ? httpResponse(200, json, JSON_OK) : httpResponse(507, json, jsonError("could not create the folder"));
  } else if (fileRoute && method == "GET" && (isFile || isText)) {
    size_t size = storage_.fileSize(path);
    if (size == 0) {
      c.out = httpResponse(404, json, jsonError("no such file"));
    } else if (cfg_.canAllocate && !cfg_.canAllocate(size)) {
      c.out = httpResponse(507, json, jsonError("the hub is short of memory; try again"));
    } else if (!storage_.readFile(path, c.ownBody, CAM_MAX_FRAME)) {
      c.out = httpResponse(404, json, jsonError("no such file"));
    } else {
      c.staticBody = c.ownBody.data();
      c.staticLen = c.ownBody.size();
      c.out = std::string("HTTP/1.1 200 OK\r\nContent-Type: ") + (isText ? json : "video/h264") + "\r\nContent-Length: " + std::to_string(c.staticLen) + "\r\nConnection: close\r\n\r\n";
    }
  } else if (fileRoute && method == "PUT" && isText) {
    if (bodyLen > 1024) c.out = httpResponse(413, json, jsonError("text is too long"));
    else c.out = storage_.writeFile(path, body, bodyLen) ? httpResponse(200, json, JSON_OK) : httpResponse(507, json, jsonError("could not save the text; storage may be full"));
  } else if (fileRoute && method == "PUT" && isFile) {
    CamNal nals[3];
    if (!camParseFrame((const uint8_t*)body, bodyLen, nals)) {
      c.out = httpResponse(422, json, jsonError("not a single H.264 picture with SPS, PPS and IDR"));
    } else if (!camCompatibleSps(nals[0], CAM_FRAMES[0].nals[0])) {
      c.out = httpResponse(422, json, jsonError("wrong video format: make it with tools/make_frames.py"));
    } else {
      // Stored with the hub's own SPS, so the NVR sees one unchanging format whichever encoder made the picture.
      const CamNal parts[3] = {CAM_FRAMES[0].nals[0], nals[1], nals[2]};
      if (parts[0].len + parts[1].len + parts[2].len + 12 > CAM_MAX_FRAME) {
        c.out = httpResponse(413, json, jsonError("file is larger than 32 KB"));
        return;
      }
      if (storage_.writeNals(path, parts, 3)) {
        rescan();
        c.out = httpResponse(200, json, JSON_OK);
      } else {
        c.out = httpResponse(507, json, jsonError("could not save the file; storage may be full"));
      }
    }
  } else if (fileRoute && method == "DELETE") {
    bool ok = storage_.exists(path) && storage_.removeTree(path);
    rescan();
    c.out = ok ? httpResponse(200, json, JSON_OK) : httpResponse(404, json, jsonError("nothing to delete at that path"));
  } else {
    c.out = httpResponse(400, json, jsonError("unsupported request"));
  }
}

bool SensorCamServer::handleHttp(HttpConn& c, uint32_t now) {
  size_t headEnd = c.in.find("\r\n\r\n");
  if (headEnd == std::string::npos) return false;
  std::string headers = c.in.substr(0, headEnd);
  std::string target = headers.substr(0, headers.find("\r\n"));
  size_t sp = target.find(' ');
  size_t sp2 = target.find(' ', sp + 1);
  std::string method = target.substr(0, sp);
  target = target.substr(sp + 1, sp2 - sp - 1);
  bool isGet = method == "GET";
  bool isApi = target.compare(0, 5, "/api/") == 0 || target == "/" || target == "/files" || target == "/setup";

  size_t contentLength = (size_t)strtoul(headerValue(headers, "content-length").c_str(), nullptr, 10);
  // A browser reports a connection closed mid-upload as a bare network error, so read the rest of the body before replying.
  auto refuse = [&](int code, const char* message) {
    c.out = httpResponse(code, "application/json", jsonError(message));
    c.answered = true;
    size_t got = c.in.size() - (headEnd + 4);
    c.discardLeft = contentLength > got && contentLength <= 128 * 1024 ? contentLength - got : 0;
    return true;
  };
  bool isOnvif = !isApi && !isGet;
  if (isOnvif == c.web) return refuse(404, "not available on this port");
  // Phones and laptops probe a known site on joining a network; sending them here is what opens the setup page by itself.
  if (c.web && isGet && cfg_.setupActive && cfg_.setupActive() && headerValue(headers, "host") != cfg_.setupAddress) {
    c.out = "HTTP/1.1 302 Found\r\nLocation: http://" + cfg_.setupAddress + "/setup\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    c.answered = true;
    return true;
  }
  if (contentLength > (isApi ? CAM_MAX_FRAME : MAX_REQUEST)) {
    if (!isApi) {
      closeHttp(c);
      return false;
    }
    return refuse(413, "file is larger than 32 KB");
  }

  // A picture upload borrows the one picture buffer, so it never needs a second block that size.
  bool filePut = method == "PUT" && target.compare(0, 15, "/api/file?path=") == 0;
  CamPathKind kind = filePut ? camClassifyPath(target.substr(15)) : CAM_PATH_INVALID;
  if (filePut && kind == CAM_PATH_INVALID) return refuse(400, "invalid path");
  if ((kind == CAM_PATH_FILE || kind == CAM_PATH_CLEAR) && !c.toFrameBuffer) {
    if (uploading_) return refuse(507, "the hub is busy with another upload; try again");
    uploading_ = &c;
    c.toFrameBuffer = true;
    c.bodyWanted = contentLength;
    frameData_.assign(c.in, headEnd + 4, std::string::npos);
    c.in.resize(headEnd + 4);
  }
  if (c.toFrameBuffer) {
    if (frameData_.size() < contentLength) return false;
  } else {
    // Room for the body is claimed once at its exact size, because string growth would briefly need about twice that.
    size_t whole = headEnd + 4 + contentLength;
    if (c.in.capacity() < whole) {
      if (cfg_.canAllocate && !cfg_.canAllocate(whole)) return refuse(507, "the hub is short of memory; try again");
      c.in.reserve(whole);
    }
    if (c.in.size() < whole) return false;
  }

  subs_.heard(c.peer, now);

  int status = 200;
  if (isApi) {
    handleApi(c, method, target, headers, c.toFrameBuffer ? frameData_.data() : c.in.data() + headEnd + 4, contentLength);
    if (c.toFrameBuffer) {
      uploading_ = nullptr;
      c.toFrameBuffer = false;
      frameKey_.clear();
    }
  } else if (isGet && target.compare(0, 7, "/sensor") == 0) {
    std::string body = handleSensor(target, status);
    c.out = httpResponse(status, "application/json", body);
  } else if (isGet && target.compare(0, 7, "/status") == 0) {
    c.out = httpResponse(200, "application/json", statusJson(now));
  } else if (isGet && target.compare(0, 7, "/events") == 0) {
    c.out = httpResponse(200, "application/json", eventsJson());
  } else if (isGet) {
    c.out = httpResponse(404, "text/plain", "not found\n");
  } else {
    std::string body = handleOnvif(c, headers, c.in.substr(headEnd + 4, contentLength), now, status);
    c.out = httpResponse(status, "application/soap+xml; charset=utf-8", body);
  }
  c.answered = true;
  return true;
}

void SensorCamServer::serviceHttp(HttpConn& c, bool readable, bool writable, uint32_t now) {
  if (readable && (!c.answered || c.discardLeft)) {
    char buf[1024];
    int n = (int)recv(c.fd, buf, sizeof(buf), 0);
    if (n == 0 || (n < 0 && !netWouldBlock())) {
      closeHttp(c);
      return;
    }
    if (n > 0 && c.answered) {
      c.discardLeft -= (size_t)n < c.discardLeft ? (size_t)n : c.discardLeft;
    } else if (n > 0) {
      if (c.toFrameBuffer) {
        size_t room = c.bodyWanted > frameData_.size() ? c.bodyWanted - frameData_.size() : 0;
        frameData_.append(buf, (size_t)n < room ? (size_t)n : room);
      } else {
        c.in.append(buf, n);
      }
      if (c.in.size() > MAX_UPLOAD + 2048) {
        closeHttp(c);
        return;
      }
      handleHttp(c, now);
      if (c.fd < 0) return;
    }
  }
  if (c.answered && !c.discardLeft && (writable || c.outPos == 0)) {
    bool inHeader = c.outPos < c.out.size();
    const char* data = inHeader ? c.out.data() + c.outPos : c.staticBody + (c.outPos - c.out.size());
    size_t left = inHeader ? c.out.size() - c.outPos : c.staticLen - (c.outPos - c.out.size());
    int n = left ? (int)send(c.fd, data, (int)left, 0) : 0;
    if (n < 0 && !netWouldBlock()) {
      closeHttp(c);
      return;
    }
    if (n > 0) c.outPos += n;
    if (c.outPos >= c.out.size() + c.staticLen) {
      closeHttp(c);
      return;
    }
  }
  if (now - c.openedAt > HTTP_TIMEOUT_MS) closeHttp(c);
}

// ---- RTSP ----

void SensorCamServer::flushRtsp(RtspConn& c, uint32_t now) {
  if (c.outPos >= c.out.size()) return;
  int n = (int)send(c.fd, c.out.data() + c.outPos, (int)(c.out.size() - c.outPos), 0);
  if (n < 0) {
    if (!netWouldBlock()) closeRtsp(c);
    return;
  }
  c.outPos += n;
  if (n > 0) c.activityAt = now;
  if (c.outPos >= c.out.size()) {
    c.out.clear();
    c.outPos = 0;
    if (c.closeAfterSend) closeRtsp(c);
  }
}

void SensorCamServer::handleRtspRequest(RtspConn& c, const std::string& text, uint32_t now) {
  std::string line = text.substr(0, text.find("\r\n"));
  std::string method = line.substr(0, line.find(' '));
  size_t a = line.find(' '), b = line.rfind(' ');
  std::string uri = a != std::string::npos && b > a ? line.substr(a + 1, b - a - 1) : "";
  std::string cseq = headerValue(text, "cseq");
  static const char SESSION[] = "53454E53";
  std::string head = "RTSP/1.0 200 OK\r\nCSeq: " + cseq + "\r\nServer: SensorCam\r\n";
  subs_.heard(c.peer, now);

  if (method == "OPTIONS") {
    c.out += head + "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n\r\n";
  } else if (method == "DESCRIBE") {
    std::string sdp = cameraSdp();
    c.out += head + "Content-Base: " + uri + "/\r\nContent-Type: application/sdp\r\nContent-Length: " + std::to_string(sdp.size()) + "\r\n\r\n" + sdp;
  } else if (method == "SETUP") {
    std::string t = headerValue(text, "transport");
    size_t at = t.find("interleaved=");
    if (t.find("TCP") == std::string::npos && t.find("tcp") == std::string::npos) {
      // TCP only: clients fall back to interleaved on 461, and the NVR uses TCP anyway.
      c.out += "RTSP/1.0 461 Unsupported Transport\r\nCSeq: " + cseq + "\r\n\r\n";
    } else {
      c.channel = at == std::string::npos ? 0 : atoi(t.c_str() + at + 12);
      c.out += head + "Transport: RTP/AVP/TCP;unicast;interleaved=" + std::to_string(c.channel) + "-" + std::to_string(c.channel + 1) +
               "\r\nSession: " + SESSION + ";timeout=60\r\n\r\n";
    }
  } else if (method == "PLAY") {
    // One buffer reserved at boot for the life of the stream, sized for the largest picture, so nothing allocates here.
    for (std::string& spare : spareOut_) {
      if (c.out.capacity() >= STREAM_BUFFER) break;
      if (spare.capacity() < STREAM_BUFFER) continue;
      spare.assign(c.out);
      c.out.swap(spare);
    }
    if (c.out.capacity() < STREAM_BUFFER) {
      c.out += "RTSP/1.0 503 Service Unavailable\r\nCSeq: " + cseq + "\r\n\r\n";
      c.closeAfterSend = true;
    } else {
      c.out += head + "Session: " + SESSION + "\r\nRange: npt=0.000-\r\n\r\n";
      c.playing = true;
    }
  } else if (method == "TEARDOWN") {
    c.out += head + "Session: " + SESSION + "\r\n\r\n";
    c.closeAfterSend = true;
    c.playing = false;
  } else {
    c.out += head + "Session: " + SESSION + "\r\n\r\n";
  }
}

void SensorCamServer::serviceRtsp(RtspConn& c, bool readable, bool writable, uint32_t now) {
  if (readable) {
    char buf[1024];
    int n = (int)recv(c.fd, buf, sizeof(buf), 0);
    if (n == 0 || (n < 0 && !netWouldBlock())) {
      closeRtsp(c);
      return;
    }
    if (n > 0) {
      c.activityAt = now;
      c.in.append(buf, n);
      while (!c.in.empty()) {
        if (c.in[0] == '$') {  // interleaved RTCP from the client; ignored
          if (c.in.size() < 4) break;
          size_t size = ((uint8_t)c.in[2] << 8) | (uint8_t)c.in[3];
          if (c.in.size() < 4 + size) break;
          c.in.erase(0, 4 + size);
          continue;
        }
        size_t end = c.in.find("\r\n\r\n");
        if (end == std::string::npos) break;
        handleRtspRequest(c, c.in.substr(0, end + 2), now);
        c.in.erase(0, end + 4);
      }
      if (c.in.size() > MAX_REQUEST) {
        closeRtsp(c);
        return;
      }
    }
  }
  if (c.fd >= 0 && (writable || !c.out.empty())) flushRtsp(c, now);
  if (c.fd >= 0 && (now - c.activityAt > (c.playing ? RTSP_STALL_MS : RTSP_IDLE_MS))) closeRtsp(c);
}

void SensorCamServer::pushFrames(uint32_t now) {
  if ((int32_t)(now - nextFrameAt_) < 0) return;
  nextFrameAt_ = now + 1000 / CAM_FPS;
  const CamFrame& frame = frameToShow(now);
  if (cfg_.canAllocate && !cfg_.canAllocate(0)) return;  // every frame stands alone, so skipping some lets the network stack drain
  for (RtspConn& c : rtsp_) {
    // Every frame is independently decodable, so a slow client just skips one.
    if (c.fd < 0 || !c.playing || !c.out.empty()) continue;
    rtpAppendFrame(frame, c.rtp, c.channel, CAM_FPS, c.out);
    flushRtsp(c, now);
  }
}

// ---- ONVIF event push ----

void SensorCamServer::endNotify(NotifyIo& n) {
  if (n.fd >= 0) netClose(n.fd);
  n = NotifyIo();
}

void SensorCamServer::startNotify(int i, uint32_t now) {
  CamSub& sub = subs_.at(i);
  NotifyIo& n = notify_[i];
  sub.pending = false;
  HttpUrl url;
  if (!parseHttpUrl(sub.consumer, url)) return;
  int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;
  netNonblock(fd);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(url.port);
  to.sin_addr.s_addr = inet_addr(url.host.c_str());
  if (connect(fd, (sockaddr*)&to, sizeof(to)) != 0 && !netWouldBlock()) {
    netClose(fd);
    return;
  }
  bool active = sensors_.anyActive();
  std::string body = soapNotify(active, !sub.initialized, time(nullptr));
  sub.initialized = true;
  std::string action = "http://docs.oasis-open.org/wsn/bw-2/NotificationConsumer/Notify";
  n.out = "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
          "\r\nContent-Type: application/soap+xml; charset=utf-8; action=\"" + action + "\"\r\nContent-Length: " + std::to_string(body.size()) +
          "\r\nConnection: close\r\n\r\n" + body;
  n.fd = fd;
  n.outPos = 0;
  n.state = NotifyIo::CONNECTING;
  n.deadline = now + NOTIFY_SEND_MS;
}

void SensorCamServer::serviceNotify(NotifyIo& n, bool readable, bool writable, uint32_t now) {
  if (n.state == NotifyIo::CONNECTING && writable) {
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(n.fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (err) {
      endNotify(n);
      return;
    }
    n.state = NotifyIo::SENDING;
  }
  if (n.state == NotifyIo::SENDING && writable) {
    int sent = (int)send(n.fd, n.out.data() + n.outPos, (int)(n.out.size() - n.outPos), 0);
    if (sent < 0 && !netWouldBlock()) {
      endNotify(n);
      return;
    }
    if (sent > 0) n.outPos += sent;
    if (n.outPos >= n.out.size()) {
      // The NVR closes without an HTTP reply, so this only waits for that close.
      n.state = NotifyIo::WAIT_CLOSE;
      n.deadline = now + NOTIFY_CLOSE_MS;
    }
  }
  if (n.state == NotifyIo::WAIT_CLOSE && readable) {
    char buf[256];
    int got = (int)recv(n.fd, buf, sizeof(buf), 0);
    if (got >= 0 || !netWouldBlock()) {
      endNotify(n);
      return;
    }
  }
  if ((int32_t)(now - n.deadline) >= 0) endNotify(n);
}

// ---- WS-Discovery ----

void SensorCamServer::serviceDiscovery() {
  char buf[2048];
  sockaddr_in from{};
  socklen_t len = sizeof(from);
  int n = (int)recvfrom(udp_, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &len);
  if (n <= 0) return;
  std::string msg(buf, n);
  if (soapAction(msg) != "Probe") return;
  std::string types = xmlFirstText(msg, "Types");
  if (!types.empty() && types.find("NetworkVideoTransmitter") == std::string::npos && types.find("Device") == std::string::npos) return;
  std::string reply = discoveryProbeMatch(xmlFirstText(msg, "MessageID"), localIp_(), cfg_.onvifPort, cfg_.serial, messageNo_++);
  sendto(udp_, reply.data(), (int)reply.size(), 0, (sockaddr*)&from, len);
}

// ---- main loop ----

void SensorCamServer::poll(int maxWaitMs) {
  uint32_t now = millis_();

  sensors_.tick(now);
  serviceDelayed(now);
  // Latched, so a long-dead sensor stays silent when the millisecond counter wraps.
  for (Health& h : health_)
    if (h.heard && now - h.heardAt > SILENT_AFTER_MS) h.silent = true;
  subs_.expire(now);
  if (signal_.update(sensors_.anyActive(), now)) subs_.markAllPending();
  for (int i = 0; i < SubTable::SIZE; i++) {
    CamSub& s = subs_.at(i);
    if (s.used && s.push && s.pending && notify_[i].state == NotifyIo::IDLE) startNotify(i, now);
  }
  pushFrames(now);

  fd_set rd, wr;
  FD_ZERO(&rd);
  FD_ZERO(&wr);
  int maxFd = -1;
  auto add = [&](int fd, fd_set& set) {
    FD_SET(fd, &set);
    if (fd > maxFd) maxFd = fd;
  };

  if (httpListen_ >= 0) add(httpListen_, rd);
  if (webListen_ >= 0) add(webListen_, rd);
  if (captiveListen_ >= 0) add(captiveListen_, rd);
  if (rtspListen_ >= 0) add(rtspListen_, rd);
  if (udp_ >= 0) add(udp_, rd);
  for (HttpConn& c : http_) {
    if (c.fd < 0) continue;
    add(c.fd, rd);
    if (c.answered && !c.discardLeft) add(c.fd, wr);
  }
  for (RtspConn& c : rtsp_) {
    if (c.fd < 0) continue;
    add(c.fd, rd);
    if (!c.out.empty()) add(c.fd, wr);
  }
  for (NotifyIo& n : notify_) {
    if (n.state == NotifyIo::IDLE) continue;
    if (n.state == NotifyIo::WAIT_CLOSE) add(n.fd, rd);
    else add(n.fd, wr);
  }

  int32_t untilFrame = (int32_t)(nextFrameAt_ - now);
  int wait = untilFrame < maxWaitMs ? (untilFrame < 0 ? 0 : untilFrame) : maxWaitMs;
  timeval tv{wait / 1000, (wait % 1000) * 1000};
  if (maxFd >= 0 && select(maxFd + 1, &rd, &wr, nullptr, &tv) < 0) {
    waitErrors_++;
    lastWaitErrno_ = errno;
    return;
  }
  now = millis_();
  lastGoodWait_ = now;

  if (httpListen_ >= 0 && FD_ISSET(httpListen_, &rd)) acceptHttp(httpListen_, now);
  if (webListen_ >= 0 && FD_ISSET(webListen_, &rd)) acceptHttp(webListen_, now);
  if (captiveListen_ >= 0 && FD_ISSET(captiveListen_, &rd)) acceptHttp(captiveListen_, now);
  if (rtspListen_ >= 0 && FD_ISSET(rtspListen_, &rd)) acceptRtsp(now);
  if (udp_ >= 0 && FD_ISSET(udp_, &rd)) serviceDiscovery();
  for (HttpConn& c : http_)
    if (c.fd >= 0) serviceHttp(c, FD_ISSET(c.fd, &rd), FD_ISSET(c.fd, &wr), now);
  for (RtspConn& c : rtsp_)
    if (c.fd >= 0) serviceRtsp(c, FD_ISSET(c.fd, &rd), FD_ISSET(c.fd, &wr), now);
  for (NotifyIo& n : notify_)
    if (n.state != NotifyIo::IDLE) serviceNotify(n, FD_ISSET(n.fd, &rd), FD_ISSET(n.fd, &wr), now);
}
