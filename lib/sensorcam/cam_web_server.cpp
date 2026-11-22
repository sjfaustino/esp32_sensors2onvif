// CamWeb: HTTP connections, the requests on them, and the ONVIF requests.
#include "cam_web_server.h"
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

CamWeb::CamWeb(const CamWebParts& p)
    : cfg_(p.cfg),
      chip_(&p.chip),
      radio_(&p.radio),
      store_(&p.store),
      storage_(p.storage),
      logins_(p.logins),
      nodes_(p.nodes),
      alarm_(p.alarm),
      table_(p.table),
      mqtt_(p.mqtt),
      events_(p.events),
      video_(p.video),
      webhook_(p.webhook),
      failures_(p.failures),
      localIp_(p.localIp),
      millis_(p.millis),
      webPassword_(p.cfg.webPassword),
      extraHosts_(p.cfg.extraHosts),
      onvifPassword_(p.cfg.onvifPassword),
      schedule_(p.cfg.schedule) {
  csvRead_ = [this](size_t offset, char* out, size_t len) { return alarm_.log().csvRead(offset, out, len); };  // see HttpConn::stream
  crashReader_ = [this](size_t offset, char* out, size_t len) { return chip_->crashRead(offset, out, len); };
}

bool CamWeb::begin() {
  onvifListen_ = netListen(cfg_.onvifPort, cfg_.loopbackOnly);
  webListen_ = netListen(cfg_.webPort, cfg_.loopbackOnly);
  if (cfg_.captivePort && cfg_.captivePort != cfg_.webPort && cfg_.captivePort != cfg_.onvifPort && cfg_.captivePort != cfg_.rtspPort)
    captiveListen_ = netListen(cfg_.captivePort, cfg_.loopbackOnly);
  return onvifListen_ >= 0 && webListen_ >= 0;
}

void CamWeb::watch(fd_set& rd, fd_set& wr, int& maxFd) {
  // While every web connection is busy, or memory is short, new ones are left waiting in the network stack's queue rather
  // than accepted and closed, so a sensor's report waits a moment instead of being lost. Each is taken once room frees.
  // During a firmware update nothing new is taken: the update needs the memory, and the hub restarts after it anyway.
  bool room = sharedSockets() < MAX_HTTP && hasHeadroom() && !updating();
  // But the NVR is always let in, on its ONVIF port: kept out, its subscription would lapse and alerts stop reaching it.
  // So while full, a new ONVIF connection is taken, and acceptHttp keeps it only if it is the NVR's.
  bool forNvr = !room && !events_.nvrIp().empty() && hasHeadroom() && !updating();
  if (onvifListen_ >= 0 && (room || forNvr)) netWatch(onvifListen_, rd, maxFd);
  if (webListen_ >= 0 && room) netWatch(webListen_, rd, maxFd);
  if (captiveListen_ >= 0 && room) netWatch(captiveListen_, rd, maxFd);
  for (HttpConn& c : http_) {
    if (c.fd < 0) continue;
    netWatch(c.fd, rd, maxFd);
    if (c.answered && !c.discardLeft) netWatch(c.fd, wr, maxFd);
  }
}

void CamWeb::serve(fd_set& rd, fd_set& wr, uint32_t now) {
  if (onvifListen_ >= 0 && FD_ISSET(onvifListen_, &rd)) acceptHttp(onvifListen_, now);
  if (webListen_ >= 0 && FD_ISSET(webListen_, &rd)) acceptHttp(webListen_, now);
  if (captiveListen_ >= 0 && FD_ISSET(captiveListen_, &rd)) acceptHttp(captiveListen_, now);
  for (HttpConn& c : http_)
    if (c.fd >= 0) serviceHttp(c, FD_ISSET(c.fd, &rd), FD_ISSET(c.fd, &wr), now);
}

int CamWeb::openCount() const {
  int open = 0;
  for (const HttpConn& c : http_) open += c.fd >= 0;
  return open;
}

// Read from storage a piece at a time as it is sent, because a picture-sized buffer per reply is more than RAM allows.
bool CamWeb::sendFile(HttpConn& c, const std::string& path, const char* type) {
  size_t size = storage_.fileSize(path);
  if (!size) return false;
  const CamStorage* storage = &storage_;
  c.reader = [storage, path, size](size_t offset, char* out, size_t len) { return offset + len <= size && storage->readAt(path, offset, out, len); };
  c.pull = &c.reader;
  c.staticLen = size;
  c.out = httpHead(200, type, size);
  return true;
}

void CamWeb::closeHttp(HttpConn& c) {
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

int CamWeb::sharedSockets() const {
  int open = 0;
  for (const HttpConn& c : http_) open += c.fd >= 0;
  open += events_.pushCount();
  return open + (webhook_.fd() >= 0) + (mqtt_.fd() >= 0);
}

void CamWeb::acceptHttp(int listenFd, uint32_t now) {
  // Short of RAM or sockets: left waiting in the queue, to be taken once existing traffic has drained; but for the NVR.
  bool full = sharedSockets() >= MAX_HTTP;
  if (!hasHeadroom() || updating() || (full && (listenFd != onvifListen_ || events_.nvrIp().empty()))) {
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
  if (full) {  // the ONVIF port while every connection is busy: the NVR takes the place of the oldest that is not its own
    uint32_t nvr = inet_addr(events_.nvrIp().c_str());
    HttpConn* oldest = nullptr;
    for (HttpConn& c : http_)
      if (c.fd >= 0 && c.peer != nvr && !c.firmwareActive && (!oldest || now - c.openedAt > now - oldest->openedAt)) oldest = &c;
    if (a.sin_addr.s_addr != nvr || !oldest) {
      netClose(fd);
      refused_++;
      return;
    }
    closeHttp(*oldest);
    nvrLetIn_++;
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
void CamWeb::sendBody(HttpConn& c, const char* type, const char* data, size_t len, const std::string& extraHeaders) {
  c.staticBody = data;
  c.staticLen = len;
  c.out = httpHead(200, type, len, extraHeaders);
}

// The body is kept apart from the headers rather than copied after them, so a large reply never needs a second block
// of its size.
void CamWeb::reply(HttpConn& c, int status, const char* type, std::string body, const std::string& extraHeaders) {
  c.body = std::move(body);
  c.staticBody = c.body.data();
  c.staticLen = c.body.size();
  c.out = httpHead(status, type, c.body.size(), extraHeaders);
}

void CamWeb::undoPull(HttpConn& c) {
  CamSub* sub = c.undoSub ? events_.subs().find(c.undoSub) : nullptr;
  if (sub) sub->lastPull = c.undoState;
  c.undoSub = 0;
}

// Memory ran out while answering. Only this request fails: what it was building is let go, and it is told to try
// again. A hub that stopped here would miss alerts while it restarted, and a new firmware would be undone.
void CamWeb::shortOfMemory(HttpConn& c) {
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

std::string CamWeb::handleOnvif(HttpConn& c, const std::string& headers, const std::string& body, uint32_t now, int& status) {
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
  ctx.now = chip_->wallTime();
  if (!onvifPassword_.empty()) {
    ctx.snapshotToken = camSnapshotToken(cfg_.snapshotSecret, onvifPassword_);
    // ONVIF allows these before logging in. A client needs the clock to build its login.
    static const char* const OPEN[] = {"GetSystemDateAndTime", "GetCapabilities",      "GetServices", "GetServiceCapabilities",
                                       "GetHostname",          "GetEndpointReference", "GetWsdlUrl"};
    bool open = false;
    for (const char* name : OPEN) open = open || action == name;
    bool presented = !open && !xmlFirstText(body, "Password").empty(), allowed = logins_.allowed(c.peer, now);
    bool ok = open || (presented && allowed && camWsseValid(body, HUB_USER, onvifPassword_));
    if (presented && allowed) logins_.note(c.peer, ok, now);
    if (ok && presented) noteOnvifCode(body);
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
    reply = soapSubscribeReply(ctx, events_.subs().addPush(consumer, c.peer, now).id);
  } else if (action == "CreatePullPointSubscription") {
    reply = soapPullPointReply(ctx, events_.subs().addPull(c.peer, now).id);
  } else if (action == "PullMessages") {
    CamSub* sub = ownSub(events_.subs(), path, c.peer);
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
    CamSub* sub = ownSub(events_.subs(), path, c.peer);
    if (!sub) {
      status = 400;
      return soapFault("no such subscription from this address");
    }
    events_.subs().remove(sub->id);
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
void CamWeb::serveSnapshot(HttpConn& c) {
  video_.current(millis_());
  if (!video_.file().empty() && sendFile(c, camPreviewPath(video_.file()), "image/jpeg")) return;
  int id = 0;
  uint8_t direction = 0;
  bool delayed = false;
  bool on = alarm_.board().shown(millis_(), id, direction, delayed);
  const CamJpeg& builtin = CAM_SNAPSHOTS[builtinFrame(on, delayed)];
  sendBody(c, "image/jpeg", (const char*)builtin.data, builtin.len);
}

void CamWeb::servicePolls(uint32_t now) {
  int state = alarm_.board().anyActive() ? 1 : 0;
  for (HttpConn& c : http_) {
    if (c.fd < 0 || !c.pollWaiting) continue;
    CamSub* sub = events_.subs().find(c.pollSub);
    bool changed = sub && sub->lastPull != state;
    if (sub && !changed && (int32_t)(now - c.pollUntil) < 0) continue;
    bool initial = sub && sub->lastPull < 0;
    // The state counts as told only once the reply is ready. Without the memory for it, the request waits for the next
    // pass, still owed this change.
    try {
      SoapContext ctx;
      ctx.host = c.pollHost;
      ctx.now = chip_->wallTime();
      reply(c, 200, "application/soap+xml; charset=utf-8", soapEnvelope(soapPullMessagesReply(ctx, changed, state == 1, initial)));
    } catch (const std::bad_alloc&) {
      failures_.note();
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
bool CamWeb::knownHost(const std::string& header) const { return camKnownHost(header, localIp_(), cfg_.hostName, cfg_.setupAddress, extraHosts_); }

void CamWeb::setPassword(const std::string& password) {
  webPassword_ = password;
  onvifPassword_ = password;
  video_.setOnvifPassword(password);
}

void CamWeb::noteOnvifCode(const std::string& body) {
  std::string code = xmlFirstText(body, "Nonce") + "|" + xmlFirstText(body, "Created");
  uint64_t h = 1469598103934665603ull;  // FNV-1a, 64 bits
  for (char ch : code) h = (h ^ (uint8_t)ch) * 1099511628211ull;
  for (uint64_t seen : onvifCodes_)
    if (seen == h) {
      onvifRepeats_++;
      return;
    }
  onvifCodes_[onvifCodeNext_] = h;
  onvifCodeNext_ = (onvifCodeNext_ + 1) % 64;
}

// Checks the login and keeps count of wrong ones.
bool CamWeb::authorized(const HttpConn& c, const std::string& headers) {
  if (webPassword_.empty()) return true;
  std::string auth = headerValue(headers, "authorization");
  uint32_t now = millis_();
  if (auth.empty() || !logins_.allowed(c.peer, now)) return false;
  bool ok = camBasicValid(auth, HUB_USER, webPassword_);
  logins_.note(c.peer, ok, now);
  return ok;
}

void CamWeb::finishFirmware(HttpConn& c, bool ok) {
  if (c.nodeImage) {
    finishNodeImage(c, ok);
    return;
  }
  // The signature is checked before the update is kept: a firmware not signed with the project's key never runs.
  uint8_t key[32];
  bool signedOk = ok && s2oFirmwareKey(cfg_.firmwareKey, key) && firmwareSig_.good(key);
  // Nor one older than this, which may have a fault since fixed. Its number is signed with it, so it cannot be raised.
  bool current = signedOk && firmwareSig_.release() >= (int64_t)cfg_.firmwareRelease;
  bool done = current && chip_->firmwareEnd(true);
  if (!current) chip_->firmwareEnd(false);
  firmwareOwner_ = nullptr;
  c.firmwareActive = false;
  c.discardLeft = c.firmwareLeft;
  c.drainFrom = millis_();
  c.firmwareLeft = 0;
  c.out = done ? jsonOk(JSON_OK)
          : signedOk && !current
              ? jsonFail(400, "that firmware is older than this one, so nothing was changed; an older one goes on only over the cable")
          : ok && !signedOk ? jsonFail(400, "that firmware is not signed for this hub, so nothing was changed")
                            : jsonFail(400, "that is not valid firmware for this hub, so nothing was changed");
  c.answered = true;
}

void CamWeb::feedFirmware(HttpConn& c, const char* data, size_t n, uint32_t now) {
  size_t take = n < c.firmwareLeft ? n : c.firmwareLeft;
  bool written = true;
  if (c.nodeImage) {
    nodeSig_.take((const uint8_t*)data, take);  // the whole file is kept, signature and all, for the nodes to check
    written = !take || nodes_.images().write(data, take);
  } else {
    size_t image = firmwareSig_.take((const uint8_t*)data, take);  // the signature at the end is held back from flash
    written = !image || chip_->firmwareWrite((const uint8_t*)data, image);
  }
  c.firmwareLeft -= take;  // before finishing, so only the bytes still on their way are read and dropped
  if (!written) {
    finishFirmware(c, false);
    return;
  }
  c.openedAt = now;  // the time limit is for a stalled upload, not a long one. A later clock reading here would make the elapsed time wrap.
  if (!c.firmwareLeft) finishFirmware(c, true);
}

bool CamWeb::handleHttp(HttpConn& c, uint32_t now) {
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
    c.drainFrom = now;
    return true;
  };
  // The same for a missing login, on the requests that are acted on before their body has arrived.
  auto needLogin = [&](size_t drainLimit) {
    c.out = UNAUTHORIZED;
    c.answered = true;
    size_t got = c.in.size() - (headEnd + 4);
    c.discardLeft = contentLength > got && contentLength <= drainLimit ? contentLength - got : 0;
    c.drainFrom = now;
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
    return refuse(421, "the hub does not answer to this name; open it by its IP address and add the name under Network > HTTP > Other names");
  if (isApi && !isGet) {  // a browser names the site a request came from, so another site cannot use this browser's login
    std::string origin = headerValue(headers, "origin");
    if (!origin.empty() && origin != "http://" + headerValue(headers, "host")) return refuse(403, "request from another site refused");
  }
  if (method == "PUT" && target == "/api/firmware") {  // goes straight to flash, because it is far larger than memory
    if (!authorized(c, headers)) return needLogin(MAX_FIRMWARE);
    // A refused firmware is read to its end before the reply, as the browser reports a reply that comes while it is
    // still sending as a bare network error, without the reason.
    if (!contentLength || contentLength > MAX_FIRMWARE) return refuse(413, "that file is not the right size for firmware", MAX_FIRMWARE);
    if (contentLength <= S2O_FW_TRAILER) return refuse(400, "that firmware is not signed for this hub, so nothing was changed", MAX_FIRMWARE);
    if (firmwareOwner_ || !chip_->firmwareBegin(contentLength - S2O_FW_TRAILER))
      return refuse(507, "could not start the update; the hub may be short of memory, so restart it and try again", MAX_FIRMWARE);
    firmwareOwner_ = &c;
    firmwareSig_.begin(contentLength);
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
  if (isApi && !borrowsFrame && contentLength > (target == "/api/restore" ? 16384 : 2048)) return refuse(413, "request is too large");
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

  events_.subs().heard(c.peer, now);

  int status = 200;
  if (isApi) {
    handleApi(c, method, target, headers, c.toFrameBuffer ? video_.buffer().data() : c.in.data() + headEnd + 4, contentLength);
    if (c.toFrameBuffer) {
      uploading_ = nullptr;
      c.toFrameBuffer = false;
      video_.giveBack();
    }
  } else if (isSnapshot) {
    if (!onvifPassword_.empty() && !camSameText(queryParam(target, "token"), camSnapshotToken(cfg_.snapshotSecret, onvifPassword_)))
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
  memcpy(out, framed.data() + within, take);  // NOLINT(bugprone-not-null-terminated-result): bytes for the socket, not a string
  return take;
}

void CamWeb::serviceHttp(HttpConn& c, bool readable, bool writable, uint32_t now) {
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
        failures_.note();
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
  bool stalled = !c.pollWaiting && now - c.openedAt > CAM_HTTP_STALL_MS;
  bool trickling = c.discardLeft && now - c.drainFrom > drainMs_;  // still sending the body of a refused request
  if (stalled || trickling) closeHttp(c);
}

// ---- what the rest of the hub asks of the web side ----

bool CamWeb::downloadingHistory() const {
  for (const HttpConn& c : http_)
    if (c.fd >= 0 && c.stream == &csvRead_) return true;
  return false;
}

std::string CamWeb::handleSensor(const std::string& target, int& status) {
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
