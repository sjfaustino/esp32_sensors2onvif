#include "cam_rtp.h"
#include <string.h>

static const size_t RTP_MTU = 1400;
static const uint32_t SSRC = 0x53454E53;

static size_t writePacket(uint8_t* out, RtpState& st, int channel, bool marker, const uint8_t* head, size_t headLen,
                          const uint8_t* body, size_t bodyLen) {
  size_t rtpLen = 12 + headLen + bodyLen;
  uint8_t h[16] = {'$',
                   (uint8_t)channel,
                   (uint8_t)(rtpLen >> 8),
                   (uint8_t)rtpLen,
                   0x80,
                   (uint8_t)(96 | (marker ? 0x80 : 0)),
                   (uint8_t)(st.seq >> 8),
                   (uint8_t)st.seq,
                   (uint8_t)(st.ts >> 24),
                   (uint8_t)(st.ts >> 16),
                   (uint8_t)(st.ts >> 8),
                   (uint8_t)st.ts,
                   (uint8_t)(SSRC >> 24),
                   (uint8_t)(SSRC >> 16),
                   (uint8_t)(SSRC >> 8),
                   (uint8_t)SSRC};
  st.seq++;
  memcpy(out, h, sizeof(h));
  if (headLen) memcpy(out + sizeof(h), head, headLen);
  memcpy(out + sizeof(h) + headLen, body, bodyLen);
  return sizeof(h) + headLen + bodyLen;
}

void rtpStartFrame(RtpSend& s, const CamFrame& frame, RtpState& st, uint32_t nowMs) {
  if (!st.started) {
    st.started = true;
    st.startMs = nowMs;
  }
  st.ts = (nowMs - st.startMs) * 90u;  // the 90 kHz video clock; it wraps smoothly, as RTP expects
  s.frame = frame.count ? &frame : nullptr;
  s.nal = 0;
  s.offset = 0;
}

size_t rtpNextPacket(RtpSend& s, RtpState& st, int channel, uint8_t* out) {
  if (!s.frame) return 0;
  const CamNal& n = s.frame->nals[s.nal];
  bool lastNal = s.nal == s.frame->count - 1;
  size_t len;
  bool nalDone;
  if (n.len <= RTP_MTU) {
    len = writePacket(out, st, channel, lastNal, nullptr, 0, n.data, n.len);
    nalDone = true;
  } else {  // FU-A fragmentation: the NAL header byte becomes the FU indicator and header
    if (s.offset == 0) s.offset = 1;
    size_t left = n.len - s.offset, part = left > RTP_MTU ? RTP_MTU : left;
    nalDone = part == left;
    uint8_t fu[2] = {(uint8_t)((n.data[0] & 0xE0) | 28), (uint8_t)((n.data[0] & 0x1F) | (s.offset == 1 ? 0x80 : 0) | (nalDone ? 0x40 : 0))};
    len = writePacket(out, st, channel, lastNal && nalDone, fu, 2, n.data + s.offset, part);
    s.offset += part;
  }
  if (nalDone) {
    s.offset = 0;
    if (++s.nal >= s.frame->count) s.frame = nullptr;
  }
  return len;
}

void rtpAppendFrame(const CamFrame& frame, RtpState& st, int channel, uint32_t nowMs, std::string& out) {
  RtpSend s;
  rtpStartFrame(s, frame, st, nowMs);
  uint8_t packet[RTP_PACKET_MAX];
  while (size_t len = rtpNextPacket(s, st, channel, packet)) out.append((const char*)packet, len);
}

std::string base64(const uint8_t* d, size_t len) {
  static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = d[i] << 16;
    if (i + 1 < len) v |= d[i + 1] << 8;
    if (i + 2 < len) v |= d[i + 2];
    o += t[(v >> 18) & 63];
    o += t[(v >> 12) & 63];
    o += i + 1 < len ? t[(v >> 6) & 63] : '=';
    o += i + 2 < len ? t[v & 63] : '=';
  }
  return o;
}

std::string camHex(const uint8_t* d, size_t len) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < len; i++) {
    out += digits[d[i] >> 4];
    out += digits[d[i] & 15];
  }
  return out;
}

std::string cameraSdp() {
  const CamNal& sps = CAM_FRAMES[0].nals[0];
  const CamNal& pps = CAM_FRAMES[0].nals[1];
  std::string profile = camHex(sps.data + 1, 3);
  return std::string("v=0\r\no=- 0 0 IN IP4 0.0.0.0\r\ns=SensorCam\r\nt=0 0\r\na=control:*\r\nm=video 0 RTP/AVP 96\r\n") +
         "c=IN IP4 0.0.0.0\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1;profile-level-id=" + profile +
         ";sprop-parameter-sets=" + base64(sps.data, sps.len) + "," + base64(pps.data, pps.len) +
         "\r\na=framerate:" + std::to_string(CAM_FPS) + "\r\na=control:track1\r\n";
}
