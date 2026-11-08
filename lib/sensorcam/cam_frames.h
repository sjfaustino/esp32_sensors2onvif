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

const int CAM_DIRECTIONS = 5;

// Built-in pictures: [0] "all clear" and [1] a generic alert. They are used when LittleFS has no
// file to show, and [0]'s SPS is the format every stored picture must match.
extern const CamFrame CAM_FRAMES[];
extern const int CAM_WIDTH;
extern const int CAM_HEIGHT;
extern const int CAM_FPS;
