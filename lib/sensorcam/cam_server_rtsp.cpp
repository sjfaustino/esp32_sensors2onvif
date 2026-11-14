// The RTSP connections that carry the video to the NVR, and the pictures they send.
#include <stdlib.h>
#include <string.h>
#include <new>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_server.h"
#include "cam_server_internal.h"

// ---- RTSP ----

// Sends what the connection can take: the rest of the packet under way, then any RTSP reply, then the frame's next
// packets. A reply never goes between the parts of a packet, which would break the stream's framing.
void SensorCamServer::flushRtsp(RtspConn& c, uint32_t now) {
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
      closeRtsp(c);
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
          closeRtsp(c);
          return;
        }
      }
    }
    if ((size_t)n < left) return;  // the network stack is full; the rest goes when it drains
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
    bool allowed = logins_.allowed(c.peer, now);
    bool ok = allowed && (camDigestValid(auth, method, HUB_USER, cfg_.onvifPassword, RTSP_REALM, c.nonce) || camBasicValid(auth, HUB_USER, cfg_.onvifPassword));
    if (!auth.empty() && allowed) logins_.note(c.peer, ok, now);
    if (ok) {
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
        try {
          handleRtspRequest(c, c.in.substr(0, end + 2), now);
        } catch (const std::bad_alloc&) {  // the viewer reconnects, as it does after any dropped connection
          memoryFailures_++;
          closeRtsp(c);
          return;
        }
        c.in.erase(0, end + 4);
      }
      if (c.in.size() > MAX_HEADERS) {
        closeRtsp(c);
        return;
      }
    }
  }
  if (c.fd >= 0 && (writable || !c.out.empty())) flushRtsp(c, now);  // a new reply is tried at once
  if (c.fd >= 0 && (now - c.activityAt > (c.playing ? CAM_RTSP_STALL_MS : CAM_RTSP_IDLE_MS))) closeRtsp(c);
}

void SensorCamServer::pushFrames(uint32_t now) {
  if ((int32_t)(now - nextFrameAt_) < 0) return;
  nextFrameAt_ = now + 1000 / CAM_FPS;
  const CamFrame& frame = frameToShow(now);
  if (!hasHeadroom()) return;  // every frame stands alone, so skipping some lets the network stack drain
  for (RtspConn& c : rtsp_) {
    // Every frame is independently decodable, so a viewer still sending the last one just skips this one.
    if (c.fd < 0 || !c.playing || c.video.frame || c.packetPos < c.packetLen || !c.out.empty()) continue;
    rtpStartFrame(c.video, frame, c.rtp, now);
    flushRtsp(c, now);
  }
}

// ---- pictures, driven by the folders on storage ----

bool SensorCamServer::loadFrame(const std::string& path) {
  // frameData_ is one fixed buffer reserved at boot. Allocating per picture is what exhausted RAM.
  releasePicture();
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
  std::string key = on ? slotName(id) + "/" + std::to_string(direction) + (delayed ? "d" : "") + (sensors_.several(id) ? "s" : "") : "clear";
  if (key == frameKey_) return *frame_;
  frameKey_ = key;
  frameFile_.clear();
  liveSeq_++;

  std::vector<std::string> candidates;
  bool several = on && sensors_.several(id);
  if (several) {
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

// Streams partway through the loaded picture drop the rest of that frame; each frame stands alone, so the next one plays
// as normal. A download of it stops, and the page asks again.
void SensorCamServer::releasePicture() {
  pictureGen_++;
  for (RtspConn& c : rtsp_)
    if (c.video.frame == &loadedFrame_) c.video.frame = nullptr;  // the packet already made is a copy, and still goes out whole
}

void SensorCamServer::closeRtsp(RtspConn& c) {
  if (c.fd >= 0) netClose(c.fd);
  c = RtspConn();
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

int SensorCamServer::streamCount() const {
  int streams = 0;
  for (const RtspConn& c : rtsp_) streams += c.playing;
  return streams;
}
