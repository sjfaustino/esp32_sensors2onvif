#pragma once
#include <stddef.h>
#include <stdint.h>

// One H.264 NAL unit without a start code.
struct CamNal {
  const uint8_t* data;
  size_t len;
};

// SPS, PPS, IDR, in that order. Every frame is independently decodable.
struct CamFrame {
  const CamNal* nals;
  int count;
};

extern const CamFrame CAM_FRAMES[];  // [0] is "all clear"; [n] is sensor n active
extern const int CAM_FRAME_COUNT;
extern const int CAM_SENSOR_COUNT;
extern const int CAM_WIDTH;
extern const int CAM_HEIGHT;
extern const int CAM_FPS;
