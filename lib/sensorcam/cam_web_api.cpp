// CamWeb: the web page's API, and the JSON it reads.
#include "cam_web_server.h"
#include "cam_number.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_web.h"
#include "cam_server_internal.h"

// Pictures may use the storage only up to what the event history still needs: its full size, both CSV files, and room
// to rewrite the saved files. Without that room no report could be saved, and so none could be confirmed.
size_t CamWeb::historyRoom() const {
  size_t logs = alarm_.log().bytesOnStorage();
  return HISTORY_ROOM + (logs < 2 * alarm_.log().fileMax() ? 2 * alarm_.log().fileMax() - logs : 0);
}

bool CamWeb::roomBesideHistory(size_t bytes) const {
  uint32_t used = 0, total = 0;
  if (!chip_->storageUsage(used, total)) return true;
  size_t needed = (bytes + 4095) / 4096 * 4096 + 2 * 4096 + historyRoom();  // whole blocks, the new copy beside the old, and its folder
  return total > used && total - used >= needed;
}

static bool hasFile(const std::vector<CamFsEntry>& items, const std::string& name) {
  for (const CamFsEntry& e : items)
    if (!e.dir && e.name == name) return true;
  return false;
}

// Built as one string that grows only when the memory for it is there, and is never copied.
// Empty when the hub is too short of memory to build it.
std::string CamWeb::treeJson() {
  uint32_t used = 0, total = 0;
  chip_->storageUsage(used, total);
  std::vector<CamFsEntry> root;
  storage_.list("", root);
  std::string out;
  auto makeRoom = [&](size_t bytes) {  // reserved in advance, so a failed allocation is a refusal and not a crash
    if (out.capacity() - out.size() >= bytes) return true;
    size_t wanted = out.capacity() * 2 + bytes;
    if (!roomFor(wanted)) return false;
    out.reserve(wanted);
    return true;
  };
  if (!makeRoom(128)) return std::string();
  bool clear = false;
  uint32_t clearSize = 0;
  for (const CamFsEntry& s : root) {
    if (s.dir || s.name != "clear.h264") continue;
    clear = true;
    clearSize = s.size;
  }
  CamJson j(out);
  j.object()
      .num("used", used)
      .num("total", total)
      .num("kept", historyRoom())
      .num("clear", clear, clearSize)
      .flag("clearPreview", hasFile(root, "clear.jpg"))
      .array("nodes");
  for (const CamFsEntry& s : root) {
    if (!s.dir || camClassifyPath(s.name) != CAM_PATH_NODE) continue;
    if (!makeRoom(320)) return std::string();  // the longest node entry before its sensors
    CamNodeSettings node;
    std::string nodeText;
    if (!storage_.readFile(s.name + "/settings.txt", nodeText, 256) || !camParseNodeSettings(nodeText, node)) node = CamNodeSettings();
    uint8_t mac[6];
    camParseMac(s.name, mac);
    int any = table_.findNode(mac);
    uint32_t every = any ? table_.nodeOf(any).info.everyS : 0;
    j.object()
        .str("mac", s.name)
        .flag("paired", storage_.exists(s.name + "/pairing.txt"))
        .num("late", node.lateS != 0, node.lateS)
        .num("silent", node.silentS != 0, node.silentS);
    j.num("every", every != 0, every).num("autoSilent", every != 0, camAutoSilentS(every));
    j.num("reportEvery", node.everyS != 0, node.everyS).num("repeat", node.repeatS != 0, node.repeatS);
    time_t t = chip_->wallTime();
    uint32_t walk = any && t >= CAM_CLOCK_VALID_AFTER && table_.nodeOf(any).walkUntil > (uint32_t)t ? table_.nodeOf(any).walkUntil - (uint32_t)t : 0;
    j.num("walkLeft", walk).array("sensors");
    std::vector<CamFsEntry> inputs;
    storage_.list(s.name, inputs);
    for (const CamFsEntry& n : inputs) {
      std::string base = s.name + "/" + n.name;
      if (!n.dir || camClassifyPath(base) != CAM_PATH_SENSOR) continue;
      uint8_t input = 0;
      int slot = camNumber(n.name, input) ? table_.findSlot(mac, input) : 0;
      if (!slot) continue;                        // beyond the sensor limit, so it does nothing
      if (!makeRoom(1280)) return std::string();  // the longest possible entry: all ten pictures, and a 64-byte name that is all escapes
      const CamSensorSettings& set = table_.slotOf(slot).settings;
      j.object().num("input", input).str("name", table_.slotOf(slot).name).num("hold", set.holdS != 0, set.holdS);
      j.flag("interior", set.interior);
      if (set.quietFrom >= 0)
        j.array("quiet").num(set.quietFrom).num(set.quietTo).end();
      else
        j.null("quiet");
      j.array("files");
      std::vector<CamFsEntry> items;
      storage_.list(base, items);
      for (const CamFsEntry& f : items) {
        if (f.dir || camClassifyPath(base + "/" + f.name) != CAM_PATH_FILE) continue;
        j.object().str("name", f.name).num("size", f.size).flag("preview", hasFile(items, camPreviewPath(f.name))).end();
      }
      j.end().end();
    }
    j.end().end();
  }
  j.finish();
  return out;
}

// A Wi-Fi name or password can hold any character, so form values arrive percent-encoded.
static std::string percentDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string CamWeb::ApiRequest::field(const char* name) const { return percentDecode(queryParam(form(), name)); }

bool CamWeb::ApiRequest::has(const char* name) const {
  std::string f = form(), key = std::string(name) + "=";
  return f.compare(1, key.size(), key) == 0 || f.find("&" + key) != std::string::npos;
}

static bool parseIpv4(const std::string& s, uint32_t& out) {
  out = 0;
  size_t i = 0;
  for (int part = 0; part < 4; part++) {
    if (part && (i >= s.size() || s[i++] != '.')) return false;
    int value = 0, digits = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) {
      value = value * 10 + (s[i++] - '0');
      if (++digits > 3 || value > 255) return false;
    }
    if (!digits) return false;
    out = (out << 8) | (uint32_t)value;
  }
  return i == s.size();
}

// Refuses anything that could never work, because a bad fixed address takes the hub off the network.
static bool validAddress(const CamAddress& a) {
  uint32_t ip, mask, gateway, dns;
  if (!parseIpv4(a.ip, ip) || !parseIpv4(a.mask, mask) || !parseIpv4(a.gateway, gateway)) return false;
  if (!a.dns.empty() && !parseIpv4(a.dns, dns)) return false;
  uint32_t hosts = ~mask;
  if (mask == 0 || (hosts & (hosts + 1)) != 0 || hosts < 3) return false;  // the mask must be contiguous, with room for hosts
  if ((ip & hosts) == 0 || (ip & hosts) == hosts) return false;            // not the network or broadcast address
  return (gateway & mask) == (ip & mask) && gateway != ip;
}

// A plain whole number and nothing else.
static bool wholeNumber(const std::string& s, long long& out) { return camNumber(s, out); }

static bool validNtpServer(const std::string& s) {
  if (s.empty() || s.size() > 63) return false;
  for (char ch : s)
    if (!isalnum((unsigned char)ch) && ch != '.' && ch != '-') return false;
  return true;
}

// Also the hub's network name, so letters, digits and inner hyphens only.
static bool validHubName(const std::string& s) {
  bool ok = s.size() <= 63 && (s.empty() || (s.front() != '-' && s.back() != '-'));
  for (char ch : s) ok = ok && (isalnum((unsigned char)ch) || ch == '-');
  return ok;
}

static bool validOffset(long long minutes) { return minutes >= -720 && minutes <= 840 && minutes % 15 == 0; }

// Sent unencoded in a URL and in JSON, so it is kept to characters that need no escaping in either.
static bool validToken(const std::string& s) {
  bool ok = !s.empty() && s.size() <= 64;
  for (char ch : s) ok = ok && (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~');
  return ok;
}

// The same rule as an NVR's event address: http:// with an IPv4 host, because the hub has no name lookup or TLS for
// these. Nothing that could end the request line early.
static bool validHookUrl(const std::string& url) {
  HttpUrl u;
  bool ok = url.size() <= 200;
  for (char ch : url) ok = ok && (unsigned char)ch > 0x20 && (unsigned char)ch < 0x7f;
  return url.empty() || (ok && parseHttpUrl(url, u) && inet_addr(u.host.c_str()) != INADDR_NONE);
}

// An MQTT user name or password: sent as it is, so anything but control characters, up to 64 bytes.
static bool validMqttText(const std::string& s) {
  bool ok = s.size() <= 64;
  for (char ch : s) ok = ok && (unsigned char)ch >= 0x20 && ch != 0x7f;
  return ok;
}

// 1 for a good value, 0 for a bad one, -1 for a key it does not know, which no backup may hold.
static int backupCheck(const std::string& key, const std::string& value) {
  long long n = 0;
  bool number = wholeNumber(value, n);
  uint32_t ip;
  if (key == "net.onvif" || key == "net.web" || key == "net.rtsp" || key == "net.mqport") return number && n >= 1 && n <= 65535;
  if (key == "net.mqhost") return value.empty() || parseIpv4(value, ip);
  if (key == "net.rearm") return number && n >= 0 && n <= CAM_MAX_DISARM_MINUTES;
  if (key == "net.mquser" || key == "net.mqpw") return validMqttText(value);
  if (key == "net.ntp" || key == "net.rben" || key == "net.hlth" || key == "net.hooktx" || key == "net.hookal" || key == "net.hookpr")
    return value == "0" || value == "1";
  if (key == "net.hookdy") return number && n >= -1 && n <= 1439;  // the daily check's minute; -1 for none
  if (key == "net.enkey") return value.size() >= 16 && validToken(value);
  if (key == "net.pairkey") {
    uint8_t k[CAM_X25519_LEN];
    return camParseHex(value, k, sizeof(k));
  }
  if (key == "net.hook") return validHookUrl(value);
  if (key == "net.tzoff") return number && validOffset(n);
  if (key == "net.tzdst") return number && n >= 0 && n <= 2;
  if (key == "net.rbday") return number && n >= -1 && n <= 6;
  if (key == "net.rbmin") return number && n >= 0 && n <= 1439;
  if (key == "net.name") return !value.empty() && validHubName(value);
  if (key == "net.ntpsrv") return validNtpServer(value);
  if (key == "net.token") return validToken(value);
  if (key == "net.ip" || key == "net.mask" || key == "net.gw") return parseIpv4(value, ip);
  if (key == "net.dns" || key == "net.nvrip") return value.empty() || parseIpv4(value, ip);
  if (key == "net.webpw") return value.size() <= 63;
  if (key == "net.hosts") return camHostListValid(value);
  if (key == "wifi.ssid" || key == "wifi.ssid2" || key == "net.ipfor") return value.size() <= 32;
  if (key == "wifi.password" || key == "wifi.password2") return value.empty() || (value.size() >= 8 && value.size() <= 63);
  return -1;
}

bool camBackupValueValid(const std::string& key, const std::string& value) { return backupCheck(key, value) == 1; }
bool camBackupKeyKnown(const std::string& key) { return backupCheck(key, "") != -1; }

void CamWeb::handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body,
                       size_t bodyLen) {
  std::string route = target.substr(0, target.find('?'));
  // On the setup network the password of that network already guards these, and a captive-portal window may not offer a login.
  bool inSetup = radio_->setupActive();
  bool setupRoute = route == "/setup" || route == "/api/wifi" || route == "/api/wifi/scan" || (inSetup && route == "/");
  if (!(inSetup && setupRoute) && !authorized(c, headers)) {
    c.out = UNAUTHORIZED;
    return;
  }
  const ApiRequest r{c, method, route, target, headers, body, bodyLen};
  if (setupRoute)
    apiSetup(r);
  else if (!apiPages(r) && !apiInfo(r) && !apiSystem(r) && !apiNetwork(r) && !apiSensors(r) && !apiPairing(r) && !apiNodeImages(r))
    apiFiles(r);
}

void CamWeb::apiSetup(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.method == "GET" && (r.route == "/" || r.route == "/setup")) {
    sendBody(c, HTML_TYPE, (const char*)CAM_SETUP_PAGE, CAM_SETUP_PAGE_SIZE, PAGE_HEADERS);
  } else if (r.is("GET", "/api/wifi/scan")) {
    c.out = jsonOk(jsonObject([&](CamJson& j) {
      radio_->wifiNetworks(j);
      j.flag("needPassword", cfg_.webPassword.empty());
    }));
  } else if (r.is("POST", "/api/wifi")) {
    // A blank hub's setup network is open, so whoever sets it up chooses the hub password in the same step. It then
    // guards the pages, ONVIF, the video and the setup network itself.
    std::string ssid = r.field("ssid"), password = r.field("password"), hubPassword = r.field("hubpw");
    bool needPassword = cfg_.webPassword.empty();
    if (ssid.empty() || ssid.size() > 32 || (!password.empty() && (password.size() < 8 || password.size() > 63)))
      c.out = jsonFail(400, "the name must be 1 to 32 characters and the password empty or 8 to 63");
    else if (needPassword && (hubPassword.size() < 8 || hubPassword.size() > 63))
      c.out = jsonFail(400, "choose a hub password of 8 to 63 characters");
    else if (needPassword && !store_->passwordSave(hubPassword))
      c.out = jsonFail(507, "could not save the hub password");
    else
      c.out = saved(radio_->wifiSave(ssid, password, r.field("role") == "backup"), "could not save the Wi-Fi settings");
  } else {
    c.out = jsonFail(404, "not found");
  }
}

bool CamWeb::apiPages(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("GET", "/") || r.is("GET", "/files")) {
    sendBody(c, HTML_TYPE, (const char*)CAM_WEB_PAGE, CAM_WEB_PAGE_SIZE, PAGE_HEADERS);
  } else if (r.is("GET", "/api/tree")) {
    c.body = treeJson();
    if (c.body.empty())
      c.out = jsonFail(507, "the hub is short of memory; try again");
    else
      sendBody(c, JSON_TYPE, c.body.data(), c.body.size());
  } else if (r.is("GET", "/api/live")) {
    video_.current(millis_());
    c.out = jsonOk(
        jsonObject([&](CamJson& j) { j.num("seq", video_.seq()).str("file", video_.file().empty() ? std::string("built-in") : video_.file()); }));
  } else if (r.is("GET", "/api/live/frame")) {
    // Read from the picture as it is sent, each NAL after a start code. If the picture is replaced meanwhile, the reply
    // stops short and the page asks again.
    const CamFrame* frame = &video_.current(millis_());
    size_t size = 0;
    for (int i = 0; i < frame->count; i++) size += 4 + frame->nals[i].len;
    uint32_t gen = video_.gen();
    c.reader = [this, frame, gen](size_t offset, char* out, size_t len) {
      if (video_.isLoaded(frame) && video_.gen() != gen) return false;  // the built-in pictures never change
      for (int i = 0; i < frame->count && len; i++) {
        static const char START[4] = {0, 0, 0, 1};
        const CamNal& n = frame->nals[i];
        for (int part = 0; part < 2 && len; part++) {
          const char* data = part ? (const char*)n.data : START;
          size_t partLen = part ? n.len : 4;
          if (offset >= partLen) {
            offset -= partLen;
            continue;
          }
          size_t take = partLen - offset < len ? partLen - offset : len;
          memcpy(out, data + offset, take);
          out += take;
          len -= take;
          offset = 0;
        }
      }
      return len == 0;
    };
    c.pull = &c.reader;
    c.staticLen = size;
    c.out = httpHead(200, "video/h264", size, "X-Frame-Seq: " + std::to_string(video_.seq()) + "\r\n");
  } else {
    return false;
  }
  return true;
}

// Sized for the sensors that exist, and empty when the hub is too short of memory to build it.
std::string CamWeb::espNowJson() {
  int known = table_.sensorCount();
  size_t room = (size_t)known * 760 + CamSensorTable::MAX_UNKNOWN * 120 + 8 * 120 + 256;  // the longest possible entries
  if (!roomFor(room)) return std::string();
  std::string out;
  out.reserve(room);
  CamJson j(out);
  j.object();
  radio_->espNowInfo(j);
  j.str("key", cfg_.espNowKey).array("nodes");
  uint32_t now = millis_();
  for (int id = 1; id <= alarm_.board().count(); id++) {
    if (!table_.slotOf(id).used) continue;
    const Health& h = table_.healthOf(id);
    std::string name = table_.slotName(id);
    const char* state = alarm_.board().active(id) ? "active" : h.silent ? "silent" : !h.heard ? "never" : "idle";
    j.object()
        .flag("low", h.lowBattery)
        .str("name", table_.slotOf(id).name)
        .str("node", name.substr(0, name.find('/')))
        .num("input", table_.slotOf(id).input);
    j.str("state", state).num("ago", h.heard, (now - h.heardAt) / 1000).num("battery", h.heard && h.battery != CAM_BATTERY_UNKNOWN, h.battery);
    j.num("rssi", h.rssi != 0, h.rssi)
        .num("counter", h.counterSeen, h.counter)
        .flag("weak", h.weak)
        .num("refused", h.refused)
        .str("refusal", h.refusal)
        .flag("paired", table_.nodeOf(id).paired);
    const Node& node = table_.nodeOf(id);
    const CamNodeInfo& info = node.info;
    j.num("every", info.everyS != 0, info.everyS)
        .num("silentAfter", table_.silentMsFor(id) / 1000)
        .flag("saving", info.saving)
        .str("sensor", info.sensor)
        .str("version", info.version);
    bool told = !info.version.empty();
    j.str("restart", told ? camRestartName(info.restart) : "")
        .num("restarts", told, info.restarts)
        .flag("restartFault", told && camRestartIsFault(info.restart));
    j.num("responseMs", info.responseMs != CAM_RESPONSE_UNKNOWN, info.responseMs)
        .str("chip", info.chip != CAM_CHIP_UNKNOWN ? camChipName(info.chip) : std::string());
    j.num("daysLeft", node.daysLeft >= 0, node.daysLeft).flag("sealed", node.sealed);
    const CamNodeImage* image = nodes_.images().forChip(info.chip);
    std::string update = node.updating && node.updateSize ? "updating, " + std::to_string((uint64_t)node.updateOffset * 100 / node.updateSize) + "%"
                         : alarm_.updateReady(id)         ? "update ready"
                         : image && info.refused == image->number ? "update undone"
                                                                  : "";
    j.str("update", update).end();
  }
  j.end();
  nodes_.images().json(j);
  j.array("unknown");
  for (const UnknownSender& u : table_.unknown()) {
    if (!u.used || table_.findSlot(u.mac, u.input)) continue;  // added since it was last heard
    j.object()
        .str("mac", camMacText(u.mac))
        .num("input", u.input)
        .num("ago", (now - u.heardAt) / 1000)
        .num("rssi", u.rssi != 0, u.rssi)
        .num("messages", u.messages)
        .end();
  }
  j.finish();
  return out;
}

bool CamWeb::apiInfo(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("GET", "/api/device")) {
    int known = table_.sensorCount();
    uint32_t used = 0, total = 0;
    chip_->storageUsage(used, total);
    c.out = jsonOk(jsonObject([&](CamJson& j) {
      j.array("rows");
      chip_->deviceRows(j);
      auto row = [&](const char* label, const std::string& value) { j.array().str(label).str(value).end(); };
      row("Picture storage", std::to_string(used / 1024) + " KB used of " + std::to_string(total / 1024) + " KB");
      row("Sensors", std::to_string(known));
      row("Video streams open", std::to_string(video_.streamCount()));
      row("Event subscribers", std::to_string(events_.subs().count()));
      if (alarm_.log().saveFailed()) row("Event history", "Could not be saved; storage may be full");
      j.end().num("crash", chip_->crashSize());
    }));
  } else if (r.is("GET", "/api/crash")) {  // read from flash 1 KB at a time, because it can be larger than any free block
    size_t size = chip_->crashSize();
    if (!size) {
      c.out = jsonFail(404, "no crash report is saved");
    } else {
      c.out = httpHead(200, "application/octet-stream", size, "Content-Disposition: attachment; filename=\"hub-crash.bin\"\r\n");
      c.pull = &crashReader_;
      c.staticLen = size;
    }
  } else if (r.is("POST", "/api/crash/delete")) {
    c.out = saved(chip_->crashDelete(), "could not delete the crash report");
  } else if (r.is("GET", "/api/events")) {
    // The Events page asks every 3 seconds. The browser sends back the tag of the list it has, so an unchanged list costs
    // the hub nothing to build.
    std::string tag = eventsTag();
    if (headerValue(r.headers, "if-none-match") == tag) {
      c.out = "HTTP/1.1 304 Not Modified\r\nETag: " + tag + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
    } else {
      bool whole = true;
      std::string list = eventsJson(&whole);  // a list trimmed for lack of memory gets no tag, so it is asked for again
      reply(c, 200, JSON_TYPE, std::move(list), whole ? "ETag: " + tag + "\r\nCache-Control: no-cache\r\n" : "Cache-Control: no-store\r\n");
    }
  } else if (r.is("GET", "/api/espnow")) {
    c.body = espNowJson();
    if (c.body.empty())
      c.out = jsonFail(507, "the hub is short of memory; try again");
    else
      sendBody(c, JSON_TYPE, c.body.data(), c.body.size());
  } else if (r.is("GET", "/api/info")) {
    c.out = jsonOk(jsonObject([&](CamJson& j) {
      j.array("unsupported");
      for (const std::string& seen : unsupported_)
        if (!seen.empty()) j.str(seen);
      j.end().str("token", cfg_.sensorToken).flag("onvifLogin", !onvifPassword_.empty()).str("ip", localIp_()).str("name", cfg_.hostName);
      j.num("onvifPort", cfg_.onvifPort)
          .num("webPort", cfg_.webPort)
          .num("rtspPort", cfg_.rtspPort)
          .num("streams", video_.streamCount())
          .num("subscribers", events_.subs().count());
      j.str("serial", cfg_.serial).flag("password", !cfg_.webPassword.empty()).str("hosts", cfg_.extraHosts).flag("armed", alarm_.armed());
    }));
  } else {
    return false;
  }
  return true;
}

bool CamWeb::apiSystem(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("GET", "/api/time")) {
    c.out = jsonOk(jsonObject([&](CamJson& j) { chip_->timeInfo(j); }));
  } else if (r.is("POST", "/api/time")) {
    std::string epochText = r.field("epoch");
    CamTime t;
    t.ntp = r.field("mode") != "manual";
    t.server = r.field("server");
    long long offset = 0, dst = 0, epoch = 0;
    bool ok = wholeNumber(r.field("offset"), offset) && validOffset(offset) && wholeNumber(r.field("dst"), dst) && dst >= 0 && dst <= 2;
    if (t.ntp)
      ok = ok && validNtpServer(t.server);
    else
      ok = ok && (t.server.empty() || validNtpServer(t.server)) &&
           (epochText.empty() || (wholeNumber(epochText, epoch) && epoch > (long long)CAM_CLOCK_VALID_AFTER && epoch < 4102444800LL));
    if (!ok) {
      c.out = jsonFail(400, "check the timezone, the NTP server name and the date and time");
    } else {
      t.offsetMinutes = (int)offset;
      t.dst = (int)dst;
      t.epoch = epoch;
      c.out = saved(chip_->timeSave(t), "could not save the time settings");
    }
  } else if (r.is("GET", "/api/schedule")) {
    c.out = jsonOk(jsonObject([&](CamJson& j) { j.flag("on", schedule_.on).num("day", schedule_.day).num("minute", schedule_.minute); }));
  } else if (r.is("POST", "/api/schedule")) {
    long long day = 0, minute = 0;
    if (!wholeNumber(r.field("day"), day) || day < -1 || day > 6 || !wholeNumber(r.field("minute"), minute) || minute < 0 || minute > 1439) {
      c.out = jsonFail(400, "check the day and the time");
    } else {
      CamSchedule s;
      s.on = r.field("on") == "1";
      s.day = (int)day;
      s.minute = (int)minute;
      bool ok = store_->scheduleSave(s);
      if (ok) schedule_ = s;
      c.out = saved(ok, "could not save the schedule");
    }
  } else if (r.is("GET", "/api/radio-test") || r.is("POST", "/api/radio-test")) {
    if (!radio_->hasRadioTest()) {
      c.out = jsonFail(404, "this board has no radio test");
    } else {
      bool ok = true;
      std::string body = jsonObject([&](CamJson& j) { ok = radio_->radioTest(r.method == "POST", j); });
      c.out = ok ? jsonOk(body) : jsonFail(409, "the test is running already, or the hub is off its Wi-Fi network");
    }
  } else if (r.is("POST", "/api/reboot")) {
    c.out = saved(chip_->maintenance("reboot"), "could not do that");
  } else if (r.is("POST", "/api/reset")) {
    if (r.field("confirm") != "yes")
      c.out = jsonFail(400, "not confirmed");  // erases Wi-Fi and every setting, so a stray request must not do it
    else
      c.out = saved(chip_->maintenance("reset"), "could not do that");
  } else if (r.is("POST", "/api/newkeys")) {
    // Every node and HTTP sensor stops being heard until set up again, so a stray request must not do it.
    if (r.field("confirm") != "yes")
      c.out = jsonFail(400, "not confirmed");
    else
      c.out = saved(chip_->maintenance("newkeys"), "could not do that");
  } else if (r.is("GET", "/api/alerts")) {
    c.out = jsonOk(jsonObject([&](CamJson& j) { j.flag("alarm", alarm_.healthAlarm()); }));
  } else if (r.is("POST", "/api/alerts")) {
    bool on = r.field("alarm") == "1", ok = store_->healthSave(on);
    if (ok) alarm_.setHealthAlarm(on);
    c.out = saved(ok, "could not save the setting");
  } else if (r.is("GET", "/api/backup")) {
    c.out = httpResponse(200, "text/plain", store_->backupText());
  } else if (r.is("POST", "/api/restore")) {
    c.out = store_->restoreText(std::string(r.body, r.bodyLen))
                ? jsonOk(JSON_OK)
                : jsonFail(400, "could not restore that file: it is damaged, or the hub's settings storage is full");
  } else {
    return false;
  }
  return true;
}

bool CamWeb::apiNetwork(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("GET", "/api/nvr")) {
    uint32_t nvr = events_.nvrIp().empty() ? 0 : inet_addr(events_.nvrIp().c_str());
    bool video = video_.playingTo(nvr), events = false;
    for (int i = 0; i < SubTable::SIZE; i++) events = events || (nvr && events_.subs().at(i).used && events_.subs().at(i).peer == nvr);
    c.out = jsonOk(jsonObject([&](CamJson& j) {
      j.str("ip", events_.nvrIp()).flag("video", video).flag("events", events).array("viewers");
      video_.viewers(j);
      j.end();
    }));
  } else if (r.is("POST", "/api/nvr")) {
    std::string ip = r.field("ip");
    uint32_t parsed;
    if (!ip.empty() && !parseIpv4(ip, parsed)) {
      c.out = jsonFail(400, "check the IP address");
    } else {
      bool ok = store_->nvrSave(ip);
      if (ok) events_.setNvr(ip);
      c.out = saved(ok, "could not save the NVR address");
    }
  } else if (r.is("GET", "/api/network")) {
    c.out = jsonOk(jsonObject([&](CamJson& j) { radio_->addressInfo(j); }));
  } else if (r.is("POST", "/api/network")) {
    CamAddress a;
    a.dhcp = r.field("mode") != "static";
    a.ip = r.field("ip");
    a.mask = r.field("mask");
    a.gateway = r.field("gateway");
    a.dns = r.field("dns");
    if (!a.dhcp && !validAddress(a))
      c.out = jsonFail(400, "check the address: the gateway must be on the same network, and the address cannot be the network or broadcast address");
    else
      c.out = saved(radio_->addressSave(a), "could not save the address");
  } else if (r.is("POST", "/api/settings")) {
    auto port = [&](const char* name, long long current) {  // absent keeps the current port; anything but a plain number is invalid
      std::string text = r.field(name);
      long long value = current;
      return text.empty() || wholeNumber(text, value) ? value : -1LL;
    };
    long long onvif = port("onvif", cfg_.onvifPort), web = port("web", cfg_.webPort), rtsp = port("rtsp", cfg_.rtspPort);
    std::string name = r.field("name"), protect = r.field("protect"), password = r.field("password");
    bool hostsGiven = r.has("hosts");
    std::string hosts = r.field("hosts");
    bool nameOk = validHubName(name);
    // The hub always has a password, which guards the pages, ONVIF and the video: it can be changed, never turned off.
    // A new one has 8 characters or more. A hub that has none, from secrets.h or an older firmware, gets one here first.
    bool keeps = password.empty() && !cfg_.webPassword.empty();
    if (onvif < 1 || onvif > 65535 || web < 1 || web > 65535 || rtsp < 1 || rtsp > 65535 || onvif == web || onvif == rtsp || web == rtsp) {
      c.out = jsonFail(400, "ports must be 1 to 65535 and all different");
    } else if (!nameOk) {
      c.out = jsonFail(400, "the name may use letters, digits and hyphens, up to 63 characters, and cannot start or end with a hyphen");
    } else if (!protect.empty() && protect != "on") {
      c.out = jsonFail(400, "the hub always needs a password; enter a new one to change it");
    } else if (!keeps && (password.size() < 8 || password.size() > 63)) {
      c.out = jsonFail(400, "the password must be 8 to 63 characters");
    } else if (hostsGiven && !camHostListValid(hosts)) {
      c.out = jsonFail(400, "other names: up to 4, separated by commas, of letters, digits, dots and hyphens");
    } else {
      CamSettings s;
      s.onvifPort = (uint16_t)onvif;
      s.webPort = (uint16_t)web;
      s.rtspPort = (uint16_t)rtsp;
      s.name = name;
      s.passwordOn = true;
      s.password = password;
      s.hostsGiven = hostsGiven;
      s.hosts = hosts;
      c.out = saved(store_->settingsSave(s), "could not save the settings");
    }
  } else {
    return false;
  }
  return true;
}

// Per-sensor settings, the webhook and the full event history.
bool CamWeb::apiSensors(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("POST", "/api/sensor-settings")) {
    std::string path = queryParam(r.target, "path");
    std::string hold = r.field("hold"), from = r.field("from"), to = r.field("to"), zone = r.field("zone");
    bool sensor = table_.slotForPath(path) != 0;
    CamSensorSettings s;
    std::string text = (hold.empty() ? "" : "hold=" + hold + "\n") + (from.empty() && to.empty() ? "" : "quiet=" + from + "-" + to + "\n") +
                       (zone.empty() ? "" : "zone=" + zone + "\n");
    if (!sensor) {
      c.out = jsonFail(404, "no such sensor");
    } else if (!camParseSensorSettings(text, s)) {
      c.out = jsonFail(
          400, "the hold must be 1 to 3600 seconds, quiet hours need a start and an end that differ, and the zone must be interior or perimeter");
    } else {
      std::string file = path + "/settings.txt", clean = camSensorSettingsText(s);
      bool ok = clean.empty() ? !storage_.exists(file) || storage_.removeTree(file) : storage_.writeFile(file, clean.data(), clean.size());
      alarm_.rescan();
      c.out = saved(ok, "could not save the settings; storage may be full");
    }
  } else if (r.is("POST", "/api/node-settings")) {
    std::string path = queryParam(r.target, "path"), text;
    for (const char* key : {"late", "silent", "every", "repeat"}) {
      std::string value = r.field(key);
      if (!value.empty()) text += std::string(key) + "=" + value + "\n";
    }
    CamNodeSettings s;
    if (camClassifyPath(path) != CAM_PATH_NODE || !storage_.exists(path)) {
      c.out = jsonFail(404, "no such node");
    } else if (!camParseNodeSettings(text, s)) {
      c.out = jsonFail(
          400,
          "the past-event time must be 1 to 86400 seconds, the silent time 60 to 604800, the report time 60 to 86400, and the repeat time 1 to 60");
    } else {
      std::string file = path + "/settings.txt", clean = camNodeSettingsText(s);
      bool ok = clean.empty() ? !storage_.exists(file) || storage_.removeTree(file) : storage_.writeFile(file, clean.data(), clean.size());
      alarm_.rescan();
      c.out = saved(ok, "could not save the setting; storage may be full");
    }
  } else if (r.is("POST", "/api/walk-test")) {
    std::string path = queryParam(r.target, "path");
    uint8_t mac[6];
    time_t t = chip_->wallTime();
    bool stop = r.field("stop") == "1";
    if (camClassifyPath(path) != CAM_PATH_NODE || !camParseMac(path, mac) || !table_.findNode(mac)) {
      c.out = jsonFail(404, "no such node");
    } else if (t < CAM_CLOCK_VALID_AFTER) {
      c.out = jsonFail(409, "the hub's clock is not set yet, and the node needs it to know when the walk test ends");
    } else {
      alarm_.setWalkTest(mac, stop ? 0 : (uint32_t)t + CAM_WALK_TEST_S);
      c.out = jsonOk(JSON_OK);
    }
  } else if (r.is("GET", "/api/webhook")) {
    reply(c, 200, JSON_TYPE, webhook_.pageJson());
  } else if (r.is("POST", "/api/webhook")) {
    CamWebhook w;
    w.url = r.field("url");
    w.text = r.field("text") == "1";
    w.alerts = r.field("alerts") == "1";
    w.problems = r.field("problems") == "1";
    std::string daily = r.field("daily");
    if (!validHookUrl(w.url)) {
      c.out = jsonFail(400, "enter an address like http://192.168.1.10:8123/path, with an IP address, not a name, and http, not https");
    } else if (!daily.empty() && daily != "-1" && !camNumberIn(daily, 0, 1439, w.dailyMinute)) {
      c.out = jsonFail(400, "the daily check's time must be 0 to 1439 minutes after midnight, or -1 for none");
    } else {
      bool ok = store_->webhookSave(w);
      if (ok) webhook_.setSettings(w);
      c.out = saved(ok, "could not save the notification settings");
    }
  } else if (r.is("POST", "/api/webhook/test")) {
    if (webhook_.settings().url.empty()) {
      c.out = jsonFail(400, "save an address first");
    } else {
      std::string message = "Test from " + cfg_.hostName;
      webhook_.queue(message, jsonObject([&](CamJson& j) { j.str("event", "test").str("hub", cfg_.hostName).str("message", message); }));
      c.out = jsonOk(JSON_OK);
    }
  } else if (r.is("GET", "/api/arm")) {
    long long left = alarm_.disarmSecondsLeft(millis_());
    bool timed = !alarm_.armed() && alarm_.disarmedUntil();
    c.out = jsonOk(jsonObject([&](CamJson& j) {
      j.flag("armed", alarm_.armed()).str("mode", camArmModeName(alarm_.armMode()));
      j.flag("timed", timed).num("left_s", left >= 0, left).num("rearm", alarm_.rearmMinutes());
    }));
  } else if (r.is("POST", "/api/arm")) {
    std::string on = r.field("armed"), mode = r.field("mode"), forText = r.field("for");
    long long minutes = alarm_.rearmMinutes();
    if (mode.empty()) mode = on == "1" ? "away" : on == "0" ? "disarmed" : "";  // armed=1 and 0, as earlier firmware took
    if (mode != "away" && mode != "home" && mode != "disarmed") {
      c.out = jsonFail(400, "mode must be away, home or disarmed");
    } else if (!forText.empty() && (!wholeNumber(forText, minutes) || minutes < 0 || minutes > CAM_MAX_DISARM_MINUTES)) {
      c.out = jsonFail(400, "a disarm lasts 0 to 10080 minutes");
    } else {
      CamArmMode m = mode == "away" ? CAM_ARMED_AWAY : mode == "home" ? CAM_ARMED_HOME : CAM_DISARMED;
      c.out = saved(alarm_.setArmMode(m, "the web page", (uint32_t)minutes), "could not save the setting");
    }
  } else if (r.is("POST", "/api/test-alert")) {
    c.out = alarm_.testAlert(table_.slotForPath(queryParam(r.target, "path"))) ? jsonOk(JSON_OK) : jsonFail(404, "no such sensor");
  } else if (r.is("GET", "/api/overview")) {
    reply(c, 200, JSON_TYPE, overviewJson(millis_()));
  } else if (r.is("POST", "/api/arm/rearm")) {
    long long minutes = -1;
    if (!wholeNumber(r.field("minutes"), minutes) || minutes < 0 || minutes > CAM_MAX_DISARM_MINUTES) {
      c.out = jsonFail(400, "a disarm lasts 0 to 10080 minutes");
    } else {
      bool ok = store_->rearmSave((uint32_t)minutes);
      if (ok) alarm_.setRearmMinutes((uint32_t)minutes);
      c.out = saved(ok, "could not save the setting");
    }
  } else if (r.is("GET", "/api/mqtt")) {
    reply(c, 200, JSON_TYPE, mqtt_.pageJson());
  } else if (r.is("POST", "/api/mqtt")) {
    std::string port = r.field("port");
    CamMqtt m;
    m.host = r.field("host");
    m.user = r.field("user");
    m.password = r.field("password");
    long long n = 1883;
    uint32_t ip;
    bool portOk = (port.empty() || wholeNumber(port, n)) && n >= 1 && n <= 65535;
    if (!m.host.empty() && !parseIpv4(m.host, ip)) {
      c.out = jsonFail(400, "enter the broker's IP address, such as 192.168.1.10; names are not supported");
    } else if (!portOk) {
      c.out = jsonFail(400, "the port must be 1 to 65535");
    } else if (!validMqttText(m.user) || !validMqttText(m.password)) {
      c.out = jsonFail(400, "the user name and password may have up to 64 characters");
    } else {
      m.port = (uint16_t)n;
      if (m.password.empty() && !m.user.empty()) m.password = mqtt_.settings().password;  // left blank, the saved one is kept
      if (m.user.empty()) m.password.clear();
      bool ok = store_->mqttSave(m);
      if (ok) mqtt_.setSettings(m, millis_());  // the new settings apply at once
      c.out = saved(ok, "could not save the MQTT settings");
    }
  } else if (r.is("GET", "/api/events.csv")) {  // read from storage 1 KB at a time, because it is far larger than any free block
    // ?sensor=AA-BB-CC-DD-EE-01/1&days=7: one sensor's, or the hub's, over the last days, as each sensor's page shows.
    std::string sensor = queryParam(r.target, "sensor"), daysText = queryParam(r.target, "days");
    int days = 0;
    if (!daysText.empty() && !camNumberIn(daysText, 1, 3650, days)) {
      c.out = jsonFail(400, "days must be 1 to 3650");
      return true;
    }
    time_t now = chip_->wallTime();
    if (days && now < CAM_CLOCK_VALID_AFTER) {
      c.out = jsonFail(503, "the hub's clock is not set yet, so it cannot tell which days those are; try again");
      return true;
    }
    if (downloadingHistory()) {  // one download at a time, as the log keeps one place it has got to
      c.out = jsonFail(503, "the history is being downloaded already; try again when that is done");
      return true;
    }
    alarm_.log().save(false);
    alarm_.log().csvBegin(sensor, days ? now - (time_t)days * 86400 : 0);  // made as it is sent, so its size is not known in advance
    c.out =
        "HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\nContent-Disposition: attachment; filename=\"hub-events.csv\"\r\n"
        "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    c.stream = &csvRead_;
  } else {
    return false;
  }
  return true;
}

void CamWeb::apiFiles(const ApiRequest& r) {
  HttpConn& c = r.c;
  const std::string& method = r.method;
  bool fileRoute = r.route == "/api/file", mkdirRoute = r.route == "/api/mkdir";
  if (!fileRoute && !mkdirRoute) {
    c.out = jsonFail(404, "not found");
    return;
  }
  std::string path = queryParam(r.target, "path");
  CamPathKind kind = camClassifyPath(path);
  bool isFile = kind == CAM_PATH_FILE || kind == CAM_PATH_CLEAR;
  bool isText = kind == CAM_PATH_TEXT;
  bool isPreview = kind == CAM_PATH_PREVIEW;
  const char* body = r.body;
  size_t bodyLen = r.bodyLen;
  if (kind == CAM_PATH_INVALID) {
    c.out = jsonFail(400, "invalid path");
    return;
  }
  if (method != "GET" && method != "DELETE" && !table_.sensorFits(path)) {  // it would be stored and then never do anything
    c.out = jsonFail(409, "the hub already has its limit of " + std::to_string(CAM_MAX_SENSORS) + " sensors; delete one first");
    return;
  }
  if (fileRoute && method == "PUT" && (isFile || isPreview) && !roomBesideHistory(bodyLen)) {
    c.out = jsonFail(507, "storage is nearly full, and the rest is kept for the event history; delete some pictures first");
    return;
  }
  if (mkdirRoute && method == "POST" && !isFile && !isText && !isPreview) {
    bool ok = storage_.makeDirs(path);
    alarm_.rescan();
    c.out = saved(ok, "could not create the folder");
  } else if (fileRoute && method == "PUT" && isPreview) {
    if (bodyLen < 4 || memcmp(body, "\xFF\xD8\xFF", 3) != 0)
      c.out = jsonFail(422, "not a JPEG");
    else
      c.out = saved(storage_.writeFile(path, body, bodyLen), "could not save the preview; storage may be full");
  } else if (fileRoute && method == "GET" && (isFile || isText || isPreview)) {
    if (!sendFile(c, path, isText ? JSON_TYPE : isPreview ? "image/jpeg" : "video/h264")) c.out = jsonFail(404, "no such file");
  } else if (fileRoute && method == "PUT" && isText) {
    if (bodyLen > (path == "plan.json" ? 2048u : 1024u)) {  // the plan holds a place for each of up to 32 sensors
      c.out = jsonFail(413, "text is too long");
    } else {
      c.out = saved(storage_.writeFile(path, body, bodyLen), "could not save the text; storage may be full");
      if (path.find('/') != std::string::npos && path.compare(path.size() - 10, 10, "/text.json") != 0) alarm_.rescan();  // a name or settings
    }
  } else if (fileRoute && method == "PUT" && isFile) {
    CamNal nals[3];
    if (!camParseFrame((const uint8_t*)body, bodyLen, nals)) {
      c.out = jsonFail(422, "not a single H.264 picture with SPS, PPS and IDR");
    } else if (!camCompatibleSps(nals[0], CAM_FRAMES[0].nals[0])) {
      c.out = jsonFail(422, "wrong video format: make it with tools/make_frames.py");
    } else {
      // Stored with the hub's own SPS, so the NVR sees one unchanging format whichever encoder made the picture.
      const CamNal parts[3] = {CAM_FRAMES[0].nals[0], nals[1], nals[2]};
      if (parts[0].len + parts[1].len + parts[2].len + 12 > CAM_MAX_FRAME) {
        c.out = jsonFail(413, "file is larger than 32 KB");
        return;
      }
      bool ok = storage_.writeNals(path, parts, 3);
      if (ok) {
        if (storage_.exists(camPreviewPath(path))) storage_.removeTree(camPreviewPath(path));  // it shows the old picture
        alarm_.rescan();
      }
      c.out = saved(ok, "could not save the file; storage may be full");
    }
  } else if (fileRoute && method == "DELETE") {
    bool ok = storage_.exists(path) && storage_.removeTree(path);
    if (ok && isFile && storage_.exists(camPreviewPath(path))) storage_.removeTree(camPreviewPath(path));
    alarm_.rescan();
    c.out = ok ? jsonOk(JSON_OK) : jsonFail(404, "nothing to delete at that path");
  } else {
    c.out = jsonFail(400, "unsupported request");
  }
}
