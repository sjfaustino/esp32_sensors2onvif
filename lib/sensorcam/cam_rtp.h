#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include "cam_frames.h"

struct RtpState {
  uint16_t seq = 0;
  uint32_t ts = 0;
  bool started = false;
  uint32_t startMs = 0;
};

// Where a stream is in the frame it is sending. Packets are made one at a time straight from the frame, so a frame is
// never copied: a 32 KB copy per viewer is more memory than a board without PSRAM can spare.
struct RtpSend {
  const CamFrame* frame = nullptr;  // null between frames
  int nal = 0;
  size_t offset = 0;  // bytes of that NAL already sent
};

const size_t RTP_PACKET_MAX = 4 + 12 + 2 + 1400;  // '$' framing, RTP header, FU-A header, payload

// Starts a frame. The timestamp follows the clock from the first frame, so frames skipped for a slow viewer leave a gap
// instead of slowing the stream's clock.
void rtpStartFrame(RtpSend& s, const CamFrame& frame, RtpState& st, uint32_t nowMs);
// Writes the frame's next packet, as RTP over RTSP-interleaved TCP, into out (RTP_PACKET_MAX bytes). Gives its length,
// or 0 once the frame has been sent.
size_t rtpNextPacket(RtpSend& s, RtpState& st, int channel, uint8_t* out);
// The whole frame at once, for the tests.
void rtpAppendFrame(const CamFrame& frame, RtpState& st, int channel, uint32_t nowMs, std::string& out);

std::string base64(const uint8_t* data, size_t len);
std::string camHex(const uint8_t* data, size_t len);  // lowercase
std::string cameraSdp();
