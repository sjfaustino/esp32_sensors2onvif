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

// Small JPEGs of the two built-in pictures, in the same order as CAM_FRAMES.
struct CamJpeg {
  const uint8_t* data;
  size_t len;
};
extern const CamJpeg CAM_SNAPSHOTS[];

const int CAM_DIRECTIONS = 5;

// Built-in pictures: [0] "all clear", [1] a generic alert and [2] "past event, check logs". They are used when
// LittleFS has no file to show, and [0]'s SPS is the format every stored picture must match.
extern const CamFrame CAM_FRAMES[];
inline int builtinFrame(bool on, bool delayed) { return on ? (delayed ? 2 : 1) : 0; }
extern const int CAM_WIDTH;
extern const int CAM_HEIGHT;
extern const int CAM_FPS;
