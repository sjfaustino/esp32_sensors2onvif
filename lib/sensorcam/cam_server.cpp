#include "cam_server.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <new>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_server_internal.h"

SensorCamServer::SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn)
    : cfg_(cfg), localIp_(localIp), millis_(millisFn), sensors_(CAM_MAX_SENSORS, cfg.holdMs), signal_(CAM_REPEAT_TRUE_MS),
      storage_(cfg.fsRoot), slots_(CAM_MAX_SENSORS), health_(CAM_MAX_SENSORS) {
  csvRead_ = [this](size_t offset, char* out, size_t len) { return log_.csvRead(offset, out, len); };  // see HttpConn::stream
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
  notGot_.reserve(CAM_MAX_SENSORS);
  rescan();
  log_.load();
  mqttEventsSent_ = log_.added();  // the run before sent those
  loadWaiting();
  std::string alive;
  if (storage_.readFile(ALIVE_FILE, alive, 16) && alive.size() == 8) {  // seconds since 1970, little-endian
    uint64_t t = 0;
    for (int i = 7; i >= 0; i--) t = t << 8 | (uint8_t)alive[i];
    lastAlive_ = (time_t)(int64_t)t;
  }
  beganAt_ = nvrSeenAt_ = millis_();
  subs_.protect(cfg_.nvrIp.empty() ? 0 : inet_addr(cfg_.nvrIp.c_str()));
  return onvifListen_ >= 0 && webListen_ >= 0 && rtspListen_ >= 0;
}

std::string SensorCamServer::debugLine() {
  int web = 0, video = 0, push = 0;
  for (const HttpConn& c : http_) web += c.fd >= 0;
  for (const RtspConn& c : rtsp_) video += c.fd >= 0;
  for (const NotifyIo& n : notify_) push += n.state != NotifyIo::IDLE;
  return "wait errors " + std::to_string(waitErrors_) + " (errno " + std::to_string(lastWaitErrno_) + "), accepted " + std::to_string(accepted_) +
         ", refused " + std::to_string(refused_) + ", open web " + std::to_string(web) + " video " + std::to_string(video) + " push " + std::to_string(push) +
         ", short of memory " + std::to_string(memoryFailures_);
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

// Read from storage a piece at a time as it is sent, because a picture-sized buffer per reply is more than RAM allows.
bool SensorCamServer::sendFile(HttpConn& c, const std::string& path, const char* type) {
  size_t size = storage_.fileSize(path);
  if (!size) return false;
  const CamStorage* storage = &storage_;
  c.reader = [storage, path, size](size_t offset, char* out, size_t len) { return offset + len <= size && storage->readAt(path, offset, out, len); };
  c.pull = &c.reader;
  c.staticLen = size;
  c.out = httpHead(200, type, size);
  return true;
}

void SensorCamServer::closeHttp(HttpConn& c) {
  if (c.firmwareActive) {  // the browser went away partway through, so the update is abandoned
    camCall(cfg_.firmwareEnd, false);
    firmwareOwner_ = nullptr;
  }
  if (uploading_ == &c) {
    uploading_ = nullptr;
    frameKey_.clear();
  }
  if (c.fd >= 0) netClose(c.fd);
  c = HttpConn();
}

int SensorCamServer::sharedSockets() const {
  int open = 0;
  for (const HttpConn& c : http_) open += c.fd >= 0;
  for (const NotifyIo& n : notify_) open += n.fd >= 0;
  return open + (hook_.fd >= 0) + (mqtt_.fd >= 0);
}

void SensorCamServer::acceptHttp(int listenFd, uint32_t now) {
  // Short of RAM or sockets: left waiting in the queue, to be taken once existing traffic has drained.
  if (!hasHeadroom() || sharedSockets() >= MAX_HTTP || updating()) {
    refused_++;  // counts the passes it had to wait, for the heartbeat
    return;
  }
  sockaddr_in a{};
  socklen_t len = sizeof(a);
  int fd = (int)accept(listenFd, (sockaddr*)&a, &len);
  if (fd < 0) return;
  if (listenFd == captiveListen_ && !camCall(cfg_.setupActive)) {
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

// ---- HTTP: ONVIF SOAP on its own port; the web pages and sensor input on another ----

// Replies with data that stays valid until the connection closes, without copying it.
void SensorCamServer::sendBody(HttpConn& c, const char* type, const char* data, size_t len, const std::string& extraHeaders) {
  c.staticBody = data;
  c.staticLen = len;
  c.out = httpHead(200, type, len, extraHeaders);
}

// The body is kept apart from the headers rather than copied after them, so a large reply never needs a second block
// of its size.
void SensorCamServer::reply(HttpConn& c, int status, const char* type, std::string body, const std::string& extraHeaders) {
  c.body = std::move(body);
  c.staticBody = c.body.data();
  c.staticLen = c.body.size();
  c.out = httpHead(status, type, c.body.size(), extraHeaders);
}

void SensorCamServer::undoPull(HttpConn& c) {
  CamSub* sub = c.undoSub ? subs_.find(c.undoSub) : nullptr;
  if (sub) sub->lastPull = c.undoState;
  c.undoSub = 0;
}

// Memory ran out while answering. Only this request fails: what it was building is let go, and it is told to try
// again. A hub that stopped here would miss alerts while it restarted, and a new firmware would be undone.
void SensorCamServer::shortOfMemory(HttpConn& c) {
  static const char REPLY[] = "HTTP/1.1 507 Insufficient Storage\r\nContent-Type: application/json\r\nContent-Length: 50\r\nConnection: close\r\n\r\n"
                              "{\"ok\":false,\"error\":\"the hub is short of memory\"}\n";
  static_assert(sizeof("{\"ok\":false,\"error\":\"the hub is short of memory\"}\n") - 1 == 50, "the length above must match");
  if (c.toFrameBuffer) {
    uploading_ = nullptr;
    c.toFrameBuffer = false;
    frameKey_.clear();
  }
  std::string().swap(c.in);
  std::string().swap(c.body);
  std::string().swap(c.out);
  c.staticBody = REPLY;
  c.staticLen = sizeof(REPLY) - 1;
  c.pull = nullptr;
  c.stream = nullptr;
  c.pollWaiting = false;
  c.outPos = 0;
  c.answered = true;
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
  return ms < CAM_POLL_HOLD_MAX_MS ? ms : CAM_POLL_HOLD_MAX_MS;
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
  // Written into the replies' addresses, so anything but a plain name or address and port is replaced.
  bool plain = !ctx.host.empty() && ctx.host.size() <= 64;
  for (char ch : ctx.host) plain = plain && (isalnum((unsigned char)ch) || ch == '.' || ch == '-' || ch == ':');
  if (!plain) ctx.host = localIp_() + ":" + std::to_string(cfg_.onvifPort);
  ctx.rtspPort = cfg_.rtspPort;
  ctx.serial = cfg_.serial;
  ctx.onvifPort = cfg_.onvifPort;
  if (cfg_.onvifNetwork) cfg_.onvifNetwork(ctx);
  ctx.now = time(nullptr);
  if (!cfg_.onvifPassword.empty()) {
    ctx.snapshotToken = camSnapshotToken(cfg_.snapshotSecret, cfg_.onvifPassword);
    // ONVIF allows these before logging in. A client needs the clock to build its login.
    static const char* const OPEN[] = {"GetSystemDateAndTime", "GetCapabilities", "GetServices", "GetServiceCapabilities", "GetHostname", "GetEndpointReference", "GetWsdlUrl"};
    bool open = false;
    for (const char* name : OPEN) open = open || action == name;
    bool presented = !open && !xmlFirstText(body, "Password").empty(), allowed = logins_.allowed(c.peer, now);
    bool ok = open || (presented && allowed && camWsseValid(body, HUB_USER, cfg_.onvifPassword));
    if (presented && allowed) logins_.note(c.peer, ok, now);
    if (!ok) {
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
    if (sub) sub->pulledAt = now;
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
    if (sub) {
      c.undoSub = sub->id;
      c.undoState = sub->lastPull;
      sub->lastPull = state;
    }
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
  if (!frameFile_.empty() && sendFile(c, camPreviewPath(frameFile_), "image/jpeg")) return;
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
    // The state counts as told only once the reply is ready. Without the memory for it, the request waits for the next
    // pass, still owed this change.
    try {
      SoapContext ctx;
      ctx.host = c.pollHost;
      ctx.now = time(nullptr);
      reply(c, 200, "application/soap+xml; charset=utf-8", soapEnvelope(soapPullMessagesReply(ctx, changed, state == 1, initial)));
    } catch (const std::bad_alloc&) {
      memoryFailures_++;
      continue;
    }
    if (sub) sub->lastPull = state;
    c.pollWaiting = false;
    c.answered = true;
    c.openedAt = now;  // the time limit now covers sending this reply, not the wait before it
  }
}

// ---- file manager ----

// See camKnownHost.
bool SensorCamServer::knownHost(const std::string& header) const {
  return camKnownHost(header, localIp_(), cfg_.hostName, cfg_.setupAddress, cfg_.extraHosts);
}

// Checks the login and keeps count of wrong ones.
bool SensorCamServer::authorized(const HttpConn& c, const std::string& headers) {
  if (cfg_.webPassword.empty()) return true;
  std::string auth = headerValue(headers, "authorization");
  uint32_t now = millis_();
  if (auth.empty() || !logins_.allowed(c.peer, now)) return false;
  bool ok = camBasicValid(auth, HUB_USER, cfg_.webPassword);
  logins_.note(c.peer, ok, now);
  return ok;
}

void SensorCamServer::finishFirmware(HttpConn& c, bool ok) {
  bool done = ok && camCall(cfg_.firmwareEnd, true);
  if (!ok) camCall(cfg_.firmwareEnd, false);
  firmwareOwner_ = nullptr;
  c.firmwareActive = false;
  c.discardLeft = c.firmwareLeft;
  c.firmwareLeft = 0;
  c.out = done ? jsonOk(JSON_OK) : jsonFail(400, "that is not valid firmware for this hub, so nothing was changed");
  c.answered = true;
}

void SensorCamServer::feedFirmware(HttpConn& c, const char* data, size_t n, uint32_t now) {
  size_t take = n < c.firmwareLeft ? n : c.firmwareLeft;
  bool written = !take || camCall(cfg_.firmwareWrite, (const uint8_t*)data, take);
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
  auto refuse = [&](int code, const char* message, size_t drainLimit = 128 * 1024) {
    c.out = jsonFail(code, message);
    c.answered = true;
    size_t got = c.in.size() - (headEnd + 4);
    c.discardLeft = contentLength > got && contentLength <= drainLimit ? contentLength - got : 0;
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
  if (c.web && isGet && camCall(cfg_.setupActive) && headerValue(headers, "host") != cfg_.setupAddress) {
    c.out = "HTTP/1.1 302 Found\r\nLocation: http://" + cfg_.setupAddress + "/setup\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    c.answered = true;
    return true;
  }
  // A page from another site can point its own name at the hub (DNS rebinding) and then use it as if it were that site.
  // Its requests carry that name, so only the hub's own names and addresses are served.
  // Sensor reports are left out: their token already guards them, and a sensor set up with any name for the hub must
  // never have an alert refused.
  bool sensorReport = isGet && target.compare(0, 8, "/sensor?") == 0;
  if (c.web && !sensorReport && !knownHost(headerValue(headers, "host"))) return refuse(421, "the hub does not answer to this name; open it by its IP address and add the name under Network Manager > HTTP > Other names");
  if (isApi && !isGet) {  // a browser names the site a request came from, so another site cannot use this browser's login
    std::string origin = headerValue(headers, "origin");
    if (!origin.empty() && origin != "http://" + headerValue(headers, "host")) return refuse(403, "request from another site refused");
  }
  if (method == "PUT" && target == "/api/firmware") {  // goes straight to flash, because it is far larger than memory
    if (!authorized(c, headers)) return needLogin(MAX_FIRMWARE);
    // A refused firmware is read to its end before the reply, as the browser reports a reply that comes while it is
    // still sending as a bare network error, without the reason.
    if (!contentLength || contentLength > MAX_FIRMWARE) return refuse(413, "that file is not the right size for firmware", MAX_FIRMWARE);
    if (firmwareOwner_ || !camCall(cfg_.firmwareBegin, contentLength)) return refuse(507, "could not start the update; the hub may be short of memory, so restart it and try again", MAX_FIRMWARE);
    firmwareOwner_ = &c;
    c.firmwareActive = true;
    // Video is what uses the network's memory most, and an update that runs out of it stops the hub. The NVR
    // reconnects after the restart that follows, or at once if the update fails.
    for (RtspConn& r : rtsp_)
      if (r.fd >= 0) closeRtsp(r);
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
    if (!authorized(c, headers)) return needLogin(CAM_MAX_FRAME);  // before taking the picture buffer, which changes what the NVR sees
    if (uploading_) return refuse(507, "the hub is busy with another upload; try again");
    uploading_ = &c;
    c.toFrameBuffer = true;
    c.bodyWanted = contentLength;
    releasePicture();
    frame_ = nullptr;  // the built-in pictures are shown until the upload ends
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
    if (!cfg_.onvifPassword.empty() && !camSameText(queryParam(target, "token"), camSnapshotToken(cfg_.snapshotSecret, cfg_.onvifPassword))) c.out = httpResponse(403, "text/plain", "not authorized\n");
    else serveSnapshot(c);
  } else if (isGet && (target.compare(0, 7, "/status") == 0 || target.compare(0, 7, "/events") == 0) && !authorized(c, headers)) {
    c.out = UNAUTHORIZED;
  } else if (isGet && target.compare(0, 7, "/sensor") == 0) {
    std::string body = handleSensor(target, status);
    reply(c, status, JSON_TYPE, std::move(body));
  } else if (isGet && target.compare(0, 7, "/status") == 0) {
    reply(c, 200, JSON_TYPE, statusJson(now));
  } else if (isGet && target.compare(0, 7, "/events") == 0) {
    reply(c, 200, JSON_TYPE, eventsJson());
  } else if (isGet) {
    c.out = httpResponse(404, "text/plain", "not found\n");
  } else {
    c.in.erase(0, headEnd + 4);  // in place, because a second copy of a large request could run the hub out of memory
    c.in.resize(contentLength);
    std::string body = handleOnvif(c, headers, c.in, now, status);
    if (status == -1) {  // held until something changes
      std::string().swap(c.in);
      return false;
    }
    reply(c, status, "application/soap+xml; charset=utf-8", std::move(body));
  }
  c.answered = true;
  return true;
}

// A streamed body in HTTP chunks, so a download cut short shows as failed instead of looking complete. Every chunk but
// the last carries CHUNK bytes, framed as "3e8\r\n" + bytes + "\r\n"; then "0\r\n\r\n" ends it. With every framed chunk
// the same size, where any byte already sent falls is known without keeping count, and a piece sent only in part is
// simply asked for again. Gives the next bytes after sent, at most room, and 0 once the end has gone.
static size_t chunkedPiece(const std::function<size_t(size_t, char*, size_t)>& stream, size_t sent, char* out, size_t room) {
  static const size_t CHUNK = 1000, FRAMED = 3 + 2 + CHUNK + 2;  // "3e8", CRLF, the bytes, CRLF
  size_t index = sent / FRAMED, within = sent % FRAMED;
  char data[CHUNK];
  size_t n = stream(index * CHUNK, data, CHUNK);
  std::string framed;
  if (n) {
    char head[8];
    snprintf(head, sizeof(head), "%x\r\n", (unsigned)n);
    framed = head;
    framed.append(data, n);
    framed += "\r\n";
  }
  if (n < CHUNK) framed += "0\r\n\r\n";
  if (within >= framed.size()) return 0;
  size_t take = framed.size() - within < room ? framed.size() - within : room;
  memcpy(out, framed.data() + within, take);
  return take;
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
      c.openedAt = now;  // a large refused upload takes a while to read through; the limit is for one that stalls
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
      try {
        handleHttp(c, now);
      } catch (const std::bad_alloc&) {
        memoryFailures_++;
        undoPull(c);
        shortOfMemory(c);
      }
      if (c.fd < 0) return;
      if (c.answered) std::string().swap(c.in);  // the request is done with, and may be as large as 8 KB
    }
  }
  if (c.answered && !c.discardLeft && (writable || c.outPos == 0)) {
    bool inHeader = c.outPos < c.out.size();
    const char* data = inHeader ? c.out.data() + c.outPos : c.staticBody + (c.outPos - c.out.size());
    size_t left = inHeader ? c.out.size() - c.outPos : c.stream ? 0 : c.staticLen - (c.outPos - c.out.size());
    char piece[1024];
    if (!inHeader && c.pull && left) {  // read again from the same place if the last piece was only partly sent
      left = left < sizeof(piece) ? left : sizeof(piece);
      if (!(*c.pull)(c.outPos - c.out.size(), piece, left)) {
        closeHttp(c);
        return;
      }
      data = piece;
    } else if (!inHeader && c.stream) {  // a body of unknown length, sent in HTTP chunks
      left = chunkedPiece(*c.stream, c.outPos - c.out.size(), piece, sizeof(piece));
      if (!left) {
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
    if (n > 0) {
      c.outPos += n;
      c.openedAt = now;  // the time limit is for a stalled reply, so a long download is not cut off part way
    }
    if (!c.stream && c.outPos >= c.out.size() + c.staticLen) {
      closeHttp(c);
      return;
    }
  }
  if (!c.pollWaiting && now - c.openedAt > CAM_HTTP_STALL_MS) closeHttp(c);
}

// ---- ONVIF event push ----

void SensorCamServer::endNotify(NotifyIo& n, const char* result) {
  bool hook = n.hook;
  if (!hook && &n >= notify_ && &n < notify_ + SubTable::SIZE && n.state != NotifyIo::IDLE) {
    // Ending before the whole message was sent means the NVR did not get it. Once it was sent, waiting for the NVR to
    // close is all that is left, so any ending then is a delivery.
    subs_.at((int)(&n - notify_)).pushFailed = n.state != NotifyIo::WAIT_CLOSE;
  }
  if (n.fd >= 0) netClose(n.fd);
  n = NotifyIo();
  if (hook) {  // last, as the one step that needs memory: running out then loses only the note
    hookResultAt_ = time(nullptr);
    hookResult_ = result;
  }
}

void SensorCamServer::startNotify(int i, uint32_t now) {
  CamSub& sub = subs_.at(i);
  NotifyIo& n = notify_[i];
  HttpUrl url;
  if (!parseHttpUrl(sub.consumer, url)) {
    sub.pending = false;
    return;
  }
  // Built before the connection is opened, so running out of memory leaves no connection behind. The push stays pending
  // and is tried again on the next pass.
  std::string body = soapNotify(sensors_.anyActive(), !sub.initialized, time(nullptr));
  std::string out = "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                    "\r\nContent-Type: application/soap+xml; charset=utf-8; action=\"http://docs.oasis-open.org/wsn/bw-2/NotificationConsumer/Notify\"\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
  out.reserve(out.size() + body.size());
  out += body;
  std::string().swap(body);
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
  sub.initialized = true;
  n.out.swap(out);
  n.fd = fd;
  n.outPos = 0;
  n.state = NotifyIo::CONNECTING;
  n.deadline = now + CAM_NOTIFY_SEND_MS;
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
      std::string().swap(n.out);
      n.outPos = 0;
      n.state = NotifyIo::WAIT_CLOSE;
      n.deadline = now + (n.hook ? CAM_HOOK_WAIT_MS : CAM_NOTIFY_CLOSE_MS);
    }
  }
  if (n.state == NotifyIo::WAIT_CLOSE && readable) {
    char buf[256];
    int got = (int)recv(n.fd, buf, sizeof(buf), 0);
    if (got >= 0 || !netWouldBlock()) {
      char code[16];
      const char* result = "closed without a reply";
      if (got >= 12 && memcmp(buf, "HTTP/1.", 7) == 0) {
        snprintf(code, sizeof(code), "HTTP %.3s", buf + 9);
        result = code;
      }
      endNotify(n, result);
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

// Running out of memory drops only the piece of work that needed it, never the hub: a restart would miss alerts while it
// lasted. Whatever was dropped is tried again on a later pass, or by the client that asked.
// The two halves apart, so work that keeps running out of memory cannot stop the connections being served.
void SensorCamServer::poll(int maxWaitMs) {
  try {
    housekeeping(millis_());
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
  }
  try {
    pollOnce(maxWaitMs);
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
  }
}

void SensorCamServer::housekeeping(uint32_t now) {
  sensors_.tick(now);
  serviceDelayed(now);
  // Latched, so a long-dead sensor stays silent when the millisecond counter wraps.
  for (int i = 0; i < CAM_MAX_SENSORS; i++) {
    Health& h = health_[i];
    uint32_t silentMs = slots_[i].node.silentS ? slots_[i].node.silentS * 1000 : cfg_.silentMs;
    if (!slots_[i].used || h.silent || now - (h.heard ? h.heardAt : h.watchedAt) <= silentMs) continue;
    h.silent = true;
    healthAlert(i + 1, CAM_EVENT_SILENT, now);
  }
  // Reports that arrived since the last pass are saved together, and only then confirmed.
  if (confirmCount_) {
    bool kept = persist();
    for (int i = 0; kept && cfg_.espNowSend && i < confirmCount_; i++) cfg_.espNowSend(confirmations_[i], confirmations_[i] + 6, CAM_CONFIRMATION_LEN);
    confirmCount_ = 0;  // unconfirmed, the sensors resend, and each resend tries the save again
  }
  if (log_.waiting()) log_.save(downloadingHistory());  // events from anything else: an addition to the log is cheap
  if (waitingDirty_) saveWaiting();  // small, and it is what a restart would replay
  if (!startNoted_) noteHubStart(now);
  if (startNoted_ && (!aliveWritten_ || now - aliveAt_ >= CAM_ALIVE_EVERY_MS)) writeAlive(now);  // after the start is noted, which reads the old time
  if (cfg_.watchNvr) checkNvr(now);
  subs_.expire(now);
  servicePolls(now);
  if (signal_.update(sensors_.anyActive(), now)) subs_.markAllPending();
  for (int i = 0; i < SubTable::SIZE; i++) {  // so one failed push cannot leave the NVR counted as not listening
    CamSub& s = subs_.at(i);
    if (!s.used || !s.push || !s.pushFailed || now - s.retryAt < CAM_PUSH_RETRY_MS) continue;
    s.pending = true;
    s.retryAt = now;
  }
  for (int i = 0; i < SubTable::SIZE; i++) {
    CamSub& s = subs_.at(i);
    if (s.used && s.push && s.pending && notify_[i].state == NotifyIo::IDLE && sharedSockets() <= MAX_HTTP) startNotify(i, now);
  }
  if (hook_.state == NotifyIo::IDLE && hookCount_ && sharedSockets() < MAX_HTTP) startHook(now);  // leaves one for an event push
  mqttHousekeeping(now);
  pushFrames(now);
}

void SensorCamServer::pollOnce(int maxWaitMs) {
  uint32_t now = millis_();
  fd_set rd, wr;
  FD_ZERO(&rd);
  FD_ZERO(&wr);
  int maxFd = -1;
  auto add = [&](int fd, fd_set& set) {
    FD_SET(fd, &set);
    if (fd > maxFd) maxFd = fd;
  };

  // While every web connection is busy, or memory is short, new ones are left waiting in the network stack's queue rather
  // than accepted and closed, so a sensor's report waits a moment instead of being lost. Each is taken once room frees.
  // During a firmware update nothing new is taken: the update needs the memory, and the hub restarts after it anyway.
  bool room = sharedSockets() < MAX_HTTP && hasHeadroom() && !updating();
  if (onvifListen_ >= 0 && room) add(onvifListen_, rd);
  if (webListen_ >= 0 && room) add(webListen_, rd);
  if (captiveListen_ >= 0 && room) add(captiveListen_, rd);
  if (rtspListen_ >= 0 && !updating()) add(rtspListen_, rd);
  if (udp_ >= 0) add(udp_, rd);
  for (HttpConn& c : http_) {
    if (c.fd < 0) continue;
    add(c.fd, rd);
    if (c.answered && !c.discardLeft) add(c.fd, wr);
  }
  for (RtspConn& c : rtsp_) {
    if (c.fd < 0) continue;
    add(c.fd, rd);
    if (!c.out.empty() || c.packetPos < c.packetLen || c.video.frame) add(c.fd, wr);
  }
  for (NotifyIo& n : notify_) {
    if (n.state == NotifyIo::IDLE) continue;
    if (n.state == NotifyIo::WAIT_CLOSE) add(n.fd, rd);
    else add(n.fd, wr);
  }
  if (hook_.state != NotifyIo::IDLE) add(hook_.fd, hook_.state == NotifyIo::WAIT_CLOSE ? rd : wr);
  if (mqtt_.fd >= 0) {
    if (mqtt_.state != MqttIo::CONNECTING) add(mqtt_.fd, rd);
    if (mqtt_.state == MqttIo::CONNECTING || mqtt_.outPos < mqtt_.out.size()) add(mqtt_.fd, wr);
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
  if (mqtt_.fd >= 0) serviceMqtt(FD_ISSET(mqtt_.fd, &rd), FD_ISSET(mqtt_.fd, &wr), now);
}
