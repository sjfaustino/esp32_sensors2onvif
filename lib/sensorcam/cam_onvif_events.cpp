// CamOnvifEvents: the NVR's event subscriptions, the pushes to them, and WS-Discovery.
#include "cam_onvif_events.h"
#include <time.h>
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_server_internal.h"

CamOnvifEvents::CamOnvifEvents(const CamConfig& cfg, const CamAlarm& alarm, std::function<std::string()> localIp)
    : cfg_(cfg), alarm_(alarm), localIp_(localIp), signal_(CAM_REPEAT_TRUE_MS) {}

void CamOnvifEvents::begin() {
  openDiscovery();
  subs_.protect(cfg_.nvrIp.empty() ? 0 : inet_addr(cfg_.nvrIp.c_str()));
}

void CamOnvifEvents::networkUp() {
  openDiscovery();
  if (cfg_.loopbackOnly || udp_ < 0) return;
  std::string hello = discoveryHello(localIp_(), cfg_.onvifPort, cfg_.serial, messageNo_++);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(MULTICAST_PORT);
  to.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
  sendto(udp_, hello.data(), (int)hello.size(), 0, (sockaddr*)&to, sizeof(to));
}

void CamOnvifEvents::tick(uint32_t now, int otherSockets, int maxShared) {
  if (signal_.update(alarm_.board().anyActive(), now)) subs_.markAllPending();
  for (int i = 0; i < SubTable::SIZE; i++) {  // so one failed push cannot leave the NVR counted as not listening
    CamSub& s = subs_.at(i);
    if (!s.used || !s.push || !s.pushFailed || now - s.retryAt < CAM_PUSH_RETRY_MS) continue;
    s.pending = true;
    s.retryAt = now;
  }
  for (int i = 0; i < SubTable::SIZE; i++) {
    CamSub& s = subs_.at(i);
    if (s.used && s.push && s.pending && notify_[i].idle() && otherSockets + pushCount() <= maxShared) startPush(i, now);
  }
}

void CamOnvifEvents::watch(fd_set& rd, fd_set& wr, int& maxFd) const {
  if (udp_ >= 0) netWatch(udp_, rd, maxFd);
  for (const CamPost& n : notify_) n.watch(rd, wr, maxFd);
}

void CamOnvifEvents::serve(fd_set& rd, fd_set& wr, uint32_t now) {
  if (udp_ >= 0 && FD_ISSET(udp_, &rd)) serviceDiscovery();
  for (int i = 0; i < SubTable::SIZE; i++) {
    CamPost& n = notify_[i];
    // Ended before the whole message was sent, the NVR did not get it.
    if (!n.idle() && n.service(FD_ISSET(n.fd(), &rd), FD_ISSET(n.fd(), &wr), now)) subs_.at(i).pushFailed = !n.delivered();
  }
}

int CamOnvifEvents::pushCount() const {
  int open = 0;
  for (const CamPost& n : notify_) open += n.fd() >= 0;
  return open;
}

bool CamOnvifEvents::nvrListening(uint32_t now) const {
  uint32_t nvr = cfg_.nvrIp.empty() ? 0 : inet_addr(cfg_.nvrIp.c_str());
  for (int i = 0; i < SubTable::SIZE; i++) {
    const CamSub& s = subs_.at(i);
    if (!s.used || (nvr && s.peer != nvr)) continue;
    if (s.push ? !s.pushFailed : now - s.pulledAt < CAM_PULL_ALIVE_MS) return true;  // other requests from it do not count
  }
  return false;
}

void CamOnvifEvents::openDiscovery() {
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

void CamOnvifEvents::startPush(int i, uint32_t now) {
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

void CamOnvifEvents::serviceDiscovery() {
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
