#include "cam_rtp.h"

static const size_t RTP_MTU = 1400;
static const uint32_t SSRC = 0x53454E53;

static void appendPacket(std::string& out, RtpState& st, int channel, bool marker, const uint8_t* head, size_t headLen,
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
  out.append((const char*)h, sizeof(h));
  if (headLen) out.append((const char*)head, headLen);
  out.append((const char*)body, bodyLen);
}

void rtpAppendFrame(const CamFrame& frame, RtpState& st, int channel, int fps, std::string& out) {
  for (int i = 0; i < frame.count; i++) {
    const CamNal& n = frame.nals[i];
    bool lastNal = i == frame.count - 1;
    if (n.len <= RTP_MTU) {
      appendPacket(out, st, channel, lastNal, nullptr, 0, n.data, n.len);
      continue;
    }
    // FU-A fragmentation
    uint8_t indicator = (n.data[0] & 0xE0) | 28;
    uint8_t kind = n.data[0] & 0x1F;
    const uint8_t* p = n.data + 1;
    size_t left = n.len - 1;
    bool first = true;
    while (left) {
      size_t part = left > RTP_MTU ? RTP_MTU : left;
      bool end = part == left;
      uint8_t fu[2] = {indicator, (uint8_t)(kind | (first ? 0x80 : 0) | (end ? 0x40 : 0))};
      appendPacket(out, st, channel, lastNal && end, fu, 2, p, part);
      p += part;
      left -= part;
      first = false;
    }
  }
  st.ts += 90000 / fps;
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

std::string cameraSdp() {
  const CamNal& sps = CAM_FRAMES[0].nals[0];
  const CamNal& pps = CAM_FRAMES[0].nals[1];
  char profile[8];
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 3; i++) {
    profile[i * 2] = hex[sps.data[i + 1] >> 4];
    profile[i * 2 + 1] = hex[sps.data[i + 1] & 15];
  }
  profile[6] = 0;
  return std::string("v=0\r\no=- 0 0 IN IP4 0.0.0.0\r\ns=SensorCam\r\nt=0 0\r\na=control:*\r\nm=video 0 RTP/AVP 96\r\n") +
         "c=IN IP4 0.0.0.0\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1;profile-level-id=" + profile +
         ";sprop-parameter-sets=" + base64(sps.data, sps.len) + "," + base64(pps.data, pps.len) +
         "\r\na=framerate:" + std::to_string(CAM_FPS) + "\r\na=control:track1\r\n";
}
