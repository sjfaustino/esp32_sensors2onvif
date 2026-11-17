#include "cam_server.h"
#include "cam_number.h"
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
      signal_(CAM_REPEAT_TRUE_MS),
      storage_(cfg.fsRoot),
      nodes_(cfg_, *radio_, storage_, alarm_, millisFn),
      alarm_(cfg_, *chip_, *radio_, *store_, storage_, nodes_.images(), *this, millisFn),
      mqtt_(cfg_, alarm_, *chip_, *this, localIp),
      video_(
          cfg_, *chip_, storage_, alarm_, logins_, [this](uint32_t peer, uint32_t now) { subs_.heard(peer, now); }, millisFn),
      webhook_(cfg_, alarm_, *chip_, [this](uint32_t now) { return nvrListening(now); }) {
  csvRead_ = [this](size_t offset, char* out, size_t len) { return alarm_.log().csvRead(offset, out, len); };  // see HttpConn::stream
  crashReader_ = [this](size_t offset, char* out, size_t len) { return chip_->crashRead(offset, out, len); };
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
  if (cfg_.captivePort && cfg_.captivePort != cfg_.webPort && cfg_.captivePort != cfg_.onvifPort && cfg_.captivePort != cfg_.rtspPort)
    captiveListen_ = listenOn(cfg_.captivePort);
  bool videoOk = video_.begin(listenOn(cfg_.rtspPort));
  openDiscovery();
  lastGoodWait_ = millis_();
  nodes_.begin();
  alarm_.begin();
  mqtt_.begin();
  subs_.protect(cfg_.nvrIp.empty() ? 0 : inet_addr(cfg_.nvrIp.c_str()));
  return onvifListen_ >= 0 && webListen_ >= 0 && videoOk;
}

std::string SensorCamServer::debugLine() {
  int web = 0, video = video_.openCount(), push = 0;
  for (const HttpConn& c : http_) web += c.fd >= 0;
  for (const CamPost& n : notify_) push += !n.idle();
  return "wait errors " + std::to_string(waitErrors_) + " (errno " + std::to_string(lastWaitErrno_) + "), accepted " + std::to_string(accepted_) +
         ", refused " + std::to_string(refused_) + ", open web " + std::to_string(web) + " video " + std::to_string(video) + " push " +
         std::to_string(push) + ", short of memory " + std::to_string(memoryFailures());
}

void SensorCamServer::networkUp() {
  alarm_.networkUp();
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
  if (c.firmwareActive && c.nodeImage) {
    dropNodeImage();
  } else if (c.firmwareActive) {  // the browser went away partway through, so the update is abandoned
    chip_->firmwareEnd(false);
    firmwareOwner_ = nullptr;
  }
  if (uploading_ == &c) {
    uploading_ = nullptr;
    video_.giveBack();
  }
  if (c.fd >= 0) netClose(c.fd);
  c = HttpConn();
}

int SensorCamServer::sharedSockets() const {
  int open = 0;
  for (const HttpConn& c : http_) open += c.fd >= 0;
  for (const CamPost& n : notify_) open += n.fd() >= 0;
  return open + (webhook_.fd() >= 0) + (mqtt_.fd() >= 0);
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
  if (listenFd == captiveListen_ && !radio_->setupActive()) {
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
  static const char REPLY[] =
      "HTTP/1.1 507 Insufficient Storage\r\nContent-Type: application/json\r\nContent-Length: 50\r\nConnection: close\r\n\r\n"
      "{\"ok\":false,\"error\":\"the hub is short of memory\"}\n";
  static_assert(sizeof("{\"ok\":false,\"error\":\"the hub is short of memory\"}\n") - 1 == 50, "the length above must match");
  if (c.toFrameBuffer) {
    uploading_ = nullptr;
    c.toFrameBuffer = false;
    video_.giveBack();
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
  uint32_t id = 0;
  return slash != std::string::npos && camNumber(path.c_str() + slash + 1, path.c_str() + path.size(), id) ? id : 0;
}

// A subscription is pulled from and ended only by the address that made it. Its number is easy to guess, and another
// device pulling the NVR's would take the change the NVR was waiting for, which the NVR would then never record.
static CamSub* ownSub(SubTable& subs, const std::string& path, uint32_t peer) {
  CamSub* sub = subs.find(subIdFromPath(path));
  return sub && sub->peer == peer ? sub : nullptr;
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
  radio_->onvifNetwork(ctx);
  ctx.now = time(nullptr);
  if (!cfg_.onvifPassword.empty()) {
    ctx.snapshotToken = camSnapshotToken(cfg_.snapshotSecret, cfg_.onvifPassword);
    // ONVIF allows these before logging in. A client needs the clock to build its login.
    static const char* const OPEN[] = {"GetSystemDateAndTime", "GetCapabilities",      "GetServices", "GetServiceCapabilities",
                                       "GetHostname",          "GetEndpointReference", "GetWsdlUrl"};
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
    CamSub* sub = ownSub(subs_, path, c.peer);
    if (sub) sub->pulledAt = now;
    int state = alarm_.board().anyActive() ? 1 : 0;
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
    CamSub* sub = ownSub(subs_, path, c.peer);
    if (!sub) {
      status = 400;
      return soapFault("no such subscription from this address");
    }
    subs_.remove(sub->id);
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
  video_.current(millis_());
  if (!video_.file().empty() && sendFile(c, camPreviewPath(video_.file()), "image/jpeg")) return;
  int id = 0;
  uint8_t direction = 0;
  bool delayed = false;
  bool on = alarm_.board().shown(millis_(), id, direction, delayed);
  const CamJpeg& builtin = CAM_SNAPSHOTS[builtinFrame(on, delayed)];
  sendBody(c, "image/jpeg", (const char*)builtin.data, builtin.len);
}

void SensorCamServer::servicePolls(uint32_t now) {
  int state = alarm_.board().anyActive() ? 1 : 0;
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
  if (c.nodeImage) {
    finishNodeImage(c, ok);
    return;
  }
  bool done = ok && chip_->firmwareEnd(true);
  if (!ok) chip_->firmwareEnd(false);
  firmwareOwner_ = nullptr;
  c.firmwareActive = false;
  c.discardLeft = c.firmwareLeft;
  c.firmwareLeft = 0;
  c.out = done ? jsonOk(JSON_OK) : jsonFail(400, "that is not valid firmware for this hub, so nothing was changed");
  c.answered = true;
}

void SensorCamServer::feedFirmware(HttpConn& c, const char* data, size_t n, uint32_t now) {
  size_t take = n < c.firmwareLeft ? n : c.firmwareLeft;
  bool written = !take || (c.nodeImage ? nodes_.images().write(data, take) : chip_->firmwareWrite((const uint8_t*)data, take));
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

  std::string lengthText = headerValue(headers, "content-length");
  size_t contentLength = 0;
  bool lengthOk = lengthText.empty() || camNumber(lengthText, contentLength);
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
  if (!lengthOk) return refuse(400, "the Content-Length is not a number");
  bool isSnapshot = isGet && route == "/onvif/snapshot";  // the one GET that ONVIF clients make
  bool isOnvif = isSnapshot || (!isApi && !isGet);
  if (isOnvif == c.web) return refuse(404, "not available on this port");
  // Phones and laptops probe a known site on joining a network; sending them here is what opens the setup page by itself.
  if (c.web && isGet && radio_->setupActive() && headerValue(headers, "host") != cfg_.setupAddress) {
    c.out = "HTTP/1.1 302 Found\r\nLocation: http://" + cfg_.setupAddress + "/setup\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    c.answered = true;
    return true;
  }
  // A page from another site can point its own name at the hub (DNS rebinding) and then use it as if it were that site.
  // Its requests carry that name, so only the hub's own names and addresses are served.
  // Sensor reports are left out: their token already guards them, and a sensor set up with any name for the hub must
  // never have an alert refused.
  bool sensorReport = isGet && target.compare(0, 8, "/sensor?") == 0;
  if (c.web && !sensorReport && !knownHost(headerValue(headers, "host")))
    return refuse(421, "the hub does not answer to this name; open it by its IP address and add the name under Network Manager > HTTP > Other names");
  if (isApi && !isGet) {  // a browser names the site a request came from, so another site cannot use this browser's login
    std::string origin = headerValue(headers, "origin");
    if (!origin.empty() && origin != "http://" + headerValue(headers, "host")) return refuse(403, "request from another site refused");
  }
  if (method == "PUT" && target == "/api/firmware") {  // goes straight to flash, because it is far larger than memory
    if (!authorized(c, headers)) return needLogin(MAX_FIRMWARE);
    // A refused firmware is read to its end before the reply, as the browser reports a reply that comes while it is
    // still sending as a bare network error, without the reason.
    if (!contentLength || contentLength > MAX_FIRMWARE) return refuse(413, "that file is not the right size for firmware", MAX_FIRMWARE);
    if (firmwareOwner_ || !chip_->firmwareBegin(contentLength))
      return refuse(507, "could not start the update; the hub may be short of memory, so restart it and try again", MAX_FIRMWARE);
    firmwareOwner_ = &c;
    c.firmwareActive = true;
    // Video is what uses the network's memory most, and an update that runs out of it stops the hub. The NVR
    // reconnects after the restart that follows, or at once if the update fails.
    video_.closeAll();
    c.firmwareLeft = contentLength;
    std::string first = c.in.substr(headEnd + 4);
    c.in.clear();
    feedFirmware(c, first.data(), first.size(), now);
    return true;
  }
  if (method == "PUT" && target == "/api/node-firmware") {  // to storage, read through as it arrives, as it is far larger than memory
    if (!authorized(c, headers)) return needLogin(CAM_MAX_NODE_IMAGE);
    if (!contentLength || contentLength > CAM_MAX_NODE_IMAGE)
      return refuse(413, "that file is not the right size for a node's firmware", CAM_MAX_NODE_IMAGE);
    if (firmwareOwner_ || nodeImageOwner_) return refuse(507, "the hub is busy with another update; try again", CAM_MAX_NODE_IMAGE);
    if (!nodes_.images().begin()) return refuse(507, "could not save the file; storage may be full", CAM_MAX_NODE_IMAGE);
    startNodeImage(c, contentLength);
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
    video_.lend().assign(c.in, headEnd + 4, std::string::npos);  // the built-in pictures are shown until the upload ends
    c.in.resize(headEnd + 4);
  }
  if (c.toFrameBuffer) {
    if (video_.buffer().size() < contentLength) return false;
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
    handleApi(c, method, target, headers, c.toFrameBuffer ? video_.buffer().data() : c.in.data() + headEnd + 4, contentLength);
    if (c.toFrameBuffer) {
      uploading_ = nullptr;
      c.toFrameBuffer = false;
      video_.giveBack();
    }
  } else if (isSnapshot) {
    if (!cfg_.onvifPassword.empty() && !camSameText(queryParam(target, "token"), camSnapshotToken(cfg_.snapshotSecret, cfg_.onvifPassword)))
      c.out = httpResponse(403, "text/plain", "not authorized\n");
    else
      serveSnapshot(c);
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
        std::string& picture = video_.buffer();
        size_t room = c.bodyWanted > picture.size() ? c.bodyWanted - picture.size() : 0;
        picture.append(buf, (size_t)n < room ? (size_t)n : room);
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

void SensorCamServer::startNotify(int i, uint32_t now) {
  CamSub& sub = subs_.at(i);
  HttpUrl url;
  if (!parseHttpUrl(sub.consumer, url)) {
    sub.pending = false;
    return;
  }
  // Built before the connection is opened, so running out of memory leaves no connection behind. The push stays pending
  // and is tried again on the next pass.
  std::string body = soapNotify(alarm_.board().anyActive(), !sub.initialized, time(nullptr));
  std::string out = "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                    "\r\nContent-Type: application/soap+xml; charset=utf-8; "
                    "action=\"http://docs.oasis-open.org/wsn/bw-2/NotificationConsumer/Notify\"\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
  out.reserve(out.size() + body.size());
  out += body;
  std::string().swap(body);
  CamPost::Start started = notify_[i].start(url, out, CAM_NOTIFY_SEND_MS, CAM_NOTIFY_CLOSE_MS, now);
  if (started == CamPost::NO_SOCKET) return;  // still pending, so it is tried again on the next pass
  sub.pending = false;
  if (started == CamPost::REFUSED) {
    sub.pushFailed = true;
    return;
  }
  sub.initialized = true;
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
  alarm_.tick(now);
  webhook_.dailyCheck(now);
  subs_.expire(now);
  servicePolls(now);
  if (signal_.update(alarm_.board().anyActive(), now)) subs_.markAllPending();
  for (int i = 0; i < SubTable::SIZE; i++) {  // so one failed push cannot leave the NVR counted as not listening
    CamSub& s = subs_.at(i);
    if (!s.used || !s.push || !s.pushFailed || now - s.retryAt < CAM_PUSH_RETRY_MS) continue;
    s.pending = true;
    s.retryAt = now;
  }
  for (int i = 0; i < SubTable::SIZE; i++) {
    CamSub& s = subs_.at(i);
    if (s.used && s.push && s.pending && notify_[i].idle() && sharedSockets() <= MAX_HTTP) startNotify(i, now);
  }
  webhook_.tick(now, sharedSockets() < MAX_HTTP);  // leaves one for an event push
  try {                                            // on its own, so running out of memory there still leaves the picture to be sent
    mqtt_.tick(now, sharedSockets() < MAX_HTTP && !updating());
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
  }
  video_.tick(now);
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
  if (udp_ >= 0) add(udp_, rd);
  for (HttpConn& c : http_) {
    if (c.fd < 0) continue;
    add(c.fd, rd);
    if (c.answered && !c.discardLeft) add(c.fd, wr);
  }
  video_.watch(rd, wr, maxFd, !updating());
  for (const CamPost& n : notify_) n.watch(rd, wr, maxFd);
  webhook_.watch(rd, wr, maxFd);
  if (mqtt_.wantsRead()) add(mqtt_.fd(), rd);
  if (mqtt_.wantsWrite()) add(mqtt_.fd(), wr);

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

  if (onvifListen_ >= 0 && FD_ISSET(onvifListen_, &rd)) acceptHttp(onvifListen_, now);
  if (webListen_ >= 0 && FD_ISSET(webListen_, &rd)) acceptHttp(webListen_, now);
  if (captiveListen_ >= 0 && FD_ISSET(captiveListen_, &rd)) acceptHttp(captiveListen_, now);
  if (udp_ >= 0 && FD_ISSET(udp_, &rd)) serviceDiscovery();
  for (HttpConn& c : http_)
    if (c.fd >= 0) serviceHttp(c, FD_ISSET(c.fd, &rd), FD_ISSET(c.fd, &wr), now);
  video_.serve(rd, wr, now);
  for (int i = 0; i < SubTable::SIZE; i++) {
    CamPost& n = notify_[i];
    // Ended before the whole message was sent, the NVR did not get it.
    if (!n.idle() && n.service(FD_ISSET(n.fd(), &rd), FD_ISSET(n.fd(), &wr), now)) subs_.at(i).pushFailed = !n.delivered();
  }
  webhook_.serve(rd, wr, now);
  if (mqtt_.fd() >= 0) mqtt_.service(FD_ISSET(mqtt_.fd(), &rd), FD_ISSET(mqtt_.fd(), &wr), now);
}
