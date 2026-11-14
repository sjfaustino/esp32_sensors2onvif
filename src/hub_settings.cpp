#include "hub_settings.h"
#include <Preferences.h>
#include <vector>
#include "config.h"

HubSettings settings;

// Opens a settings namespace around one job. False when it could not be opened or the job failed.
template <typename Job>
static bool withPrefs(const char* space, Job job) {
  Preferences p;
  if (!p.begin(space, false)) return false;
  bool ok = job(p);
  p.end();
  return ok;
}

// Read back, because saving an empty value reports the same as a failure.
static bool putText(Preferences& p, const char* key, const String& value) {
  p.putString(key, value);
  return p.getString(key, "") == value;
}

static String randomHex(int words) {
  String out;
  char word[9];
  for (int i = 0; i < words; i++) {
    snprintf(word, sizeof(word), "%08lx", (unsigned long)esp_random());
    out += word;
  }
  return out;
}

void loadSettings() {
  HubSettings& s = settings;
  s.wifiSsid[0] = WIFI_SSID;
  s.wifiPassword[0] = WIFI_PASSWORD;
  withPrefs("wifi", [&](Preferences& p) {
    s.wifiSsid[0] = p.getString("ssid", s.wifiSsid[0]);
    s.wifiPassword[0] = p.getString("password", s.wifiPassword[0]);
    s.wifiSsid[1] = p.getString("ssid2", "");
    s.wifiPassword[1] = p.getString("password2", "");
    return true;
  });
  s.onvifPort = ONVIF_PORT;
  s.webPort = WEB_PORT;
  s.rtspPort = RTSP_PORT;
  s.hubName = MDNS_NAME;
  s.webPassword = WEB_PASSWORD;
  s.sensorToken = SENSOR_TOKEN;
  s.ntpServer = NTP_SERVER;
  withPrefs("net", [&](Preferences& p) {
    s.onvifPort = p.getUShort("onvif", ONVIF_PORT);
    s.webPort = p.getUShort("web", WEB_PORT);
    s.rtspPort = p.getUShort("rtsp", RTSP_PORT);
    s.hubName = p.getString("name", MDNS_NAME);
    s.extraHosts = p.getString("hosts", "");
    // A hub is password protected unless someone turns that off, so an empty setting falls back to the setup network's password.
    s.webPassword = p.getString("webpw", strlen(WEB_PASSWORD) ? WEB_PASSWORD : SETUP_AP_PASSWORD);
    s.sensorToken = p.getString("token", SENSOR_TOKEN);
    if (s.sensorToken.isEmpty()) {  // made once and kept, and shown on the HTTP page
      s.sensorToken = randomHex(2);
      p.putString("token", s.sensorToken);
    }
    s.espNowKey = p.getString("enkey", "");
    if (s.espNowKey.length() < 16) {  // made once and kept, and shown on the ESP-NOW page
      s.espNowKey = randomHex(4);
      p.putString("enkey", s.espNowKey);
    }
    s.pairingKey = p.getString("pairkey", "");
    if (s.pairingKey.length() != 64) {  // made once and kept: paired nodes keep their keys either way, but the hub's fingerprint stays the same
      s.pairingKey = randomHex(8);
      p.putString("pairkey", s.pairingKey);
    }
    s.snapshotKey = p.getString("snapkey", "");
    if (s.snapshotKey.length() < 16) {  // made once and kept; a settings reset makes a new one
      s.snapshotKey = randomHex(4);
      p.putString("snapkey", s.snapshotKey);
    }
    s.espNowUnsigned = p.getBool("enuns", false);
    s.webhook.url = p.getString("hook", "").c_str();
    s.webhook.text = p.getBool("hooktx", false);
    s.webhook.alerts = p.getBool("hookal", true);
    s.webhook.problems = p.getBool("hookpr", true);
    s.mqtt.host = p.getString("mqhost", "").c_str();
    s.mqtt.port = p.getUShort("mqport", 1883);
    s.mqtt.user = p.getString("mquser", "").c_str();
    s.mqtt.password = p.getString("mqpw", "").c_str();
    // armmode; firmware before it kept "armed" and "armhome", which say the same
    bool armed = p.getBool("armed", true);
    uint8_t mode = p.getUChar("armmode", !armed ? CAM_DISARMED : p.getBool("armhome", false) ? CAM_ARMED_HOME : CAM_ARMED_AWAY);
    s.armMode = mode <= CAM_ARMED_HOME ? (CamArmMode)mode : CAM_ARMED_AWAY;
    s.disarmedUntil = s.armMode == CAM_DISARMED ? p.getLong64("armuntil", 0) : 0;
    s.rearmMinutes = p.getUShort("rearm", 0);
    s.healthAlarm = p.getBool("hlth", false);
    s.nvrIp = p.getString("nvrip", "").c_str();
    s.timeNtp = p.getBool("ntp", true);
    s.ntpServer = p.getString("ntpsrv", NTP_SERVER);
    s.tzOffset = p.getInt("tzoff", 0);
    s.tzDst = p.getInt("tzdst", 0);
    s.schedule.on = p.getBool("rben", false);
    s.schedule.day = p.getInt("rbday", -1);
    s.schedule.minute = p.getInt("rbmin", 0);
    s.staticFor = p.getString("ipfor", "");
    s.staticIp = p.getString("ip", "");
    s.staticMask = p.getString("mask", "");
    s.staticGateway = p.getString("gw", "");
    s.staticDns = p.getString("dns", "");
    return true;
  });
}

bool storeSettings(const CamSettings& s) {
  return withPrefs("net", [&](Preferences& p) {
    bool done = p.putUShort("onvif", s.onvifPort) == 2 && p.putUShort("web", s.webPort) == 2 && p.putUShort("rtsp", s.rtspPort) == 2;
    if (!s.name.empty()) done = done && putText(p, "name", s.name.c_str());
    if (!s.passwordOn) done = done && putText(p, "webpw", "");
    else if (!s.password.empty()) done = done && putText(p, "webpw", s.password.c_str());
    if (s.hostsGiven) done = done && putText(p, "hosts", s.hosts.c_str());
    return done;
  });
}

void forgetPorts() {
  withPrefs("net", [](Preferences& p) {
    p.remove("onvif");
    p.remove("web");
    p.remove("rtsp");
    return true;
  });
}

bool storeWifi() {
  const HubSettings& s = settings;
  return withPrefs("wifi", [&](Preferences& p) {
    return putText(p, "ssid", s.wifiSsid[0]) && putText(p, "password", s.wifiPassword[0]) && putText(p, "ssid2", s.wifiSsid[1]) && putText(p, "password2", s.wifiPassword[1]);
  });
}

bool rememberWifi(const std::string& ssid, const std::string& password, bool backup) {
  HubSettings& s = settings;
  String name = ssid.c_str(), pass = password.c_str();
  // Saving a known network again with the password left blank must not erase it. An open network has none to keep.
  for (int i = 0; pass.isEmpty() && i < 2; i++)
    if (name == s.wifiSsid[i]) pass = s.wifiPassword[i];
  if (backup) {
    s.wifiSsid[1] = name;
    s.wifiPassword[1] = pass;
  } else {
    if (name != s.wifiSsid[0]) {  // the network it replaces is kept as the backup
      s.wifiSsid[1] = s.wifiSsid[0];
      s.wifiPassword[1] = s.wifiPassword[0];
    }
    s.wifiSsid[0] = name;
    s.wifiPassword[0] = pass;
  }
  return storeWifi();
}

bool storeTime(const CamTime& t) {
  String server = t.server.empty() ? settings.ntpServer : String(t.server.c_str());
  bool ok = withPrefs("net", [&](Preferences& p) { return p.putBool("ntp", t.ntp) == 1 && p.putInt("tzoff", t.offsetMinutes) == 4 && p.putInt("tzdst", t.dst) == 4 && putText(p, "ntpsrv", server); });
  if (!ok) return false;
  settings.timeNtp = t.ntp;
  settings.ntpServer = server;
  settings.tzOffset = t.offsetMinutes;
  settings.tzDst = t.dst;
  return true;
}

bool storeSchedule(const CamSchedule& s) {
  bool ok = withPrefs("net", [&](Preferences& p) { return p.putBool("rben", s.on) == 1 && p.putInt("rbday", s.day) == 4 && p.putInt("rbmin", s.minute) == 4; });
  if (ok) settings.schedule = s;
  return ok;
}

bool storeNvr(const std::string& ip) {
  return withPrefs("net", [&](Preferences& p) { return putText(p, "nvrip", ip.c_str()); });
}

bool storeAddress(const CamAddress& a, const String& network) {
  return withPrefs("net", [&](Preferences& p) {
    if (a.dhcp) return p.remove("ipfor") || !p.isKey("ipfor");
    return putText(p, "ipfor", network) && putText(p, "ip", a.ip.c_str()) && putText(p, "mask", a.mask.c_str()) && putText(p, "gw", a.gateway.c_str()) && putText(p, "dns", a.dns.c_str());
  });
}

void forgetStatic() {
  withPrefs("net", [](Preferences& p) { return p.remove("ipfor"); });
}

bool storeHealthAlarm(bool on) {
  return withPrefs("net", [on](Preferences& p) { return p.putBool("hlth", on) == 1; });
}

bool storeEspNowUnsigned(bool on) {
  return withPrefs("net", [on](Preferences& p) { return p.putBool("enuns", on) == 1; });
}

bool storeWebhook(const CamWebhook& w) {
  return withPrefs("net", [&w](Preferences& p) {
    return putText(p, "hook", w.url.c_str()) && p.putBool("hooktx", w.text) == 1 && p.putBool("hookal", w.alerts) == 1 && p.putBool("hookpr", w.problems) == 1;
  });
}

bool storeMqtt(const CamMqtt& m) {
  return withPrefs("net", [&m](Preferences& p) {
    return putText(p, "mqhost", m.host.c_str()) && p.putUShort("mqport", m.port) == 2 && putText(p, "mquser", m.user.c_str()) && putText(p, "mqpw", m.password.c_str());
  });
}

// armmode is what this firmware reads. "armed" and "armhome" are written too, so an update that is undone leaves the
// earlier firmware in the same mode.
bool storeArmed(CamArmMode mode, int64_t until) {
  return withPrefs("net", [mode, until](Preferences& p) {
    return p.putLong64("armuntil", until) == 8 && p.putUChar("armmode", mode) == 1 && p.putBool("armhome", mode == CAM_ARMED_HOME) == 1 &&
           p.putBool("armed", mode != CAM_DISARMED) == 1;
  });
}

bool storeRearm(uint32_t minutes) {
  return withPrefs("net", [minutes](Preferences& p) { return p.putUShort("rearm", (uint16_t)minutes) == 2; });
}

// Pictures live on LittleFS, so they survive this.
bool resetSettings() {
  bool ok = true;
  String token = settings.sensorToken, key = settings.espNowKey, pair = settings.pairingKey;  // the sensors are kept, so the secrets they send with are kept too
  for (const char* space : {"wifi", "net"}) ok = withPrefs(space, [](Preferences& p) { return p.clear(); }) && ok;
  return ok && withPrefs("net", [&](Preferences& p) { return putText(p, "token", token) && putText(p, "enkey", key) && putText(p, "pairkey", pair); });
}

// ---- backup and restore ----

struct BackupKey {
  const char* space;
  const char* key;
  char type;  // s text, u 16-bit number, i number, b true or false
};
static const BackupKey BACKUP_KEYS[] = {
    {"wifi", "ssid", 's'},    {"wifi", "password", 's'}, {"wifi", "ssid2", 's'},  {"wifi", "password2", 's'}, {"net", "onvif", 'u'}, {"net", "web", 'u'},
    {"net", "rtsp", 'u'},     {"net", "name", 's'},      {"net", "webpw", 's'},   {"net", "token", 's'},      {"net", "ipfor", 's'}, {"net", "ip", 's'},
    {"net", "mask", 's'},     {"net", "gw", 's'},        {"net", "dns", 's'},     {"net", "nvrip", 's'},      {"net", "ntp", 'b'},   {"net", "ntpsrv", 's'},
    {"net", "tzoff", 'i'},    {"net", "tzdst", 'i'},     {"net", "rben", 'b'},    {"net", "rbday", 'i'},      {"net", "rbmin", 'i'}, {"net", "hlth", 'b'},
    {"net", "enkey", 's'},   {"net", "enuns", 'b'},     {"net", "hook", 's'},    {"net", "hooktx", 'b'},     {"net", "hookal", 'b'}, {"net", "hookpr", 'b'}, {"net", "hosts", 's'},
    {"net", "pairkey", 's'}, {"net", "rearm", 'u'},   {"net", "mqhost", 's'},  {"net", "mqport", 'u'},    {"net", "mquser", 's'},  {"net", "mqpw", 's'},
};

std::string backupText() {
  std::string out = "sensorcam-backup 1\n";
  for (const BackupKey& k : BACKUP_KEYS) {
    withPrefs(k.space, [&](Preferences& p) {
      if (!p.isKey(k.key)) return true;
      String value = k.type == 's' ? p.getString(k.key, "") : k.type == 'u' ? String(p.getUShort(k.key, 0)) : k.type == 'i' ? String(p.getInt(k.key, 0)) : String(p.getBool(k.key, false) ? 1 : 0);
      out += std::string(k.space) + "." + k.key + "=";
      char hex[4];
      for (size_t i = 0; i < value.length(); i++) {
        snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)value[i]);
        out += hex;
      }
      out += "\n";
      return true;
    });
  }
  return out;
}

struct BackupLine {
  const BackupKey* key;
  std::string value;
};

// False for a damaged file, and for one with no settings in it.
static bool parseBackup(const std::string& text, std::vector<BackupLine>& lines) {
  if (text.compare(0, 19, "sensorcam-backup 1\n") != 0) return false;
  for (size_t at = 19; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    const BackupKey* found = nullptr;
    for (const BackupKey& k : BACKUP_KEYS)
      if (line.compare(0, eq, std::string(k.space) + "." + k.key) == 0) found = &k;
    if (!found) return false;
    std::string value;
    for (size_t i = eq + 1; i < line.size(); i += 3) {
      if (line[i] != '%' || i + 2 >= line.size() || !isxdigit((unsigned char)line[i + 1]) || !isxdigit((unsigned char)line[i + 2])) return false;
      value += (char)strtol(line.substr(i + 1, 2).c_str(), nullptr, 16);
    }
    if (!camBackupValueValid(std::string(found->space) + "." + found->key, value)) return false;
    lines.push_back({found, value});
  }
  return !lines.empty();
}

static bool writeBackupLine(const BackupLine& l) {
  return withPrefs(l.key->space, [&](Preferences& p) {
    if (l.key->type == 's') return putText(p, l.key->key, l.value.c_str());
    if (l.key->type == 'u') return p.putUShort(l.key->key, (uint16_t)atoi(l.value.c_str())) == 2;
    if (l.key->type == 'i') return p.putInt(l.key->key, atoi(l.value.c_str())) == 4;
    return p.putBool(l.key->key, l.value == "1") == 1;
  });
}

// A damaged file changes nothing, and a write that fails puts back what the earlier ones replaced.
bool restoreText(const std::string& text) {
  std::vector<BackupLine> wanted, before;
  if (!parseBackup(text, wanted)) return false;
  parseBackup(backupText(), before);
  for (size_t done = 0; done < wanted.size(); done++) {
    if (writeBackupLine(wanted[done])) continue;
    for (size_t i = 0; i <= done; i++)  // removed first, because the write may have failed for lack of space
      withPrefs(wanted[i].key->space, [&](Preferences& p) { return p.remove(wanted[i].key->key); });
    for (const BackupLine& l : before) writeBackupLine(l);
    return false;
  }
  return true;
}
