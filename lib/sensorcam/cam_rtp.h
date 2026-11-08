#pragma once
#include <stdint.h>
#include <string>
#include "cam_frames.h"

struct RtpState {
  uint16_t seq = 0;
  uint32_t ts = 0;
};

// Appends one frame as RTP over RTSP-interleaved TCP ('$' framing), and advances the timestamp.
void rtpAppendFrame(const CamFrame& frame, RtpState& st, int channel, int fps, std::string& out);

std::string base64(const uint8_t* data, size_t len);
std::string cameraSdp();
