#include "cam_storage.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#define CAM_MKDIR(p) _mkdir(p)
#else
#define CAM_MKDIR(p) mkdir(p, 0777)
#endif

static const char* const STATE_NAMES[CAM_DIRECTIONS] = {"active", "approaching", "away", "ltr", "rtl"};

std::string camStateFile(int direction, bool delayed) {
  return std::string(STATE_NAMES[direction]) + (delayed ? "-delayed" : "") + ".h264";
}

static bool validStateFile(const std::string& name) {
  for (int d = 0; d < CAM_DIRECTIONS; d++)
    if (name == camStateFile(d, false) || name == camStateFile(d, true)) return true;
  return false;
}

bool camParseMac(const std::string& t, uint8_t mac[6]) {
  if (t.size() != 17) return false;
  for (int i = 0; i < 6; i++) {
    uint8_t value = 0;
    for (int j = 0; j < 2; j++) {
      char c = t[i * 3 + j];
      if (c >= '0' && c <= '9') value = (uint8_t)(value * 16 + (c - '0'));
      else if (c >= 'A' && c <= 'F') value = (uint8_t)(value * 16 + (c - 'A' + 10));
      else return false;
    }
    if (i < 5 && t[i * 3 + 2] != '-') return false;
    mac[i] = value;
  }
  return true;
}

std::string camMacText(const uint8_t m[6]) {
  char b[18];
  snprintf(b, sizeof(b), "%02X-%02X-%02X-%02X-%02X-%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}

static bool validSensorNumber(const std::string& s) {
  if (s.empty() || s.size() > 3 || s[0] == '0') return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  return atoi(s.c_str()) <= 255;
}

CamPathKind camClassifyPath(const std::string& path) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    size_t slash = path.find('/', start);
    parts.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  uint8_t mac[6];
  if (parts.size() == 1 && parts[0] == "clear.h264") return CAM_PATH_CLEAR;
  if (parts.size() == 1 && parts[0] == "text.json") return CAM_PATH_TEXT;
  if (parts.size() > 3 || !camParseMac(parts[0], mac)) return CAM_PATH_INVALID;
  if (parts.size() == 1) return CAM_PATH_SLAVE;
  if (!validSensorNumber(parts[1])) return CAM_PATH_INVALID;
  if (parts.size() == 2) return CAM_PATH_SENSOR;
  if (parts[2] == "text.json") return CAM_PATH_TEXT;
  return validStateFile(parts[2]) ? CAM_PATH_FILE : CAM_PATH_INVALID;
}

bool camParseFrame(const uint8_t* d, size_t len, CamNal out[3]) {
  int seen[3] = {0, 0, 0};
  size_t i = 0;
  while (i + 3 <= len) {
    if (!(d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)) {
      i++;
      continue;
    }
    size_t begin = i + 3, end = begin;
    while (end < len && !(end + 3 <= len && d[end] == 0 && d[end + 1] == 0 && d[end + 2] == 1)) end++;
    size_t stop = end;
    if (end < len)
      while (stop > begin && d[stop - 1] == 0) stop--;
    if (stop > begin) {
      int type = d[begin] & 31;
      int slot = type == 7 ? 0 : type == 8 ? 1 : type == 5 ? 2 : -1;
      if (slot >= 0) {
        if (seen[slot]++) return false;
        out[slot].data = d + begin;
        out[slot].len = stop - begin;
      }
    }
    i = end;
  }
  return seen[0] && seen[1] && seen[2];
}

bool camSameNal(const CamNal& a, const CamNal& b) { return a.len == b.len && memcmp(a.data, b.data, a.len) == 0; }

namespace {
const size_t SPS_MAX = 64;

struct BitReader {
  const uint8_t* d;
  size_t bytes;
  size_t pos;
  bool ok;
  int bit() {
    if (pos / 8 >= bytes) {
      ok = false;
      return 0;
    }
    int b = (d[pos / 8] >> (7 - pos % 8)) & 1;
    pos++;
    return b;
  }
  uint32_t ue() {
    int zeros = 0;
    while (ok && !bit())
      if (++zeros > 31) ok = false;
    uint32_t v = 0;
    for (int i = 0; i < zeros && ok; i++) v = (v << 1) | (uint32_t)bit();
    return ok ? ((uint32_t)1 << zeros) - 1 + v : 0;
  }
};

size_t unescape(const CamNal& n, uint8_t* out) {
  size_t used = 0;
  for (size_t i = 0; i < n.len; i++) {
    if (i >= 2 && n.data[i] == 3 && n.data[i - 1] == 0 && n.data[i - 2] == 0) continue;
    out[used++] = n.data[i];
  }
  return used;
}

// Bits before vui_parameters_present_flag, or 0 if this is not an SPS we can read.
size_t spsCoreBits(const uint8_t* d, size_t bytes) {
  if (bytes < 4 || d[1] != 66) return 0;  // baseline only, which has no chroma fields to skip
  BitReader r{d, bytes, 32, true};
  r.ue();  // seq_parameter_set_id
  r.ue();  // log2_max_frame_num
  uint32_t poc = r.ue();
  if (poc == 0) r.ue();
  else if (poc != 2) return 0;
  r.ue();   // max_num_ref_frames
  r.bit();  // gaps_in_frame_num_allowed
  r.ue();   // width
  r.ue();   // height
  if (!r.bit()) r.bit();  // frame_mbs_only, mb_adaptive
  r.bit();                // direct_8x8
  if (r.bit())
    for (int i = 0; i < 4; i++) r.ue();  // cropping
  return r.ok ? r.pos : 0;
}
}  // namespace

bool camCompatibleSps(const CamNal& a, const CamNal& b) {
  if (a.len == 0 || b.len == 0 || a.len > SPS_MAX || b.len > SPS_MAX) return false;
  uint8_t ra[SPS_MAX], rb[SPS_MAX];
  size_t la = unescape(a, ra), lb = unescape(b, rb);
  size_t bits = spsCoreBits(ra, la);
  if (bits == 0 || bits != spsCoreBits(rb, lb)) return false;
  for (size_t i = 0; i < bits; i++)
    if (((ra[i / 8] >> (7 - i % 8)) & 1) != ((rb[i / 8] >> (7 - i % 8)) & 1)) return false;
  return true;
}

std::string CamStorage::full(const std::string& path) const { return path.empty() ? root_ : root_ + "/" + path; }

bool CamStorage::exists(const std::string& path) const {
  struct stat st;
  return stat(full(path).c_str(), &st) == 0;
}

bool CamStorage::list(const std::string& path, std::vector<CamFsEntry>& out) const {
  std::string base = full(path);
  DIR* dir = opendir(base.c_str());
  if (!dir) return false;
  while (struct dirent* e = readdir(dir)) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    struct stat st;
    if (stat((base + "/" + e->d_name).c_str(), &st) != 0) continue;
    CamFsEntry item;
    item.name = e->d_name;
    item.dir = S_ISDIR(st.st_mode);
    item.size = (uint32_t)st.st_size;
    out.push_back(item);
  }
  closedir(dir);
  return true;
}

bool CamStorage::readFile(const std::string& path, std::string& out, size_t maxBytes) const {
  FILE* f = fopen(full(path).c_str(), "rb");
  if (!f) return false;
  std::string data;
  struct stat st;
  if (stat(full(path).c_str(), &st) == 0 && (size_t)st.st_size <= maxBytes) data.reserve(st.st_size);  // one exact allocation
  char buf[1024];
  size_t n;
  bool ok = true;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    if (data.size() + n > maxBytes) {
      ok = false;
      break;
    }
    data.append(buf, n);
  }
  fclose(f);
  if (ok) out.swap(data);
  return ok;
}

bool CamStorage::makeDirs(const std::string& path) const {
  size_t pos = 0;
  while (true) {
    size_t slash = path.find('/', pos);
    std::string part = path.substr(0, slash);
    if (!exists(part) && CAM_MKDIR(full(part).c_str()) != 0) return false;
    if (slash == std::string::npos) return true;
    pos = slash + 1;
  }
}

bool CamStorage::writeFile(const std::string& path, const char* data, size_t len) const {
  size_t slash = path.rfind('/');
  if (slash != std::string::npos && !makeDirs(path.substr(0, slash))) return false;
  // Written beside the target and renamed, so a failed upload leaves the old picture intact.
  std::string target = full(path), temp = target + ".tmp";
  FILE* f = fopen(temp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, len, f) == len;
  ok = fclose(f) == 0 && ok;
  if (ok) {
#ifdef _WIN32
    remove(target.c_str());  // Windows rename will not replace an existing file
#endif
    ok = rename(temp.c_str(), target.c_str()) == 0;
  }
  if (!ok) remove(temp.c_str());
  return ok;
}

bool CamStorage::removeTree(const std::string& path) const {
  std::vector<CamFsEntry> items;
  if (!list(path, items)) return remove(full(path).c_str()) == 0;  // a file, or nothing there
  bool ok = true;
  for (const CamFsEntry& e : items) ok = removeTree(path + "/" + e.name) && ok;
  return rmdir(full(path).c_str()) == 0 && ok;
}
