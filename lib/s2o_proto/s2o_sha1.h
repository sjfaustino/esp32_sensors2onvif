// Shared by the hub and the sensor nodes (esp32_sensor_node), so both speak the same messages from one source.
// SHA-1 fed in pieces, and HMAC-SHA1 over two pieces, without using the heap.
#pragma once
#include <stddef.h>
#include <stdint.h>

class S2oSha1 {
 public:
  void add(const void* data, size_t len);
  void finish(uint8_t out[20]);

 private:
  void block();
  uint32_t h_[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  uint8_t buf_[64];
  size_t used_ = 0;
  uint64_t total_ = 0;
};

// HMAC-SHA1 over a then b.
void s2oHmacSha1(const uint8_t* key, size_t keyLen, const uint8_t* a, size_t aLen, const uint8_t* b, size_t bLen, uint8_t out[20]);
