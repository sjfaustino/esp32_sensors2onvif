// The board: what it gives the hub's library (the chip, the radio and where settings are kept), setup() and loop().
// Each part with state of its own is in a file of its own: hub_wifi (the networks and the setup network), hub_router
// (whether the router answers), hub_espnow (messages to and from the nodes), hub_time (the clock), hub_restart (why
// and when the hub restarts, and keeping a new firmware), hub_radio_test, and hub_settings (what is kept in flash).
#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include "cam_restart_rules.h"
#include "cam_server.h"
#include "cam_timing.h"
#include "config.h"
#include "device_info.h"
#include "hub_espnow.h"
#include "hub_radio_test.h"
#include "hub_restart.h"
#include "hub_router.h"
#include "hub_settings.h"
#include "hub_time.h"
#include "hub_wifi.h"

static SensorCamServer* server = nullptr;
static std::string localIp() { return WiFi.localIP().toString().c_str(); }
static uint32_t nowMs() { return millis(); }

// Changes from the web page. hub_settings.cpp stores them; the restart a change needs is decided here. A new password
// or other names, which the page server applies at once, go into the settings in memory too, for the setup network.
static bool saveSettings(const CamSettings& s) {
  bool ok = storeSettings(s);
  if (!ok) return false;
  if (s.restart) {
    requestRestart(RESTART_NEW_SETTINGS);
  } else {
    if (!s.password.empty()) settings.webPassword = s.password.c_str();
    if (s.hostsGiven) settings.extraHosts = s.hosts.c_str();
  }
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

static bool restoreBackup(const std::string& text) {
  bool ok = restoreText(text);
  if (ok) requestRestart(RESTART_RESTORED);
  return ok;
}

static void hubHealth(CamHealth& h) {
  h.freeHeap = ESP.getFreeHeap();
  h.lowestFreeHeap = ESP.getMinFreeHeap();
  h.largestBlock = largestBlock();
  h.worstLargestBlock = worstLargestBlock();
  h.espNowDropped = espNowDropped();
  h.wifiRssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  h.uptimeS = esp_timer_get_time() / 1000000ULL;
  memoryDayLow(h.lowFree, h.lowAt, h.lowWeb, h.lowVideo);
}

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
    noteUpdateStarting(server && server->nvrListeningNow());
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
  void deviceRows(CamJson& j) override {
    ::deviceRows(j, {firmwareUnproven(), lastRestartText(), server ? server->memoryFailures() : 0, espNowDropped()});
  }
  bool tellsHealth() override { return true; }
  void health(CamHealth& h) override { hubHealth(h); }
  void healthDayStart() override { memoryDayStart(); }
  int localMinute() override { return ::localMinute(); }
  bool random32(uint32_t& out) override {
    out = esp_random();  // truly random while Wi-Fi runs, as it does from the start
    return true;
  }
  int localWeekday() override { return ::localWeekday(); }
  void timeInfo(CamJson& j) override { ::timeInfo(j); }
  bool timeSave(const CamTime& t) override { return saveTime(t); }
};

class HubRadio : public CamRadio {
 public:
  bool setupActive() override { return setupIsOpen(); }
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
    s.dhcp = !usingStaticAddress();
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
  bool backupNoted(int64_t at, int paired) override { return storeBackupNoted(at, paired); }
  bool mqttSave(const CamMqtt& m) override { return storeMqtt(m); }
  bool nvrSave(const std::string& ip) override { return storeNvr(ip); }
  bool ledsSave(const CamLedPins& p) override { return storeLeds(p); }
  bool sealReserve(uint32_t next) override { return storeSealNext(next); }
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
  c.leds = settings.leds;
  c.nvrIp = settings.nvrIp;
  if (settings.webPort != 80) c.setupAddress += ":" + std::to_string(settings.webPort);
  c.hostName = std::string(settings.hubName.c_str()) + ".local";
  c.setupName = SETUP_AP_NAME;
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
  c.sealStartKept = takeSealStart(CAM_SEAL_BLOCK, c.sealStart);
  c.extraHosts = settings.extraHosts.c_str();
  c.webhook = settings.webhook;
  c.setupPasswordHash = strlen(SETUP_AP_PASSWORD) ? camSha1(SETUP_AP_PASSWORD) : std::string();
  c.mqtt = settings.mqtt;
  c.armMode = settings.armMode;
  c.disarmedUntil = settings.disarmedUntil;
  c.rearmMinutes = settings.rearmMinutes;
  c.backupAt = settings.backupAt;
  c.backupPaired = settings.backupPaired;
  c.firmwareVersion = FIRMWARE_VERSION;
#ifdef HUB_BOARD
  c.board = HUB_BOARD;
#endif
  c.firmwareKey = FIRMWARE_PUBLIC_KEY;
  c.firmwareRelease = FIRMWARE_RELEASE;
  c.serial = macSerial();
  return c;
}

void setup() {
  deviceInfoBegin();
  restartBegin(&server);
  Serial.begin(115200);
  loadSettings();
  if (!settings.webPassword.length()) Serial.println("No hub password is set: the web page, ONVIF and RTSP are open to anyone on the network.");
  wifiBegin();
  applyTime();

  // Formats on first use. Without storage the hub still runs, using its built-in pictures.
  if (!LittleFS.begin(true)) Serial.println("LittleFS could not start; using built-in pictures.");

  server = new SensorCamServer(makeConfig(), localIp, nowMs);
  if (!server->begin()) {
    if (firmwareUnproven()) {  // the previous firmware opened these ports, so it comes back with them unchanged
      Serial.println("The new firmware could not open its ports; going back to the previous firmware.");
      delay(2000);
      esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    Serial.println("Could not open the ONVIF, web or RTSP port; going back to the default ports and restarting.");
    forgetPorts();  // saved ports that cannot open must not keep the hub in a restart loop
    delay(2000);
    ESP.restart();
  }

  if (!espNowBegin()) Serial.println("ESP-NOW failed to start; HTTP sensor input still works.");

  enableLoopWDT();  // an unattended hub must restart itself if the main loop ever stalls
}

// ---- the main loop: each step below keeps one promise, and loop() runs them in this order ----

static CamRestartRules rules(routerAnswersArp);
static bool wasConnected = false, everConnected = false;

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

// What the restart rules look at, this pass.
static CamRestartRules::Pass rulesPass(bool connected, bool guest) {
  CamRestartRules::Pass p;
  p.lastPingReply = lastPingReply();  // before the time: the ping task may change it, and a reply later than now would wrap
  p.now = millis();
  p.haveNetwork = settings.wifiSsid[0].length() > 0;
  p.connected = connected;
  p.guest = guest;
  p.usingStatic = usingStaticAddress();
  p.freeHeap = ESP.getFreeHeap();
  p.stalledMs = server->stalledMs(p.now);
  p.pingReplies = pingReplies();
  p.pingLost = pingLost();
  p.uploading = server->updating();
  p.scheduledMinute = scheduledMinute();
  p.alarmBusy = server->alarmBusy();
  return p;
}

static void printHeartbeat() {
  static uint32_t printedAt = 0;
  if (millis() - printedAt <= 10000) return;
  printedAt = millis();
  Serial.printf(
      "ONVIF on %s:%u, web port %u, RTSP port %u, ESP-NOW to %s on Wi-Fi channel %d, status %d, RSSI %d, drops %u, last drop reason %u, free heap "
      "%u, largest block %u, last reset reason %d cause %u, ping ok %u lost %u, setup %s, %s\n",
      WiFi.localIP().toString().c_str(), settings.onvifPort, settings.webPort, settings.rtspPort, WiFi.macAddress().c_str(), WiFi.channel(),
      (int)WiFi.status(), WiFi.RSSI(), (unsigned)wifiDrops(), (unsigned)wifiDropReason(), ESP.getFreeHeap(), largestBlock(), (int)esp_reset_reason(),
      (unsigned)lastRestartCause(), (unsigned)pingReplies(), (unsigned)pingLost(), setupIsOpen() ? "open" : "off", server->debugLine().c_str());
}

// The status LEDs, if the page says where they are, set ten times a second from what the hub is doing; see
// cam_status_leds.h. A pin the page moves an LED off is let go, as an input, for whatever else is wired to it.
static void showStatus(bool connected) {
  static uint32_t shownAt = 0;
  static CamLedPins driven;  // none at first
  if (!server || millis() - shownAt < 100) return;
  shownAt = millis();
  CamLedPins want = server->ledPins();
  if (want.green != driven.green || want.red != driven.red) {
    for (int pin : {driven.green, driven.red})
      if (pin >= 0) pinMode(pin, INPUT);
    for (int pin : {want.green, want.red})
      if (pin >= 0) pinMode(pin, OUTPUT);
  }
  driven = want;
  CamLeds l = server->leds(connected, setupIsOpen());
  auto light = [&](int pin, bool on) {
    if (pin >= 0) digitalWrite(pin, on != want.lowOn ? HIGH : LOW);
  };
  light(want.green, l.green);
  light(want.red, l.red);
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  bool justLost = !connected && wasConnected;
  if (connected && !wasConnected) onConnected();  // before keepOnNetwork, which forgets when the network went
  wasConnected = connected;
  bool guest = keepOnNetwork(connected, justLost);
  serveSetupNetwork();
  keepTime();
  restartWhenAsked();
  sampleMemory(server ? server->webOpen() : 0, server ? server->videoOpen() : 0);  // for Device Info and the daily check
  CamRestartRules::Pass pass = rulesPass(connected, guest);
  CamRestartRule rule = rules.check(pass);
  proveFirmware(pass, rules, espNowRunning());  // first, so an update that failed says why before any other restart undoes it
  restartForRule(rule);
  printHeartbeat();
  runRadioTest();
  showStatus(connected);

  // Sensor messages first, so the server saves them together and then confirms them, in the same pass.
  espNowDeliver(*server);
  server->poll(20);
}
