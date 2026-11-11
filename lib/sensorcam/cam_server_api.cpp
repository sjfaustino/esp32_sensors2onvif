// SensorCamServer: the web page's API, and the JSON it reads.
#include "cam_server.h"
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

std::string camJsonEscape(const std::string& s) {
  std::string out;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') out += '\\';
    if ((unsigned char)ch >= 0x20) out += ch;
  }
  return out;
}

static bool hasFile(const std::vector<CamFsEntry>& items, const std::string& name) {
  for (const CamFsEntry& e : items)
    if (!e.dir && e.name == name) return true;
  return false;
}

// Built as one string that grows only when the memory for it is there, and is never copied.
// Empty when the hub is too short of memory to build it.
std::string SensorCamServer::treeJson() {
  uint32_t used = 0, total = 0;
  if (cfg_.fsUsage) cfg_.fsUsage(used, total);
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
  std::string clear = "null";
  for (const CamFsEntry& s : root)
    if (!s.dir && s.name == "clear.h264") clear = std::to_string(s.size);
  out += "{\"used\":" + std::to_string(used) + ",\"total\":" + std::to_string(total) + ",\"clear\":" + clear + ",\"clearPreview\":" + (hasFile(root, "clear.jpg") ? "true" : "false") + ",\"nodes\":[";
  bool firstNode = true;
  for (const CamFsEntry& s : root) {
    if (!s.dir || camClassifyPath(s.name) != CAM_PATH_NODE) continue;
    if (!makeRoom(80)) return std::string();
    CamNodeSettings node;
    std::string nodeText;
    if (!storage_.readFile(s.name + "/settings.txt", nodeText, 256) || !camParseNodeSettings(nodeText, node)) node = CamNodeSettings();
    out += std::string(firstNode ? "" : ",") + "{\"mac\":\"" + s.name + "\",\"late\":" + (node.lateS ? std::to_string(node.lateS) : std::string("null")) + ",\"sensors\":[";
    firstNode = false;
    uint8_t mac[6];
    camParseMac(s.name, mac);
    std::vector<CamFsEntry> inputs;
    storage_.list(s.name, inputs);
    bool firstSensor = true;
    for (const CamFsEntry& n : inputs) {
      std::string base = s.name + "/" + n.name;
      if (!n.dir || camClassifyPath(base) != CAM_PATH_SENSOR) continue;
      int slot = findSlot(mac, (uint8_t)atoi(n.name.c_str()));
      if (!slot) continue;  // beyond the sensor limit, so it does nothing
      if (!makeRoom(960)) return std::string();  // the longest possible entry, with all ten pictures
      const CamSensorSettings& set = slots_[slot - 1].settings;
      out += std::string(firstSensor ? "" : ",") + "{\"input\":" + n.name + ",\"name\":\"" + camJsonEscape(slots_[slot - 1].name) +
             "\",\"hold\":" + (set.holdS ? std::to_string(set.holdS) : std::string("null")) +
             ",\"quiet\":" + (set.quietFrom >= 0 ? "[" + std::to_string(set.quietFrom) + "," + std::to_string(set.quietTo) + "]" : std::string("null")) + ",\"files\":[";
      firstSensor = false;
      std::vector<CamFsEntry> items;
      storage_.list(base, items);
      bool firstFile = true;
      for (const CamFsEntry& f : items) {
        if (f.dir || camClassifyPath(base + "/" + f.name) != CAM_PATH_FILE) continue;
        out += std::string(firstFile ? "" : ",") + "{\"name\":\"" + f.name + "\",\"size\":" + std::to_string(f.size) + ",\"preview\":" + (hasFile(items, camPreviewPath(f.name)) ? "true" : "false") + "}";
        firstFile = false;
      }
      out += "]}";
    }
    out += "]}";
  }
  out += "]}\n";
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
static bool wholeNumber(const std::string& s, long long& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  out = strtoll(s.c_str(), &end, 10);
  return *end == 0;
}

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

bool camBackupValueValid(const std::string& key, const std::string& value) {
  long long n = 0;
  bool number = wholeNumber(value, n);
  uint32_t ip;
  if (key == "net.onvif" || key == "net.web" || key == "net.rtsp") return number && n >= 1 && n <= 65535;
  if (key == "net.ntp" || key == "net.rben" || key == "net.hlth" || key == "net.enuns" || key == "net.hooktx" || key == "net.hookal" || key == "net.hookpr")
    return value == "0" || value == "1";
  if (key == "net.enkey") return value.size() >= 16 && validToken(value);
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
  if (key == "wifi.ssid" || key == "wifi.ssid2" || key == "net.ipfor") return value.size() <= 32;
  if (key == "wifi.password" || key == "wifi.password2") return value.empty() || (value.size() >= 8 && value.size() <= 63);
  return false;
}

static std::string peerText(uint32_t peer) {
  const uint8_t* b = (const uint8_t*)&peer;
  return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]);
}

void SensorCamServer::handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body, size_t bodyLen) {
  std::string route = target.substr(0, target.find('?'));
  // On the setup network the password of that network already guards these, and a captive-portal window may not offer a login.
  bool inSetup = cfg_.setupActive();
  bool setupRoute = route == "/setup" || route == "/api/wifi" || route == "/api/wifi/scan" || (inSetup && route == "/");
  if (!(inSetup && setupRoute) && !authorized(headers)) {
    c.out = UNAUTHORIZED;
    return;
  }
  const ApiRequest r{c, method, route, target, body, bodyLen};
  if (setupRoute) apiSetup(r);
  else if (!apiPages(r) && !apiInfo(r) && !apiSystem(r) && !apiNetwork(r) && !apiSensors(r)) apiFiles(r);
}

void SensorCamServer::apiSetup(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.method == "GET" && (r.route == "/" || r.route == "/setup")) {
    sendBody(c, HTML_TYPE, CAM_SETUP_PAGE, sizeof(CAM_SETUP_PAGE) - 1);
  } else if (r.is("GET", "/api/wifi/scan")) {
    c.out = jsonOk(cfg_.wifiNetworks());
  } else if (r.is("POST", "/api/wifi")) {
    std::string form = r.form();
    std::string ssid = percentDecode(queryParam(form, "ssid")), password = percentDecode(queryParam(form, "password"));
    if (ssid.empty() || ssid.size() > 32 || (!password.empty() && (password.size() < 8 || password.size() > 63))) c.out = jsonFail(400, "the name must be 1 to 32 characters and the password empty or 8 to 63");
    else c.out = saved(cfg_.wifiSave(ssid, password, queryParam(form, "role") == "backup"), "could not save the Wi-Fi settings");
  } else {
    c.out = jsonFail(404, "not found");
  }
}

bool SensorCamServer::apiPages(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("GET", "/") || r.is("GET", "/files")) {
    sendBody(c, HTML_TYPE, CAM_WEB_PAGE, sizeof(CAM_WEB_PAGE) - 1);
  } else if (r.is("GET", "/api/tree")) {
    c.body = treeJson();
    if (c.body.empty()) c.out = jsonFail(507, "the hub is short of memory; try again");
    else sendBody(c, JSON_TYPE, c.body.data(), c.body.size());
  } else if (r.is("GET", "/api/live")) {
    if (!frame_) frameToShow(millis_());
    c.out = jsonOk("{\"seq\":" + std::to_string(liveSeq_) + ",\"file\":\"" + (frameFile_.empty() ? std::string("built-in") : frameFile_) + "\"}\n");
  } else if (r.is("GET", "/api/live/frame")) {
    if (!frame_) frameToShow(millis_());
    std::string* out = borrowFileOut(c);
    if (!out) {
      c.out = jsonFail(507, "the hub is busy; try again");
      return true;
    }
    out->clear();
    for (int i = 0; i < frame_->count; i++) {
      out->append("\0\0\0\1", 4);
      out->append((const char*)frame_->nals[i].data, frame_->nals[i].len);
    }
    sendBody(c, "video/h264", out->data(), out->size(), "X-Frame-Seq: " + std::to_string(liveSeq_) + "\r\n");
  } else {
    return false;
  }
  return true;
}

// Sized for the sensors that exist, and empty when the hub is too short of memory to build it.
std::string SensorCamServer::espNowJson() {
  int known = 0;
  for (const Slot& s : slots_) known += s.used;
  size_t room = (size_t)known * 330 + MAX_UNKNOWN * 120 + 256;  // the longest possible entries
  if (!roomFor(room)) return std::string();
  std::string out;
  out.reserve(room);
  out += "{" + (cfg_.espNowInfo ? cfg_.espNowInfo() : std::string("\"mac\":\"\",\"channel\":0")) + ",\"key\":\"" + cfg_.espNowKey +
         "\",\"unsigned\":" + (cfg_.espNowUnsigned ? "true" : "false") + ",\"nodes\":[";
  bool first = true;
  uint32_t now = millis_();
  auto optional = [](bool known, long long value) { return known ? std::to_string(value) : std::string("null"); };
  for (int id = 1; id <= sensors_.count(); id++) {
    if (!slots_[id - 1].used) continue;
    const Health& h = health_[id - 1];
    std::string name = slotName(id);
    const char* state = sensors_.active(id) ? "active" : h.silent ? "silent" : !h.heard ? "never" : "idle";
    out += std::string(first ? "" : ",") + "{\"low\":" + (h.lowBattery ? "true" : "false") + ",\"name\":\"" + camJsonEscape(slots_[id - 1].name) + "\",\"node\":\"" + name.substr(0, name.find('/')) + "\",\"input\":" + std::to_string(slots_[id - 1].input) +
             ",\"state\":\"" + state + "\",\"ago\":" + optional(h.heard, (now - h.heardAt) / 1000) +
             ",\"battery\":" + optional(h.heard && h.battery != CAM_BATTERY_UNKNOWN, h.battery) + ",\"rssi\":" + optional(h.rssi != 0, h.rssi) +
             ",\"counter\":" + optional(h.counterSeen, h.counter) + ",\"weak\":" + (h.weak ? "true" : "false") +
             ",\"refused\":" + std::to_string(h.refused) + ",\"refusal\":\"" + h.refusal + "\"}";
    first = false;
  }
  out += "],\"unknown\":[";
  first = true;
  for (const UnknownSender& u : unknown_) {
    if (!u.used || findSlot(u.mac, u.input)) continue;  // added since it was last heard
    out += std::string(first ? "" : ",") + "{\"mac\":\"" + camMacText(u.mac) + "\",\"input\":" + std::to_string(u.input) + ",\"ago\":" + std::to_string((now - u.heardAt) / 1000) +
               ",\"rssi\":" + optional(u.rssi != 0, u.rssi) + ",\"messages\":" + std::to_string(u.messages) + "}";
    first = false;
  }
  out += "]}\n";
  return out;
}

bool SensorCamServer::apiInfo(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("GET", "/api/device")) {
    std::string rows = cfg_.deviceRows();
    int known = 0;
    for (const Slot& s : slots_) known += s.used;
    uint32_t used = 0, total = 0;
    if (cfg_.fsUsage) cfg_.fsUsage(used, total);
    c.out = jsonOk("{\"rows\":[" + rows + (rows.empty() ? "" : ",") + "[\"Picture storage\",\"" + std::to_string(used / 1024) + " KB used of " + std::to_string(total / 1024) + " KB\"]," +
                   "[\"Sensors\",\"" + std::to_string(known) + "\"],[\"Video streams open\",\"" + std::to_string(streamCount()) + "\"],[\"Event subscribers\",\"" + std::to_string(subs_.count()) + "\"]" +
                   (eventsSaveFailed_ ? ",[\"Event history\",\"Could not be saved; storage may be full\"]" : "") + "],\"crash\":" +
                   std::to_string(cfg_.crashSize ? cfg_.crashSize() : 0) + "}\n");
  } else if (r.is("GET", "/api/crash")) {  // read from flash 1 KB at a time, because it can be larger than any free block
    size_t size = cfg_.crashSize && cfg_.crashRead ? cfg_.crashSize() : 0;
    if (!size) {
      c.out = jsonFail(404, "no crash report is saved");
    } else {
      c.out = httpHead(200, "application/octet-stream", size, "Content-Disposition: attachment; filename=\"hub-crash.bin\"\r\n");
      c.pull = &cfg_.crashRead;
      c.staticLen = size;
    }
  } else if (r.is("POST", "/api/crash/delete")) {
    c.out = saved(cfg_.crashDelete && cfg_.crashDelete(), "could not delete the crash report");
  } else if (r.is("GET", "/api/events")) {
    c.out = jsonOk(eventsJson());
  } else if (r.is("GET", "/api/espnow")) {
    c.body = espNowJson();
    if (c.body.empty()) c.out = jsonFail(507, "the hub is short of memory; try again");
    else sendBody(c, JSON_TYPE, c.body.data(), c.body.size());
  } else if (r.is("GET", "/api/info")) {
    std::string unsupported;
    for (const std::string& seen : unsupported_)
      if (!seen.empty()) unsupported += (unsupported.empty() ? "\"" : ",\"") + seen + "\"";
    c.out = jsonOk("{\"unsupported\":[" + unsupported + "],\"token\":\"" + cfg_.sensorToken + "\",\"onvifLogin\":" + (cfg_.onvifPassword.empty() ? "false" : "true") + ",\"ip\":\"" + localIp_() + "\",\"name\":\"" + cfg_.hostName + "\",\"onvifPort\":" + std::to_string(cfg_.onvifPort) +
                   ",\"webPort\":" + std::to_string(cfg_.webPort) + ",\"rtspPort\":" + std::to_string(cfg_.rtspPort) +
                   ",\"streams\":" + std::to_string(streamCount()) + ",\"subscribers\":" + std::to_string(subs_.count()) +
                   ",\"serial\":\"" + cfg_.serial + "\",\"password\":" + (cfg_.webPassword.empty() ? "false" : "true") + "}\n");
  } else {
    return false;
  }
  return true;
}

bool SensorCamServer::apiSystem(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("GET", "/api/time")) {
    c.out = jsonOk(cfg_.timeJson());
  } else if (r.is("POST", "/api/time")) {
    std::string form = r.form(), epochText = queryParam(form, "epoch");
    CamTime t;
    t.ntp = queryParam(form, "mode") != "manual";
    t.server = queryParam(form, "server");
    long long offset = 0, dst = 0, epoch = 0;
    bool ok = wholeNumber(queryParam(form, "offset"), offset) && validOffset(offset) && wholeNumber(queryParam(form, "dst"), dst) && dst >= 0 && dst <= 2;
    if (t.ntp) ok = ok && validNtpServer(t.server);
    else ok = ok && (t.server.empty() || validNtpServer(t.server)) && (epochText.empty() || (wholeNumber(epochText, epoch) && epoch > (long long)CAM_CLOCK_VALID_AFTER && epoch < 4102444800LL));
    if (!ok) {
      c.out = jsonFail(400, "check the timezone, the NTP server name and the date and time");
    } else {
      t.offsetMinutes = (int)offset;
      t.dst = (int)dst;
      t.epoch = epoch;
      c.out = saved(cfg_.timeSave(t), "could not save the time settings");
    }
  } else if (r.is("GET", "/api/schedule")) {
    c.out = jsonOk(std::string("{\"on\":") + (cfg_.schedule.on ? "true" : "false") + ",\"day\":" + std::to_string(cfg_.schedule.day) + ",\"minute\":" + std::to_string(cfg_.schedule.minute) + "}\n");
  } else if (r.is("POST", "/api/schedule")) {
    std::string form = r.form();
    long long day = 0, minute = 0;
    if (!wholeNumber(queryParam(form, "day"), day) || day < -1 || day > 6 || !wholeNumber(queryParam(form, "minute"), minute) || minute < 0 || minute > 1439) {
      c.out = jsonFail(400, "check the day and the time");
    } else {
      CamSchedule s;
      s.on = queryParam(form, "on") == "1";
      s.day = (int)day;
      s.minute = (int)minute;
      bool ok = cfg_.scheduleSave(s);
      if (ok) cfg_.schedule = s;
      c.out = saved(ok, "could not save the schedule");
    }
  } else if (r.is("POST", "/api/reboot")) {
    c.out = saved(cfg_.maintenance("reboot"), "could not do that");
  } else if (r.is("POST", "/api/reset")) {
    if (queryParam(r.form(), "confirm") != "yes") c.out = jsonFail(400, "not confirmed");  // erases Wi-Fi and every setting, so a stray request must not do it
    else c.out = saved(cfg_.maintenance("reset"), "could not do that");
  } else if (r.is("GET", "/api/alerts")) {
    c.out = jsonOk(std::string("{\"alarm\":") + (cfg_.healthAlarm ? "true" : "false") + "}\n");
  } else if (r.is("POST", "/api/alerts")) {
    bool on = queryParam(r.form(), "alarm") == "1", ok = cfg_.healthSave(on);
    if (ok) cfg_.healthAlarm = on;
    c.out = saved(ok, "could not save the setting");
  } else if (r.is("GET", "/api/backup")) {
    c.out = httpResponse(200, "text/plain", cfg_.backupText());
  } else if (r.is("POST", "/api/restore")) {
    c.out = cfg_.restoreText(std::string(r.body, r.bodyLen)) ? jsonOk(JSON_OK) : jsonFail(400, "could not restore that file: it is damaged, or the hub's settings storage is full");
  } else {
    return false;
  }
  return true;
}

bool SensorCamServer::apiNetwork(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("GET", "/api/nvr")) {
    uint32_t nvr = cfg_.nvrIp.empty() ? 0 : inet_addr(cfg_.nvrIp.c_str());
    bool video = false, events = false;
    std::string viewers;
    for (RtspConn& v : rtsp_) {
      if (v.fd < 0 || !v.playing) continue;
      viewers += (viewers.empty() ? "\"" : ",\"") + peerText(v.peer) + "\"";
      video = video || (nvr && v.peer == nvr);
    }
    for (int i = 0; i < SubTable::SIZE; i++) events = events || (nvr && subs_.at(i).used && subs_.at(i).peer == nvr);
    c.out = jsonOk("{\"ip\":\"" + cfg_.nvrIp + "\",\"video\":" + (video ? "true" : "false") + ",\"events\":" + (events ? "true" : "false") + ",\"viewers\":[" + viewers + "]}\n");
  } else if (r.is("POST", "/api/nvr")) {
    std::string ip = queryParam(r.form(), "ip");
    uint32_t parsed;
    if (!ip.empty() && !parseIpv4(ip, parsed)) {
      c.out = jsonFail(400, "check the IP address");
    } else {
      bool ok = cfg_.nvrSave(ip);
      if (ok) cfg_.nvrIp = ip;
      c.out = saved(ok, "could not save the NVR address");
    }
  } else if (r.is("GET", "/api/network")) {
    c.out = jsonOk(cfg_.addressJson());
  } else if (r.is("POST", "/api/network")) {
    std::string form = r.form();
    CamAddress a;
    a.dhcp = queryParam(form, "mode") != "static";
    a.ip = queryParam(form, "ip");
    a.mask = queryParam(form, "mask");
    a.gateway = queryParam(form, "gateway");
    a.dns = queryParam(form, "dns");
    if (!a.dhcp && !validAddress(a)) c.out = jsonFail(400, "check the address: the gateway must be on the same network, and the address cannot be the network or broadcast address");
    else c.out = saved(cfg_.addressSave(a), "could not save the address");
  } else if (r.is("POST", "/api/settings")) {
    std::string form = r.form();
    auto port = [&](const char* name, long long current) {  // absent keeps the current port; anything but a plain number is invalid
      std::string text = queryParam(form, name);
      long long value = current;
      return text.empty() || wholeNumber(text, value) ? value : -1LL;
    };
    long long onvif = port("onvif", cfg_.onvifPort), web = port("web", cfg_.webPort), rtsp = port("rtsp", cfg_.rtspPort);
    std::string name = queryParam(form, "name"), protect = queryParam(form, "protect"), password = percentDecode(queryParam(form, "password"));
    bool nameOk = validHubName(name);
    bool passwordOn = protect.empty() ? !cfg_.webPassword.empty() : protect == "on";
    if (onvif < 1 || onvif > 65535 || web < 1 || web > 65535 || rtsp < 1 || rtsp > 65535 || onvif == web || onvif == rtsp || web == rtsp) {
      c.out = jsonFail(400, "ports must be 1 to 65535 and all different");
    } else if (!nameOk) {
      c.out = jsonFail(400, "the name may use letters, digits and hyphens, up to 63 characters, and cannot start or end with a hyphen");
    } else if (password.size() > 63 || (passwordOn && password.empty() && cfg_.webPassword.empty())) {
      c.out = jsonFail(400, "enter a password of up to 63 characters");
    } else {
      CamSettings s;
      s.onvifPort = (uint16_t)onvif;
      s.webPort = (uint16_t)web;
      s.rtspPort = (uint16_t)rtsp;
      s.name = name;
      s.passwordOn = passwordOn;
      s.password = password;
      c.out = saved(cfg_.settingsSave(s), "could not save the settings");
    }
  } else {
    return false;
  }
  return true;
}

// ESP-NOW signing, per-sensor settings, the webhook and the full event history.
bool SensorCamServer::apiSensors(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.is("POST", "/api/espnow")) {
    bool on = queryParam(r.form(), "unsigned") == "1", ok = cfg_.espNowSave(on);
    if (ok) cfg_.espNowUnsigned = on;
    c.out = saved(ok, "could not save the setting");
  } else if (r.is("POST", "/api/sensor-settings")) {
    std::string path = queryParam(r.target, "path"), form = r.form();
    std::string hold = queryParam(form, "hold"), from = queryParam(form, "from"), to = queryParam(form, "to");
    uint8_t mac[6];
    size_t slash = path.find('/');
    bool sensor = camClassifyPath(path) == CAM_PATH_SENSOR && camParseMac(path.substr(0, slash), mac) && findSlot(mac, (uint8_t)atoi(path.c_str() + slash + 1));
    CamSensorSettings s;
    std::string text = (hold.empty() ? "" : "hold=" + hold + "\n") + (from.empty() && to.empty() ? "" : "quiet=" + from + "-" + to + "\n");
    if (!sensor) {
      c.out = jsonFail(404, "no such sensor");
    } else if (!camParseSensorSettings(text, s)) {
      c.out = jsonFail(400, "the hold must be 1 to 3600 seconds, and quiet hours need a start and an end that differ");
    } else {
      std::string file = path + "/settings.txt", clean = camSensorSettingsText(s);
      bool ok = clean.empty() ? !storage_.exists(file) || storage_.removeTree(file) : storage_.writeFile(file, clean.data(), clean.size());
      rescan();
      c.out = saved(ok, "could not save the settings; storage may be full");
    }
  } else if (r.is("POST", "/api/node-settings")) {
    std::string path = queryParam(r.target, "path"), late = queryParam(r.form(), "late");
    CamNodeSettings s;
    if (camClassifyPath(path) != CAM_PATH_NODE || !storage_.exists(path)) {
      c.out = jsonFail(404, "no such node");
    } else if (!camParseNodeSettings(late.empty() ? "" : "late=" + late + "\n", s)) {
      c.out = jsonFail(400, "the time must be 1 to 86400 seconds");
    } else {
      std::string file = path + "/settings.txt", clean = camNodeSettingsText(s);
      bool ok = clean.empty() ? !storage_.exists(file) || storage_.removeTree(file) : storage_.writeFile(file, clean.data(), clean.size());
      rescan();
      c.out = saved(ok, "could not save the setting; storage may be full");
    }
  } else if (r.is("GET", "/api/webhook")) {
    c.out = jsonOk(webhookJson());
  } else if (r.is("POST", "/api/webhook")) {
    std::string form = r.form();
    CamWebhook w;
    w.url = percentDecode(queryParam(form, "url"));
    w.text = queryParam(form, "text") == "1";
    w.alerts = queryParam(form, "alerts") == "1";
    w.problems = queryParam(form, "problems") == "1";
    if (!validHookUrl(w.url)) {
      c.out = jsonFail(400, "enter an address like http://192.168.1.10:8123/path, with an IP address, not a name, and http, not https");
    } else {
      bool ok = cfg_.webhookSave(w);
      if (ok) cfg_.webhook = w;
      c.out = saved(ok, "could not save the notification settings");
    }
  } else if (r.is("POST", "/api/webhook/test")) {
    if (cfg_.webhook.url.empty()) {
      c.out = jsonFail(400, "save an address first");
    } else {
      std::string message = "Test from " + cfg_.hostName;
      queueHook(message, "{\"event\":\"test\",\"hub\":\"" + cfg_.hostName + "\",\"message\":\"" + message + "\"}");
      c.out = jsonOk(JSON_OK);
    }
  } else if (r.is("GET", "/api/events.csv")) {  // read from storage 1 KB at a time, because it is far larger than any free block
    flushCsv();
    csvOldBytes_ = storage_.fileSize(EVENTS_CSV_OLD);
    size_t total = csvOldBytes_ + storage_.fileSize(EVENTS_CSV);
    c.out = httpHead(200, "text/csv; charset=utf-8", sizeof(CSV_HEADER) - 1 + total, "Content-Disposition: attachment; filename=\"hub-events.csv\"\r\n") + CSV_HEADER;
    c.pull = &csvRead_;
    c.staticLen = total;
  } else {
    return false;
  }
  return true;
}

void SensorCamServer::apiFiles(const ApiRequest& r) {
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
  if (method != "GET" && method != "DELETE" && !sensorFits(path)) {  // it would be stored and then never do anything
    c.out = jsonFail(409, "the hub already has its limit of " + std::to_string(CAM_MAX_SENSORS) + " sensors; delete one first");
    return;
  }
  if (mkdirRoute && method == "POST" && !isFile && !isText && !isPreview) {
    bool ok = storage_.makeDirs(path);
    rescan();
    c.out = saved(ok, "could not create the folder");
  } else if (fileRoute && method == "PUT" && isPreview) {
    if (bodyLen < 4 || memcmp(body, "\xFF\xD8\xFF", 3) != 0) c.out = jsonFail(422, "not a JPEG");
    else c.out = saved(storage_.writeFile(path, body, bodyLen), "could not save the preview; storage may be full");
  } else if (fileRoute && method == "GET" && (isFile || isText || isPreview)) {
    size_t size = storage_.fileSize(path);
    if (size == 0) {
      c.out = jsonFail(404, "no such file");
    } else if (!borrowFileOut(c)) {
      c.out = jsonFail(507, "the hub is busy; try again");
    } else if (!storage_.readFile(path, *fileOut_, CAM_MAX_FRAME)) {
      c.out = jsonFail(404, "no such file");
    } else {
      sendBody(c, isText ? JSON_TYPE : isPreview ? "image/jpeg" : "video/h264", fileOut_->data(), fileOut_->size());
    }
  } else if (fileRoute && method == "PUT" && isText) {
    if (bodyLen > 1024) {
      c.out = jsonFail(413, "text is too long");
    } else {
      c.out = saved(storage_.writeFile(path, body, bodyLen), "could not save the text; storage may be full");
      if (path.find('/') != std::string::npos && path.compare(path.size() - 10, 10, "/text.json") != 0) rescan();  // a name or settings
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
        rescan();
      }
      c.out = saved(ok, "could not save the file; storage may be full");
    }
  } else if (fileRoute && method == "DELETE") {
    bool ok = storage_.exists(path) && storage_.removeTree(path);
    if (ok && isFile && storage_.exists(camPreviewPath(path))) storage_.removeTree(camPreviewPath(path));
    rescan();
    c.out = ok ? jsonOk(JSON_OK) : jsonFail(404, "nothing to delete at that path");
  } else {
    c.out = jsonFail(400, "unsupported request");
  }
}
