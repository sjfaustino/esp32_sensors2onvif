#include "cam_auth.h"
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include "cam_rtp.h"
#include "cam_soap.h"

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

// The message plus 0x80, zeros and the bit length, in whole 64-byte blocks.
static std::string padded(const std::string& data) {  // as MD5 has it: the length little-endian
  std::string m = data;
  m += (char)0x80;
  while (m.size() % 64 != 56) m += (char)0;
  uint64_t bits = (uint64_t)data.size() * 8;
  for (int i = 0; i < 8; i++) m += (char)(bits >> (8 * i));
  return m;
}

std::string camHmacSha1(const std::string& key, const std::string& data) {
  uint8_t out[20];
  camHmacSha1((const uint8_t*)key.data(), key.size(), (const uint8_t*)data.data(), data.size(), nullptr, 0, out);
  return std::string((const char*)out, 20);
}

std::string camSha1(const std::string& data) {
  CamSha1 s;
  s.add(data.data(), data.size());
  uint8_t out[20];
  s.finish(out);
  return std::string((const char*)out, 20);
}

std::string camMd5Hex(const std::string& data) {
  static const uint8_t S[64] = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
                                14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                                4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
      0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
      0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
      0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
      0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
      0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  std::string m = padded(data);
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

// Spaces and line breaks are passed over, and = only ends it; anything else that is not base64 makes the whole of it
// nothing, so "a\x01b" is not read as "ab".
std::string camBase64Decode(const std::string& text) {
  std::string out;
  uint32_t bits = 0;
  int count = 0;
  bool ended = false;  // by =
  for (char ch : text) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
    if (ch == '=') {
      ended = true;
      continue;
    }
    int v = ch >= 'A' && ch <= 'Z'   ? ch - 'A'
            : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26
            : ch >= '0' && ch <= '9' ? ch - '0' + 52
            : ch == '+'              ? 62
            : ch == '/'              ? 63
                                     : -1;
    if (v < 0 || ended) return std::string();
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

// Takes the same time however much of the two matches, so the time taken says nothing about a secret. Their SHA-1s are
// compared, not the texts, so the time does not follow the length of either.
bool camSameText(const std::string& a, const std::string& b) {
  std::string ha = camSha1(a), hb = camSha1(b);
  uint8_t differ = 0;
  for (size_t i = 0; i < ha.size(); i++) differ |= (uint8_t)ha[i] ^ (uint8_t)hb[i];
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

bool camDigestValid(const std::string& auth, const std::string& method, const std::string& user, const std::string& password,
                    const std::string& realm, const std::string& nonce) {
  if (auth.compare(0, 7, "Digest ") != 0 || camAuthParam(auth, "username") != user || camAuthParam(auth, "nonce") != nonce) return false;
  std::string ha1 = camMd5Hex(user + ":" + realm + ":" + password), ha2 = camMd5Hex(method + ":" + camAuthParam(auth, "uri"));
  return camSameText(camAuthParam(auth, "response"), camMd5Hex(ha1 + ":" + nonce + ":" + ha2));  // in the same time, right or wrong
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

bool camKnownHost(const std::string& header, const std::string& ip, const std::string& hubName, const std::string& setupHost,
                  const std::string& extraNames) {
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

// Where an address's wrong passwords are counted: its own entry; else a free one; else the one quiet longest of those
// that have not used up their free tries; else, every entry holding an address shut out today, the shared one. The
// NVR's address is never put in the shared one: it takes the entry quiet longest, whatever that holds.
int CamLoginGuard::slotFor(uint32_t peer, uint32_t now) const {
  for (int i = 0; i < ENTRIES; i++)
    if (kept(entries_[i], now) && entries_[i].peer == peer) return i;
  for (int i = 0; i < ENTRIES; i++)
    if (!kept(entries_[i], now)) return i;
  bool nvr = peer && peer == protected_;
  int spare = -1;
  for (int i = 0; i < ENTRIES; i++)
    if ((nvr || entries_[i].failures < FREE_TRIES) && (spare < 0 || now - entries_[i].lastAt > now - entries_[spare].lastAt)) spare = i;
  return spare >= 0 ? spare : ENTRIES;
}

bool CamLoginGuard::allowed(uint32_t peer, uint32_t now) const {
  int i = slotFor(peer, now);
  const Entry& e = entries_[i];
  bool counts = i == ENTRIES || e.peer == peer;  // a spare entry is someone else's count
  return !(counts && kept(e, now) && now - e.lastAt < lockMs(e.failures));
}

void CamLoginGuard::note(uint32_t peer, bool ok, uint32_t now) {
  int i = slotFor(peer, now);
  Entry& e = entries_[i];
  bool own = i < ENTRIES && e.peer == peer && kept(e, now);
  if (ok) {
    if (own) e = Entry();  // the shared count stays: a right password from one address says nothing of the others
    return;
  }
  if (!kept(e, now) || (i < ENTRIES && !own)) e = Entry();
  e.peer = i < ENTRIES ? peer : 0;
  if (e.failures < 0xFFFF) e.failures++;
  e.lastAt = now;
}

// Keyed with a secret that stays on the hub, so a snapshot link seen in an NVR's settings cannot be used to guess the
// password offline. A new password, or a new secret, gives a new link.
std::string camSnapshotToken(const std::string& secret, const std::string& password) {
  std::string digest = camHmacSha1(secret, "snapshot:" + password);
  return camHex((const uint8_t*)digest.data(), 8);
}
