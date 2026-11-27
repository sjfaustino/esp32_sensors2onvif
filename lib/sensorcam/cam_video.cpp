// The RTSP connections that carry the video to the NVR, and the pictures they send.
#include <stdlib.h>
#include <string.h>
#include <new>
#include "cam_auth.h"
#include "cam_number.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_video.h"
#include "cam_server_internal.h"

// ---- RTSP ----

bool CamVideo::begin(int listenFd) {
  listen_ = listenFd;
  nextFrameAt_ = millis_();
  try {
    frameData_.reserve(cfg_.pictureMax);
  } catch (const std::bad_alloc&) {  // never a reason not to start: a picture larger than what was reserved is refused
    failures_.note();
    frameData_.reserve(CAM_MAX_FRAME);
  }
  return listen_ >= 0;
}

void CamVideo::watch(fd_set& rd, fd_set& wr, int& maxFd, bool accepting) const {
  if (listen_ >= 0 && accepting) netWatch(listen_, rd, maxFd);
  for (const RtspConn& c : rtsp_) {
    if (c.fd < 0) continue;
    netWatch(c.fd, rd, maxFd);
    if (!c.out.empty() || c.packetPos < c.packetLen || c.video.frame) netWatch(c.fd, wr, maxFd);
  }
}

void CamVideo::serve(fd_set& rd, fd_set& wr, uint32_t now) {
  if (listen_ >= 0 && FD_ISSET(listen_, &rd)) acceptViewer(now);
  for (RtspConn& c : rtsp_)
    if (c.fd >= 0) serviceConn(c, FD_ISSET(c.fd, &rd), FD_ISSET(c.fd, &wr), now);
}

void CamVideo::closeAll() {
  for (RtspConn& c : rtsp_)
    if (c.fd >= 0) closeConn(c);
}

int CamVideo::openCount() const {
  int open = 0;
  for (const RtspConn& c : rtsp_) open += c.fd >= 0;
  return open;
}

bool CamVideo::playingTo(uint32_t peer) const {
  for (const RtspConn& c : rtsp_)
    if (peer && c.fd >= 0 && c.playing && c.peer == peer) return true;
  return false;
}

void CamVideo::viewers(CamJson& j) const {
  for (const RtspConn& c : rtsp_)
    if (c.fd >= 0 && c.playing) j.str(netAddressText(c.peer));
}

// Sends what the connection can take: the rest of the packet under way, then any RTSP reply, then the frame's next
// packets. A reply never goes between the parts of a packet, which would break the stream's framing.
void CamVideo::flush(RtspConn& c, uint32_t now) {
  for (;;) {
    bool inPacket = c.packetPos < c.packetLen;
    if (!inPacket && c.outPos >= c.out.size() && !c.video.frame) return;
    if (!inPacket && c.outPos >= c.out.size()) {
      c.packetLen = rtpNextPacket(c.video, c.rtp, c.channel, c.packet);
      c.packetPos = 0;
      continue;
    }
    const char* data = inPacket ? (const char*)c.packet + c.packetPos : c.out.data() + c.outPos;
    size_t left = inPacket ? c.packetLen - c.packetPos : c.out.size() - c.outPos;
    int n = (int)send(c.fd, data, (int)left, 0);
    if (n < 0 && !netWouldBlock()) {
      closeConn(c);
      return;
    }
    if (n <= 0) return;
    c.activityAt = now;
    if (inPacket) {
      c.packetPos += n;
    } else {
      c.outPos += n;
      if (c.outPos >= c.out.size()) {
        c.out.clear();
        c.outPos = 0;
        if (c.closeAfterSend) {
          closeConn(c);
          return;
        }
      }
    }
    if ((size_t)n < left) return;  // the network stack is full; the rest goes when it drains
  }
}

void CamVideo::request(RtspConn& c, const std::string& text, uint32_t now) {
  std::string line = text.substr(0, text.find("\r\n"));
  std::string method = line.substr(0, line.find(' '));
  size_t a = line.find(' '), b = line.rfind(' ');
  std::string uri = a != std::string::npos && b > a ? line.substr(a + 1, b - a - 1) : "";
  std::string cseq = headerValue(text, "cseq");
  static const char SESSION[] = "53454E53";
  std::string head = "RTSP/1.0 200 OK\r\nCSeq: " + cseq + "\r\nServer: SensorCam\r\n";
  events_.subs().heard(c.peer, now);

  if (!onvifPassword_.empty() && method != "OPTIONS" && !c.authed) {
    if (c.nonce.empty()) c.nonce = camMd5Hex(cfg_.serial + std::to_string(now) + std::to_string(c.fd));
    std::string auth = headerValue(text, "authorization");
    bool allowed = logins_.allowed(c.peer, now);
    bool ok =
        allowed && (camDigestValid(auth, method, HUB_USER, onvifPassword_, RTSP_REALM, c.nonce) || camBasicValid(auth, HUB_USER, onvifPassword_));
    if (!auth.empty() && allowed) logins_.note(c.peer, ok, now);
    if (ok) {
      c.authed = true;
    } else {
      c.out += "RTSP/1.0 401 Unauthorized\r\nCSeq: " + cseq + "\r\nWWW-Authenticate: Digest realm=\"" + RTSP_REALM + "\", nonce=\"" + c.nonce +
               "\"\r\nWWW-Authenticate: Basic realm=\"" + RTSP_REALM + "\"\r\n\r\n";
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
    int channel = 0;  // the first of the pair, which the reply repeats; 255 is left for the second
    if (t.find("TCP") == std::string::npos && t.find("tcp") == std::string::npos) {
      // TCP only: clients fall back to interleaved on 461, and the NVR uses TCP anyway.
      c.out += "RTSP/1.0 461 Unsupported Transport\r\nCSeq: " + cseq + "\r\n\r\n";
    } else if (at != std::string::npos && !camNumberIn(t.substr(at + 12, t.find_first_of("-;", at + 12) - (at + 12)), 0, 254, channel)) {
      c.out += "RTSP/1.0 400 Bad Request\r\nCSeq: " + cseq + "\r\n\r\n";
    } else {
      c.channel = channel;
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

void CamVideo::serviceConn(RtspConn& c, bool readable, bool writable, uint32_t now) {
  if (readable) {
    char buf[1024];
    int n = (int)recv(c.fd, buf, sizeof(buf), 0);
    if (n == 0 || (n < 0 && !netWouldBlock())) {
      closeConn(c);
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
        // A client sending requests without reading the replies would pile them up in memory, and needs no login for
        // that. The rest waits until it reads; should it go on sending, the requests grow past MAX_HEADERS and it is closed.
        if (c.out.size() - c.outPos > MAX_UNREAD_REPLIES) break;
        size_t end = c.in.find("\r\n\r\n");
        if (end == std::string::npos) break;
        try {
          request(c, c.in.substr(0, end + 2), now);
        } catch (const std::bad_alloc&) {  // the viewer reconnects, as it does after any dropped connection
          failures_.note();
          closeConn(c);
          return;
        }
        c.in.erase(0, end + 4);
      }
      if (c.in.size() > MAX_HEADERS) {
        closeConn(c);
        return;
      }
    }
  }
  if (c.fd >= 0 && (writable || !c.out.empty())) flush(c, now);  // a new reply is tried at once
  // Not playing, a connection's time runs from its opening, not from what it last sent: anyone could otherwise keep
  // both by sending a request now and then without the password, and keep the NVR's video out.
  if (c.fd >= 0 && (c.playing ? now - c.activityAt > CAM_RTSP_STALL_MS : now - c.openedAt > CAM_RTSP_IDLE_MS)) closeConn(c);
}

void CamVideo::tick(uint32_t now) {
  if ((int32_t)(now - nextFrameAt_) < 0) return;
  nextFrameAt_ = now + 1000 / CAM_FPS;
  uint32_t seq = liveSeq_;
  show(now);
  // A moving picture's next frame, unless it was chosen just now and starts at its first. Its frames are keyframes, at
  // the same rate as a still picture's, so the NVR gets what it always has, only changing.
  if (seq == liveSeq_ && frame_ && isLoaded(frame_) && frameCount_ > 1) {
    frameAt_ = (frameAt_ + 1) % frameCount_;
    frame_ = &frames_[frameAt_];
    liveSeq_++;
  }
  const CamFrame& frame = *frame_;
  if (!chip_->hasHeadroom()) return;  // every frame stands alone, so skipping some lets the network stack drain
  for (RtspConn& c : rtsp_) {
    // Every frame is independently decodable, so a viewer still sending the last one just skips this one.
    if (c.fd < 0 || !c.playing || c.video.frame || c.packetPos < c.packetLen || !c.out.empty()) continue;
    rtpStartFrame(c.video, frame, c.rtp, now);
    flush(c, now);
  }
}

// ---- pictures, driven by the folders on storage ----

std::string& CamVideo::lend() {
  releasePicture();
  frame_ = nullptr;
  lent_ = true;
  return frameData_;
}

bool CamVideo::loadFrame(const std::string& path) {
  // frameData_ is one fixed buffer reserved at boot. Allocating per picture is what exhausted RAM.
  releasePicture();
  frameCount_ = frameAt_ = 0;
  if (!storage_.readFile(path, frameData_, frameData_.capacity())) return false;  // what was reserved, so never a new block
  int n = camParsePicture((const uint8_t*)frameData_.data(), frameData_.size(), pictureNals_, camPictureUnits(CAM_MAX_FRAMES));
  if (!n || !camSameNal(pictureNals_[0], CAM_FRAMES[0].nals[0])) return false;
  for (int i = 0; i < n; i++) {
    frameNals_[i][0] = pictureNals_[0];
    frameNals_[i][1] = pictureNals_[1 + 2 * i];
    frameNals_[i][2] = pictureNals_[2 + 2 * i];
    frames_[i] = CamFrame{frameNals_[i], 3};
  }
  frameCount_ = n;
  return true;
}

// A missing picture falls back to the nearest one that exists, then to the built-in pictures.
const CamFrame& CamVideo::show(uint32_t now) {
  int id = 0;
  uint8_t direction = 0;
  bool delayed = false;
  bool on = alarm_.board().shown(now, id, direction, delayed);
  if (lent_) {  // an upload is using the picture buffer
    if (!frameKey_.empty()) liveSeq_++;
    frameKey_.clear();
    frameFile_.clear();
    frame_ = &CAM_FRAMES[builtinFrame(on, delayed)];
    return *frame_;
  }
  std::string key =
      on ? table_.slotName(id) + "/" + std::to_string(direction) + (delayed ? "d" : "") + (alarm_.board().several(id) ? "s" : "") : "clear";
  if (key == frameKey_) return *frame_;
  frameKey_ = key;
  frameFile_.clear();
  liveSeq_++;

  std::vector<std::string> candidates;
  bool several = on && alarm_.board().several(id);
  if (several) {
    // Stands for several sensors' past events, so no one sensor's picture fits: the built-in one says to check the logs.
  } else if (on) {
    // A past event never borrows the live picture, which would make it look like it is happening now.
    std::string base = table_.slotName(id) + "/";
    candidates = {base + camStateFile(direction, delayed), base + camStateFile(0, delayed)};
  } else {
    candidates = {"clear.h264"};
  }
  frame_ = &CAM_FRAMES[builtinFrame(on, delayed)];
  for (const std::string& path : candidates) {
    if (loadFrame(path)) {
      frame_ = &frames_[0];
      frameFile_ = path;
      break;
    }
  }
  return *frame_;
}

// Streams partway through the loaded picture drop the rest of that frame; each frame stands alone, so the next one plays
// as normal. A download of it stops, and the page asks again.
void CamVideo::releasePicture() {
  pictureGen_++;
  for (RtspConn& c : rtsp_)
    if (isLoaded(c.video.frame)) c.video.frame = nullptr;  // the packet already made is a copy, and still goes out whole
}

void CamVideo::closeConn(RtspConn& c) {
  if (c.fd >= 0) netClose(c.fd);
  c = RtspConn();
}

void CamVideo::acceptViewer(uint32_t now) {
  sockaddr_in a{};
  socklen_t len = sizeof(a);
  int fd = (int)accept(listen_, (sockaddr*)&a, &len);
  if (fd < 0) return;
  if (!chip_->hasHeadroom()) {
    netClose(fd);
    return;
  }
  RtspConn* place = nullptr;
  for (RtspConn& c : rtsp_)
    if (c.fd < 0) {
      place = &c;
      break;
    }
  if (!place && nvr_ && a.sin_addr.s_addr == nvr_) {  // full: the NVR takes the place of the oldest that is not its own
    for (RtspConn& c : rtsp_)
      if (c.peer != nvr_ && (!place || now - c.openedAt > now - place->openedAt)) place = &c;
    if (place) {
      closeConn(*place);
      nvrLetIn_++;
    }
  }
  if (!place) {
    netClose(fd);
    return;
  }
  netNonblock(fd);
  place->fd = fd;
  place->peer = a.sin_addr.s_addr;
  place->activityAt = place->openedAt = now;
}

int CamVideo::streamCount() const {
  int streams = 0;
  for (const RtspConn& c : rtsp_) streams += c.playing;
  return streams;
}
