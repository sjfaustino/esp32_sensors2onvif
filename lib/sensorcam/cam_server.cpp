#include "cam_server.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_server_internal.h"

SensorCamServer::SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn)
    : cfg_(cfg), localIp_(localIp), millis_(millisFn), sensors_(CAM_MAX_SENSORS, cfg.holdMs), signal_(REPEAT_TRUE_MS),
      storage_(cfg.fsRoot), slots_(CAM_MAX_SENSORS), health_(CAM_MAX_SENSORS) {
  csvRead_ = [this](size_t offset, char* out, size_t len) {  // the older file, then the current one
    if (offset < csvOldBytes_) {
      size_t n = len < csvOldBytes_ - offset ? len : csvOldBytes_ - offset;
      if (!storage_.readAt(EVENTS_CSV_OLD, offset, out, n)) return false;
      offset += n;
      out += n;
      len -= n;
    }
    return !len || storage_.readAt(EVENTS_CSV, offset - csvOldBytes_, out, len);
  };
}

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
  onvifListen_ = listenOn(cfg_.onvifPort);
  webListen_ = listenOn(cfg_.webPort);
  if (cfg_.captivePort && cfg_.captivePort != cfg_.webPort && cfg_.captivePort != cfg_.onvifPort && cfg_.captivePort != cfg_.rtspPort) captiveListen_ = listenOn(cfg_.captivePort);
  rtspListen_ = listenOn(cfg_.rtspPort);
  openDiscovery();
  nextFrameAt_ = millis_();
  lastGoodWait_ = nextFrameAt_;
  frameData_.reserve(CAM_MAX_FRAME);
  for (std::string& spare : spareOut_) spare.reserve(STREAM_BUFFER);
  rescan();
  loadEvents();
  std::string alive;
  if (storage_.readFile(ALIVE_FILE, alive, 16) && alive.size() == sizeof(int64_t)) {
    int64_t t;
    memcpy(&t, alive.data(), sizeof(t));
    lastAlive_ = (time_t)t;
  }
  beganAt_ = nvrSeenAt_ = millis_();
  return onvifListen_ >= 0 && webListen_ >= 0 && rtspListen_ >= 0;
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
  networkSeen_ = true;
  openDiscovery();
  if (cfg_.loopbackOnly || udp_ < 0) return;
  std::string hello = discoveryHello(localIp_(), cfg_.onvifPort, cfg_.serial, messageNo_++);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(MULTICAST_PORT);
  to.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
  sendto(udp_, hello.data(), (int)hello.size(), 0, (sockaddr*)&to, sizeof(to));
}

std::string* SensorCamServer::borrowFileOut(HttpConn& c) {
  if (fileOutOwner_) return fileOutOwner_ == &c ? fileOut_ : nullptr;
  for (std::string& spare : spareOut_) {
    if (spare.capacity() < STREAM_BUFFER) continue;
    fileOut_ = &spare;
    fileOutOwner_ = &c;
    return fileOut_;
  }
  return nullptr;  // every stream buffer is carrying video
}

void SensorCamServer::closeHttp(HttpConn& c) {
  if (c.firmwareActive) {  // the browser went away partway through, so the update is abandoned
    cfg_.firmwareEnd(false);
    firmwareOwner_ = nullptr;
  }
  if (fileOutOwner_ == &c) {
    fileOutOwner_ = nullptr;
    fileOut_ = nullptr;
  }
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

int SensorCamServer::sharedSockets() const {
  int open = 0;
  for (const HttpConn& c : http_) open += c.fd >= 0;
  for (const NotifyIo& n : notify_) open += n.fd >= 0;
  return open + (hook_.fd >= 0);
}

void SensorCamServer::acceptHttp(int listenFd, uint32_t now) {
  sockaddr_in a{};
  socklen_t len = sizeof(a);
  int fd = (int)accept(listenFd, (sockaddr*)&a, &len);
  if (fd < 0) return;
  if (listenFd == captiveListen_ && !cfg_.setupActive()) {
    netClose(fd);
    return;
  }
  if (!hasHeadroom() || sharedSockets() >= MAX_HTTP) {  // short of RAM or sockets: the client will retry once existing traffic has drained
    refused_++;
    netClose(fd);
    return;
  }
  accepted_++;
  for (HttpConn& c : http_) {
    if (c.fd >= 0) continue;
    netNonblock(fd);
    c.fd = fd;
    c.web = listenFd != onvifListen_;
    c.peer = a.sin_addr.s_addr;
    c.openedAt = now;
    return;
  }
  netClose(fd);
}

void SensorCamServer::acceptRtsp(uint32_t now) {
  sockaddr_in a{};
  socklen_t len = sizeof(a);
  int fd = (int)accept(rtspListen_, (sockaddr*)&a, &len);
  if (fd < 0) return;
  if (!hasHeadroom()) {
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

// ---- HTTP: ONVIF SOAP on its own port; the web pages and sensor input on another ----

// Replies with data that stays valid until the connection closes, without copying it.
void SensorCamServer::sendBody(HttpConn& c, const char* type, const char* data, size_t len, const std::string& extraHeaders) {
  c.staticBody = data;
  c.staticLen = len;
  c.out = httpHead(200, type, len, extraHeaders);
}

int SensorCamServer::streamCount() const {
  int streams = 0;
  for (const RtspConn& c : rtsp_) streams += c.playing;
  return streams;
}

// How long a PullMessages may wait, from an xs:duration such as PT1M. Zero when it asks not to wait.
static uint32_t pollWaitMs(const std::string& timeout) {
  if (timeout.compare(0, 2, "PT") != 0) return 0;
  uint32_t ms = 0, number = 0;
  for (size_t i = 2; i < timeout.size(); i++) {
    char ch = timeout[i];
    if (ch >= '0' && ch <= '9') {
      number = number * 10 + (uint32_t)(ch - '0');
    } else {
      if (ch == 'H') ms += number * 3600000;
      if (ch == 'M') ms += number * 60000;
      if (ch == 'S') ms += number * 1000;
      number = 0;
    }
  }
  return ms < MAX_POLL_MS ? ms : MAX_POLL_MS;
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
  ctx.onvifPort = cfg_.onvifPort;
  if (cfg_.onvifNetwork) cfg_.onvifNetwork(ctx);
  ctx.now = time(nullptr);
  if (!cfg_.onvifPassword.empty()) {
    ctx.snapshotToken = camSnapshotToken(cfg_.onvifPassword);
    // ONVIF allows these before logging in. A client needs the clock to build its login.
    static const char* const OPEN[] = {"GetSystemDateAndTime", "GetCapabilities", "GetServices", "GetServiceCapabilities", "GetHostname", "GetEndpointReference", "GetWsdlUrl"};
    bool open = false;
    for (const char* name : OPEN) open = open || action == name;
    if (!open && !camWsseValid(body, HUB_USER, cfg_.onvifPassword)) {
      status = 400;
      return soapNotAuthorized();
    }
  }

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
    uint32_t wait = pollWaitMs(xmlFirstText(body, "Timeout"));
    int waiting = 0;
    for (const HttpConn& other : http_) waiting += other.fd >= 0 && other.pollWaiting;
    if (sub && !changed && wait && waiting < MAX_WAITING_POLLS) {  // answered by servicePolls when something changes
      c.pollWaiting = true;
      c.pollSub = subIdFromPath(path);
      c.pollUntil = now + wait;
      c.pollHost = ctx.host;
      status = -1;
      return "";
    }
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
    bool safe = !action.empty() && action.size() <= 48;
    for (char ch : action) safe = safe && isalnum((unsigned char)ch);
    for (std::string& seen : unsupported_) {
      if (!safe || seen == action) break;
      if (seen.empty()) {
        seen = action;
        break;
      }
    }
    status = 400;
    return soapFault("Action not supported");
  }
  status = 200;
  return soapEnvelope(reply);
}

// The JPEG of the picture being sent, or the built-in one for the same state when that picture has none.
void SensorCamServer::serveSnapshot(HttpConn& c) {
  if (!frame_) frameToShow(millis_());
  if (!frameFile_.empty()) {
    std::string path = camPreviewPath(frameFile_);
    std::string* out = storage_.fileSize(path) ? borrowFileOut(c) : nullptr;
    if (out && storage_.readFile(path, *out, CAM_MAX_FRAME)) {
      sendBody(c, "image/jpeg", out->data(), out->size());
      return;
    }
  }
  int id = 0;
  uint8_t direction = 0;
  bool delayed = false;
  bool on = sensors_.shown(millis_(), id, direction, delayed);
  const CamJpeg& builtin = CAM_SNAPSHOTS[builtinFrame(on, delayed)];
  sendBody(c, "image/jpeg", (const char*)builtin.data, builtin.len);
}

void SensorCamServer::servicePolls(uint32_t now) {
  int state = sensors_.anyActive() ? 1 : 0;
  for (HttpConn& c : http_) {
    if (c.fd < 0 || !c.pollWaiting) continue;
    CamSub* sub = subs_.find(c.pollSub);
    bool changed = sub && sub->lastPull != state;
    if (sub && !changed && (int32_t)(now - c.pollUntil) < 0) continue;
    bool initial = sub && sub->lastPull < 0;
    if (sub) sub->lastPull = state;
    SoapContext ctx;
    ctx.host = c.pollHost;
    ctx.now = time(nullptr);
    c.out = httpResponse(200, "application/soap+xml; charset=utf-8", soapEnvelope(soapPullMessagesReply(ctx, changed, state == 1, initial)));
    c.pollWaiting = false;
    c.answered = true;
    c.openedAt = now;  // the time limit now covers sending this reply, not the wait before it
  }
}

// ---- sensor table and pictures, both driven by the folders on storage ----

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
    if (!frameKey_.empty()) liveSeq_++;
    frameKey_.clear();
    frameFile_.clear();
    frame_ = &CAM_FRAMES[builtinFrame(on, delayed)];
    return *frame_;
  }
  std::string key = on ? slotName(id) + "/" + std::to_string(direction) + (delayed ? "d" : "") : "clear";
  if (key == frameKey_) return *frame_;
  frameKey_ = key;
  frameFile_.clear();
  liveSeq_++;

  std::vector<std::string> candidates;
  if (on && direction == CAM_DIRECTION_SEVERAL) {
    // Stands for several sensors' past events, so no one sensor's picture fits: the built-in one says to check the logs.
  } else if (on) {
    // A past event never borrows the live picture, which would make it look like it is happening now.
    std::string base = slotName(id) + "/";
    candidates = {base + camStateFile(direction, delayed), base + camStateFile(0, delayed)};
  } else {
    candidates = {"clear.h264"};
  }
  frame_ = &CAM_FRAMES[builtinFrame(on, delayed)];
  for (const std::string& path : candidates) {
    if (loadFrame(path)) {
      frame_ = &loadedFrame_;
      frameFile_ = path;
      break;
    }
  }
  return *frame_;
}

// ---- file manager ----

bool SensorCamServer::authorized(const std::string& headers) const {
  return cfg_.webPassword.empty() || camBasicValid(headerValue(headers, "authorization"), HUB_USER, cfg_.webPassword);
}

void SensorCamServer::finishFirmware(HttpConn& c, bool ok) {
  bool done = ok && cfg_.firmwareEnd(true);
  if (!ok) cfg_.firmwareEnd(false);
  firmwareOwner_ = nullptr;
  c.firmwareActive = false;
  c.discardLeft = c.firmwareLeft;
  c.firmwareLeft = 0;
  c.out = done ? jsonOk(JSON_OK) : jsonFail(400, "that is not valid firmware for this hub, so nothing was changed");
  c.answered = true;
}

void SensorCamServer::feedFirmware(HttpConn& c, const char* data, size_t n, uint32_t now) {
  size_t take = n < c.firmwareLeft ? n : c.firmwareLeft;
  bool written = !take || cfg_.firmwareWrite((const uint8_t*)data, take);
  c.firmwareLeft -= take;  // before finishing, so only the bytes still on their way are read and dropped
  if (!written) {
    finishFirmware(c, false);
    return;
  }
  c.openedAt = now;  // the time limit is for a stalled upload, not a long one. A later clock reading here would make the elapsed time wrap.
  if (!c.firmwareLeft) finishFirmware(c, true);
}

bool SensorCamServer::handleHttp(HttpConn& c, uint32_t now) {
  if (c.pollWaiting) return false;
  size_t headEnd = c.in.find("\r\n\r\n");
  if ((headEnd == std::string::npos ? c.in.size() : headEnd) > MAX_HEADERS) {
    closeHttp(c);
    return false;
  }
  if (headEnd == std::string::npos) return false;
  std::string headers = c.in.substr(0, headEnd);
  std::string target = headers.substr(0, headers.find("\r\n"));
  size_t sp = target.find(' ');
  size_t sp2 = target.find(' ', sp + 1);
  std::string method = target.substr(0, sp);
  target = target.substr(sp + 1, sp2 - sp - 1);
  bool isGet = method == "GET";
  std::string route = target.substr(0, target.find('?'));
  bool isApi = route.compare(0, 5, "/api/") == 0 || route == "/" || route == "/files" || route == "/setup";

  size_t contentLength = (size_t)strtoul(headerValue(headers, "content-length").c_str(), nullptr, 10);
  // A browser reports a connection closed mid-upload as a bare network error, so read the rest of the body before replying.
  auto refuse = [&](int code, const char* message) {
    c.out = jsonFail(code, message);
    c.answered = true;
    size_t got = c.in.size() - (headEnd + 4);
    c.discardLeft = contentLength > got && contentLength <= 128 * 1024 ? contentLength - got : 0;
    return true;
  };
  // The same for a missing login, on the requests that are acted on before their body has arrived.
  auto needLogin = [&](size_t drainLimit) {
    c.out = UNAUTHORIZED;
    c.answered = true;
    size_t got = c.in.size() - (headEnd + 4);
    c.discardLeft = contentLength > got && contentLength <= drainLimit ? contentLength - got : 0;
    return true;
  };
  bool isSnapshot = isGet && route == "/onvif/snapshot";  // the one GET that ONVIF clients make
  bool isOnvif = isSnapshot || (!isApi && !isGet);
  if (isOnvif == c.web) return refuse(404, "not available on this port");
  // Phones and laptops probe a known site on joining a network; sending them here is what opens the setup page by itself.
  if (c.web && isGet && cfg_.setupActive() && headerValue(headers, "host") != cfg_.setupAddress) {
    c.out = "HTTP/1.1 302 Found\r\nLocation: http://" + cfg_.setupAddress + "/setup\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    c.answered = true;
    return true;
  }
  if (isApi && !isGet) {  // a browser names the site a request came from, so another site cannot use this browser's login
    std::string origin = headerValue(headers, "origin");
    if (!origin.empty() && origin != "http://" + headerValue(headers, "host")) return refuse(403, "request from another site refused");
  }
  if (method == "PUT" && target == "/api/firmware") {  // goes straight to flash, because it is far larger than memory
    if (!authorized(headers)) return needLogin(MAX_FIRMWARE);
    if (!contentLength || contentLength > MAX_FIRMWARE) return refuse(413, "that file is not the right size for firmware");
    if (firmwareOwner_ || !cfg_.firmwareBegin(contentLength)) return refuse(507, "could not start the update");
    firmwareOwner_ = &c;
    c.firmwareActive = true;
    c.firmwareLeft = contentLength;
    std::string first = c.in.substr(headEnd + 4);
    c.in.clear();
    feedFirmware(c, first.data(), first.size(), now);
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
  bool borrowsFrame = kind == CAM_PATH_FILE || kind == CAM_PATH_CLEAR || kind == CAM_PATH_PREVIEW;
  // Only pictures may be large, and they arrive in a buffer reserved at boot, so nothing here asks for a picture-sized block.
  if (isApi && !borrowsFrame && contentLength > (target == "/api/restore" ? 8192 : 2048)) return refuse(413, "request is too large");
  if (borrowsFrame && !c.toFrameBuffer) {
    if (!authorized(headers)) return needLogin(CAM_MAX_FRAME);  // before taking the picture buffer, which changes what the NVR sees
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
      if (!roomFor(whole)) return refuse(507, "the hub is short of memory; try again");
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
  } else if (isSnapshot) {
    if (!cfg_.onvifPassword.empty() && queryParam(target, "token") != camSnapshotToken(cfg_.onvifPassword)) c.out = httpResponse(403, "text/plain", "not authorized\n");
    else serveSnapshot(c);
  } else if (isGet && (target.compare(0, 7, "/status") == 0 || target.compare(0, 7, "/events") == 0) && !authorized(headers)) {
    c.out = UNAUTHORIZED;
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
    c.in.erase(0, headEnd + 4);  // in place, because a second copy of a large request could run the hub out of memory
    c.in.resize(contentLength);
    std::string body = handleOnvif(c, headers, c.in, now, status);
    if (status == -1) {  // held until something changes
      c.in.clear();
      return false;
    }
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
    } else if (n > 0 && c.firmwareActive) {
      feedFirmware(c, buf, (size_t)n, now);
    } else if (n > 0 && c.pollWaiting) {
      // a held request needs nothing more from the client
    } else if (n > 0) {
      if (c.toFrameBuffer) {
        size_t room = c.bodyWanted > frameData_.size() ? c.bodyWanted - frameData_.size() : 0;
        frameData_.append(buf, (size_t)n < room ? (size_t)n : room);
      } else {
        c.in.append(buf, n);
      }
      handleHttp(c, now);
      if (c.fd < 0) return;
    }
  }
  if (c.answered && !c.discardLeft && (writable || c.outPos == 0)) {
    bool inHeader = c.outPos < c.out.size();
    const char* data = inHeader ? c.out.data() + c.outPos : c.staticBody + (c.outPos - c.out.size());
    size_t left = inHeader ? c.out.size() - c.outPos : c.staticLen - (c.outPos - c.out.size());
    char piece[1024];
    if (!inHeader && c.pull && left) {  // read again from the same place if the last piece was only partly sent
      left = left < sizeof(piece) ? left : sizeof(piece);
      if (!(*c.pull)(c.outPos - c.out.size(), piece, left)) {
        closeHttp(c);
        return;
      }
      data = piece;
    }
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
  if (!c.pollWaiting && now - c.openedAt > HTTP_TIMEOUT_MS) closeHttp(c);
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

  if (!cfg_.onvifPassword.empty() && method != "OPTIONS" && !c.authed) {
    if (c.nonce.empty()) c.nonce = camMd5Hex(cfg_.serial + std::to_string(now) + std::to_string(c.fd));
    std::string auth = headerValue(text, "authorization");
    if (camDigestValid(auth, method, HUB_USER, cfg_.onvifPassword, RTSP_REALM, c.nonce) || camBasicValid(auth, HUB_USER, cfg_.onvifPassword)) {
      c.authed = true;
    } else {
      c.out += "RTSP/1.0 401 Unauthorized\r\nCSeq: " + cseq + "\r\nWWW-Authenticate: Digest realm=\"" + RTSP_REALM + "\", nonce=\"" + c.nonce + "\"\r\nWWW-Authenticate: Basic realm=\"" + RTSP_REALM + "\"\r\n\r\n";
      return;
    }
  }

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
      if (spare.capacity() < STREAM_BUFFER || &spare == fileOut_) continue;  // a lent buffer is back within moments
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
      if (c.in.size() > MAX_HEADERS) {
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
  if (!hasHeadroom()) return;  // every frame stands alone, so skipping some lets the network stack drain
  for (RtspConn& c : rtsp_) {
    // Every frame is independently decodable, so a slow client just skips one.
    if (c.fd < 0 || !c.playing || !c.out.empty()) continue;
    rtpAppendFrame(frame, c.rtp, c.channel, now, c.out);
    flushRtsp(c, now);
  }
}

// ---- ONVIF event push ----

void SensorCamServer::endNotify(NotifyIo& n, const char* result) {
  if (n.hook) {
    hookResult_ = result;
    hookResultAt_ = time(nullptr);
  } else if (&n >= notify_ && &n < notify_ + SubTable::SIZE && n.state != NotifyIo::IDLE) {
    // A failure before the message was sent means the NVR did not get it. The NVR closes without replying, so the
    // other endings are deliveries.
    std::string r = result;
    subs_.at((int)(&n - notify_)).pushFailed = r == "could not connect" || r == "the connection failed";
  }
  if (n.fd >= 0) netClose(n.fd);
  n = NotifyIo();
}

void SensorCamServer::startNotify(int i, uint32_t now) {
  CamSub& sub = subs_.at(i);
  NotifyIo& n = notify_[i];
  HttpUrl url;
  if (!parseHttpUrl(sub.consumer, url)) {
    sub.pending = false;
    return;
  }
  int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;  // still pending, so it is tried again on the next pass
  sub.pending = false;
  netNonblock(fd);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(url.port);
  to.sin_addr.s_addr = inet_addr(url.host.c_str());
  if (connect(fd, (sockaddr*)&to, sizeof(to)) != 0 && !netWouldBlock()) {
    netClose(fd);
    sub.pushFailed = true;
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
      endNotify(n, "could not connect");
      return;
    }
    n.state = NotifyIo::SENDING;
  }
  if (n.state == NotifyIo::SENDING && writable) {
    int sent = (int)send(n.fd, n.out.data() + n.outPos, (int)(n.out.size() - n.outPos), 0);
    if (sent < 0 && !netWouldBlock()) {
      endNotify(n, "the connection failed");
      return;
    }
    if (sent > 0) n.outPos += sent;
    if (n.outPos >= n.out.size()) {
      // The NVR closes without an HTTP reply, so this only waits for that close. A webhook replies.
      n.state = NotifyIo::WAIT_CLOSE;
      n.deadline = now + (n.hook ? HOOK_WAIT_MS : NOTIFY_CLOSE_MS);
    }
  }
  if (n.state == NotifyIo::WAIT_CLOSE && readable) {
    char buf[256];
    int got = (int)recv(n.fd, buf, sizeof(buf), 0);
    if (got >= 0 || !netWouldBlock()) {
      std::string result = got >= 12 && memcmp(buf, "HTTP/1.", 7) == 0 ? "HTTP " + std::string(buf + 9, 3) : std::string("closed without a reply");
      endNotify(n, result.c_str());
      return;
    }
  }
  if ((int32_t)(now - n.deadline) >= 0) endNotify(n, n.state == NotifyIo::WAIT_CLOSE ? "no reply" : "could not connect");
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
  for (int i = 0; i < CAM_MAX_SENSORS; i++) {
    Health& h = health_[i];
    if (!slots_[i].used || h.silent || now - (h.heard ? h.heardAt : h.watchedAt) <= cfg_.silentMs) continue;
    h.silent = true;
    healthAlert(i + 1, 1, now);
  }
  if (eventsDirty_ && now - eventsSavedAt_ >= EVENT_SAVE_MS) saveEvents();
  if (!startNoted_) noteHubStart(now);
  if (startNoted_ && (!aliveWritten_ || now - aliveAt_ >= ALIVE_EVERY_MS)) writeAlive(now);  // after the start is noted, which reads the old time
  if (cfg_.watchNvr) checkNvr(now);
  subs_.expire(now);
  servicePolls(now);
  if (signal_.update(sensors_.anyActive(), now)) subs_.markAllPending();
  for (int i = 0; i < SubTable::SIZE; i++) {
    CamSub& s = subs_.at(i);
    if (s.used && s.push && s.pending && notify_[i].state == NotifyIo::IDLE && sharedSockets() <= MAX_HTTP) startNotify(i, now);
  }
  if (hook_.state == NotifyIo::IDLE && hookCount_ && sharedSockets() < MAX_HTTP) startHook(now);  // leaves one for an event push
  pushFrames(now);

  fd_set rd, wr;
  FD_ZERO(&rd);
  FD_ZERO(&wr);
  int maxFd = -1;
  auto add = [&](int fd, fd_set& set) {
    FD_SET(fd, &set);
    if (fd > maxFd) maxFd = fd;
  };

  if (onvifListen_ >= 0) add(onvifListen_, rd);
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
  if (hook_.state != NotifyIo::IDLE) add(hook_.fd, hook_.state == NotifyIo::WAIT_CLOSE ? rd : wr);

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

  if (onvifListen_ >= 0 && FD_ISSET(onvifListen_, &rd)) acceptHttp(onvifListen_, now);
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
  if (hook_.state != NotifyIo::IDLE) serviceNotify(hook_, FD_ISSET(hook_.fd, &rd), FD_ISSET(hook_.fd, &wr), now);
}
