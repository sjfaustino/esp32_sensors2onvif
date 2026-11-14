#include "cam_x25519.h"
#include <string.h>

// A number below 2^255 - 19 as 16 limbs of 16 bits, each kept in 64 bits so products and carries fit.
typedef int64_t Field[16];

static const Field K121665 = {0xDB41, 1};

static void carry(Field o) {
  for (int i = 0; i < 16; i++) {
    o[i] += (int64_t)1 << 16;
    int64_t c = o[i] >> 16;
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c * ((int64_t)1 << 16);
  }
}

// Swaps p and q when b is 1, in the same time either way.
static void swap(Field p, Field q, int b) {
  int64_t mask = ~((int64_t)b - 1);
  for (int i = 0; i < 16; i++) {
    int64_t t = mask & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}

static void pack(uint8_t* o, const Field n) {
  Field m, t;
  for (int i = 0; i < 16; i++) t[i] = n[i];
  carry(t);
  carry(t);
  carry(t);
  for (int j = 0; j < 2; j++) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; i++) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    int b = (int)((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    swap(t, m, 1 - b);
  }
  for (int i = 0; i < 16; i++) {
    o[2 * i] = (uint8_t)(t[i] & 0xff);
    o[2 * i + 1] = (uint8_t)(t[i] >> 8);
  }
}

static void unpack(Field o, const uint8_t* n) {
  for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
  o[15] &= 0x7fff;
}

static void add(Field o, const Field a, const Field b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

static void sub(Field o, const Field a, const Field b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void mul(Field o, const Field a, const Field b) {
  int64_t t[31];
  for (int i = 0; i < 31; i++) t[i] = 0;
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; i++) o[i] = t[i];
  carry(o);
  carry(o);
}

static void square(Field o, const Field a) { mul(o, a, a); }

static void invert(Field o, const Field in) {
  Field c;
  for (int i = 0; i < 16; i++) c[i] = in[i];
  for (int a = 253; a >= 0; a--) {
    square(c, c);
    if (a != 2 && a != 4) mul(c, c, in);
  }
  for (int i = 0; i < 16; i++) o[i] = c[i];
}

void camX25519(uint8_t out[CAM_X25519_LEN], const uint8_t scalar[CAM_X25519_LEN], const uint8_t point[CAM_X25519_LEN]) {
  uint8_t z[32];
  memcpy(z, scalar, 32);
  z[31] = (uint8_t)((scalar[31] & 127) | 64);
  z[0] &= 248;
  Field x, a, b, c, d, e, f;
  unpack(x, point);
  for (int i = 0; i < 16; i++) {
    b[i] = x[i];
    d[i] = a[i] = c[i] = 0;
  }
  a[0] = d[0] = 1;
  for (int i = 254; i >= 0; --i) {  // the Montgomery ladder, the same steps whatever the key
    int r = (z[i >> 3] >> (i & 7)) & 1;
    swap(a, b, r);
    swap(c, d, r);
    add(e, a, c);
    sub(a, a, c);
    add(c, b, d);
    sub(b, b, d);
    square(d, e);
    square(f, a);
    mul(a, c, a);
    mul(c, b, e);
    add(e, a, c);
    sub(a, a, c);
    square(b, a);
    sub(c, d, f);
    mul(a, c, K121665);
    add(a, a, d);
    mul(c, c, a);
    mul(a, d, f);
    mul(d, b, x);
    square(b, e);
    swap(a, b, r);
    swap(c, d, r);
  }
  invert(c, c);
  mul(a, a, c);
  pack(out, a);
}

void camX25519Public(uint8_t out[CAM_X25519_LEN], const uint8_t privateKey[CAM_X25519_LEN]) {
  static const uint8_t BASE[32] = {9};
  camX25519(out, privateKey, BASE);
}
