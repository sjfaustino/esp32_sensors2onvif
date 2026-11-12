#include "cam_auth.h"
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include "cam_rtp.h"
#include "cam_soap.h"

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

// The message plus 0x80, zeros and the bit length, in whole 64-byte blocks.
static std::string padded(const std::string& data, bool bigEndian) {
  std::string m = data;
  m += (char)0x80;
  while (m.size() % 64 != 56) m += (char)0;
  uint64_t bits = (uint64_t)data.size() * 8;
  for (int i = 0; i < 8; i++) m += (char)(bits >> (bigEndian ? 56 - 8 * i : 8 * i));
  return m;
}

std::string camHmacSha1(const std::string& key, const std::string& data) {
  std::string k = key.size() > 64 ? camSha1(key) : key;
  k.resize(64, '\0');
  std::string inner(64, '\0'), outer(64, '\0');
  for (int i = 0; i < 64; i++) {
    inner[i] = (char)(k[i] ^ 0x36);
    outer[i] = (char)(k[i] ^ 0x5c);
  }
  return camSha1(outer + camSha1(inner + data));
}

std::string camSha1(const std::string& data) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::string m = padded(data, true);
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      const uint8_t* p = (const uint8_t*)m.data() + off + i * 4;
      w[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
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
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  std::string out;
  for (uint32_t v : h)
    for (int s = 24; s >= 0; s -= 8) out += (char)(v >> s);
  return out;
}

std::string camMd5Hex(const std::string& data) {
  static const uint8_t S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                                4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  std::string m = padded(data, false);
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++) {
      const uint8_t* p = (const uint8_t*)m.data() + off + i * 4;
      w[i] = (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      uint32_t t = d;
      d = c;
      c = b;
      b = b + rol(a + f + K[i] + w[g], S[i]);
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
  }
  uint8_t bytes[16];
  for (int i = 0; i < 16; i++) bytes[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
  return camHex(bytes, sizeof(bytes));
}

std::string camBase64Decode(const std::string& text) {
  std::string out;
  uint32_t bits = 0;
  int count = 0;
  for (char ch : text) {
    int v = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 : ch >= '0' && ch <= '9' ? ch - '0' + 52 : ch == '+' ? 62 : ch == '/' ? 63 : -1;
    if (v < 0) continue;
    bits = bits << 6 | (uint32_t)v;
    if (++count == 4) {
      out += (char)(bits >> 16);
      out += (char)(bits >> 8);
      out += (char)bits;
      bits = 0;
      count = 0;
    }
  }
  if (count == 3) {
    out += (char)(bits >> 10);
    out += (char)(bits >> 2);
  } else if (count == 2) {
    out += (char)(bits >> 4);
  }
  return out;
}

// Takes the same time however much of the two matches, so the time taken says nothing about a secret.
bool camSameText(const std::string& a, const std::string& b) {
  uint8_t differ = a.size() != b.size();
  for (size_t i = 0; i < a.size(); i++) differ |= (uint8_t)a[i] ^ (uint8_t)(i < b.size() ? b[i] : 0);
  return !differ;
}

bool camWsseValid(const std::string& body, const std::string& user, const std::string& password) {
  if (xmlFirstText(body, "Username") != user) return false;
  std::string sent = xmlFirstText(body, "Password");
  if (sent.empty()) return false;
  std::string digest = camSha1(camBase64Decode(xmlFirstText(body, "Nonce")) + xmlFirstText(body, "Created") + password);
  bool asDigest = camSameText(sent, base64((const uint8_t*)digest.data(), digest.size())), asText = camSameText(sent, password);
  return asDigest || asText;
}

std::string camAuthParam(const std::string& header, const char* name) {
  std::string key = std::string(name) + "=";
  size_t at = 0;
  while ((at = header.find(key, at)) != std::string::npos) {
    bool whole = at == 0 || header[at - 1] == ' ' || header[at - 1] == ',';
    size_t start = at + key.size();
    if (whole) {
      if (start < header.size() && header[start] == '"') {
        size_t end = header.find('"', start + 1);
        return end == std::string::npos ? "" : header.substr(start + 1, end - start - 1);
      }
      size_t end = header.find(',', start);
      return header.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    at = start;
  }
  return "";
}

bool camDigestValid(const std::string& auth, const std::string& method, const std::string& user, const std::string& password, const std::string& realm, const std::string& nonce) {
  if (auth.compare(0, 7, "Digest ") != 0 || camAuthParam(auth, "username") != user || camAuthParam(auth, "nonce") != nonce) return false;
  std::string ha1 = camMd5Hex(user + ":" + realm + ":" + password), ha2 = camMd5Hex(method + ":" + camAuthParam(auth, "uri"));
  return camAuthParam(auth, "response") == camMd5Hex(ha1 + ":" + nonce + ":" + ha2);
}

bool camBasicValid(const std::string& auth, const std::string& user, const std::string& password) {
  return auth.compare(0, 6, "Basic ") == 0 && camSameText(camBase64Decode(auth.substr(6)), user + ":" + password);
}

static std::string lower(std::string s) {
  for (char& ch : s) ch = (char)tolower((unsigned char)ch);
  return s;
}

// The names in a comma-separated list, lowercased and trimmed.
static std::vector<std::string> nameList(const std::string& names) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= names.size()) {
    size_t comma = names.find(',', at);
    if (comma == std::string::npos) comma = names.size();
    std::string name = names.substr(at, comma - at);
    while (!name.empty() && name.front() == ' ') name.erase(0, 1);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (!name.empty()) out.push_back(lower(name));
    at = comma + 1;
  }
  return out;
}

bool camHostListValid(const std::string& names) {
  std::vector<std::string> list = nameList(names);
  if (list.size() > 4 || names.size() > 200) return false;
  for (const std::string& name : list) {
    if (name.size() > 63 || name.front() == '.' || name.front() == '-' || name.back() == '.' || name.back() == '-') return false;
    for (char ch : name)
      if (!isalnum((unsigned char)ch) && ch != '.' && ch != '-') return false;
  }
  return true;
}

bool camKnownHost(const std::string& header, const std::string& ip, const std::string& hubName, const std::string& setupHost, const std::string& extraNames) {
  if (header.empty()) return true;
  std::string host = lower(header.substr(0, header.find(':')));
  while (!host.empty() && host.back() == '.') host.pop_back();
  std::string name = lower(hubName.substr(0, hubName.find('.')));
  if (host == ip || host == lower(setupHost.substr(0, setupHost.find(':'))) || (!name.empty() && host == name)) return true;
  static const char* const ENDINGS[] = {".local", ".lan", ".home", ".home.arpa", ".localdomain", ".internal"};  // none can be bought
  for (const char* ending : ENDINGS)
    if (!name.empty() && host == name + ending) return true;
  for (const std::string& extra : nameList(extraNames))
    if (host == extra) return true;
  return false;
}

uint32_t CamLoginGuard::lockMs(uint16_t failures) {
  if (failures < FREE_TRIES) return 0;
  int doublings = failures - FREE_TRIES < 8 ? failures - FREE_TRIES : 8;
  uint32_t ms = FIRST_LOCK_MS << doublings;
  return ms < LONGEST_LOCK_MS ? ms : LONGEST_LOCK_MS;
}

bool CamLoginGuard::allowed(uint32_t peer, uint32_t now) const {
  for (const Entry& e : entries_)
    if (e.peer == peer && e.failures && now - e.lastAt < lockMs(e.failures)) return false;
  return true;
}

void CamLoginGuard::note(uint32_t peer, bool ok, uint32_t now) {
  Entry* entry = nullptr;
  Entry* spare = &entries_[0];  // a free entry, or else the one quiet for longest
  for (Entry& e : entries_) {
    if (e.peer == peer && e.failures) entry = &e;
    if (spare->failures && (!e.failures || now - e.lastAt > now - spare->lastAt)) spare = &e;
  }
  if (ok) {
    if (entry) *entry = Entry();
    return;
  }
  if (!entry) {
    entry = spare;
    *entry = Entry();
    entry->peer = peer;
  }
  if (entry->failures && now - entry->lastAt >= FORGET_MS) entry->failures = 0;
  if (entry->failures < 0xFFFF) entry->failures++;
  entry->lastAt = now;
}

// Keyed with a secret that stays on the hub, so a snapshot link seen in an NVR's settings cannot be used to guess the
// password offline. A new password, or a new secret, gives a new link.
std::string camSnapshotToken(const std::string& secret, const std::string& password) {
  std::string digest = camHmacSha1(secret, "snapshot:" + password);
  return camHex((const uint8_t*)digest.data(), 8);
}
