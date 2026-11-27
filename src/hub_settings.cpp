#include "hub_settings.h"
#include <Preferences.h>
#include <bootloader_random.h>
#include <vector>
#include "cam_backup.h"
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

// For the keys, made in loadSettings before Wi-Fi starts. esp_random() is truly random only while the radio runs or this
// other source of noise is on, and that source must not be on once Wi-Fi is.
static String randomHex(int words) {
  bootloader_random_enable();
  String out;
  char word[9];
  for (int i = 0; i < words; i++) {
    snprintf(word, sizeof(word), "%08lx", (unsigned long)esp_random());
    out += word;
  }
  bootloader_random_disable();
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
    if (s.espNowKey.length() < 16) {  // made once and kept, and shown on the Nodes and pairing page
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
    s.webhook.url = p.getString("hook", "").c_str();
    // A format saved by firmware from before ntfy and Gotify is plain text or JSON.
    s.webhook.format = p.isKey("hookfm") ? p.getInt("hookfm", CAM_HOOK_JSON) : p.getBool("hooktx", false) ? CAM_HOOK_TEXT : CAM_HOOK_JSON;
    if (s.webhook.format < CAM_HOOK_JSON || s.webhook.format > CAM_HOOK_GOTIFY) s.webhook.format = CAM_HOOK_JSON;
    s.webhook.token = p.getString("hooktk", "").c_str();
    s.webhook.weeklyDay = p.getInt("hookwd", -1);
    s.webhook.weeklyMinute = p.getInt("hookwm", 9 * 60);
    s.webhook.alerts = p.getBool("hookal", true);
    s.webhook.problems = p.getBool("hookpr", true);
    s.webhook.dailyMinute = p.getInt("hookdy", -1);
    s.webhook.language = p.getString("hooklg", "en").c_str();  // by its code, as "pt"
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
    s.backupAt = p.getLong64("bkat", 0);
    s.backupPaired = p.getInt("bkpr", 0);
    s.healthAlarm = p.getBool("hlth", false);
    s.nvrIp = p.getString("nvrip", "").c_str();
    s.timeNtp = p.getBool("ntp", true);
    s.ntpServer = p.getString("ntpsrv", NTP_SERVER);
    s.tzOffset = p.getInt("tzoff", 0);
    s.tzDst = p.getInt("tzdst", 0);
    s.schedule.on = p.getBool("rben", false);
    s.schedule.day = p.getInt("rbday", -1);
    s.schedule.minute = p.getInt("rbmin", 0);
    s.leds.green = p.getInt("ledg", -1);
    s.leds.red = p.getInt("ledr", -1);
    s.leds.rgb = p.getInt("ledrgb", -1);
    s.leds.lowOn = p.getBool("ledlo", false);
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
    if (!s.passwordOn)
      done = done && putText(p, "webpw", "");
    else if (!s.password.empty())
      done = done && putText(p, "webpw", s.password.c_str());
    if (s.hostsGiven) done = done && putText(p, "hosts", s.hosts.c_str());
    return done;
  });
}

// Not in CAM_SETTINGS, so never in a backup: restoring one would take the numbers back to ones used before.
bool takeSealStart(uint32_t block, uint32_t& start) {
  start = esp_random();  // a hub from before this: as random as it always was. A settings reset keeps it.
  return withPrefs("seal", [&](Preferences& p) {
    if (p.isKey("next")) start = p.getUInt("next", start);
    return p.putUInt("next", start + block) == 4;  // before any is used, so a power cut cannot hand them out again
  });
}

bool storeSealNext(uint32_t next) {
  return withPrefs("seal", [&](Preferences& p) { return p.putUInt("next", next) == 4; });
}

bool storePassword(const std::string& password) {
  if (!withPrefs("net", [&](Preferences& p) { return putText(p, "webpw", password.c_str()); })) return false;
  settings.webPassword = password.c_str();  // the setup network takes it the next time it opens
  return true;
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
    return putText(p, "ssid", s.wifiSsid[0]) && putText(p, "password", s.wifiPassword[0]) && putText(p, "ssid2", s.wifiSsid[1]) &&
           putText(p, "password2", s.wifiPassword[1]);
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
  bool ok = withPrefs("net", [&](Preferences& p) {
    return p.putBool("ntp", t.ntp) == 1 && p.putInt("tzoff", t.offsetMinutes) == 4 && p.putInt("tzdst", t.dst) == 4 && putText(p, "ntpsrv", server);
  });
  if (!ok) return false;
  settings.timeNtp = t.ntp;
  settings.ntpServer = server;
  settings.tzOffset = t.offsetMinutes;
  settings.tzDst = t.dst;
  return true;
}

bool storeSchedule(const CamSchedule& s) {
  bool ok = withPrefs(
      "net", [&](Preferences& p) { return p.putBool("rben", s.on) == 1 && p.putInt("rbday", s.day) == 4 && p.putInt("rbmin", s.minute) == 4; });
  if (ok) settings.schedule = s;
  return ok;
}

bool storeLeds(const CamLedPins& l) {
  bool ok = withPrefs("net", [&](Preferences& p) {
    return p.putInt("ledg", l.green) == 4 && p.putInt("ledr", l.red) == 4 && p.putInt("ledrgb", l.rgb) == 4 && p.putBool("ledlo", l.lowOn) == 1;
  });
  if (ok) settings.leds = l;
  return ok;
}

bool storeNvr(const std::string& ip) {
  return withPrefs("net", [&](Preferences& p) { return putText(p, "nvrip", ip.c_str()); });
}

bool storeAddress(const CamAddress& a, const String& network) {
  return withPrefs("net", [&](Preferences& p) {
    if (a.dhcp) return p.remove("ipfor") || !p.isKey("ipfor");
    return putText(p, "ipfor", network) && putText(p, "ip", a.ip.c_str()) && putText(p, "mask", a.mask.c_str()) &&
           putText(p, "gw", a.gateway.c_str()) && putText(p, "dns", a.dns.c_str());
  });
}

void forgetStatic() {
  withPrefs("net", [](Preferences& p) { return p.remove("ipfor"); });
}

bool storeHealthAlarm(bool on) {
  return withPrefs("net", [on](Preferences& p) { return p.putBool("hlth", on) == 1; });
}

bool storeWebhook(const CamWebhook& w) {
  return withPrefs("net", [&w](Preferences& p) {
    return putText(p, "hook", w.url.c_str()) && p.putBool("hooktx", w.format == CAM_HOOK_TEXT) == 1 && p.putInt("hookfm", w.format) == 4 &&
           putText(p, "hooktk", w.token.c_str()) && p.putInt("hookwd", w.weeklyDay) == 4 && p.putInt("hookwm", w.weeklyMinute) == 4 &&
           p.putBool("hookal", w.alerts) == 1 && p.putBool("hookpr", w.problems) == 1 && p.putInt("hookdy", w.dailyMinute) == 4 &&
           putText(p, "hooklg", w.language.c_str());
  });
}

bool storeMqtt(const CamMqtt& m) {
  return withPrefs("net", [&m](Preferences& p) {
    return putText(p, "mqhost", m.host.c_str()) && p.putUShort("mqport", m.port) == 2 && putText(p, "mquser", m.user.c_str()) &&
           putText(p, "mqpw", m.password.c_str());
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

bool storeBackupNoted(int64_t at, int paired) {
  return withPrefs("net", [at, paired](Preferences& p) { return p.putLong64("bkat", at) == 8 && p.putInt("bkpr", paired) == 4; });
}

bool storeRearm(uint32_t minutes) {
  return withPrefs("net", [minutes](Preferences& p) { return p.putUShort("rearm", (uint16_t)minutes) == 2; });
}

// Pictures live on LittleFS, so they survive this.
bool resetSettings() {
  bool ok = true;
  String token = settings.sensorToken, key = settings.espNowKey,
         pair = settings.pairingKey;  // the sensors are kept, so the secrets they send with are kept too
  for (const char* space : {"wifi", "net"}) ok = withPrefs(space, [](Preferences& p) { return p.clear(); }) && ok;
  return ok &&
         withPrefs("net", [&](Preferences& p) { return putText(p, "token", token) && putText(p, "enkey", key) && putText(p, "pairkey", pair); });
}

bool forgetKeys() {
  return withPrefs("net", [](Preferences& p) {
    for (const char* key : {"token", "enkey", "pairkey", "snapkey"}) p.remove(key);
    return !p.isKey("token") && !p.isKey("enkey") && !p.isKey("pairkey") && !p.isKey("snapkey");
  });
}

// ---- backup and restore ----

using BackupKey = CamSetting;  // the list, CAM_SETTINGS, is in cam_settings.cpp beside the check of each value

static const char BACKUP_HEADER[] = "sensorcam-backup 1\n";
static const size_t BACKUP_HEADER_LEN = sizeof(BACKUP_HEADER) - 1;

std::string backupText() {
  std::string out = BACKUP_HEADER;
  for (const BackupKey& k : camSettings()) {
    withPrefs(k.space, [&](Preferences& p) {
      if (!p.isKey(k.key)) return true;
      String value = k.kind == CAM_KIND_TEXT   ? p.getString(k.key, "")
                     : k.kind == CAM_KIND_PORT ? String(p.getUShort(k.key, 0))
                     : k.kind == CAM_KIND_INT  ? String(p.getInt(k.key, 0))
                                               : String(p.getBool(k.key, false) ? 1 : 0);
      out += std::string(k.space) + "." + k.key + "=" + camBackupEscape(value.c_str());
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
  if (text.compare(0, BACKUP_HEADER_LEN, BACKUP_HEADER) != 0) return false;
  for (size_t at = BACKUP_HEADER_LEN; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    const BackupKey* found = nullptr;
    size_t dot = line.find('.');
    for (const BackupKey& k : camSettings()) {
      size_t spaceLen = strlen(k.space);
      if (dot == spaceLen && dot < eq && line.compare(0, dot, k.space) == 0 && line.compare(dot + 1, eq - dot - 1, k.key) == 0) found = &k;
    }
    if (!found) return false;
    std::string value;
    if (!camBackupUnescape(line.substr(eq + 1), value)) return false;
    std::string name = std::string(found->space) + "." + found->key;
    if (!camBackupValueValid(name, value)) return false;
    if (camBackupValueKept(name, value)) lines.push_back({found, value});
  }
  return !lines.empty();
}

static bool writeBackupLine(const BackupLine& l) {
  return withPrefs(l.key->space, [&](Preferences& p) {
    if (l.key->kind == CAM_KIND_TEXT) return putText(p, l.key->key, l.value.c_str());
    if (l.key->kind == CAM_KIND_PORT) return p.putUShort(l.key->key, (uint16_t)atoi(l.value.c_str())) == 2;
    if (l.key->kind == CAM_KIND_INT) return p.putInt(l.key->key, atoi(l.value.c_str())) == 4;
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
