#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_now.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <lwip/etharp.h>
#include <sys/time.h>
#include <freertos/queue.h>
#include <ping/ping_sock.h>
#include "cam_restart_rules.h"
#include "cam_server.h"
#include "cam_timing.h"
#include "config.h"
#include "device_info.h"
#include "hub_settings.h"

static SensorCamServer* server;
static std::string localIp() { return WiFi.localIP().toString().c_str(); }
static uint32_t nowMs() { return millis(); }

// ---- Wi-Fi and the setup network. The saved networks are in settings, from hub_settings.h ----
static int wifiTrying = 0;
static uint32_t wifiTryAt = 0;
static DNSServer dnsServer;
static bool setupOpen = false;
static bool usingStatic = false;

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
  RESTART_NEW_KEYS,
  RESTART_CAUSES
};
static const char* const RESTART_TEXT[RESTART_CAUSES] = {"Software restart",
                                                         "WiFi lost for 5 minutes",
                                                         "Low memory",
                                                         "Server stalled",
                                                         "Router stopped answering",
                                                         "New WiFi settings",
                                                         "New web, port or address settings",
                                                         "Fixed address could not reach the router",
                                                         "Scheduled restart",
                                                         "Settings reset",
                                                         "Restart button",
                                                         "Firmware update",
                                                         "Settings restored",
                                                         "New keys"};

static bool restartPending = false;
static uint32_t restartRequestedAt = 0;
static RestartCause restartRequested = RESTART_UNKNOWN;

// The restart waits for the reply to reach the browser.
static void requestRestart(RestartCause cause) {
  restartRequested = cause;
  restartRequestedAt = millis();
  restartPending = true;
}

// Changes from the web page. hub_settings.cpp stores them; the restart a change needs is decided here.
static bool saveSettings(const CamSettings& s) {
  bool ok = storeSettings(s);
  if (ok) requestRestart(RESTART_NEW_SETTINGS);
  return ok;
}

static bool saveWifi(const std::string& ssid, const std::string& password, bool backup) {
  if (!rememberWifi(ssid, password, backup)) return false;
  if (!backup || WiFi.status() != WL_CONNECTED) requestRestart(RESTART_NEW_WIFI);  // a new backup needs no restart while the main network is working
  return true;
}

static void tryNetwork(int i) {
  wifiTrying = i;
  wifiTryAt = millis();
  WiFi.disconnect(false);
  IPAddress ip, mask, gateway, dns;
  usingStatic = settings.staticFor.length() && settings.staticFor == settings.wifiSsid[i] && ip.fromString(settings.staticIp) &&
                mask.fromString(settings.staticMask) && gateway.fromString(settings.staticGateway);
  if (usingStatic)
    WiFi.config(ip, gateway, mask, dns.fromString(settings.staticDns) ? dns : gateway);
  else
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(settings.wifiSsid[i].c_str(), settings.wifiPassword[i].c_str());
}

// The backup worked, so it becomes the main network and the old main becomes the backup.
static void promoteBackup() {
  std::swap(settings.wifiSsid[0], settings.wifiSsid[1]);
  std::swap(settings.wifiPassword[0], settings.wifiPassword[1]);
  wifiTrying = 0;
  bool ok = storeWifi();
  Serial.printf("Joined the backup network; it is now the main network%s.\n", ok ? "" : " until the next restart (could not save)");
}

// The setup network's password: one from secrets.h, else the hub password. A blank hub has neither, so its setup network
// is open and its setup page asks for a hub password first. A password too short for WPA2, from an old firmware, keeps
// the setup network off rather than open.
static bool setupPassword(String& out) {
  out = strlen(SETUP_AP_PASSWORD) >= 8 ? String(SETUP_AP_PASSWORD) : settings.webPassword;
  return out.isEmpty() || out.length() >= 8;
}

// WPA3 only, with protected management frames as WPA3 requires. Under WPA2, anyone who records a device joining could try
// passwords offline, as fast as their computer allows, and this password is usually the hub password. WPA3 lets them
// try only by asking the hub, one guess at a time. Should WPA3 not start, WPA2 is better than no setup network, as it
// is the way back onto the network.
static void startSetupNetwork(const String& password) {
  if (password.isEmpty()) {
    WiFi.softAP(SETUP_AP_NAME);
    return;
  }
  wifi_config_t conf;
  bool wpa3 = WiFi.softAP(SETUP_AP_NAME, password.c_str(), 1, 0, 4, false, WIFI_AUTH_WPA3_PSK) && esp_wifi_get_config(WIFI_IF_AP, &conf) == ESP_OK;
  if (wpa3) {
    conf.ap.pmf_cfg.capable = true;
    conf.ap.pmf_cfg.required = true;
    conf.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wpa3 = esp_wifi_set_config(WIFI_IF_AP, &conf) == ESP_OK;
  }
  if (!wpa3) {
    Serial.println("The setup network could not start with WPA3; it uses WPA2 instead.");
    WiFi.softAP(SETUP_AP_NAME, password.c_str());
  }
}

static void openSetup() {
  String password;
  if (setupOpen || !setupPassword(password)) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  startSetupNetwork(password);
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

// The scan runs in the background so the video and ONVIF keep being served; the page asks again until it is done.
static void networksInfo(CamJson& j) {
  j.str("name", std::string(settings.hubName.c_str()) + ".local" + (settings.webPort == 80 ? std::string() : ":" + std::to_string(settings.webPort)));
  j.str("current", settings.wifiSsid[0].c_str()).str("backup", settings.wifiSsid[1].c_str());
  int16_t n = WiFi.scanComplete();
  if (n < 0 && n != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true);
  j.flag("scanning", n < 0).array("networks");
  for (int i = 0; i < n && i < 20; i++)
    j.object().str("ssid", WiFi.SSID(i).c_str()).num("rssi", WiFi.RSSI(i)).flag("open", WiFi.encryptionType(i) == WIFI_AUTH_OPEN).end();
  if (n >= 0) WiFi.scanDelete();
  j.end();
}

static String tzName(int minutes) {
  char b[12];
  snprintf(b, sizeof(b), "<%c%02d%02d>", minutes < 0 ? '-' : '+', abs(minutes) / 60, abs(minutes) % 60);
  return b;
}

// POSIX timezone text. Its sign is the opposite of the usual one: UTC+1 is written as -1.
static String posixTz() {
  char offset[10];
  snprintf(offset, sizeof(offset), "%c%d:%02d", settings.tzOffset < 0 ? '+' : '-', abs(settings.tzOffset) / 60, abs(settings.tzOffset) % 60);
  String tz = tzName(settings.tzOffset) + offset;
  char rule[32];
  if (settings.tzDst == 1) {  // Europe changes at 01:00 UTC everywhere
    int hour = settings.tzOffset / 60;
    snprintf(rule, sizeof(rule), ",M3.5.0/%d,M10.5.0/%d", 1 + hour, 2 + hour);
    tz += tzName(settings.tzOffset + 60) + rule;
  } else if (settings.tzDst == 2) {
    tz += tzName(settings.tzOffset + 60) + ",M3.2.0/2,M11.1.0/2";
  }
  return tz;
}

static bool clockSet() { return time(nullptr) > CAM_CLOCK_VALID_AFTER; }

// A manual time does not survive a power cut, so until it is set again NTP is asked, and stopped by loop() once it answers.
static void applyTime() {
  String tz = posixTz();
  if (settings.timeNtp || !clockSet()) {
    configTzTime(tz.c_str(), settings.ntpServer.c_str());
  } else {
    esp_sntp_stop();  // otherwise NTP would overwrite the time that was set by hand
    setenv("TZ", tz.c_str(), 1);
    tzset();
  }
}

static void timeInfo(CamJson& j) {
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  char shown[24];
  strftime(shown, sizeof(shown), "%Y-%m-%d %H:%M:%S", &local);
  j.flag("ntp", settings.timeNtp).str("server", settings.ntpServer.c_str()).num("offset", settings.tzOffset).num("dst", settings.tzDst);
  j.flag("synced", clockSet()).str("local", shown);
}

static bool saveTime(const CamTime& t) {
  if (!storeTime(t)) return false;
  if (!t.ntp && t.epoch) {  // before applyTime, so NTP is not started for a clock that is about to be set
    timeval now = {(time_t)t.epoch, 0};
    settimeofday(&now, nullptr);
  }
  applyTime();
  return true;
}

static bool maintenance(const std::string& action) {
  if (action == "newkeys") {  // the start that follows makes new ones, as on the first
    bool ok = forgetKeys();
    if (ok) requestRestart(RESTART_NEW_KEYS);
    return ok;
  }
  if (action != "reset") {
    requestRestart(RESTART_BUTTON);
    return true;
  }
  bool ok = resetSettings();
  if (ok) requestRestart(RESTART_SETTINGS_RESET);
  return ok;
}

static void addressInfo(CamJson& j) {
  j.str("type", "WiFi").flag("dhcp", !usingStatic).str("staticFor", settings.staticFor.c_str()).str("ip", WiFi.localIP().toString().c_str());
  j.str("mask", WiFi.subnetMask().toString().c_str()).str("gateway", WiFi.gatewayIP().toString().c_str()).str("dns", WiFi.dnsIP().toString().c_str());
  j.str("mac", WiFi.macAddress().c_str());
}

static bool saveAddress(const CamAddress& a) {
  String network = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : settings.wifiSsid[0];
  if (!a.dhcp && network.isEmpty()) return false;
  bool ok = storeAddress(a, network);
  if (ok) requestRestart(RESTART_NEW_SETTINGS);
  return ok;
}

static void hubDeviceRows(CamJson& j);
static void hubHealth(CamHealth& h);
static void sendToNode(const uint8_t mac[6], const uint8_t* data, int len);
static const char* lastRestartText();
static bool startWasPlanned();

// The core would otherwise accept every new firmware the moment it starts. loop() does that once it has proved itself.
extern "C" bool verifyRollbackLater() { return true; }

static bool firmwareUnproven = false;
static bool espNowStarted = false;
// Set as an update starts and kept through the restart into it: whether the NVR was listening then, so the new firmware
// must get it listening again before it is kept. A random value after a power cut is told apart by the mark.
struct UpdateProof {
  uint32_t mark;
  bool needNvr;
};
static const uint32_t UPDATE_PROOF_MARK = 0x50524F46;
RTC_NOINIT_ATTR static UpdateProof updateProof;

// Until this is called, any restart makes the bootloader go back to the previous firmware.
static void confirmFirmware() {
  if (firmwareUnproven && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) firmwareUnproven = false;
}

static bool restoreBackup(const std::string& text) {
  bool ok = restoreText(text);
  if (ok) requestRestart(RESTART_RESTORED);
  return ok;
}

static bool radioTest(bool start, CamJson& j);

// ---- what the board does for the hub: the chip, the radio, and where settings are kept ----

class HubChip : public CamChip {
 public:
  bool storageUsage(uint32_t& used, uint32_t& total) override {
    used = LittleFS.usedBytes();
    total = LittleFS.totalBytes();
    return true;
  }
  // Wi-Fi and lwIP fail badly when RAM runs out, so a large request must leave them room.
  bool canAllocate(size_t n) override { return largestBlock() >= n + 1024 && ESP.getFreeHeap() >= n + 16 * 1024; }
  bool hasHeadroom() override { return ESP.getFreeHeap() >= 10 * 1024; }
  bool firmwareBegin(size_t size) override {
    confirmFirmware();  // the update code refuses to run from an unconfirmed firmware, and this one has just answered a logged-in request
    updateProof.mark = UPDATE_PROOF_MARK;
    updateProof.needNvr = server && server->nvrListeningNow();
    return Update.begin(size, U_FLASH);
  }
  bool firmwareWrite(const uint8_t* data, size_t len) override { return Update.write((uint8_t*)data, len) == len; }
  bool firmwareEnd(bool commit) override {
    if (!commit) {
      Update.abort();
      return true;
    }
    bool ok = Update.end(true);
    if (ok) requestRestart(RESTART_FIRMWARE);
    return ok;
  }
  size_t crashSize() override { return ::crashSize(); }
  bool crashRead(size_t offset, char* out, size_t len) override { return readCrash(offset, out, len); }
  bool crashDelete() override { return deleteCrash(); }
  bool maintenance(const std::string& action) override { return ::maintenance(action); }
  void deviceRows(CamJson& j) override { hubDeviceRows(j); }
  bool tellsHealth() override { return true; }
  void health(CamHealth& h) override { hubHealth(h); }
  int localMinute() override {
    time_t now = time(nullptr);
    if (now < CAM_CLOCK_VALID_AFTER) return -1;
    struct tm local;
    localtime_r(&now, &local);
    return local.tm_hour * 60 + local.tm_min;
  }
  int localWeekday() override {
    time_t now = time(nullptr);
    if (now < CAM_CLOCK_VALID_AFTER) return -1;
    struct tm local;
    localtime_r(&now, &local);
    return local.tm_wday;
  }
  void timeInfo(CamJson& j) override { ::timeInfo(j); }
  bool timeSave(const CamTime& t) override { return saveTime(t); }
};

class HubRadio : public CamRadio {
 public:
  bool setupActive() override { return setupOpen; }
  void wifiNetworks(CamJson& j) override { networksInfo(j); }
  bool wifiSave(const std::string& ssid, const std::string& password, bool backup) override { return saveWifi(ssid, password, backup); }
  void addressInfo(CamJson& j) override { ::addressInfo(j); }
  bool addressSave(const CamAddress& a) override { return saveAddress(a); }
  void onvifNetwork(SoapContext& s) override {
    s.ip = WiFi.localIP().toString().c_str();
    s.mac = WiFi.macAddress().c_str();
    s.gateway = WiFi.gatewayIP().toString().c_str();
    s.dns = WiFi.dnsIP().toString().c_str();
    s.hostName = settings.hubName.c_str();
    s.ntpServer = settings.ntpServer.c_str();
    s.prefixLength = __builtin_popcount((uint32_t)WiFi.subnetMask());
    s.dhcp = !usingStatic;
    s.firmwareVersion = FIRMWARE_VERSION;
    s.timeFromNtp = settings.timeNtp;
  }
  void espNowInfo(CamJson& j) override { j.str("mac", WiFi.macAddress().c_str()).num("channel", WiFi.channel()); }
  int wifiChannel() override { return (int)WiFi.channel(); }
  bool hasEspNow() override { return true; }
  void espNowSend(const uint8_t mac[6], const uint8_t* data, int len) override { sendToNode(mac, data, len); }
  bool hasRadioTest() override { return true; }
  bool radioTest(bool start, CamJson& j) override { return ::radioTest(start, j); }
};

class HubStore : public CamStore {
 public:
  bool settingsSave(const CamSettings& s) override { return saveSettings(s); }
  bool passwordSave(const std::string& password) override { return storePassword(password); }
  bool healthSave(bool alarm) override { return storeHealthAlarm(alarm); }
  std::string backupText() override { return ::backupText(); }
  bool restoreText(const std::string& text) override { return restoreBackup(text); }
  bool scheduleSave(const CamSchedule& s) override { return storeSchedule(s); }
  bool webhookSave(const CamWebhook& w) override { return storeWebhook(w); }
  bool armedSave(CamArmMode mode, int64_t until) override { return storeArmed(mode, until); }
  bool rearmSave(uint32_t minutes) override { return storeRearm(minutes); }
  bool mqttSave(const CamMqtt& m) override { return storeMqtt(m); }
  bool nvrSave(const std::string& ip) override { return storeNvr(ip); }
};

static HubChip hubChip;
static HubRadio hubRadio;
static HubStore hubStore;

static CamConfig makeConfig() {
  CamConfig c;
  c.chip = &hubChip;
  c.radio = &hubRadio;
  c.store = &hubStore;
  c.onvifPort = settings.onvifPort;
  c.webPort = settings.webPort;
  c.schedule = settings.schedule;
  c.nvrIp = settings.nvrIp;
  if (settings.webPort != 80) c.setupAddress += ":" + std::to_string(settings.webPort);
  c.hostName = std::string(settings.hubName.c_str()) + ".local";
  c.rtspPort = settings.rtspPort;
  c.holdMs = SENSOR_HOLD_MS;
  c.sensorToken = settings.sensorToken.c_str();
  c.webPassword = settings.webPassword.c_str();
  c.onvifPassword = settings.webPassword.c_str();
  c.healthAlarm = settings.healthAlarm;
  c.espNowKey = settings.espNowKey.c_str();
  c.pairingKey = settings.pairingKey.c_str();
  c.startReason = lastRestartText();
  c.startPlanned = startWasPlanned();
  c.snapshotSecret = settings.snapshotKey.c_str();
  c.sealStart = esp_random();
  c.extraHosts = settings.extraHosts.c_str();
  c.webhook = settings.webhook;
  c.setupPasswordHash = strlen(SETUP_AP_PASSWORD) ? camSha1(SETUP_AP_PASSWORD) : std::string();
  c.mqtt = settings.mqtt;
  c.armMode = settings.armMode;
  c.disarmedUntil = settings.disarmedUntil;
  c.rearmMinutes = settings.rearmMinutes;
  c.firmwareVersion = FIRMWARE_VERSION;
  c.serial = macSerial();
  return c;
}

// A start time for timers where 0 means "not running". It must never be later than millis(), or the elapsed time wraps to a huge value.
static volatile uint32_t wifiDrops = 0;
static volatile uint8_t wifiDropReason = 0;

RTC_NOINIT_ATTR static uint32_t restartCause;
static uint32_t lastRestartCause = 0;
static void restartFor(RestartCause cause) {
  if (server) server->saveNow();
  restartCause = cause;
  ESP.restart();
}

static volatile uint32_t pingReplies = 0, pingLost = 0, lastPingReply = 0;
static volatile uint32_t pingRepliesEver = 0, pingLostEver = 0;  // never reset by a new connection, for the radio test
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
  config.interval_ms = CAM_ROUTER_PING_MS;
  config.timeout_ms = CAM_ROUTER_PING_WAIT_MS;
  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = [](esp_ping_handle_t, void*) {
    lastPingReply = millis();
    pingReplies = pingReplies + 1;
    pingRepliesEver = pingRepliesEver + 1;
  };
  callbacks.on_ping_timeout = [](esp_ping_handle_t, void*) {
    pingLost = pingLost + 1;
    pingLostEver = pingLostEver + 1;
  };
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

static CamRestartRules rules(routerAnswersArp);

static bool wasConnected = false;

// ---- the Long Range radio test ----

// Whether the hub's Wi-Fi keeps working with ESP-NOW's Long Range mode allowed beside the usual modes, as nodes far away
// would need. For LR_TEST_MS only: then the modes go back as they were, by the hub's own clock, not by anything over the
// network, so a test that cut the hub off its network undoes itself and the hub reconnects as after any drop. Not kept
// through a restart either. Meanwhile it watches the Wi-Fi drops, the router's answers to the ping every 20 s, and the
// signal.
static const uint32_t LR_TEST_MS = 120000;
static const uint32_t LR_TEST_DELAY_MS = 1000;  // the change waits this long, so the reply that started it gets out first
struct LrTest {
  bool running = false, done = false, applied = false;
  uint32_t startedAt = 0, lastSample = 0;     // startedAt: when LR was allowed, or asked for until then
  uint8_t before = 0, during = 0, after = 0;  // the protocol bits: as found, as read back with LR, as put back
  esp_err_t setErr = ESP_OK, restoreErr = ESP_OK;
  uint32_t drops0 = 0, replies0 = 0, lost0 = 0, drops = 0, replies = 0, lost = 0;
  int rssiBefore = 0, rssiMin = 0, rssiMax = -127, samples = 0, offSamples = 0;
  long rssiSum = 0;
  bool connectedAtEnd = false;
};
static LrTest lr;

static void lrEnd() {
  lr.restoreErr = esp_wifi_set_protocol(WIFI_IF_STA, lr.before);
  esp_wifi_get_protocol(WIFI_IF_STA, &lr.after);
  lr.drops = wifiDrops - lr.drops0;
  lr.replies = pingRepliesEver - lr.replies0;
  lr.lost = pingLostEver - lr.lost0;
  lr.connectedAtEnd = WiFi.status() == WL_CONNECTED;
  lr.running = false;
  lr.done = true;
  Serial.printf("Long Range test over: %u drops, %u router replies, %u lost; modes back to 0x%02X.\n", (unsigned)lr.drops, (unsigned)lr.replies,
                (unsigned)lr.lost, lr.after);
}

static void runLrTest() {
  if (!lr.running) return;
  uint32_t now = millis();
  if (!lr.applied) {
    if (now - lr.startedAt < LR_TEST_DELAY_MS) return;
    lr.applied = true;
    lr.startedAt = lr.lastSample = now;
    lr.setErr = esp_wifi_set_protocol(WIFI_IF_STA, lr.before | WIFI_PROTOCOL_LR);
    esp_wifi_get_protocol(WIFI_IF_STA, &lr.during);
    Serial.printf("Long Range test for %u s: modes 0x%02X -> 0x%02X (%s).\n", (unsigned)(LR_TEST_MS / 1000), lr.before, lr.during,
                  esp_err_to_name(lr.setErr));
    return;
  }
  if (now - lr.lastSample >= 1000) {
    lr.lastSample = now;
    if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      lr.rssiSum += rssi;
      lr.samples++;
      if (rssi < lr.rssiMin) lr.rssiMin = rssi;
      if (rssi > lr.rssiMax) lr.rssiMax = rssi;
    } else {
      lr.offSamples++;
    }
  }
  if (now - lr.startedAt >= LR_TEST_MS) lrEnd();
}

static bool radioTest(bool start, CamJson& j) {
  if (start) {
    if (lr.running || WiFi.status() != WL_CONNECTED) return false;
    lr = LrTest();
    esp_wifi_get_protocol(WIFI_IF_STA, &lr.before);
    lr.drops0 = wifiDrops;
    lr.replies0 = pingRepliesEver;
    lr.lost0 = pingLostEver;
    lr.rssiBefore = WiFi.RSSI();
    lr.rssiMin = 0;
    lr.startedAt = millis();
    lr.running = true;  // runLrTest allows LR a second from now, after this reply, and puts it back by the clock
  }
  j.str("state", !lr.running ? (lr.done ? "done" : "not run") : lr.applied ? "running" : "starting");
  if (!lr.running && !lr.done) return true;
  uint32_t elapsed = !lr.applied ? 0 : (lr.running ? millis() : lr.startedAt + LR_TEST_MS) - lr.startedAt;
  j.num("seconds", elapsed / 1000).num("of", LR_TEST_MS / 1000);
  j.str("set", esp_err_to_name(lr.setErr)).num("modesBefore", lr.before).num("modesDuring", lr.during).flag("lrTaken", lr.during & WIFI_PROTOCOL_LR);
  j.num("rssiBefore", lr.rssiBefore).num("samplesOn", lr.samples).num("samplesOff", lr.offSamples);
  j.num("rssiMin", lr.samples != 0, lr.rssiMin)
      .num("rssiMax", lr.samples != 0, lr.rssiMax)
      .num("rssiAverage", lr.samples != 0, lr.samples ? lr.rssiSum / lr.samples : 0);
  uint32_t drops = lr.running ? wifiDrops - lr.drops0 : lr.drops, replies = lr.running ? pingRepliesEver - lr.replies0 : lr.replies,
           lost = lr.running ? pingLostEver - lr.lost0 : lr.lost;
  j.num("wifiDrops", drops).num("routerReplies", replies).num("routerLost", lost);
  if (lr.done) j.str("restore", esp_err_to_name(lr.restoreErr)).num("modesAfter", lr.after).flag("connectedAtEnd", lr.connectedAtEnd);
  j.flag("connectedNow", WiFi.status() == WL_CONNECTED);
  return true;
}

struct SensorMessage {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t len;
  uint8_t data[CAM_SEALED_EXTRA + CAM_NODE_INFO_MAX];  // the longest the hub reads, sealed node info; anything longer is dropped
};
// A message longer than the queue's room is dropped without a word, so each one the hub reads must fit.
static_assert(sizeof(SensorMessage::data) >= S2O_REPORT_MAX, "a report with every target must fit");
static_assert(sizeof(SensorMessage::data) >= CAM_HELLO_LEN, "a pairing hello must fit");
static_assert(sizeof(SensorMessage::data) >= CAM_SEALED_EXTRA + S2O_REPORT_MAX, "a sealed report with every target must fit");
static QueueHandle_t sensorQueue;
static volatile uint32_t espNowDropped = 0;  // arrived while the queue was full; their senders resend, since nothing confirmed them

// Runs on the Wi-Fi task, so it only queues. loop() owns all sensor state.
static void onEspNow(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  SensorMessage m;
  if (len < 1 || len > (int)sizeof(m.data)) return;
  memcpy(m.data, data, len);
  m.len = (uint8_t)len;
  memcpy(m.mac, info->src_addr, 6);
  m.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  if (xQueueSend(sensorQueue, &m, 0) != pdTRUE) espNowDropped = espNowDropped + 1;
}

// ESP-NOW sends only to registered peers, and holds at most 20, so when it is full the first in its list makes room.
static void sendToNode(const uint8_t mac[6], const uint8_t* data, int len) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;  // the channel Wi-Fi is on
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) == ESP_ERR_ESPNOW_FULL) {
      esp_now_peer_info_t oldest;
      if (esp_now_fetch_peer(true, &oldest) == ESP_OK) esp_now_del_peer(oldest.peer_addr);
      esp_now_add_peer(&peer);
    }
  }
  esp_now_send(mac, data, len);
}

// Asked for by someone: not a crash, a watchdog, a power cut, or one of the hub's own restarts for a fault.
static bool startWasPlanned() {
  if (esp_reset_reason() != ESP_RST_SW) return false;
  switch (lastRestartCause) {
    case RESTART_NEW_WIFI:
    case RESTART_NEW_SETTINGS:
    case RESTART_SCHEDULED:
    case RESTART_SETTINGS_RESET:
    case RESTART_BUTTON:
    case RESTART_FIRMWARE:
    case RESTART_RESTORED:
    case RESTART_NEW_KEYS: return true;
    default: return false;
  }
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

static void hubHealth(CamHealth& h) {
  h.freeHeap = ESP.getFreeHeap();
  h.lowestFreeHeap = ESP.getMinFreeHeap();
  h.largestBlock = largestBlock();
  h.worstLargestBlock = worstLargestBlock();
  h.espNowDropped = espNowDropped;
  h.wifiRssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  h.uptimeS = esp_timer_get_time() / 1000000ULL;
}

static void hubDeviceRows(CamJson& j) { deviceRows(j, {firmwareUnproven, lastRestartText(), server ? server->memoryFailures() : 0, espNowDropped}); }

void setup() {
  deviceInfoBegin();
  esp_ota_img_states_t firmwareState;
  firmwareUnproven =
      esp_ota_get_state_partition(esp_ota_get_running_partition(), &firmwareState) == ESP_OK && firmwareState == ESP_OTA_IMG_PENDING_VERIFY;
  lastRestartCause = esp_reset_reason() == ESP_RST_SW ? restartCause : 0;
  restartCause = 0;
  Serial.begin(115200);
  loadSettings();
  if (!settings.webPassword.length()) Serial.println("No hub password is set: the web page, ONVIF and RTSP are open to anyone on the network.");
  WiFi.persistent(false);  // the settings are kept in one place only, in hub_settings.cpp
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // power save loses multicast and adds latency
  WiFi.setAutoReconnect(false);  // loop() chooses between the main and backup networks itself
  WiFi.setHostname(settings.hubName.c_str());
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        wifiDrops = wifiDrops + 1;
        wifiDropReason = info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (settings.wifiSsid[0].length()) tryNetwork(0);
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
  espNowStarted = sensorQueue && esp_now_init() == ESP_OK;
  if (espNowStarted)
    esp_now_register_recv_cb(onEspNow);
  else
    Serial.println("ESP-NOW failed to start; HTTP sensor input still works.");

  enableLoopWDT();  // an unattended hub must restart itself if the main loop ever stalls
}

// ---- the main loop: each step below keeps one promise, and loop() runs them in this order ----

static bool everConnected = false;
static bool hadGuest = false;

// Back on Wi-Fi. A drop and return without a restart is logged, because someone may be keeping the hub off its network.
static void onConnected() {
  if (everConnected && rules.lostAt()) server->networkLost((millis() - rules.lostAt()) / 1000 + 1);
  everConnected = true;
  server->networkUp();
  startGatewayPing();
  static bool nameAnnounced = false;
  if (!nameAnnounced && MDNS.begin(settings.hubName.c_str())) {
    MDNS.addService("http", "tcp", settings.webPort);
    nameAnnounced = true;
  }
}

// Off the network: keep asking, on the main network then the backup, with the setup network open behind them. True while
// someone is on the setup network. The restart after a while off it is CamRestartRules'.
static bool keepOnNetwork(bool connected, bool justLost) {
  if (connected) {
    hadGuest = false;
    if (wifiTrying == 1) promoteBackup();
    if (setupOpen) closeSetup();
    return false;
  }
  bool haveNetwork = settings.wifiSsid[0].length() > 0;
  if (!haveNetwork) openSetup();
  if (justLost && haveNetwork) tryNetwork(0);
  // Connection attempts move the radio, which would drop whoever is on the setup network and block its scan.
  bool guest = setupOpen && WiFi.softAPgetStationNum() > 0;
  if (guest && !hadGuest) WiFi.disconnect(false);
  hadGuest = guest;
  if (!guest && haveNetwork && millis() - wifiTryAt > CAM_WIFI_TRY_MS) {
    if (wifiTrying == 0 && settings.wifiSsid[1].length() > 0) {
      tryNetwork(1);
    } else {  // neither network worked: open the setup network and keep trying behind it
      openSetup();
      tryNetwork(0);
    }
  }
  return guest;
}

// A restart asked for on the page.
static void restartWhenAsked() {
  if (restartPending && millis() - restartRequestedAt > CAM_RESTART_REPLY_MS) {
    confirmFirmware();  // someone used this firmware's pages to ask for this restart, so it must not count as a failure
    restartFor(restartRequested);
  }
}

// The minute the page set for the scheduled restart.
static bool scheduledMinute() {
  if (!settings.schedule.on || !clockSet()) return false;
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  return (settings.schedule.day < 0 || settings.schedule.day == local.tm_wday) && local.tm_hour * 60 + local.tm_min == settings.schedule.minute;
}

// What the restart rules look at, this pass.
static CamRestartRules::Pass rulesPass(bool connected, bool guest) {
  CamRestartRules::Pass p;
  p.lastPingReply = lastPingReply;  // before the time: the ping task may change it, and a reply later than now would wrap
  p.now = millis();
  p.haveNetwork = settings.wifiSsid[0].length() > 0;
  p.connected = connected;
  p.guest = guest;
  p.usingStatic = usingStatic;
  p.freeHeap = ESP.getFreeHeap();
  p.stalledMs = server->stalledMs(p.now);
  p.pingReplies = pingReplies;
  p.pingLost = pingLost;
  p.uploading = server->updating();
  p.scheduledMinute = scheduledMinute();
  p.alarmBusy = server->alarmBusy();
  return p;
}

// Keeps a new firmware once it has proved itself, or goes back to the previous one; see CamRestartRules::firmware.
static void proveFirmware(const CamRestartRules::Pass& p) {
  if (!firmwareUnproven) return;
  bool needNvr = updateProof.mark == UPDATE_PROOF_MARK && updateProof.needNvr;
  CamFirmwareVerdict verdict = rules.firmware(p, espNowStarted, needNvr, server->nvrListeningNow());
  if (verdict == CAM_FIRMWARE_KEEP) {
    confirmFirmware();
    updateProof.mark = 0;
    Serial.println("The new firmware has proved itself and is kept.");
  } else if (verdict == CAM_FIRMWARE_UNDO) {
    Serial.printf("The new firmware has not %s in 10 minutes; going back to the previous firmware.\n", !espNowStarted ? "started ESP-NOW"
                                                                                                       : !p.connected
                                                                                                           ? "stayed on the network"
                                                                                                           : "got the NVR listening again");
    server->saveNow();
    updateProof.mark = 0;
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

// Carries out what a restart rule decided.
static void restartForRule(CamRestartRule rule) {
  switch (rule) {
    case CAM_RULE_NONE: return;
    case CAM_RULE_WIFI_LOST: return restartFor(RESTART_WIFI_LOST);
    case CAM_RULE_LOW_MEMORY: return restartFor(RESTART_LOW_MEMORY);
    case CAM_RULE_STALLED: return restartFor(RESTART_STALLED);
    case CAM_RULE_SCHEDULED: return restartFor(RESTART_SCHEDULED);
    case CAM_RULE_BAD_FIXED_ADDRESS: forgetStatic(); return restartFor(RESTART_BAD_FIXED_ADDRESS);
    case CAM_RULE_ROUTER_SILENT: return restartFor(RESTART_ROUTER_SILENT);
  }
}

static void printHeartbeat() {
  static uint32_t printedAt = 0;
  if (millis() - printedAt <= 10000) return;
  printedAt = millis();
  Serial.printf(
      "ONVIF on %s:%u, web port %u, RTSP port %u, ESP-NOW to %s on Wi-Fi channel %d, status %d, RSSI %d, drops %u, last drop reason %u, free heap "
      "%u, largest block %u, last reset reason %d cause %u, ping ok %u lost %u, setup %s, %s\n",
      WiFi.localIP().toString().c_str(), settings.onvifPort, settings.webPort, settings.rtspPort, WiFi.macAddress().c_str(), WiFi.channel(),
      (int)WiFi.status(), WiFi.RSSI(), (unsigned)wifiDrops, (unsigned)wifiDropReason, ESP.getFreeHeap(), largestBlock(), (int)esp_reset_reason(),
      (unsigned)lastRestartCause, (unsigned)pingReplies, (unsigned)pingLost, setupOpen ? "open" : "off", server->debugLine().c_str());
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  bool justLost = !connected && wasConnected;
  if (connected && !wasConnected) onConnected();  // before keepOnNetwork, which forgets when the network went
  wasConnected = connected;
  bool guest = keepOnNetwork(connected, justLost);
  if (setupOpen) dnsServer.processNextRequest();
  if (!settings.timeNtp && esp_sntp_enabled() && clockSet()) applyTime();  // the one-off NTP reading after a power cut has arrived
  restartWhenAsked();
  sampleMemory();  // for Device Info: how cut up the memory has been
  CamRestartRules::Pass pass = rulesPass(connected, guest);
  CamRestartRule rule = rules.check(pass);
  proveFirmware(pass);  // first, so an update that failed says why before any other restart undoes it
  restartForRule(rule);
  printHeartbeat();
  runLrTest();

  // Sensor messages first, so the server saves them together and then confirms them, in the same pass.
  SensorMessage m;
  while (sensorQueue && xQueueReceive(sensorQueue, &m, 0) == pdTRUE) server->espNowPacket(m.mac, m.data, m.len, m.rssi);
  server->poll(20);
}
