#pragma once
#include <stdint.h>
#include <string>
#include "cam_frames.h"

struct RtpState {
  uint16_t seq = 0;
  uint32_t ts = 0;
  bool started = false;
  uint32_t startMs = 0;
};

// Appends one frame as RTP over RTSP-interleaved TCP ('$' framing). The timestamp follows the clock from the first
// frame, so frames skipped for a slow viewer leave a gap instead of slowing the stream's clock.
void rtpAppendFrame(const CamFrame& frame, RtpState& st, int channel, uint32_t nowMs, std::string& out);

std::string base64(const uint8_t* data, size_t len);
std::string camHex(const uint8_t* data, size_t len);  // lowercase
std::string cameraSdp();
