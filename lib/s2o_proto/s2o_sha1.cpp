#include "s2o_sha1.h"
#include <string.h>

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void S2oSha1::block() {
  uint32_t w[80];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)buf_[i * 4] << 24 | (uint32_t)buf_[i * 4 + 1] << 16 | (uint32_t)buf_[i * 4 + 2] << 8 | buf_[i * 4 + 3];
  for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f = i < 20 ? (b & c) | (~b & d) : i < 40 ? b ^ c ^ d : i < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
    uint32_t k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
    uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rol(b, 30);
    b = a;
    a = t;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  used_ = 0;
}

void S2oSha1::add(const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  total_ += len;
  while (len) {
    size_t take = 64 - used_ < len ? 64 - used_ : len;
    memcpy(buf_ + used_, p, take);
    used_ += take;
    p += take;
    len -= take;
    if (used_ == 64) block();
  }
}

void S2oSha1::finish(uint8_t out[20]) {
  uint64_t bits = total_ * 8;
  buf_[used_++] = 0x80;
  if (used_ > 56) {
    memset(buf_ + used_, 0, 64 - used_);
    block();
  }
  memset(buf_ + used_, 0, 56 - used_);
  for (int i = 0; i < 8; i++) buf_[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
  block();
  for (int i = 0; i < 20; i++) out[i] = (uint8_t)(h_[i / 4] >> (24 - 8 * (i % 4)));
}

void s2oHmacSha1(const uint8_t* key, size_t keyLen, const uint8_t* a, size_t aLen, const uint8_t* b, size_t bLen, uint8_t out[20]) {
  uint8_t k[64] = {0};
  if (keyLen > 64) {
    S2oSha1 s;
    s.add(key, keyLen);
    s.finish(k);
  } else {
    memcpy(k, key, keyLen);
  }
  uint8_t pad[64], innerHash[20];
  for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
  S2oSha1 inner;
  inner.add(pad, 64);
  inner.add(a, aLen);
  inner.add(b, bLen);
  inner.finish(innerHash);
  for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
  S2oSha1 outer;
  outer.add(pad, 64);
  outer.add(innerHash, 20);
  outer.finish(out);
}
