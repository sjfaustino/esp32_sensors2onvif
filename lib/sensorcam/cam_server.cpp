#include "cam_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"

static const size_t MAX_REQUEST = 16 * 1024;
static const uint32_t HTTP_TIMEOUT_MS = 10000;
static const uint32_t RTSP_IDLE_MS = 120000;
static const uint32_t RTSP_STALL_MS = 30000;
static const uint32_t NOTIFY_SEND_MS = 2000;
static const uint32_t NOTIFY_CLOSE_MS = 1000;
static const uint32_t REPEAT_TRUE_MS = 1000;
static const char MULTICAST_GROUP[] = "239.255.255.250";
static const uint16_t MULTICAST_PORT = 3702;

SensorCamServer::SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn)
    : cfg_(cfg), localIp_(localIp), millis_(millisFn), sensors_(CAM_SENSOR_COUNT, cfg.holdMs), signal_(REPEAT_TRUE_MS) {}

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
  rtspListen_ = listenOn(cfg_.rtspPort);
  openDiscovery();
  nextFrameAt_ = millis_();
  return httpListen_ >= 0 && rtspListen_ >= 0;
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
  if (c.fd >= 0) netClose(c.fd);
  c = HttpConn();
}

void SensorCamServer::closeRtsp(RtspConn& c) {
  if (c.fd >= 0) netClose(c.fd);
  c = RtspConn();
}

void SensorCamServer::acceptHttp(uint32_t now) {
  sockaddr_in a{};
  socklen_t len = sizeof(a);
  int fd = (int)accept(httpListen_, (sockaddr*)&a, &len);
  if (fd < 0) return;
  for (HttpConn& c : http_) {
    if (c.fd >= 0) continue;
    netNonblock(fd);
    c.fd = fd;
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
  const char* text = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 403 ? "Forbidden" : "Not Found";
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

bool SensorCamServer::sensorInput(int id, bool active) {
  return active ? sensors_.trigger(id, millis_()) : sensors_.clear(id);
}

std::string SensorCamServer::handleSensor(const std::string& target, int& status) {
  if (!cfg_.sensorToken.empty() && queryParam(target, "token") != cfg_.sensorToken) {
    status = 403;
    return "{\"ok\":false,\"error\":\"bad token\"}\n";
  }
  int id = atoi(queryParam(target, "id").c_str());
  std::string state = queryParam(target, "state");
  bool ok = sensorInput(id, !(state == "clear" || state == "off"));
  status = ok ? 200 : 404;
  return ok ? "{\"ok\":true}\n" : "{\"ok\":false,\"error\":\"unknown sensor\"}\n";
}

std::string SensorCamServer::statusJson(uint32_t now) {
  std::string active;
  for (int id = 1; id <= sensors_.count(); id++)
    if (sensors_.active(id)) active += (active.empty() ? "" : ",") + std::to_string(id);
  int streams = 0;
  for (RtspConn& c : rtsp_) streams += c.playing;
  return "{\"sensors\":" + std::to_string(sensors_.count()) + ",\"active\":[" + active + "],\"subscribers\":" +
         std::to_string(subs_.count()) + ",\"streams\":" + std::to_string(streams) + ",\"uptime_s\":" + std::to_string(now / 1000) + "}\n";
}

bool SensorCamServer::handleHttp(HttpConn& c, uint32_t now) {
  size_t headEnd = c.in.find("\r\n\r\n");
  if (headEnd == std::string::npos) return false;
  std::string headers = c.in.substr(0, headEnd);
  size_t contentLength = (size_t)strtoul(headerValue(headers, "content-length").c_str(), nullptr, 10);
  if (contentLength > MAX_REQUEST) {
    closeHttp(c);
    return false;
  }
  if (c.in.size() < headEnd + 4 + contentLength) return false;

  subs_.heard(c.peer, now);
  std::string target = headers.substr(0, headers.find("\r\n"));
  bool isGet = target.compare(0, 4, "GET ") == 0;
  size_t sp = target.find(' ');
  size_t sp2 = target.find(' ', sp + 1);
  target = target.substr(sp + 1, sp2 - sp - 1);

  int status = 200;
  if (isGet && target.compare(0, 7, "/sensor") == 0) {
    std::string body = handleSensor(target, status);
    c.out = httpResponse(status, "application/json", body);
  } else if (isGet && target.compare(0, 7, "/status") == 0) {
    c.out = httpResponse(200, "application/json", statusJson(now));
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
  if (readable && !c.answered) {
    char buf[1024];
    int n = (int)recv(c.fd, buf, sizeof(buf), 0);
    if (n == 0 || (n < 0 && !netWouldBlock())) {
      closeHttp(c);
      return;
    }
    if (n > 0) {
      c.in.append(buf, n);
      if (c.in.size() > MAX_REQUEST + 2048) {
        closeHttp(c);
        return;
      }
      handleHttp(c, now);
      if (c.fd < 0) return;
    }
  }
  if (c.answered && (writable || c.outPos == 0)) {
    int n = (int)send(c.fd, c.out.data() + c.outPos, (int)(c.out.size() - c.outPos), 0);
    if (n < 0 && !netWouldBlock()) {
      closeHttp(c);
      return;
    }
    if (n > 0) c.outPos += n;
    if (c.outPos >= c.out.size()) {
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
    c.out += head + "Session: " + SESSION + "\r\nRange: npt=0.000-\r\n\r\n";
    c.playing = true;
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
  const CamFrame& frame = CAM_FRAMES[sensors_.frameIndex(now)];
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
  if (rtspListen_ >= 0) add(rtspListen_, rd);
  if (udp_ >= 0) add(udp_, rd);
  for (HttpConn& c : http_) {
    if (c.fd < 0) continue;
    add(c.fd, rd);
    if (c.answered) add(c.fd, wr);
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
  if (maxFd >= 0 && select(maxFd + 1, &rd, &wr, nullptr, &tv) < 0) return;
  now = millis_();

  if (httpListen_ >= 0 && FD_ISSET(httpListen_, &rd)) acceptHttp(now);
  if (rtspListen_ >= 0 && FD_ISSET(rtspListen_, &rd)) acceptRtsp(now);
  if (udp_ >= 0 && FD_ISSET(udp_, &rd)) serviceDiscovery();
  for (HttpConn& c : http_)
    if (c.fd >= 0) serviceHttp(c, FD_ISSET(c.fd, &rd), FD_ISSET(c.fd, &wr), now);
  for (RtspConn& c : rtsp_)
    if (c.fd >= 0) serviceRtsp(c, FD_ISSET(c.fd, &rd), FD_ISSET(c.fd, &wr), now);
  for (NotifyIo& n : notify_)
    if (n.state != NotifyIo::IDLE) serviceNotify(n, FD_ISSET(n.fd, &rd), FD_ISSET(n.fd, &wr), now);
}
