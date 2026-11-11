#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_now.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_sntp.h>
#include <lwip/etharp.h>
#include <nvs.h>
#include <sys/time.h>
#include <freertos/queue.h>
#include <ping/ping_sock.h>
#include "cam_server.h"
#include "config.h"

static SensorCamServer* server;
static std::string localIp() { return WiFi.localIP().toString().c_str(); }
static uint32_t nowMs() { return millis(); }

// What malloc can really hand out. ESP.getMaxAllocHeap() also counts instruction RAM, which strings cannot use.
static uint32_t largestBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT); }

static std::string macSerial() {
  uint64_t mac = ESP.getEfuseMac();
  char b[13];
  snprintf(b, sizeof(b), "%012llx", (unsigned long long)mac);
  return b;
}

// ---- Wi-Fi settings and the setup network ----
static const uint32_t WIFI_TRY_MS = 30000;  // how long each saved network gets before moving on
static String wifiSsid[2], wifiPassword[2];  // [0] is the main network, [1] the backup
static int wifiTrying = 0;
static uint32_t wifiTryAt = 0;
static DNSServer dnsServer;
static bool setupOpen = false;
static uint16_t onvifPort = ONVIF_PORT, webPort = WEB_PORT, rtspPort = RTSP_PORT;
static String hubName = MDNS_NAME, webPassword = WEB_PASSWORD, sensorToken = SENSOR_TOKEN;
static String espNowKey;  // sensor nodes sign their ESP-NOW messages with it
static bool espNowUnsigned = false;
static CamWebhook webhook;
static bool healthAlarm = false;
// A fixed address belongs to one WiFi network, because the main and backup networks can use different address ranges.
static String staticFor, staticIp, staticMask, staticGateway, staticDns;
static bool usingStatic = false;
static std::string nvrIp;
static bool timeNtp = true;
static String ntpServer = NTP_SERVER;
static int tzOffset = 0, tzDst = 0;
static CamSchedule schedule;
static uint32_t connectedAt = 0;
static bool staticChecked = false;
static const uint32_t STATIC_CHECK_MS = 120000;

// Why the hub restarted itself, kept across the restart so Device Info and the heartbeat can say which rule caused it.
enum RestartCause : uint32_t {
  RESTART_UNKNOWN,
  RESTART_WIFI_LOST,
  RESTART_LOW_MEMORY,
  RESTART_STALLED,
  RESTART_ROUTER_SILENT,
  RESTART_NEW_WIFI,
  RESTART_NEW_SETTINGS,
  RESTART_BAD_FIXED_ADDRESS,
  RESTART_SCHEDULED,
  RESTART_SETTINGS_RESET,
  RESTART_BUTTON,
  RESTART_FIRMWARE,
  RESTART_RESTORED,
  RESTART_CAUSES
};
static const char* const RESTART_TEXT[RESTART_CAUSES] = {
    "Software restart",  "WiFi lost for 5 minutes",   "Low memory",     "Server stalled",  "Router stopped answering", "New WiFi settings", "New web, port or address settings",
    "Fixed address could not reach the router", "Scheduled restart", "Settings reset", "Restart button", "Firmware update", "Settings restored"};

static bool restartPending = false;
static uint32_t restartRequestedAt = 0;
static RestartCause restartRequested = RESTART_UNKNOWN;

// The restart waits for the reply to reach the browser.
static void requestRestart(RestartCause cause) {
  restartRequested = cause;
  restartRequestedAt = millis();
  restartPending = true;
}

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

// Settings saved on the web page win over the defaults compiled in from config.h and secrets.h.
static void loadSettings() {
  withPrefs("net", [](Preferences& p) {
    onvifPort = p.getUShort("onvif", ONVIF_PORT);
    webPort = p.getUShort("web", WEB_PORT);
    rtspPort = p.getUShort("rtsp", RTSP_PORT);
    hubName = p.getString("name", MDNS_NAME);
    // A hub is password protected unless someone turns that off, so an empty setting falls back to the setup network's password.
    webPassword = p.getString("webpw", strlen(WEB_PASSWORD) ? WEB_PASSWORD : SETUP_AP_PASSWORD);
    sensorToken = p.getString("token", SENSOR_TOKEN);
    if (sensorToken.isEmpty()) {  // made once and kept, and shown on the HTTP page
      char made[17];
      snprintf(made, sizeof(made), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
      sensorToken = made;
      p.putString("token", sensorToken);
    }
    espNowKey = p.getString("enkey", "");
    if (espNowKey.length() < 16) {  // made once and kept, and shown on the ESP-NOW page
      char made[33];
      snprintf(made, sizeof(made), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random(),
               (unsigned long)esp_random());
      espNowKey = made;
      p.putString("enkey", espNowKey);
    }
    espNowUnsigned = p.getBool("enuns", false);
    webhook.url = p.getString("hook", "").c_str();
    webhook.text = p.getBool("hooktx", false);
    webhook.alerts = p.getBool("hookal", true);
    webhook.problems = p.getBool("hookpr", true);
    healthAlarm = p.getBool("hlth", false);
    nvrIp = p.getString("nvrip", "").c_str();
    timeNtp = p.getBool("ntp", true);
    ntpServer = p.getString("ntpsrv", NTP_SERVER);
    tzOffset = p.getInt("tzoff", 0);
    tzDst = p.getInt("tzdst", 0);
    schedule.on = p.getBool("rben", false);
    schedule.day = p.getInt("rbday", -1);
    schedule.minute = p.getInt("rbmin", 0);
    staticFor = p.getString("ipfor", "");
    staticIp = p.getString("ip", "");
    staticMask = p.getString("mask", "");
    staticGateway = p.getString("gw", "");
    staticDns = p.getString("dns", "");
    return true;
  });
}

static bool saveSettings(const CamSettings& s) {
  bool ok = withPrefs("net", [&](Preferences& p) {
    bool done = p.putUShort("onvif", s.onvifPort) == 2 && p.putUShort("web", s.webPort) == 2 && p.putUShort("rtsp", s.rtspPort) == 2;
    if (!s.name.empty()) done = done && putText(p, "name", s.name.c_str());
    if (!s.passwordOn) done = done && putText(p, "webpw", "");
    else if (!s.password.empty()) done = done && putText(p, "webpw", s.password.c_str());
    return done;
  });
  if (ok) requestRestart(RESTART_NEW_SETTINGS);
  return ok;
}

// Keeps the name and password, which cannot stop the ports from opening.
static void forgetPorts() {
  withPrefs("net", [](Preferences& p) {
    p.remove("onvif");
    p.remove("web");
    p.remove("rtsp");
    return true;
  });
}

// Networks saved on the setup page win over the one compiled in from secrets.h.
static void loadWifi() {
  wifiSsid[0] = WIFI_SSID;
  wifiPassword[0] = WIFI_PASSWORD;
  withPrefs("wifi", [](Preferences& p) {
    wifiSsid[0] = p.getString("ssid", wifiSsid[0]);
    wifiPassword[0] = p.getString("password", wifiPassword[0]);
    wifiSsid[1] = p.getString("ssid2", "");
    wifiPassword[1] = p.getString("password2", "");
    return true;
  });
}

static bool storeWifi() {
  return withPrefs("wifi", [](Preferences& p) {
    return putText(p, "ssid", wifiSsid[0]) && putText(p, "password", wifiPassword[0]) && putText(p, "ssid2", wifiSsid[1]) && putText(p, "password2", wifiPassword[1]);
  });
}

static bool saveWifi(const std::string& ssid, const std::string& password, bool backup) {
  String name = ssid.c_str(), pass = password.c_str();
  // Saving a known network again with the password left blank must not erase it. An open network has none to keep.
  for (int i = 0; pass.isEmpty() && i < 2; i++)
    if (name == wifiSsid[i]) pass = wifiPassword[i];
  if (backup) {
    wifiSsid[1] = name;
    wifiPassword[1] = pass;
  } else {
    if (name != wifiSsid[0]) {  // the network it replaces is kept as the backup
      wifiSsid[1] = wifiSsid[0];
      wifiPassword[1] = wifiPassword[0];
    }
    wifiSsid[0] = name;
    wifiPassword[0] = pass;
  }
  if (!storeWifi()) return false;
  if (!backup || WiFi.status() != WL_CONNECTED) requestRestart(RESTART_NEW_WIFI);  // a new backup needs no restart while the main network is working
  return true;
}

static void tryNetwork(int i) {
  wifiTrying = i;
  wifiTryAt = millis();
  WiFi.disconnect(false);
  IPAddress ip, mask, gateway, dns;
  usingStatic = staticFor.length() && staticFor == wifiSsid[i] && ip.fromString(staticIp) && mask.fromString(staticMask) && gateway.fromString(staticGateway);
  if (usingStatic) WiFi.config(ip, gateway, mask, dns.fromString(staticDns) ? dns : gateway);
  else WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(wifiSsid[i].c_str(), wifiPassword[i].c_str());
}

// The backup worked, so it becomes the main network and the old main becomes the backup.
static void promoteBackup() {
  std::swap(wifiSsid[0], wifiSsid[1]);
  std::swap(wifiPassword[0], wifiPassword[1]);
  wifiTrying = 0;
  bool ok = storeWifi();
  Serial.printf("Joined the backup network; it is now the main network%s.\n", ok ? "" : " until the next restart (could not save)");
}

static void openSetup() {
  if (setupOpen || strlen(SETUP_AP_PASSWORD) < 8) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_NAME, SETUP_AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());
  setupOpen = true;
  Serial.printf("Setup network %s is open at %s\n", SETUP_AP_NAME, WiFi.softAPIP().toString().c_str());
}

static void closeSetup() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  setupOpen = false;
  Serial.println("Joined Wi-Fi; setup network closed.");
}

static std::string jsonText(const String& s) { return camJsonEscape(s.c_str()); }

// The scan runs in the background so the video and ONVIF keep being served; the page asks again until it is done.
static std::string networksJson() {
  std::string head = std::string("{\"name\":\"") + hubName.c_str() + ".local" + (webPort == 80 ? std::string() : ":" + std::to_string(webPort)) + "\",\"current\":\"" + jsonText(wifiSsid[0]) + "\",\"backup\":\"" + jsonText(wifiSsid[1]) + "\",";
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return head + "\"scanning\":true,\"networks\":[]}\n";
  if (n < 0) {
    WiFi.scanNetworks(true);
    return head + "\"scanning\":true,\"networks\":[]}\n";
  }
  std::string list;
  for (int i = 0; i < n && i < 20; i++) {
    if (i) list += ",";
    list += "{\"ssid\":\"" + jsonText(WiFi.SSID(i)) + "\",\"rssi\":" + std::to_string(WiFi.RSSI(i)) + ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  WiFi.scanDelete();
  return head + "\"scanning\":false,\"networks\":[" + list + "]}\n";
}

static String tzName(int minutes) {
  char b[12];
  snprintf(b, sizeof(b), "<%c%02d%02d>", minutes < 0 ? '-' : '+', abs(minutes) / 60, abs(minutes) % 60);
  return b;
}

// POSIX timezone text. Its sign is the opposite of the usual one: UTC+1 is written as -1.
static String posixTz() {
  char offset[10];
  snprintf(offset, sizeof(offset), "%c%d:%02d", tzOffset < 0 ? '+' : '-', abs(tzOffset) / 60, abs(tzOffset) % 60);
  String tz = tzName(tzOffset) + offset;
  char rule[32];
  if (tzDst == 1) {  // Europe changes at 01:00 UTC everywhere
    int hour = tzOffset / 60;
    snprintf(rule, sizeof(rule), ",M3.5.0/%d,M10.5.0/%d", 1 + hour, 2 + hour);
    tz += tzName(tzOffset + 60) + rule;
  } else if (tzDst == 2) {
    tz += tzName(tzOffset + 60) + ",M3.2.0/2,M11.1.0/2";
  }
  return tz;
}

static bool clockSet() { return time(nullptr) > CAM_CLOCK_VALID_AFTER; }

// A manual time does not survive a power cut, so until it is set again NTP is asked, and stopped by loop() once it answers.
static void applyTime() {
  String tz = posixTz();
  if (timeNtp || !clockSet()) {
    configTzTime(tz.c_str(), ntpServer.c_str());
  } else {
    esp_sntp_stop();  // otherwise NTP would overwrite the time that was set by hand
    setenv("TZ", tz.c_str(), 1);
    tzset();
  }
}

static std::string timeJson() {
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  char shown[24];
  strftime(shown, sizeof(shown), "%Y-%m-%d %H:%M:%S", &local);
  return std::string("{\"ntp\":") + (timeNtp ? "true" : "false") + ",\"server\":\"" + jsonText(ntpServer) + "\",\"offset\":" + std::to_string(tzOffset) + ",\"dst\":" + std::to_string(tzDst) +
         ",\"synced\":" + (clockSet() ? "true" : "false") + ",\"local\":\"" + shown + "\"}\n";
}

static bool saveTime(const CamTime& t) {
  String server = t.server.empty() ? ntpServer : String(t.server.c_str());
  bool ok = withPrefs("net", [&](Preferences& p) { return p.putBool("ntp", t.ntp) == 1 && p.putInt("tzoff", t.offsetMinutes) == 4 && p.putInt("tzdst", t.dst) == 4 && putText(p, "ntpsrv", server); });
  if (!ok) return false;
  timeNtp = t.ntp;
  ntpServer = server;
  tzOffset = t.offsetMinutes;
  tzDst = t.dst;
  if (!t.ntp && t.epoch) {  // before applyTime, so NTP is not started for a clock that is about to be set
    timeval now = {(time_t)t.epoch, 0};
    settimeofday(&now, nullptr);
  }
  applyTime();
  return true;
}

static bool saveSchedule(const CamSchedule& s) {
  bool ok = withPrefs("net", [&](Preferences& p) { return p.putBool("rben", s.on) == 1 && p.putInt("rbday", s.day) == 4 && p.putInt("rbmin", s.minute) == 4; });
  if (ok) schedule = s;
  return ok;
}

// Pictures live on LittleFS, so they survive this.
static bool maintenance(const std::string& action) {
  if (action != "reset") {
    requestRestart(RESTART_BUTTON);
    return true;
  }
  bool ok = true;
  String token = sensorToken, key = espNowKey;  // the sensors are kept, so the secrets they send with are kept too
  for (const char* space : {"wifi", "net"}) ok = withPrefs(space, [](Preferences& p) { return p.clear(); }) && ok;
  ok = ok && withPrefs("net", [&](Preferences& p) { return putText(p, "token", token) && putText(p, "enkey", key); });
  if (ok) requestRestart(RESTART_SETTINGS_RESET);
  return ok;
}

static bool saveNvr(const std::string& ip) {
  return withPrefs("net", [&](Preferences& p) { return putText(p, "nvrip", ip.c_str()); });
}

static void forgetStatic() {
  withPrefs("net", [](Preferences& p) { return p.remove("ipfor"); });
}

static std::string addressJson() {
  return std::string("{\"type\":\"WiFi\",\"dhcp\":") + (usingStatic ? "false" : "true") + ",\"staticFor\":\"" + jsonText(staticFor) + "\",\"ip\":\"" + WiFi.localIP().toString().c_str() +
         "\",\"mask\":\"" + WiFi.subnetMask().toString().c_str() + "\",\"gateway\":\"" + WiFi.gatewayIP().toString().c_str() + "\",\"dns\":\"" + WiFi.dnsIP().toString().c_str() +
         "\",\"mac\":\"" + WiFi.macAddress().c_str() + "\"}\n";
}

static bool saveAddress(const CamAddress& a) {
  String network = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : wifiSsid[0];
  if (!a.dhcp && network.isEmpty()) return false;
  bool ok = withPrefs("net", [&](Preferences& p) {
    if (a.dhcp) return p.remove("ipfor") || !p.isKey("ipfor");
    return putText(p, "ipfor", network) && putText(p, "ip", a.ip.c_str()) && putText(p, "mask", a.mask.c_str()) && putText(p, "gw", a.gateway.c_str()) && putText(p, "dns", a.dns.c_str());
  });
  if (ok) requestRestart(RESTART_NEW_SETTINGS);
  return ok;
}

static std::string deviceRows();

// The core would otherwise accept every new firmware the moment it starts. loop() does that once it has proved itself.
extern "C" bool verifyRollbackLater() { return true; }

static const uint32_t FIRMWARE_PROOF_MS = 60000;
static bool firmwareUnproven = false;

// Until this is called, any restart makes the bootloader go back to the previous firmware.
static void confirmFirmware() {
  if (firmwareUnproven && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) firmwareUnproven = false;
}

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
    {"net", "enkey", 's'},   {"net", "enuns", 'b'},     {"net", "hook", 's'},    {"net", "hooktx", 'b'},     {"net", "hookal", 'b'}, {"net", "hookpr", 'b'},
};

static std::string backupText() {
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

// All or nothing: a damaged file changes nothing, and a write that fails puts back what the earlier ones replaced.
static bool restoreText(const std::string& text) {
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
  requestRestart(RESTART_RESTORED);
  return true;
}

// ---- Crash reports: after a crash the chip saves one to flash, kept until deleted or replaced by the next ----
static size_t crashBytes = 0;  // checked once at start, because checking reads the whole report
static String crashSummary = "None saved";

static void loadCrash() {
  size_t addr = 0;
  if (esp_core_dump_image_check() != ESP_OK || esp_core_dump_image_get(&addr, &crashBytes) != ESP_OK) {
    crashBytes = 0;
    return;
  }
  esp_core_dump_summary_t* s = new esp_core_dump_summary_t();
  if (esp_core_dump_get_summary(s) == ESP_OK) {
    char running[APP_ELF_SHA256_SZ] = "", at[12];
    esp_app_get_elf_sha256(running, sizeof(running));
    snprintf(at, sizeof(at), "0x%08lx", (unsigned long)s->exc_pc);
    bool same = strncmp(running, (const char*)s->app_elf_sha256, sizeof(running) - 1) == 0;
    crashSummary = String("In task ") + s->exc_task + " at " + at + (same ? ", in this firmware" : ", in an earlier firmware");
  } else {
    crashSummary = "Saved, but it could not be read";
  }
  delete s;
}

static bool readCrash(size_t offset, char* out, size_t len) {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  return p && offset + len <= crashBytes && esp_partition_read(p, offset, out, len) == ESP_OK;
}

static bool deleteCrash() {
  if (esp_core_dump_image_erase() != ESP_OK) return false;
  crashBytes = 0;
  crashSummary = "None saved";
  return true;
}

static CamConfig makeConfig() {
  CamConfig c;
  c.onvifPort = onvifPort;
  c.webPort = webPort;
  c.settingsSave = saveSettings;
  c.espNowInfo = [] { return std::string("\"mac\":\"") + WiFi.macAddress().c_str() + "\",\"channel\":" + std::to_string(WiFi.channel()); };
  c.onvifNetwork = [](SoapContext& s) {
    s.ip = WiFi.localIP().toString().c_str();
    s.mac = WiFi.macAddress().c_str();
    s.gateway = WiFi.gatewayIP().toString().c_str();
    s.dns = WiFi.dnsIP().toString().c_str();
    s.hostName = hubName.c_str();
    s.ntpServer = ntpServer.c_str();
    s.prefixLength = __builtin_popcount((uint32_t)WiFi.subnetMask());
    s.dhcp = !usingStatic;
    s.firmwareVersion = FIRMWARE_VERSION;
    s.timeFromNtp = timeNtp;
  };
  c.timeJson = timeJson;
  c.timeSave = saveTime;
  c.schedule = schedule;
  c.scheduleSave = saveSchedule;
  c.deviceRows = deviceRows;
  c.crashSize = [] { return crashBytes; };
  c.crashRead = readCrash;
  c.crashDelete = deleteCrash;
  c.maintenance = maintenance;
  c.nvrIp = nvrIp;
  c.nvrSave = saveNvr;
  c.addressJson = addressJson;
  c.addressSave = saveAddress;
  if (webPort != 80) c.setupAddress += ":" + std::to_string(webPort);
  c.hostName = std::string(hubName.c_str()) + ".local";
  c.setupActive = [] { return setupOpen; };
  c.wifiNetworks = networksJson;
  c.wifiSave = saveWifi;
  c.rtspPort = rtspPort;
  c.holdMs = SENSOR_HOLD_MS;
  c.sensorToken = sensorToken.c_str();
  c.webPassword = webPassword.c_str();
  c.onvifPassword = webPassword.c_str();
  c.healthAlarm = healthAlarm;
  c.healthSave = [](bool on) { return withPrefs("net", [on](Preferences& p) { return p.putBool("hlth", on) == 1; }); };
  c.espNowKey = espNowKey.c_str();
  c.espNowUnsigned = espNowUnsigned;
  c.espNowSave = [](bool on) { return withPrefs("net", [on](Preferences& p) { return p.putBool("enuns", on) == 1; }); };
  c.webhook = webhook;
  c.webhookSave = [](const CamWebhook& w) {
    return withPrefs("net", [&w](Preferences& p) {
      return putText(p, "hook", w.url.c_str()) && p.putBool("hooktx", w.text) == 1 && p.putBool("hookal", w.alerts) == 1 && p.putBool("hookpr", w.problems) == 1;
    });
  };
  c.localMinute = [] {
    time_t now = time(nullptr);
    if (now < CAM_CLOCK_VALID_AFTER) return -1;
    struct tm local;
    localtime_r(&now, &local);
    return local.tm_hour * 60 + local.tm_min;
  };
  c.firmwareBegin = [](size_t size) {
    confirmFirmware();  // the update code refuses to run from an unconfirmed firmware, and this one has just answered a logged-in request
    return Update.begin(size, U_FLASH);
  };
  c.firmwareWrite = [](const uint8_t* data, size_t len) { return Update.write((uint8_t*)data, len) == len; };
  c.firmwareEnd = [](bool commit) {
    if (!commit) {
      Update.abort();
      return true;
    }
    bool ok = Update.end(true);
    if (ok) requestRestart(RESTART_FIRMWARE);
    return ok;
  };
  c.backupText = backupText;
  c.restoreText = restoreText;
  // Wi-Fi and lwIP fail badly when RAM runs out, so a large request must leave them room.
  c.canAllocate = [](size_t n) { return largestBlock() >= n + 1024 && ESP.getFreeHeap() >= n + 16 * 1024; };
  c.hasHeadroom = [] { return ESP.getFreeHeap() >= 10 * 1024; };
  c.fsUsage = [](uint32_t& used, uint32_t& total) {
    used = LittleFS.usedBytes();
    total = LittleFS.totalBytes();
  };
  c.serial = macSerial();
  return c;
}

// A start time for timers where 0 means "not running". It must never be later than millis(), or the elapsed time wraps to a huge value.
static uint32_t timerStart() {
  uint32_t t = millis();
  return t == 0 ? 1 : t;
}

static const uint32_t WIFI_RESTART_MS = 5UL * 60UL * 1000UL;
static const uint32_t LOW_HEAP_BYTES = 8 * 1024;
static const uint32_t LOW_HEAP_RESTART_MS = 30000;
static volatile uint32_t wifiDrops = 0;
static volatile uint8_t wifiDropReason = 0;

RTC_NOINIT_ATTR static uint32_t restartCause;
static uint32_t lastRestartCause = 0;
static void restartFor(RestartCause cause) {
  if (server) server->saveNow();
  restartCause = cause;
  ESP.restart();
}

static const uint32_t PING_LOST_RESTART_MS = 90000;
static volatile uint32_t pingReplies = 0, pingLost = 0, lastPingReply = 0;
static esp_ping_handle_t pingSession = nullptr;
static uint32_t pingTarget = 0;

// Wi-Fi can report "connected" while the network stack answers nothing, so the router is asked directly.
// Called on every connection: replies count only from this one, and the backup network has a different router.
static void startGatewayPing() {
  pingReplies = 0;  // the restart rule waits for a reply, which also brings the reply time up to date
  pingLost = 0;
  uint32_t gateway = WiFi.gatewayIP();
  if (pingSession && gateway == pingTarget) return;
  if (pingSession) {
    esp_ping_stop(pingSession);
    esp_ping_delete_session(pingSession);
    pingSession = nullptr;
  }
  pingTarget = gateway;
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.target_addr.type = IPADDR_TYPE_V4;
  config.target_addr.u_addr.ip4.addr = gateway;
  config.count = ESP_PING_COUNT_INFINITE;
  config.interval_ms = 20000;
  config.timeout_ms = 3000;
  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = [](esp_ping_handle_t, void*) {
    lastPingReply = millis();
    pingReplies = pingReplies + 1;
  };
  callbacks.on_ping_timeout = [](esp_ping_handle_t, void*) { pingLost = pingLost + 1; };
  if (esp_ping_new_session(&config, &callbacks, &pingSession) == ESP_OK) esp_ping_start(pingSession);
}

struct ArpQuery {
  struct netif* network;
  ip4_addr_t address;
  bool found;
};

static esp_err_t findInArpTable(void* arg) {
  ArpQuery* q = (ArpQuery*)arg;
  struct eth_addr* mac;
  const ip4_addr_t* ip;
  q->found = q->network && etharp_find_addr(q->network, &q->address, &mac, &ip) >= 0;
  return ESP_OK;
}

// A router may ignore ping, but it has to answer ARP to be a router at all.
static bool routerAnswersArp() {
  ArpQuery q = {(struct netif*)esp_netif_get_netif_impl(WiFi.STA.netif()), {}, false};
  q.address.addr = (uint32_t)WiFi.gatewayIP();
  return esp_netif_tcpip_exec(findInArpTable, &q) != ESP_OK || q.found;  // unable to ask is not evidence of a bad address
}

static bool wasConnected = false;

struct SensorMessage {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t len;
  uint8_t data[32];  // the longest message is 13 + 3 * 3 + 8 = 30 bytes
};
static QueueHandle_t sensorQueue;

// Runs on the Wi-Fi task, so it only queues. loop() owns all sensor state.
static void onEspNow(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  SensorMessage m;
  if (len < 1 || len > (int)sizeof(m.data)) return;
  memcpy(m.data, data, len);
  m.len = (uint8_t)len;
  memcpy(m.mac, info->src_addr, 6);
  m.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  xQueueSend(sensorQueue, &m, 0);
}

static String uptimeText() {
  uint64_t s = esp_timer_get_time() / 1000000ULL;
  char b[32];
  snprintf(b, sizeof(b), "%lud %02luh %02lum %02lus", (unsigned long)(s / 86400), (unsigned long)(s % 86400 / 3600), (unsigned long)(s % 3600 / 60), (unsigned long)(s % 60));
  return b;
}

static const char* lastRestartText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Power on or reset button";
    case ESP_RST_PANIC: return "Crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Low power supply voltage";
    case ESP_RST_SW: break;
    default: return "Other";
  }
  return RESTART_TEXT[lastRestartCause < RESTART_CAUSES ? lastRestartCause : RESTART_UNKNOWN];
}

static uint32_t programBytes = 0;

static std::string deviceRows() {
  std::string out;
  auto add = [&](const char* label, const String& value) { out += (out.empty() ? "[\"" : ",[\"") + std::string(label) + "\",\"" + jsonText(value) + "\"]"; };
  add("Device name", hubName);
  add("Firmware version", FIRMWARE_VERSION);
  add("Firmware built", __DATE__ " " __TIME__);
  add("Firmware check", firmwareUnproven ? "New firmware, being checked" : esp_ota_get_last_invalid_partition() ? "The last update failed and was undone" : "Good");
  add("Serial number", macSerial().c_str());
  add("Chip", String(ESP.getChipModel()) + " revision " + ESP.getChipRevision());
  add("CPU", String(ESP.getChipCores()) + " cores at " + ESP.getCpuFreqMHz() + " MHz");
  add("Flash size", String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
  add("PSRAM", ESP.getPsramSize() ? String(ESP.getPsramSize() / (1024 * 1024)) + " MB" : String("None"));
  add("Program size", String(programBytes / 1024) + " KB of " + esp_ota_get_running_partition()->size / 1024 + " KB");
  add("Arduino core", ESP_ARDUINO_VERSION_STR);
  add("ESP-IDF", esp_get_idf_version());
  add("Uptime", uptimeText());
  add("Last restart", lastRestartText());
  add("Last crash", crashSummary);
  add("Free memory", String(ESP.getFreeHeap() / 1024) + " KB");
  add("Lowest free memory", String(ESP.getMinFreeHeap() / 1024) + " KB");
  add("Largest free block", String(largestBlock() / 1024) + " KB");
  nvs_stats_t settings;
  if (nvs_get_stats(nullptr, &settings) == ESP_OK) add("Settings storage", String(settings.used_entries) + " of " + settings.total_entries + " entries used");
  add("WiFi network", WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("Not connected"));
  add("WiFi signal", WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : String("-"));
  add("IP address", WiFi.localIP().toString());
  add("MAC address", WiFi.macAddress());
  return out;
}

void setup() {
  programBytes = ESP.getSketchSize();  // verifies the whole firmware, which stalls the hub for 0.4 s, so only once
  loadCrash();
  esp_ota_img_states_t firmwareState;
  firmwareUnproven = esp_ota_get_state_partition(esp_ota_get_running_partition(), &firmwareState) == ESP_OK && firmwareState == ESP_OTA_IMG_PENDING_VERIFY;
  lastRestartCause = esp_reset_reason() == ESP_RST_SW ? restartCause : 0;
  restartCause = 0;
  Serial.begin(115200);
  loadWifi();
  loadSettings();
  if (!webPassword.length()) Serial.println("No hub password is set: the web page, ONVIF and RTSP are open to anyone on the network.");
  WiFi.persistent(false);  // the settings are kept in one place only, by loadWifi and saveWifi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // power save loses multicast and adds latency
  WiFi.setAutoReconnect(false);  // loop() chooses between the main and backup networks itself
  WiFi.setHostname(hubName.c_str());
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    wifiDrops = wifiDrops + 1;
    wifiDropReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (wifiSsid[0].length()) tryNetwork(0);
  applyTime();

  // Formats on first use. Without storage the hub still runs, using its built-in pictures.
  if (!LittleFS.begin(true)) Serial.println("LittleFS could not start; using built-in pictures.");

  server = new SensorCamServer(makeConfig(), localIp, nowMs);
  if (!server->begin()) {
    if (firmwareUnproven) {  // the previous firmware opened these ports, so it comes back with them unchanged
      Serial.println("The new firmware could not open its ports; going back to the previous firmware.");
      delay(2000);
      esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    Serial.println("Could not open the ONVIF, web or RTSP port; going back to the default ports and restarting.");
    forgetPorts();  // saved ports that cannot open must not keep the hub in a restart loop
    delay(2000);
    ESP.restart();
  }

  sensorQueue = xQueueCreate(32, sizeof(SensorMessage));
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onEspNow);
  else Serial.println("ESP-NOW failed to start; HTTP sensor input still works.");

  enableLoopWDT();  // an unattended hub must restart itself if the main loop ever stalls
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  bool justLost = !connected && wasConnected;
  if (connected && !wasConnected) {
    server->networkUp();
    connectedAt = timerStart();
    startGatewayPing();
    staticChecked = false;
    static bool nameAnnounced = false;
    if (!nameAnnounced && MDNS.begin(hubName.c_str())) {
      MDNS.addService("http", "tcp", webPort);
      nameAnnounced = true;
    }
  }
  wasConnected = connected;

  // An unattended hub must not stay off the network: keep asking, then start clean.
  static uint32_t lostAt = 0;
  static bool hadGuest = false;
  if (connected) {
    lostAt = 0;
    hadGuest = false;
    if (wifiTrying == 1) promoteBackup();
    if (setupOpen) closeSetup();
  } else {
    if (lostAt == 0) lostAt = timerStart();
    bool haveNetwork = wifiSsid[0].length() > 0;
    if (!haveNetwork) openSetup();
    if (justLost && haveNetwork) tryNetwork(0);
    // Connection attempts move the radio, which would drop whoever is on the setup network and block its scan.
    bool guest = setupOpen && WiFi.softAPgetStationNum() > 0;
    if (guest && !hadGuest) WiFi.disconnect(false);
    hadGuest = guest;
    if (guest) {
      lostAt = timerStart();  // someone is using the setup page; a restart would throw them off
    } else if (haveNetwork && millis() - wifiTryAt > WIFI_TRY_MS) {
      if (wifiTrying == 0 && wifiSsid[1].length() > 0) {
        tryNetwork(1);
      } else {  // neither network worked: open the setup network and keep trying behind it
        openSetup();
        tryNetwork(0);
      }
    }
    if (haveNetwork && millis() - lostAt > WIFI_RESTART_MS) restartFor(RESTART_WIFI_LOST);
  }
  if (setupOpen) dnsServer.processNextRequest();
  if (!timeNtp && esp_sntp_enabled() && clockSet()) applyTime();  // the one-off NTP reading after a power cut has arrived
  if (restartPending && millis() - restartRequestedAt > 1500) {
    confirmFirmware();  // someone used this firmware's pages to ask for this restart, so it must not count as a failure
    restartFor(restartRequested);
  }
  // A new firmware has to stay on the network for a minute. Any restart before that, including the Wi-Fi one above, undoes the update.
  if (firmwareUnproven && (connected ? millis() - connectedAt > FIRMWARE_PROOF_MS : wifiSsid[0].isEmpty() && millis() > FIRMWARE_PROOF_MS)) confirmFirmware();

  // Out of RAM the network stack stops working while this loop carries on, so the loop watchdog cannot see it.
  static uint32_t lowSince = 0;
  if (ESP.getFreeHeap() >= LOW_HEAP_BYTES) {
    lowSince = 0;
  } else {
    if (lowSince == 0) lowSince = timerStart();
    if (millis() - lowSince > LOW_HEAP_RESTART_MS) restartFor(RESTART_LOW_MEMORY);
  }
  if (server->stalledMs(millis()) > 30000) restartFor(RESTART_STALLED);  // the loop runs but the server has stopped serving

  // The uptime check stops the hub from restarting again within the same minute.
  static uint32_t scheduleCheckedAt = 0;
  if (schedule.on && millis() > 180000 && millis() - scheduleCheckedAt > 20000) {
    scheduleCheckedAt = millis();
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    if (clockSet() && (schedule.day < 0 || schedule.day == local.tm_wday) && local.tm_hour * 60 + local.tm_min == schedule.minute) restartFor(RESTART_SCHEDULED);
  }
  // A wrong fixed address leaves Wi-Fi connected but the hub unreachable, with no other way back to it.
  if (connected && usingStatic && !staticChecked && millis() - connectedAt > STATIC_CHECK_MS) {
    staticChecked = true;
    if (pingReplies == 0 && pingLost > 0 && !routerAnswersArp()) {
      forgetStatic();
      restartFor(RESTART_BAD_FIXED_ADDRESS);
    }
  }
  // Only after the router has answered at least once, so a router that ignores ping cannot cause restarts.
  // The reply time is read first because the ping task can update it at any moment, and a time later than millis() would wrap.
  uint32_t lastReply = lastPingReply;
  if (connected && pingReplies > 0 && millis() - lastReply > PING_LOST_RESTART_MS) restartFor(RESTART_ROUTER_SILENT);

  static uint32_t printedAt = 0;
  if (millis() - printedAt > 10000) {
    printedAt = millis();
    Serial.printf("ONVIF on %s:%u, web port %u, RTSP port %u, ESP-NOW to %s on Wi-Fi channel %d, status %d, RSSI %d, drops %u, last drop reason %u, free heap %u, largest block %u, last reset reason %d cause %u, ping ok %u lost %u, setup %s, %s\n",
                  WiFi.localIP().toString().c_str(), onvifPort, webPort, rtspPort, WiFi.macAddress().c_str(), WiFi.channel(), (int)WiFi.status(), WiFi.RSSI(), (unsigned)wifiDrops,
                  (unsigned)wifiDropReason, ESP.getFreeHeap(), largestBlock(), (int)esp_reset_reason(), (unsigned)lastRestartCause, (unsigned)pingReplies, (unsigned)pingLost, setupOpen ? "open" : "off", server->debugLine().c_str());
  }

  SensorMessage m;
  while (xQueueReceive(sensorQueue, &m, 0) == pdTRUE) server->espNowPacket(m.mac, m.data, m.len, m.rssi);
  server->poll(20);
}
